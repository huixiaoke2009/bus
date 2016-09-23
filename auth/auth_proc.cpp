
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

    for(int i = 0; i < AUTH_DATABASE_NUM; i++)
    {
        m_DBConfig[i].Port = 0;
        memset(m_DBConfig[i].Host, 0x0, sizeof(m_DBConfig[i].Host));
        memset(m_DBConfig[i].User, 0x0, sizeof(m_DBConfig[i].User));
        memset(m_DBConfig[i].Pass, 0x0, sizeof(m_DBConfig[i].Pass));
        memset(m_DBConfig[i].DBName, 0x0, sizeof(m_DBConfig[i].DBName));
        memset(m_DBConfig[i].TableName, 0x0, sizeof(m_DBConfig[i].TableName));
    }
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
    char DBConfPath[256] = {0};
    
    if (IniFile.IsValid())
    {
        IniFile.GetInt("AUTH", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("AUTH", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        IniFile.GetString("AUTH", "DBConfPath", "", DBConfPath, sizeof(DBConfPath));
        IniFile.GetInt("AUTH", "StateTime", 0, &m_StateTime);
        
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

    CIniFile DBFile(DBConfPath);
    if (!DBFile.IsValid())
    {
        printf("ERR:conf file [%s] is not valid\n", DBConfPath);
        return -1;
    }

    for(int i = 0; i < AUTH_DATABASE_NUM; i++)
    {
        string str = CStrTool::Format("DB_%d", i);
        DBFile.GetString(str.c_str(), "Host", "", m_DBConfig[i].Host, sizeof(m_DBConfig[i].Host));
        DBFile.GetInt(str.c_str(), "Port", 0, &m_DBConfig[i].Port);
        DBFile.GetString(str.c_str(), "User", "", m_DBConfig[i].User, sizeof(m_DBConfig[i].User));
        DBFile.GetString(str.c_str(), "Pass", "", m_DBConfig[i].Pass, sizeof(m_DBConfig[i].Pass));
        DBFile.GetString(str.c_str(), "DB", "", m_DBConfig[i].DBName, sizeof(m_DBConfig[i].DBName));
        DBFile.GetString(str.c_str(), "Table", "", m_DBConfig[i].TableName, sizeof(m_DBConfig[i].TableName));
    }
    
    for(int i = 0; i < AUTH_DATABASE_NUM; i++)
    {
        Ret = m_DBConn[i].Connect(m_DBConfig[i].Host, m_DBConfig[i].User, m_DBConfig[i].Pass, m_DBConfig[i].DBName, m_DBConfig[i].Port);
        if (Ret != 0)
        {
            printf("Connect DB[%s:%s@%s:%d:%s] failed, Ret=%d, ErrMsg=%s\n", m_DBConfig[i].User, m_DBConfig[i].Pass, m_DBConfig[i].Host, m_DBConfig[i].Port, m_DBConfig[i].DBName, Ret, m_DBConn[i].GetErrMsg());
            return -1;
        }
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
        case Cmd_Auth_Register_Req:
        {
            mm::AuthRegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", HeaderIn.CmdID);
                return -1;
            }

            string strPasswd = CurReq.passwd();
            string strNickName = CurReq.nickname();
            int Sex = CurReq.sex();
            uint64_t Birthday = CurReq.birthday();
            string strTelNo = CurReq.telno(); 
            string strAddress = CurReq.address();
            string strEmail = CurReq.email();
            
            uint64_t UserID = 0;
            Ret = Register(strPasswd, UserID);
            mm::AuthRegisterRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_nickname(strNickName);
            CurRsp.set_sex(Sex);
            CurRsp.set_birthday(Birthday);
            CurRsp.set_telno(strTelNo);
            CurRsp.set_address(strAddress);
            CurRsp.set_email(strEmail);
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
        case Cmd_Auth_Login_Req:
        {
            mm::AuthLoginReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", HeaderIn.CmdID);
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            string strPasswd = CurReq.passwd();
            int Plat = CurReq.plat();
            
            int Result = LoginCheck(UserID, strPasswd, Plat);

            mm::AuthLoginRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_ret(Result);

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_Auth_Login_Rsp;
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


/* 0 验证通过  1 系统错误  2 密码错误或用户不存在 */
int CAuth::LoginCheck(uint64_t UserID, const string& strPasswd, int Plat)
{
    int DBIndex = GetUserDBIndex(UserID);
    int TableIndex = GetUserTableIndex(UserID);
    
    char SqlStr[1024] = {0};
    int RecNum = 0;
    int SqlLen = snprintf(SqlStr, sizeof(SqlStr), "select passwd from %s.%s_%d where userid=%lu", m_DBConfig[DBIndex].DBName, m_DBConfig[DBIndex].TableName, TableIndex, UserID);
    int Ret = m_DBConn[DBIndex].Query(SqlStr, SqlLen, &RecNum);
    if (Ret != 0)
    {
        XF_LOG_WARN(0, UserID,  "query db ret failed, ret=%d, errmsg=%s, sql=%s", Ret, m_DBConn[DBIndex].GetErrMsg(), SqlStr);
        return 1;
    }

    if(RecNum == 0)
    {
        return 2;
    }

    //读取数据
    MYSQL_ROW CurRow = m_DBConn[DBIndex].FetchRecord();
    unsigned long *pCurRowLen = m_DBConn[DBIndex].FetchLength();

    if ((CurRow[0] == NULL)||(pCurRowLen[0] == 0))
    {
        XF_LOG_WARN(0, UserID,  "sql query ret is not valid, prow=%s, len=%ld", CurRow[0], pCurRowLen[0]);
        return 1;
    }

    string strResult(CurRow[0], pCurRowLen[0]);

    if (strResult != strPasswd)
    {
        return 2;
    }
    else
    {
        return 0;
    }
    
    return 1;
}


/* 0 系统错误  1 注册成功 */
int CAuth::Register(const std::string& strPasswd, uint64_t& UserID)
{
    UserID = time(NULL);  // 这里还没想好方案，先这样子吧
    
    int DBIndex = GetUserDBIndex(UserID);
    int TableIndex = GetUserTableIndex(UserID);
    
    char SqlStr[1024] = {0};
    int SqlLen = snprintf(SqlStr, sizeof(SqlStr), "insert into %s.%s_%d (userid, passwd) values (%lu, '%s')", m_DBConfig[DBIndex].DBName, m_DBConfig[DBIndex].TableName, TableIndex, UserID, strPasswd.c_str());
    int Ret = m_DBConn[DBIndex].Query(SqlStr, SqlLen);
    if (Ret != 0)
    {
        XF_LOG_WARN(0, UserID,  "query db failed, ret=%d, errmsg=%s, sql=%s", Ret, m_DBConn[DBIndex].GetErrMsg(), SqlStr);
        return 1;
    }

    return 0;
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

