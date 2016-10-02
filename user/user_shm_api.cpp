

#include "user_shm_api.h"
#include "log/log.h"
#include "util/util.h"
#include "ini_file/ini_file.h"
#include "bus.pb.h"
#include "mm.pb.h"
#include "app.pb.h"

using namespace std;
using namespace mmlib;


CUserShmApi::CUserShmApi()
{
    m_MaxUserNodeNum = 0;
    m_CheckUserPos = 0;
    m_pUserHead = NULL;
}

CUserShmApi::~CUserShmApi()
{
    
}

int CUserShmApi::Init(const char *pConfFile)
{
    int Ret = 0;

    //读取配置文件
    CIniFile IniFile(pConfFile);

    int UserShmKey = 0;
    int UserShmSize = 0;
    char UserLockPath[256] = {0};
    
    if (IniFile.IsValid())
    {
        IniFile.GetInt("USER", "UserShmKey", 0, &UserShmKey);
        IniFile.GetInt("USER", "UserShmSize", 0, &UserShmSize);
        IniFile.GetInt("USER", "NodeNum", 0, &m_MaxUserNodeNum);
        IniFile.GetString("USER", "UserLockPath", "user.lock", UserLockPath, sizeof(UserLockPath));
    }
    else
    {
        printf("ERR:conf file [%s] is not valid\n", pConfFile);
        return -1;
    }
    
    if (UserShmKey == 0)
    {
        printf("ERR:conf User/UserShmKey is not valid\n");
        return -1;
    }

    if (UserShmSize == 0)
    {
        printf("ERR:conf User/UserShmSize is not valid\n");
        return -1;
    }
    
    if (m_MaxUserNodeNum == 0)
    {
        printf("ERR:conf User/NodeNum is not valid\n");
        return -1;
    }

    int UserMemHeadSize = USER_MEM_HEAD_SIZE;
    int UserInfoMemSize = m_UserInfoMap.CalcSize(m_MaxUserNodeNum, m_MaxUserNodeNum);

    int UserMemNeed =  UserInfoMemSize + UserMemHeadSize;
    if (UserMemNeed > UserShmSize)
    {
        printf("ERR:conf USER/UserShmSize[%d] is not enough, need %d\n", UserShmSize, UserMemNeed);
        return -1;
    }

    printf("UserMemNeed=%d, UserShmSize=%d\n", UserMemNeed, UserShmSize);

    Ret = m_UserMem.Create(UserShmKey, UserShmSize, 0666);
    if ((Ret != m_UserMem.SUCCESS)&&(Ret != m_UserMem.SHM_EXIST))
    {
        printf("ERR:create user shm failed, key=%d, size=%d, ret=%d\n", UserShmKey, UserShmSize, Ret);
        return -1;
    }

    Ret = m_UserMem.Attach();
    if (Ret != m_UserMem.SUCCESS)
    {
        printf("ERR:attach user shm failed, key=%d, size=%d, ret=%d\n", UserShmKey, UserShmSize, Ret);
        return -1;
    }

    printf("INFO:user shm create succ\n");

    m_pUserHead = (UserMemHead *)m_UserMem.GetMem();
    void *pUserInfoMem = ((char *)m_UserMem.GetMem()) + UserMemHeadSize;

    if (m_pUserHead == NULL)
    {
        printf("ERR:create shm failed\n");
        return -1;
    }

    LOCK_ALL(CFileLock::FILE_LOCK_WRITE);
    bool ClearFlag = false;
    if (memcmp(m_pUserHead->Magic, USER_MEM_MAGIC, sizeof(m_pUserHead->Magic)) != 0)
    {
        ClearFlag = true;
        memset(m_pUserHead, 0, USER_MEM_HEAD_SIZE);
        memcpy(m_pUserHead->Magic, USER_MEM_MAGIC, sizeof(m_pUserHead->Magic));
        printf("WARN:user map shoud clear\n");
    }

    Ret = m_UserInfoMap.Init(pUserInfoMem, UserInfoMemSize, m_MaxUserNodeNum, m_MaxUserNodeNum);
    if (Ret != 0)
    {
        printf("ERR:init user info shm failed, ret=%d\n", Ret);
        return -1;
    }

    printf("INFO:user info map init succ\n");

    if (ClearFlag)
    {
        m_UserInfoMap.Clear();
        ClearFlag = false;
    }

    Ret = m_UserInfoMap.Verify();
    if (Ret != 0)
    {
        printf("WARN:user info verify failed, Ret = %d\n", Ret);
        return -1;
    }
    else
    {
        printf("INFO:user info map verify succ\n");
    }

    Ret = m_UserInfoLock.Init(UserLockPath);
    if (Ret != m_UserInfoLock.SUCCESS)
    {
        printf("ERR:init user lock failed, ret=%d\n", Ret);
        return -1;
    }
    
    return 0;
}

int CUserShmApi::InsertUserInfo(const ShmUserInfo& Info)
{
    uint64_t UserID = Info.UserID;
    
    if(UserID == 0)
    {
        XF_LOG_WARN(0, 0, "UserID == 0 is illegal");
        return -1;
    }
    
    int Ret = 0;
    LOCK_HASHLIST_HEAD(CFileLock::FILE_LOCK_WRITE);
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);
    Ret = m_UserInfoMap.Insert(UserID, Info);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, UserID, "Insert failed, Ret=%d", Ret);
        return -1;
    }

    XF_LOG_INFO(0, UserID, "UserID = %lu Insert success", UserID);

    return 0;
}


int CUserShmApi::RemoveUserInfo(uint64_t UserID)
{
    int Ret = 0;
    LOCK_HASHLIST_HEAD(CFileLock::FILE_LOCK_WRITE);
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);

    Ret = m_UserInfoMap.Remove(UserID);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, UserID, "Remove failed, Ret=%d", Ret);
        return -1;
    }

    XF_LOG_INFO(0, UserID, "UserID = %lu Remove success", UserID);

    return 0;
}

int CUserShmApi::RemoveUserInfoWhenInValid(uint64_t UserID)
{
    int Ret = 0;
    LOCK_HASHLIST_HEAD(CFileLock::FILE_LOCK_WRITE);
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);
    ShmUserInfo* pCurUserInfo = m_UserInfoMap.Get(UserID);
    if(pCurUserInfo == NULL)
    {
        XF_LOG_WARN(0, UserID, "Get failed, Ret=%d", Ret);
        return -1;
    }

    time_t nowTime= time(NULL);
    if((uint64_t)(nowTime - USER_INVALID_TIME) <= pCurUserInfo->LastActiveTime)
    {
        return 0;   
    }
    
    Ret = m_UserInfoMap.Remove(UserID);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, UserID, "UserID = %lu Remove failed, Ret=%d", UserID, Ret);
        return -1;
    }

    XF_LOG_INFO(0, UserID, "UserID = %lu Remove success", UserID);

    return 0;
}

int CUserShmApi::GetUserInfo(uint64_t UserID, ShmUserInfo& Info)
{
    int Ret = 0;
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);
    Ret =  m_UserInfoMap.Get(UserID, Info);
    if(Ret != 0)//内存中不存在
    {
        return -1;
    }

    return 0;
}


int CUserShmApi::UpdateUserInfo(const ShmUserInfo &newUserInfo)
{
    uint64_t UserID = newUserInfo.UserID;
    
    int Ret = 0;
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);
    Ret = m_UserInfoMap.Update(UserID, newUserInfo);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, UserID, "Update failed, Ret=%d", Ret);
        return -1;
    }

    XF_LOG_INFO(0, UserID, "UserID = %lu Update success", UserID);
    
    return 0;
}

void CUserShmApi::MemFullCallBack(uint64_t UserID, ShmUserInfo &CurUserInfo, void* p)
{
    time_t nowTime= time(NULL);
    if((uint64_t)(nowTime - USER_INVALID_TIME) > CurUserInfo.LastActiveTime)
    {
        vector<uint64_t>* pVct = (vector<uint64_t>*) p;
        pVct->push_back(UserID);
    }
}

int CUserShmApi::CheckUserIsValid(vector<uint64_t>* pVct)
{
    LOCK_INDEX(CFileLock::FILE_LOCK_WRITE, m_CheckUserPos);
    m_UserInfoMap.ProcessAll(m_CheckUserPos, MemFullCallBack, (void*)pVct);
    m_CheckUserPos = (m_CheckUserPos+1)%m_MaxUserNodeNum;

    return 0;
}


int CUserShmApi::AddFriendReq(uint64_t UserID, uint64_t OtherUserID, const string& strNickName)
{
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, OtherUserID);
    ShmUserInfo* pCurUserInfo = m_UserInfoMap.Get(OtherUserID);
    if(pCurUserInfo == NULL)
    {
        XF_LOG_WARN(0, OtherUserID, "get user failed");
        return -1;
    }

    uint64_t MaxRequestTime = 0;
    int Index = 0;
    for(int i = 0; i < MAX_REQUEST_NUM; i++)
    {
        RequestInfo& Request = pCurUserInfo->RequestList[i];
        if(Request.UserID == 0 || Request.IsValid == 0)
        {
            Index = i;
            break;
        }
        else
        {
            if(Request.RequestTime > MaxRequestTime)
            {
                MaxRequestTime = Request.RequestTime;
                Index = i;
            }
        }
    }

    RequestInfo& Request = pCurUserInfo->RequestList[Index];
    Request.UserID = UserID;
    snprintf(Request.NickName, MAX_NAME_LENGTH, "%s", strNickName.c_str());
    Request.RequestTime = time(NULL);

    pCurUserInfo->LastActiveTime = time(NULL);
            
    return 0;
}

