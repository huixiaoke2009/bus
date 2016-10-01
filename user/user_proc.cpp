
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
    m_CheckTime = 0;
    m_pSendBuff = NULL;
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

    int LoaderSendShmKey = 0;
    int LoaderSendShmSize = 0;
    int LoaderRecvShmKey = 0;
    int LoaderRecvShmSize = 0;
    int WriterShmKey = 0;
    int WriterShmSize = 0;
    char BusConfPath[256] = {0};

    if (IniFile.IsValid())
    {
        IniFile.GetInt("USER", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("USER", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        IniFile.GetInt("USER", "StateTime", 0, &m_StateTime);
        IniFile.GetInt("USER", "CheckTime", 0, &m_CheckTime);
        IniFile.GetInt("USER", "LoaderSendShmKey", 0, &LoaderSendShmKey);
        IniFile.GetInt("USER", "LoaderSendShmSize", 0, &LoaderSendShmSize);
        IniFile.GetInt("USER", "LoaderRecvShmKey", 0, &LoaderRecvShmKey);
        IniFile.GetInt("USER", "LoaderRecvShmSize", 0, &LoaderRecvShmSize);
        IniFile.GetInt("USER", "WriterShmKey", 0, &WriterShmKey);
        IniFile.GetInt("USER", "WriterShmSize", 0, &WriterShmSize);
        
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

    
    if (0 == LoaderSendShmKey || 0 == LoaderSendShmSize)
    {
        printf("Error 0 == LoaderSendShmKey(%x) || 0 == LoaderSendShmSize(%d)", LoaderSendShmKey, LoaderSendShmSize);
        return -1;
    }
    
    Ret = m_LoaderSendQueue.Init(LoaderSendShmKey, LoaderSendShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_LoaderSendQueue failed, key=%d, size=%d, err=%s\n",
                LoaderSendShmKey, LoaderSendShmSize, m_LoaderSendQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_LoaderSendQueue succ, key=0x%x, size=%u\n", LoaderSendShmKey, LoaderSendShmSize);

    if (0 == LoaderRecvShmKey || 0 == LoaderRecvShmSize)
    {
        printf("Error 0 == LoaderRecvShmKey(%x) || 0 == LoaderRecvShmSize(%d)", LoaderRecvShmKey, LoaderRecvShmSize);
        return -1;
    }

    Ret = m_LoaderRecvQueue.Init(LoaderRecvShmKey, LoaderRecvShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_LoaderRecvQueue failed, key=%d, size=%d, err=%s\n",
                LoaderRecvShmKey, LoaderRecvShmSize, m_LoaderRecvQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_LoaderRecvQueue succ, key=0x%x, size=%u\n", LoaderRecvShmKey, LoaderRecvShmSize);

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

    Ret = m_UserShm.Init(pConfFile);
    if(Ret != 0)
    {
        printf("user shm init failed, Ret=%d", Ret);
        return -1;
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
    time_t LastCheckTime = time(NULL);
    
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

        // 从loader收包
        RecvLen = XY_PKG_MAX_LEN;
        Ret = m_LoaderRecvQueue.OutQueue(pRecvBuff, &RecvLen);
        if (Ret == m_LoaderRecvQueue.E_SHM_QUEUE_EMPTY)
        {
            
        }
        else if(Ret != 0)
        {
             //出错了
             XF_LOG_WARN(0, 0, "Run|OutQueue failed, ret=%d, errmsg=%s", Ret, m_LoaderRecvQueue.GetErrMsg());
             continue;
        }
        else
        {
            EmptyFlag = 0;
            
            XF_LOG_TRACE(0, 0, "Run|OutQueue success|%d|%s", RecvLen, CStrTool::Str2Hex(pRecvBuff, RecvLen));

            mm::LoadUserInfoRsp CurRsp;
            if(!CurRsp.ParseFromArray(pRecvBuff, RecvLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed");
                continue;
            }

            int Result = CurRsp.ret();
            string strRequest = CurRsp.request();

            if(Result != 0)
            {
                XF_LOG_WARN(0, 0, "Load UserInfo failed, Ret=%d", Result);
                continue;
            }
            
            Ret = DealPkg(strRequest.c_str(), strRequest.size());
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

        if(NowTime - LastCheckTime >= m_CheckTime)
        {
            LastCheckTime = NowTime;
            CheckValid();
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

    uint64_t UserID = HeaderIn.UserID;
    unsigned int CmdID = HeaderIn.CmdID;
    
    switch(CmdID)
    {
        case Cmd_User_Register_Req:
        {
            mm::UserRegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, UserID, "pkg parse failed, cmdid=%0x", CmdID);
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            string strNickName = CurReq.nickname();
            int Sex = CurReq.sex();
            uint64_t Birthday = CurReq.birthday();
            string strTelNo = CurReq.telno(); 
            string strAddress = CurReq.address();
            string strEmail = CurReq.email();

            ShmUserInfo Info;
            Info.UserID = UserID;
            Info.LastActiveTime = time(NULL);
            snprintf(Info.NickName, MAX_NAME_LENGTH, "%s", strNickName.c_str());
            Info.Level = 0;
            Info.VipLevel = 0;
            Info.Sex = Sex % 2;
            Info.Birthday = Birthday;
            snprintf(Info.TelNo, MAX_TELNO_LENGTH, "%s", strTelNo.c_str());
            snprintf(Info.Address, MAX_ADDR_LENGTH, "%s", strAddress.c_str());
            snprintf(Info.EMail, MAX_EMAIL_LENGTH, "%s", strEmail.c_str());

            Ret = m_UserShm.InsertUserInfo(Info);
            if(Ret != 0)
            {
                XF_LOG_WARN(0, UserID, "Register failed, Ret=%d", Ret);
            }

            WriteUserInfo(Info.UserID);

            mm::UserRegisterRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_ret(0);
            
            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_User_Register_Rsp;
            Header.SN = HeaderIn.SN;
            Header.ConnPos = HeaderIn.ConnPos;
            Header.UserID = HeaderIn.UserID;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, HeaderIn.SrcID, TO_SRV, 0, CurRsp);
            
            break;
        }
        case Cmd_User_AddFriend_Req:
        {
            app::AddFriendReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, UserID, "pkg parse failed, cmdid=%0x", CmdID);
                return -1;
            }

            // 加载自己的数据
            Ret = LoadUserInfoWhileNotExist(UserID, string(pCurBuffPos, PkgLen));
            if(Ret == 1)
            {
                return 0;
            }
            else if(Ret != 0)
            {
                XF_LOG_WARN(0, UserID, "LoadUserInfoWhileNotExist failed, Ret=%d", Ret);
                return -1;
            }

            // 这里没有加锁有可能是拿不到的,但运气不会那么背
            ShmUserInfo CurUserInfo;
            Ret = m_UserShm.GetUserInfo(UserID, CurUserInfo);
            if(Ret != 0)
            {
                XF_LOG_WARN(0, UserID, "user not exist");
                return -1;
            }

            string strNickName(CurUserInfo.NickName);

            uint64_t OtherUserID = CurReq.userid();

            // 如果要加的好友也是这台服务器的,则内部处理,否则传到其它server处理
            int ServerID = GetUserServer(OtherUserID);
            if(ServerID == GetServerID())
            {
                // 加载该好友信息
                Ret = LoadUserInfoWhileNotExist(OtherUserID, string(pCurBuffPos, PkgLen));
                if(Ret == 1)
                {
                    return 0;
                }
                else if(Ret != 0)
                {
                    XF_LOG_WARN(0, UserID, "LoadUserInfoWhileNotExist failed, Ret=%d", Ret);
                    return -1;
                }

                Ret = m_UserShm.AddFriendReq(UserID, OtherUserID, strNickName);
                if(Ret != 0)
                {
                   XF_LOG_WARN(0, UserID, "m_UserShm AddFriendReq failed, Ret=%d", Ret);
                   return -1;
                }

                WriteUserInfo(OtherUserID);

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
            }
            else
            {
                mm::UserAddFriendReq CurReq2;
                CurReq2.set_userid(UserID);
                CurReq2.set_nickname(CurUserInfo.NickName);
                CurReq2.set_otheruserid(OtherUserID);

                // 响应也让对方server去回,这里要带上连接要素
                XYHeaderIn Header;
                Header.SrcID = HeaderIn.SrcID;
                Header.CmdID = Cmd_User_AddFriend_Req2;
                Header.SN = HeaderIn.SN;
                Header.ConnPos = HeaderIn.ConnPos;
                Header.UserID = HeaderIn.UserID;
                Header.PkgTime = time(NULL);
                Header.Ret = 0;
                
                Send2Server(Header, ServerID, TO_SRV, 0, CurReq2);
            }

            break;
        }
        case Cmd_User_AddFriend_Req2:
        {
            mm::UserAddFriendReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", CmdID);
                return -1;
            }

            uint64_t UserID1 = CurReq.userid();
            string strNickName = CurReq.nickname();
            uint64_t UserID2 = CurReq.otheruserid();

            // 加载自己的数据
            Ret = LoadUserInfoWhileNotExist(UserID2, string(pCurBuffPos, PkgLen));
            if(Ret == 1)
            {
                return 0;
            }
            else if(Ret != 0)
            {
                XF_LOG_WARN(0, UserID2, "LoadUserInfoWhileNotExist failed, Ret=%d", Ret);
                return -1;
            }

            Ret = m_UserShm.AddFriendReq(UserID1, UserID2, strNickName);
            if(Ret != 0)
            {
               XF_LOG_WARN(0, UserID2, "m_UserShm AddFriendReq failed, Ret=%d", Ret);
               return -1;
            }

            WriteUserInfo(UserID2);

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



int CUser::LoadUserInfo(uint64_t UserID, const string& strRequest)
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
    
    Ret = m_LoaderSendQueue.InQueue(acBuff, PkgLen);
    if(Ret != CShmQueue::SUCCESS)
    {
        XF_LOG_WARN(0, UserID, "m_LoaderSendQueue InQueue failed, Ret=%d", Ret);
        return -1;
    }

    XF_LOG_DEBUG(0, UserID, "UserID = %ld now Loading...", UserID);
    
    return 0;
}


// 0表示用户存在,可以往下跑  1表示用户不在内存,正在加载  其它表示异常
int CUser::LoadUserInfoWhileNotExist(uint64_t UserID, const string& strRequest)
{
    int Ret = 0;
    ShmUserInfo Info;
    Ret = m_UserShm.GetUserInfo(UserID, Info);
    if(Ret != 0)
    {
        Ret = LoadUserInfo(UserID, strRequest);
        if(Ret != 0)
        {
            return -1;
        }

        return 1;
    }

    return 0;
}


int CUser::WriteUserInfo(uint64_t UserID)
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

int CUser::CheckValid()
{
    vector<uint64_t> vctUserID;
    m_UserShm.CheckUserIsValid(&vctUserID);
    for(int i = 0; i < (int)vctUserID.size(); i++)
    {
        m_UserShm.RemoveUserInfoWhenInValid(vctUserID[i]);
    }

    return 0;
}