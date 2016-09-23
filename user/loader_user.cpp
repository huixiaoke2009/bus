#include <signal.h>
#include "ini_file/ini_file.h"
#include "log/log.h"
#include "loader_user.h"
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


CLoaderUser::CLoaderUser()
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

CLoaderUser::~CLoaderUser()
{

}

int CLoaderUser::Init(const char* pConfFile)
{
    int Ret = 0;

    int ProcNum = 0;
    int LoaderRecvShmKey = 0;
    int LoaderRecvShmSize = 0;
    int LoaderSendShmKey = 0;
    int LoaderSendShmSize = 0;
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
        CurConf.GetInt("LOADER", "ProcNum", 1, &ProcNum);
        CurConf.GetInt("LOADER", "LoaderRecvShmKey", 0, &LoaderRecvShmKey);
        CurConf.GetInt("LOADER", "LoaderRecvShmSize", 0, &LoaderRecvShmSize);
        CurConf.GetInt("LOADER", "LoaderSendShmKey", 0, &LoaderSendShmKey);
        CurConf.GetInt("LOADER", "LoaderSendShmSize", 0, &LoaderSendShmSize);
        CurConf.GetString("LOADER", "DBConfPath", "", DBConfPath, sizeof(DBConfPath));
        CurConf.GetString("LOADER", "UserConfPath", "", UserConfPath, sizeof(UserConfPath));
        
        CurConf.GetInt("LOADER", "LogLocal", 0, &LogLocal);
        CurConf.GetString("LOADER", "ModuleName", "loader", ModuleName, sizeof(ModuleName));
        CurConf.GetInt("LOADER", "LogLevel", 3, &LogLevel);
        CurConf.GetString("LOADER", "LogPath", "log", LogPath, sizeof(LogPath));
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
    
    if (LoaderRecvShmKey == 0)
    {
        printf("conf LOADER/LoaderRecvShmKey[%d] is not valid\n", LoaderRecvShmKey);
        return -1;
    }

    if (LoaderRecvShmSize == 0)
    {
        printf("conf LOADER/LoaderRecvShmSize[%d] is not valid\n", LoaderRecvShmSize);
        return -1;
    }

    Ret = m_LoaderRecvQueue.Init(LoaderRecvShmKey, LoaderRecvShmSize);
    if (Ret != 0)
    {
        printf("init m_LoaderRecvQueue[%d/%d] failed, Ret=%d\n", LoaderRecvShmKey, LoaderRecvShmSize, Ret);
        return -1;
    }

    if (LoaderSendShmKey == 0)
    {
        printf("conf LOADER/LoaderSendShmKey[%d] is not valid\n", LoaderSendShmKey);
        return -1;
    }

    if (LoaderSendShmSize == 0)
    {
        printf("conf LOADER/LoaderSendShmSize[%d] is not valid\n", LoaderSendShmSize);
        return -1;
    }

    Ret = m_LoaderSendQueue.Init(LoaderSendShmKey, LoaderSendShmSize);
    if (Ret != 0)
    {
        printf("init m_LoaderRecvQueue[%d/%d] failed, Ret=%d\n", LoaderSendShmKey, LoaderSendShmSize, Ret);
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

int CLoaderUser::Entity(int argc, char *argv[])
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

        char pRecvBuff[XY_PKG_MAX_LEN] = {0};
        int RecvLen = sizeof(pRecvBuff);
        Ret = m_LoaderRecvQueue.OutQueue(pRecvBuff, &RecvLen);
        if (Ret == m_LoaderRecvQueue.E_SHM_QUEUE_EMPTY)
        {
            EmptyFlag = 1;
        }
        else if (Ret != m_LoaderRecvQueue.SUCCESS)
        {
            XF_LOG_WARN(0, 0, "m_LoaderRecvQueue out failed, ret=%d", Ret);
            EmptyFlag = 1;    //为了防止死循环输出日志
        }
        else
        {
            mm::LoadUserInfoReq CurReq;
            if(!CurReq.ParseFromArray(pRecvBuff, RecvLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed");
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            string strRequest = CurReq.request();
            ProcessLoaderUserInfo(UserID, strRequest);
        }

        if (EmptyFlag == 1)
        {
            usleep(10000);
        }

    }

    return 0;
}



int CLoaderUser::ProcessLoaderUserInfo(uint64_t UserID, const string& strRequest)
{
    int Ret = 0;
    char SqlStr[256] = {0};
    int SqlLen = 0;

    int DBIndex = GetUserDBIndex(UserID);
    int TableIndex = GetUserTableIndex(UserID);
    int Result = 0;
do{
    int RecNum = 0;
    SqlLen = snprintf(SqlStr, sizeof(SqlStr), "select data from %s.%s_%d where id=%lu limit 1", m_DBConfig[DBIndex].DBName, m_DBConfig[DBIndex].TableName, TableIndex, UserID);
    Ret = m_DBConn[DBIndex].Query(SqlStr, SqlLen, &RecNum);
    if (Ret != 0)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID,  "query db ret failed, ret=%d, errmsg=%s, sql=%s", Ret, m_DBConn[DBIndex].GetErrMsg(), SqlStr);
        //TODO 需要返回加载失败
        break;
    }

    ShmUserInfo CurUserInfo;
    

    //用户不存在
    if (0 == RecNum)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID, "query db recnum is 0, rec_num=%d, sql=%s", RecNum, SqlStr);
        break;
    }

    if (RecNum != 1)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID, "query db recnum is not 1, rec_num=%d, sql=%s", RecNum, SqlStr);
        break;
    }

    //读取数据
    MYSQL_ROW CurRow = m_DBConn[DBIndex].FetchRecord();
    unsigned long *pCurRowLen = m_DBConn[DBIndex].FetchLength();

    if (CurRow[0] == NULL || pCurRowLen[0] == 0)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID,  "sql query ret is not valid, prow=%s, len=%ld", CurRow[0], pCurRowLen[0]);
        break;
    }

    Ret = UserProtoString2Struct(CurUserInfo, std::string(CurRow[0], pCurRowLen[0]));
    if (Ret != 0)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID,  "UserProtoString2Struct faile ret=%d", Ret);
        break;
    }

    Ret = m_UserShm.InsertUserInfo(CurUserInfo);
    if (0 != Ret)
    {
        Result = -1;
        XF_LOG_WARN(0, UserID,  "UpdateUserInfo faile");
        break;
    }

}while(false);
    
    //释放查询结果
    m_DBConn[DBIndex].ReleaseRes();

    mm::LoadUserInfoRsp CurRsp;
    CurRsp.set_ret(Result);
    CurRsp.set_request(strRequest);

    const int BuffLen = XY_PKG_MAX_LEN+16;
    char acBuff[BuffLen] = {0};

    int PkgLen = CurRsp.ByteSize();

    if(!CurRsp.SerializeToArray(acBuff, BuffLen))
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
    
    return 0;
}

int CLoaderUser::UserProtoString2Struct(ShmUserInfo &CurUserInfo, const std::string &strProto)
{
    mm::DBUserInfo User;

    if (!User.ParseFromArray(strProto.c_str(),strProto.size()))
    {
        XF_LOG_WARN(0, 0,  "error=%s|ParseFromString faile|errmsg:%s", User.InitializationErrorString().c_str(),User.ShortDebugString().c_str());
        return -1;
    }

    CurUserInfo.UserID = User.userid();
    CurUserInfo.Status = USER_STATUS_LOADED;
    snprintf(CurUserInfo.NickName, sizeof(CurUserInfo.NickName), User.nickname().c_str());
    CurUserInfo.Level = User.level();
    CurUserInfo.VipLevel = User.viplevel();
    CurUserInfo.Sex = User.sex();
    snprintf(CurUserInfo.TelNo, sizeof(CurUserInfo.TelNo), User.telno().c_str());
    snprintf(CurUserInfo.Address, sizeof(CurUserInfo.Address), User.address().c_str());
    snprintf(CurUserInfo.EMail, sizeof(CurUserInfo.EMail), User.email().c_str());
    snprintf(CurUserInfo.PersonalNote, sizeof(CurUserInfo.PersonalNote), User.personalnote().c_str());
    
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
    CLoaderUser Proc;

    Ret = Proc.Init(argv[1]);
    if (Ret != 0)
    {
        printf("ERR:proc init failed, ret=%d\n", Ret);
        return -1;
    }

    Ret = Proc.Run(argc, argv);

    return Ret;
}

