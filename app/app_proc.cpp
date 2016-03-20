
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

#include "app_proc.h"
#include "log/log.h"
#include "util/util.h"
#include "ini_file/ini_file.h"

#include "app.pb.h"
#include "bus_header.h"
#include "cmd.h"

using namespace std;
using namespace mmlib;

bool StopFlag = false;
bool ReloadFlag = false;

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


CApp::CApp()
{
    m_SendFlag = 0;
    m_ServerID = 0;
}

CApp::~CApp()
{

}


int CApp::Init(const char *pConfFile)
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
        IniFile.GetInt("APP", "SendFlag", 0, &m_SendFlag);
        IniFile.GetInt("APP", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("APP", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        
        IniFile.GetString("LOG", "ModuleName", "app", ModuleName, sizeof(ModuleName));
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

    

    printf("svr init success\n");
    
    return 0;
}


int CApp::Run()
{
    int Ret = 0;
    
    char *pRecvBuff = (char *)malloc(XY_MAXBUFF_LEN);
    int RecvLen = XY_MAXBUFF_LEN;
    
    while(true)
    {
        if(m_SendFlag == 1)
        {
            BusHeader CurHeader;
            int HeaderLen = CurHeader.GetHeaderLen();
            
            RecvLen = XY_MAXBUFF_LEN - HeaderLen;
            
            app::MyData CurReq;
            CurReq.set_data1(CRandomTool::Get(0, 100));
            CurReq.set_data2(CStrTool::Format("%d", CRandomTool::Get(0, 100)));
            CurReq.mutable_data3()->set_kk(CRandomTool::Get(0, 100));
            CurReq.mutable_data3()->set_ss(CStrTool::Format("%d", CRandomTool::Get(0, 100)));
            if (!CurReq.SerializeToArray(pRecvBuff+HeaderLen, RecvLen))
            {
                printf("SerializeToArray failed, %s\n", CurReq.ShortDebugString().c_str());
                return -1;
            }
            
            RecvLen = CurReq.ByteSize();
            CurHeader.PkgLen = RecvLen+HeaderLen;
            CurHeader.CmdID = 0x10010003;
            CurHeader.SrcID = m_ServerID;
            CurHeader.DstID = 201;
            CurHeader.SN = 0;
            CurHeader.Ret = 0;
            
            CurHeader.Write(pRecvBuff);
            
            m_SendQueue.InQueue(pRecvBuff, CurHeader.PkgLen);
            if (Ret == m_SendQueue.E_SHM_QUEUE_FULL)
            {
                printf("m_SendQueue in queue full\n");
            }
            else if (Ret != 0)
            {
                printf("m_SendQueue in queue fail|ret=%d\n",Ret);
            }
            else
            {
                //入queue成功
                XF_LOG_TRACE(0, 0, "m_ProxyQueue in queue success\n");
            }
            
            usleep(100000);
        }
        else
        {
            // recieve
            RecvLen = XY_MAXBUFF_LEN;
            Ret = m_RecvQueue.OutQueue(pRecvBuff, &RecvLen);
            if (Ret == m_RecvQueue.E_SHM_QUEUE_EMPTY)
            {
                
            }
            else if(Ret != 0)
            {
                 //出错了
                 printf("out queue failed, ret=%d, errmsg=%s\n", Ret, m_RecvQueue.GetErrMsg());
            }
            else
            {
                BusHeader CurHeader;
                int HeaderLen = CurHeader.GetHeaderLen();
                CurHeader.Read(pRecvBuff);
                app::MyData CurRsp;
                if (!CurRsp.ParseFromArray(pRecvBuff+HeaderLen, RecvLen-HeaderLen))
                {
                    printf("Parse Pkg failed\n");
                    return -1;
                }
                
                printf("msg:%ld:%s\n", time(NULL), CurRsp.ShortDebugString().c_str());
            }
            
            usleep(1000);
        }
        
    }
    
    return 0;
}
