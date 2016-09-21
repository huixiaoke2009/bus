#include <signal.h>
#include "ini_file/ini_file.h"
#include "log/log.h"
#include "writer_user.h"
#include <time.h>
#include <unistd.h>
#include "bus.pb.h"
#include "mm.pb.h"
#include "app.pb.h"


using namespace mmlib;
using namespace std;


bool StopFlag = false;
bool ReloadFlag = false;

static void SigHandler(int iSigNo)
{
    Log(0, 0, LOG_INFO, "%s get signal %d", __func__, iSigNo);
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


CWriterUser::CWriterUser()
{
    for(int i = 0; i < USER_DATABASE_NUM; i++)
    {
        m_DBConfig[i].Port = 0;
        memset(m_DBConfig[i].Host, 0x0, sizeof(m_DBConfig[i].Host));
        memset(m_DBConfig[i].User, 0x0, sizeof(m_DBConfig[i].User));
        memset(m_DBConfig[i].Pass, 0x0, sizeof(m_DBConfig[i].Pass));
        memset(m_DBConfig[i].DBName, 0x0, sizeof(m_DBConfig[i].DBName));
        memset(m_DBConfig[i].TableName, 0x0, sizeof(m_DBConfig[i].TableName));
    }
}

CWriterUser::~CWriterUser()
{

}

int CWriterUser::Init(const char* pConfFile)
{
    int Ret = 0;

    int ProcNum = 0;
    int WriterShmKey = 0;
    int WriterShmSize = 0;
    int LogLocal = 0;
    char ModuleName[32] = {0};
    int LogLevel = 3;
    char LogPath[256] = {0};

    char DBConfPath[256] = {0};
    char UserConfPath[256] = {0};
    
    CIniFile CurConf(pConfFile);
    if (CurConf.IsValid())
    {
        //读取配置信息
        CurConf.GetInt("WRITER", "ProcNum", 1, &ProcNum);
        CurConf.GetInt("WRITER", "WriterShmKey", 0, &WriterShmKey);
        CurConf.GetInt("WRITER", "WriterShmSize", 0, &WriterShmSize);
        CurConf.GetString("WRITER", "DBConfPath", "", DBConfPath, sizeof(DBConfPath));
        CurConf.GetString("WRITER", "UserConfPath", "", UserConfPath, sizeof(UserConfPath));
        
        CurConf.GetInt("WRITER", "LogLocal", 0, &LogLocal);
        CurConf.GetString("WRITER", "ModuleName", "writer", ModuleName, sizeof(ModuleName));
        CurConf.GetInt("WRITER", "LogLevel", 3, &LogLevel);
        CurConf.GetString("WRITER", "LogPath", "log", LogPath, sizeof(LogPath));
    }
    else
    {
        printf("config file[%s] is not valid\n", pConfFile);
        return -1;
    }

    Ret = m_UserShm.Init(UserConfPath);
    if (Ret != 0)
    {
        return -1;
    }
    
    if (WriterShmKey == 0)
    {
        printf("conf WRITER/WriterShmKey[%d] is not valid\n", WriterShmKey);
        return -1;
    }

    if (WriterShmSize == 0)
    {
        printf("conf WRITER/WriterShmSize[%d] is not valid\n", WriterShmSize);
        return -1;
    }

    Ret = m_WriterQueue.Init(WriterShmKey, WriterShmSize);
    if (Ret != 0)
    {
        printf("init m_WriterQueue[%d/%d] failed, Ret=%d\n", WriterShmKey, WriterShmSize, Ret);
        return -1;
    }

    //初始化进程数
    SetChildNum(ProcNum, ProcNum);

    //开启日志
    OpenLog(ModuleName);
    if (LogLocal == 1)
    {
        SetLogLocal(1, LogLevel, LogPath);
    }

    printf("INFO:open log succ\n");


    CIniFile DBFile(DBConfPath);
    if (!DBFile.IsValid())
    {
        printf("ERR:conf file [%s] is not valid\n", DBConfPath);
        return -1;
    }

    for(int i = 0; i < USER_DATABASE_NUM; i++)
    {
        string str = CStrTool::Format("DB_%d", i);
        DBFile.GetString(str.c_str(), "Host", "", m_DBConfig[i].Host, sizeof(m_DBConfig[i].Host));
        DBFile.GetInt(str.c_str(), "Port", 0, &m_DBConfig[i].Port);
        DBFile.GetString(str.c_str(), "User", "", m_DBConfig[i].User, sizeof(m_DBConfig[i].User));
        DBFile.GetString(str.c_str(), "Pass", "", m_DBConfig[i].Pass, sizeof(m_DBConfig[i].Pass));
        DBFile.GetString(str.c_str(), "DB", "", m_DBConfig[i].DBName, sizeof(m_DBConfig[i].DBName));
        DBFile.GetString(str.c_str(), "Table", "", m_DBConfig[i].TableName, sizeof(m_DBConfig[i].TableName));
    }
    

    //设置信号处理
    StopFlag = false;
    ReloadFlag = false;
    struct sigaction stSiga;
    memset(&stSiga, 0, sizeof(stSiga));
    stSiga.sa_handler = SigHandler;

    sigaction(SIGCHLD, &stSiga, NULL);
    sigaction(SIGUSR1, &stSiga, NULL);
    sigaction(SIGUSR2, &stSiga, NULL);

    //忽略信号量
    signal(SIGPIPE, SIG_IGN);

    printf("INFO:init signal succ\n");

    return 0;
}

int CWriterUser::Entity(int argc, char *argv[])
{
    int Ret = 0;

    for(int i = 0; i < USER_DATABASE_NUM; i++)
    {
        Ret = m_DBConn[i].Connect(m_DBConfig[i].Host, m_DBConfig[i].User, m_DBConfig[i].Pass, m_DBConfig[i].DBName, m_DBConfig[i].Port);
        if (Ret != 0)
        {
            printf("Connect DB[%s:%s@%s:%d:%s] failed, Ret=%d, ErrMsg=%s\n", m_DBConfig[i].User, m_DBConfig[i].Pass, m_DBConfig[i].Host, m_DBConfig[i].Port, m_DBConfig[i].DBName, Ret, m_DBConn[i].GetErrMsg());
            return -1;
        }
    }

    int EmptyFlag = 0;
    while (!StopFlag)
    {
        EmptyFlag = 0;

        char RecvBuff[128] = {0};
        int RecvLen = sizeof(RecvBuff);
        Ret = m_WriterQueue.OutQueue(RecvBuff, &RecvLen);
        if (Ret == m_WriterQueue.E_SHM_QUEUE_EMPTY)
        {
            EmptyFlag = 1;
        }
        else if (Ret != m_WriterQueue.SUCCESS)
        {
            XF_LOG_WARN(0, 0, "m_WriterQueue out failed, ret=%d", Ret);
            EmptyFlag = 1;    //为了防止死循环输出日志
        }
        else
        {
            mm::WriteUserInfoReq CurReq;
            if(!CurReq.ParseFromArray(RecvBuff, RecvLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed");
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            ProcessWriterUserInfo(UserID);
        }

        if (EmptyFlag == 1)
        {
            usleep(10000);
        }

    }

    return 0;
}

int CWriterUser::Struct2ProtoStringUser(const ShmUserInfo &CurUserInfo,std::string &strProto, uint64_t UserID)
{
    mm::DBUserInfo User;
    User.set_userid(CurUserInfo.UserID);
    User.set_nickname(CurUserInfo.NickName);
    User.set_level(CurUserInfo.Level);
    User.set_viplevel(CurUserInfo.VipLevel);
    User.set_sex(CurUserInfo.Sex);
    User.set_birthday(CurUserInfo.Birthday);
    User.set_telno(CurUserInfo.TelNo);
    User.set_address(CurUserInfo.Address);
    User.set_email(CurUserInfo.EMail);
    User.set_personalnote(CurUserInfo.PersonalNote);

    if (!User.SerializeToString(&strProto))
    {
        XF_LOG_WARN(0, UserID,  "SerialToString faile|errmsg:%s", User.ShortDebugString().c_str());
        return -1;
    }

    return 0;
}

int CWriterUser::CheckUserInfoValid(uint64_t UserID, const ShmUserInfo &CurUserInfo)
{
    if (UserID != CurUserInfo.UserID)
    {
        XF_LOG_WARN(0, UserID, "userid no equal|%ld", CurUserInfo.UserID);
        return 1;
    }

    if (0 == CurUserInfo.UserID)
    {
        XF_LOG_WARN(0, UserID, "UserID == 0");
        return 2;
    }


    return 0;
}


int CWriterUser::ProcessWriterUserInfo(uint64_t UserID)
{
    int Ret = 0;
    ShmUserInfo CurUserInfo;
    Ret = m_UserShm.GetUserInfo(UserID, CurUserInfo);
    if (0 != Ret)
    {
        XF_LOG_WARN(0, UserID, "GetUserInfo faile ret=%d", Ret);
        return -1;
    }
    
    Ret = CheckUserInfoValid(UserID, CurUserInfo);
    if ( 0 != Ret)
    {
        XF_LOG_WARN(0, UserID, "CheckUserInfoValid fail|ret=%d", Ret);
        return -1;
    }

    int DBIndex = UserID % USER_DATABASE_NUM;
    int TableIndex = (UserID>>1) % USER_TABLE_NUM;

    //有效状态才回写
    string strProto;
    Ret = Struct2ProtoStringUser(CurUserInfo, strProto, UserID);
    if ( 0 != Ret)
    {
        XF_LOG_WARN(0, UserID, "Struct2ProtoStringUser fail|ret=%d", Ret);
        return -3;
    }

    char szValString[sizeof(ShmUserInfo)] = {0};

    int iValStringLen = m_DBConn[DBIndex].EscapeString(szValString, strProto.c_str(), strProto.size());

    if (iValStringLen >= (int)sizeof(szValString))
    {
        XF_LOG_WARN(0, UserID, "EscapeString fail|vallen=%d|szlen=%d",iValStringLen, (int)sizeof(szValString));
        return -4;
    }
    //szValString 已经是不是二进制流，被转义成字符串。
    string strSql = CStrTool::Format("REPLACE INTO %s.%s_%d values ( %lu,'", m_DBConfig[DBIndex].DBName, m_DBConfig[DBIndex].TableName, TableIndex, UserID);
    strSql += szValString;
    strSql += "')";

    XF_LOG_INFO(0, UserID, "sqlsize=%d|datasize=%d|valsize=%d", (int)strSql.size(), (int)strProto.size(), (int)strlen(szValString));

    Ret = m_DBConn[DBIndex].Query((char *)strSql.c_str(), strSql.size());
    if (0 != Ret)
    {
        XF_LOG_WARN(0, UserID, "Queryfailed, ret=%d|errMsg=%s", Ret, m_DBConn[DBIndex].GetErrMsg());
        return -2;
    }

    return 0;
}


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("usage: %s <conf_file>\n", argv[0]);
        return -1;
    }

    daemon(1, 1);

    int Ret = 0;
    CWriterUser Proc;

    Ret = Proc.Init(argv[1]);
    if (Ret != 0)
    {
        printf("ERR:proc init failed, ret=%d\n", Ret);
        return -1;
    }

    Ret = Proc.Run(argc, argv);

    return Ret;
}

