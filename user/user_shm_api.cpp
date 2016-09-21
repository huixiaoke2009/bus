

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
}

CUserShmApi::~CUserShmApi()
{
    
}

int CUserShmApi::Init(const char *pConfFile)
{
    int Ret = 0;

    //¶ÁÈ¡ÅäÖÃÎÄ¼þ
    CIniFile IniFile(pConfFile);

    int UserShmKey = 0;
    int UserShmSize = 0;
    int LoaderShmKey = 0;
    int LoaderShmSize = 0;
    int WriterShmKey = 0;
    int WriterShmSize = 0;
    char UserLockPath[256] = {0};
    
    if (IniFile.IsValid())
    {
        IniFile.GetInt("USER", "UserShmKey", 0, &UserShmKey);
        IniFile.GetInt("USER", "UserShmSize", 0, &UserShmSize);
        IniFile.GetInt("USER", "LoaderShmKey", 0, &LoaderShmKey);
        IniFile.GetInt("USER", "LoaderShmSize", 0, &LoaderShmSize);
        IniFile.GetInt("USER", "WriterShmKey", 0, &WriterShmKey);
        IniFile.GetInt("USER", "WriterShmSize", 0, &WriterShmSize);
        IniFile.GetInt("USER", "NodeNum", 0, &m_MaxUserNodeNum);
        IniFile.GetString("USER", "UserLockPath", "user.lock", UserLockPath, sizeof(UserLockPath));
    }
    else
    {
        printf("ERR:conf file [%s] is not valid\n", pConfFile);
        return -1;
    }

    if (0 == LoaderShmKey|| 0 == LoaderShmSize)
    {
        printf("Error 0 == LoaderShmKey(%x) || 0 == LoaderShmSize(%d)", LoaderShmKey, LoaderShmSize);
        return -1;
    }
    
    Ret = m_LoaderQueue.Init(LoaderShmKey, LoaderShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_LoaderQueue failed, key=%d, size=%d, err=%s\n",
                LoaderShmKey, LoaderShmSize, m_LoaderQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_LoaderQueue succ, key=0x%x, size=%u\n", LoaderShmKey, LoaderShmSize);

    if (0 == WriterShmKey|| 0 == WriterShmSize)
    {
        printf("Error 0 == WriterShmKey(%x) || 0 == WriterShmSize(%d)", WriterShmKey, WriterShmSize);
        return -1;
    }
    
    Ret = m_WriterQueue.Init(WriterShmKey, WriterShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_WriterQueue failed, key=%d, size=%d, err=%s\n",
                WriterShmKey, WriterShmSize, m_WriterQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_WriterQueue succ, key=0x%x, size=%u\n", WriterShmKey, WriterShmSize);

    
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

int CUserShmApi::Register(const ShmUserInfo& Info)
{
    if(Info.UserID == 0)
    {
        XF_LOG_WARN(0, 0, "UserID == 0 is illegal");
        return -1;
    }
    
    int Ret = 0;
    LOCK_HASHLIST_HEAD(CFileLock::FILE_LOCK_WRITE);
    Ret = m_UserInfoMap.Insert(Info.UserID, Info);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, Info.UserID, "Insert failed, Ret=%d", Ret);
        return -1;
    }

    WriteUserInfo(Info.UserID);

    return 0;
}


int CUserShmApi::RemoveUserInfo(uint64_t UserID)
{
    int Ret = 0;
    LOCK_HASHLIST_HEAD(CFileLock::FILE_LOCK_WRITE);
    LOCK_USER(CFileLock::FILE_LOCK_WRITE, UserID);

    XF_LOG_INFO(0, UserID, "UserID = %lu Remove", UserID);

    Ret = m_UserInfoMap.Remove(UserID);
    if(Ret != 0)
    {
        XF_LOG_WARN(0, UserID, "Remove failed, Ret=%d", Ret);
        return -1;
    }

    return 0;
}

int CUserShmApi::LoadUserInfo(uint64_t UserID, const string& strRequest)
{
    int Ret = 0;
    
    mm::LoadUserInfoReq CurReq;
    CurReq.set_userid(UserID);
    CurReq.set_request(strRequest);

    const int BuffLen = XY_PKG_MAX_LEN;
    char acBuff[BuffLen] = {0};

    int PkgLen = CurReq.ByteSize();

    if(!CurReq.SerializeToArray(acBuff, BuffLen))
    {
        XF_LOG_WARN(0, 0, "pack err msg failed");
        return -1;
    }
    
    Ret = m_LoaderQueue.InQueue(acBuff, PkgLen);
    if(Ret != CShmQueue::SUCCESS)
    {
        XF_LOG_WARN(0, UserID, "WriteUserInfo failed, Ret=%d", Ret);
        return -1;
    }
    
    return 0;
}


int CUserShmApi::WriteUserInfo(uint64_t UserID)
{
    int Ret = 0;
    
    mm::WriteUserInfoReq CurReq;
    CurReq.set_userid(UserID);

    const int BuffLen = 256;
    char acBuff[BuffLen] = {0};

    int PkgLen = CurReq.ByteSize();

    if(!CurReq.SerializeToArray(acBuff, BuffLen))
    {
        XF_LOG_WARN(0, 0, "pack err msg failed");
        return -1;
    }
    
    Ret = m_WriterQueue.InQueue(acBuff, PkgLen);
    if(Ret != CShmQueue::SUCCESS)
    {
        XF_LOG_WARN(0, UserID, "WriteUserInfo failed, Ret=%d", Ret);
        return -1;
    }
    
    return 0;
}

