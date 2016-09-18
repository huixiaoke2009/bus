
#include <functional>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <vector>

#include "user_proc.h"
#include "log/log.h"
#include "util/util.h"
#include "ini_file/ini_file.h"

#include "bus.pb.h"
#include "mm.pb.h"
#include "app.pb.h"
#include "bus_header.h"


#include "cmd.h"

using namespace std;
using namespace mmlib;

bool StopFlag = false;
bool ReloadFlag = false;

template<typename T>
void SAFE_DELETE(T *& p)
{
    if (p)
    {
        delete p;
        p = NULL;
    }
}


static void SigHandler(int iSigNo)
{
    XF_LOG_INFO(0, 0, "%s get signal %d", __func__, iSigNo);
    switch(iSigNo)
    {
    case SIGUSR1:
        StopFlag = true;
        break;

    case SIGUSR2:
        ReloadFlag = true;
        break;

    default:
        break;
    }

    return;
}


CUser::CUser()
{
    m_ServerID = 0;
    m_StateTime = 0;
    m_pSendBuff = NULL;
    m_MaxUserNodeNum = 0;
}

CUser::~CUser()
{
    if(m_pSendBuff)
    {
        SAFE_DELETE(m_pSendBuff);
    }
}


int CUser::Init(const char *pConfFile)
{
    //安装信号量
    StopFlag = false;
    ReloadFlag = false;
    struct sigaction stSiga;
    memset(&stSiga, 0, sizeof(stSiga));
    stSiga.sa_handler = SigHandler;

    sigaction(SIGCHLD, &stSiga, NULL);
    sigaction(SIGUSR1, &stSiga, NULL);
    sigaction(SIGUSR2, &stSiga, NULL);

    //忽略信号量
    struct sigaction stSiga2;
    memset(&stSiga2, 0, sizeof(stSiga2));
    stSiga2.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &stSiga2, NULL);

    int Ret = 0;

    //读取配置文件
    CIniFile IniFile(pConfFile);

    char ModuleName[256] = {0};
    int LogLocal = 0;
    int LogLevel = 0;
    char LogPath[1024] = {0};

    int UserShmKey = 0;
    int UserShmSize = 0;
    int LoaderShmKey = 0;
    int LoaderShmSize = 0;
    int WriterShmKey = 0;
    int WriterShmSize = 0;
    
    char BusConfPath[256] = {0};

    if (IniFile.IsValid())
    {
        IniFile.GetInt("USER", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("USER", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        IniFile.GetInt("USER", "UserShmKey", 0, &UserShmKey);
        IniFile.GetInt("USER", "UserShmSize", 0, &UserShmSize);
        IniFile.GetInt("USER", "LoaderShmKey", 0, &LoaderShmKey);
        IniFile.GetInt("USER", "LoaderShmSize", 0, &LoaderShmSize);
        IniFile.GetInt("USER", "WriterShmKey", 0, &WriterShmKey);
        IniFile.GetInt("USER", "WriterShmSize", 0, &WriterShmSize);
        IniFile.GetInt("USER", "NodeNum", 0, &m_MaxUserNodeNum);
        IniFile.GetInt("USER", "StateTime", 0, &m_StateTime);
        
        IniFile.GetString("LOG", "ModuleName", "user", ModuleName, sizeof(ModuleName));
        IniFile.GetInt("LOG", "LogLocal", 1, &LogLocal);
        IniFile.GetInt("LOG", "LogLevel", 3, &LogLevel);
        IniFile.GetString("LOG", "LogPath", "/dev/null", LogPath, sizeof(LogPath));
    }
    else
    {
        printf("ERR:conf file [%s] is not valid\n", pConfFile);
        return -1;
    }
    
    if(m_ServerID == 0)
    {
        printf("ERR:BUS/ServerID is not valid\n");
        return -1;
    }
    
    if(BusConfPath[0] == 0)
    {
        printf("ERR:BUS/BusConfPath is not valid\n");
        return -1;
    }
    
    if (ModuleName[0] == 0)
    {
        printf("ERR:LOG/ModuleName is not valid\n");
        return -1;
    }

    OpenLog(ModuleName);
    if (LogLocal == 1)
    {
        SetLogLocal(1, LogLevel, LogPath);
    }
    
    int SendShmKey = 0;
    int SendShmSize = 0;
    int RecvShmKey = 0;
    int RecvShmSize = 0;
    
    CIniFile BusFile(BusConfPath);
    if (!BusFile.IsValid())
    {
        printf("ERR:conf file [%s] is not valid\n", BusConfPath);
        return -1;
    }
    
    string strServer = CStrTool::Format("SERVER_%d", m_ServerID);
    
    BusFile.GetInt("BUS_GLOBAL", "GCIMKey", 0, &SendShmKey);
    BusFile.GetInt("BUS_GLOBAL", "GCIMSize", 0, &SendShmSize);
    BusFile.GetInt(strServer.c_str(), "QueueKey", 0, &RecvShmKey);
    BusFile.GetInt(strServer.c_str(), "QueueSize", 0, &RecvShmSize);

    if (0 == RecvShmKey || 0 == RecvShmSize)
    {
        printf("Error 0 == RecvShmKey(%x) || 0 == RecvShmSize(%d)", RecvShmKey, RecvShmSize);
        return -1;
    }
    
    Ret = m_RecvQueue.Init(RecvShmKey, RecvShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_RecvQueue failed, key=%d, size=%d, err=%s\n",
                RecvShmKey, RecvShmSize, m_RecvQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_RecvQueue succ, key=0x%x, size=%u\n", RecvShmKey, RecvShmSize);

    if (0 == SendShmKey || 0 == SendShmSize)
    {
        printf("Error 0 == SendShmKey(%x) || 0 == SendShmSize(%d)", SendShmKey, SendShmSize);
        return -1;
    }
    
    Ret = m_SendQueue.Init(SendShmKey, SendShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_SendQueue failed, key=%d, size=%d, err=%s\n",
                SendShmKey, SendShmSize, m_SendQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_SendQueue succ, key=0x%x, size=%u\n", SendShmKey, SendShmSize);


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

    if(!m_pSendBuff)
    {
        m_pSendBuff = (char*)malloc(XY_PKG_MAX_LEN);
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

    printf("svr init success\n");

    return 0;
}


int CUser::Run()
{
    int Ret = 0;
    
    char *pRecvBuff = (char *)malloc(XY_PKG_MAX_LEN);
    int RecvLen = XY_MAXBUFF_LEN;

    time_t LastStateTime = time(NULL);
    
    while(!StopFlag)
    {
        int EmptyFlag = 1;  //没有数据标志位
        
        RecvLen = XY_PKG_MAX_LEN;
        Ret = m_RecvQueue.OutQueue(pRecvBuff, &RecvLen);
        if (Ret == m_RecvQueue.E_SHM_QUEUE_EMPTY)
        {
            
        }
        else if(Ret != 0)
        {
             //出错了
             XF_LOG_WARN(0, 0, "Run|OutQueue failed, ret=%d, errmsg=%s", Ret, m_RecvQueue.GetErrMsg());
             continue;
        }
        else
        {
            EmptyFlag = 0;
            
            XF_LOG_TRACE(0, 0, "Run|OutQueue success|%d|%s", RecvLen, CStrTool::Str2Hex(pRecvBuff, RecvLen));
            
            BusHeader CurBusHeader;
            CurBusHeader.Read(pRecvBuff);
            int PkgLen = RecvLen - CurBusHeader.GetHeaderLen();
            char *pSendBuff = pRecvBuff + CurBusHeader.GetHeaderLen();

            Ret = DealPkg(pSendBuff, PkgLen);
            if(Ret != 0)
            {
                XF_LOG_WARN(0, 0, "Run|DealPkg failed, Ret=%d", Ret);
                continue;
            }
        }

        // 向bus更新自己的状态
        time_t NowTime = time(NULL);
        if(NowTime - LastStateTime >= m_StateTime)
        {
            LastStateTime = NowTime;
            SendStateMessage();
        }

        if(EmptyFlag)
        {
            usleep(1000);
        }
    }
    
    return 0;
}


int CUser::DealPkg(const char *pCurBuffPos, int PkgLen)
{
    int Ret = 0;
    
    XYHeaderIn HeaderIn;
    HeaderIn.Read(pCurBuffPos);
    int HeaderInLen = HeaderIn.GetHeaderLen();
    
    switch(HeaderIn.CmdID)
    {
        case Cmd_Auth_Register_Req:
        {
            mm::UserRegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Cmd_User_AddFriend_Req);
                return -1;
            }
            
            break;
        }
        case Cmd_User_AddFriend_Req:
        {
            app::AddFriendReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Cmd_User_AddFriend_Req);
                return -1;
            }

            app::AddFriendRsp CurRsp;
            CurRsp.set_ret(0);

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_User_AddFriend_Rsp;
            Header.SN = HeaderIn.SN;
            Header.ConnPos = HeaderIn.ConnPos;
            Header.UserID = HeaderIn.UserID;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, HeaderIn.SrcID, TO_SRV, 0, CurRsp);
            
            break;
        }
        default:
        {
            XF_LOG_WARN(0, 0, "unknow cmdid %0x", HeaderIn.CmdID);
            break;
        }
    }
    
    return 0;
}


int CUser::Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message)
{
    BusHeader CurBusHeader;
    int HeaderLen = CurBusHeader.GetHeaderLen() + Header.GetHeaderLen();
    CurBusHeader.PkgLen = HeaderLen + Message.ByteSize();
    CurBusHeader.CmdID = Cmd_Transfer;
    CurBusHeader.SrcID = GetServerID();
    CurBusHeader.DstID = DstID;
    CurBusHeader.SendType = SendType;
    CurBusHeader.Flag = Flag;
    CurBusHeader.Write(m_pSendBuff);
    
    Header.Write(m_pSendBuff+CurBusHeader.GetHeaderLen());

    if(!Message.SerializeToArray(m_pSendBuff+HeaderLen, XY_PKG_MAX_LEN-HeaderLen))
    {
        XF_LOG_WARN(0, 0, "pack err msg failed");
        return -1;
    }

    int Ret = m_SendQueue.InQueue(m_pSendBuff, CurBusHeader.PkgLen);
    if(Ret == m_SendQueue.E_SHM_QUEUE_FULL)
    {
        XF_LOG_WARN(0, 0, "m_SendQueue InQueue failed, queue full");
        return -1;
    }
    else if (Ret != 0)
    {
        XF_LOG_WARN(0, 0, "m_SendQueue InQueue failed, Ret=%d", Ret);
        return -1;
    }
    else
    {
        XF_LOG_TRACE(0, 0, "m_SendQueue InQueue Success,[%s]", CStrTool::Str2Hex(m_pSendBuff, CurBusHeader.PkgLen));
    }
    
    return 0;
}

int CUser::SendStateMessage()
{
    BusHeader CurBusHeader;
    int HeaderLen = CurBusHeader.GetHeaderLen();

    bus::StateChangeMsg CurStateReq;
    CurStateReq.set_serverid(GetServerID());
    CurStateReq.set_status(BUS_SVR_ONLINE);

    CurBusHeader.PkgLen = HeaderLen + CurStateReq.ByteSize();
    CurBusHeader.CmdID = Cmd_StateChange;
    CurBusHeader.SrcID = GetServerID();
    CurBusHeader.DstID = 0;
    CurBusHeader.SendType = TO_SRV;
    CurBusHeader.Flag = 0;
    CurBusHeader.Write(m_pSendBuff);

    if(!CurStateReq.SerializeToArray(m_pSendBuff+HeaderLen, XY_PKG_MAX_LEN-HeaderLen))
    {
        XF_LOG_WARN(0, 0, "pack err msg failed");
        return -1;
    }

    int Ret = m_SendQueue.InQueue(m_pSendBuff, CurBusHeader.PkgLen);
    if(Ret == m_SendQueue.E_SHM_QUEUE_FULL)
    {
        XF_LOG_WARN(0, 0, "m_SendQueue InQueue failed, queue full");
        return -1;
    }
    else if (Ret != 0)
    {
        XF_LOG_WARN(0, 0, "m_SendQueue InQueue failed, Ret=%d", Ret);
        return -1;
    }
    else
    {
        XF_LOG_TRACE(0, 0, "m_SendQueue InQueue Success,[%s]", CStrTool::Str2Hex(m_pSendBuff, CurBusHeader.PkgLen));
    }
    
    return 0;
}

