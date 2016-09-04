
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

#include "auth_proc.h"
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


CAuth::CAuth()
{
    m_ServerID = 0;
    m_StateTime = 0;
    m_pSendBuff = NULL;

    m_DBPort = 0;
    memset(m_DBHost, 0x0, sizeof(m_DBHost));
    memset(m_DBUser, 0x0, sizeof(m_DBUser));
    memset(m_DBPass, 0x0, sizeof(m_DBPass));
    memset(m_DBName, 0x0, sizeof(m_DBName));
    memset(m_TableName, 0x0, sizeof(m_TableName));
}

CAuth::~CAuth()
{
    if(m_pSendBuff)
    {
        SAFE_DELETE(m_pSendBuff);
    }
}


int CAuth::Init(const char *pConfFile)
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

    char BusConfPath[256] = {0};

    if (IniFile.IsValid())
    {
        IniFile.GetInt("AUTH", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("AUTH", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        IniFile.GetInt("AUTH", "StateTime", 0, &m_StateTime);
        IniFile.GetString("AUTH", "Host", "", m_DBHost, sizeof(m_DBHost));
        IniFile.GetInt("AUTH", "Port", 0, &m_DBPort);
        IniFile.GetString("AUTH", "User", "", m_DBUser, sizeof(m_DBUser));
        IniFile.GetString("AUTH", "Pass", "", m_DBPass, sizeof(m_DBPass));
        IniFile.GetString("AUTH", "DB", "", m_DBName, sizeof(m_DBName));
        IniFile.GetString("AUTH", "Table", "", m_TableName, sizeof(m_TableName));
        
        IniFile.GetString("LOG", "ModuleName", "auth", ModuleName, sizeof(ModuleName));
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

    if (0 == SendShmKey || 0 == SendShmSize)
    {
        printf("Error 0 == SendShmKey(%x) || 0 == SendShmSize(%d)", SendShmKey, SendShmSize);
        return -1;
    }
    
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

    
    Ret = m_SendQueue.Init(SendShmKey, SendShmSize);
    if (Ret != 0)
    {
        printf("ERR:init m_SendQueue failed, key=%d, size=%d, err=%s\n",
                SendShmKey, SendShmSize, m_SendQueue.GetErrMsg());
        return -1;
    }
    
    printf("init m_SendQueue succ, key=0x%x, size=%u\n", SendShmKey, SendShmSize);

    Ret = m_DBConn.Connect(m_DBHost, m_DBUser, m_DBPass, m_DBName, m_DBPort);
    if (Ret != 0)
    {
        XF_LOG_ERROR(0, 0, "Connect DB[%s:%s@%s:%d:%s] failed, Ret=%d, ErrMsg=%s", m_DBUser, m_DBPass, m_DBHost, m_DBPort, m_DBName, Ret, m_DBConn.GetErrMsg());
        return -1;
    }

    printf("svr init success\n");

    if(!m_pSendBuff)
    {
        m_pSendBuff = (char*)malloc(XY_PKG_MAX_LEN);
    }
    
    return 0;
}


int CAuth::Run()
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


int CAuth::DealPkg(const char *pCurBuffPos, int PkgLen)
{
    int Ret = 0;
    XYHeaderIn HeaderIn;
    HeaderIn.Read(pCurBuffPos);
    int HeaderInLen = HeaderIn.GetHeaderLen();
    
    switch(HeaderIn.CmdID)
    {
        case Cmd_Login_Req:
        {
            mm::LoginReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Cmd_Login_Req);
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            string strPasswd = CurReq.passwd();
            int Result = LoginCheck(UserID, strPasswd);

            mm::LoginRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_ret(Result);

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_Login_Rsp;
            Header.SN = HeaderIn.SN;
            Header.ConnPos = HeaderIn.ConnPos;
            Header.UserID = HeaderIn.UserID;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, HeaderIn.SrcID, TO_SRV, 0, CurRsp);
            
            break;
        }
        case Cmd_Auth_Register_Req:
        {
            app::RegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Cmd_Auth_Register_Req);
                return -1;
            }

            string strPasswd = CurReq.passwd();
            
            uint64_t UserID = 0;
            Ret = Register(strPasswd, UserID);
            app::RegisterRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_ret(Ret);

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_Auth_Register_Rsp;
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


int CAuth::Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message)
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


/* 0 系统错误  1 验证通过  2 密码错误或用户不存在 */
int CAuth::LoginCheck(uint64_t UserID, const string& strPasswd)
{
    char SqlStr[1024] = {0};
    int RecNum = 0;
    int SqlLen = snprintf(SqlStr, sizeof(SqlStr), "select passwd from %s.%s where userid=%lu", m_DBName, m_TableName, UserID);
    int Ret = m_DBConn.Query(SqlStr, SqlLen, &RecNum);
    if (Ret != 0)
    {
        XF_LOG_WARN(0, UserID,  "query db ret failed, ret=%d, errmsg=%s, sql=%s", Ret, m_DBConn.GetErrMsg(), SqlStr);
        return 0;
    }

    if(RecNum == 0)
    {
        return 2;
    }

    //读取数据
    MYSQL_ROW CurRow = m_DBConn.FetchRecord();
    unsigned long *pCurRowLen = m_DBConn.FetchLength();

    if ((CurRow[0] == NULL)||(pCurRowLen[0] == 0))
    {
        XF_LOG_WARN(0, UserID,  "sql query ret is not valid, prow=%s, len=%ld", CurRow[0], pCurRowLen[0]);
        return 0;
    }

    string strResult(CurRow[0], pCurRowLen[0]);

    if (strResult != strPasswd)
    {
        return 2;
    }
    else
    {
        return 1;
    }
    
    return 0;
}


/* 0 系统错误  1 注册成功 */
int CAuth::Register(const std::string& strPasswd, uint64_t& UserID)
{
    UserID = time(NULL);  // 这里还没想好方案，先这样子吧
    
    char SqlStr[1024] = {0};
    int SqlLen = snprintf(SqlStr, sizeof(SqlStr), "insert into %s.%s values (%lu, '%s')", m_DBName, m_TableName, UserID, strPasswd.c_str());
    int Ret = m_DBConn.Query(SqlStr, SqlLen);
    if (Ret != 0)
    {
        XF_LOG_WARN(0, UserID,  "query db ret failed, ret=%d, errmsg=%s, sql=%s", Ret, m_DBConn.GetErrMsg(), SqlStr);
        return 0;
    }

    return 1;
}



int CAuth::SendStateMessage()
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

