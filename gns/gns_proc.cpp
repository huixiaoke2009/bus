
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

#include "gns_proc.h"
#include "log/log.h"
#include "util/util.h"
#include "ini_file/ini_file.h"

#include "bus.pb.h"
#include "mm.pb.h"
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


CGns::CGns()
{
    m_ServerID = 0;
    m_StateTime = 0;
    m_pSendBuff = NULL;
    m_MaxGnsNodeNum = 0;
}

CGns::~CGns()
{
    if(m_pSendBuff)
    {
        SAFE_DELETE(m_pSendBuff);
    }
}


int CGns::Init(const char *pConfFile)
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

    int GnsShmKey = 0;
    int GnsShmSize = 0;
    char BusConfPath[256] = {0};

    if (IniFile.IsValid())
    {
        IniFile.GetInt("GNS", "ServerID", 0, (int*)&m_ServerID);
        IniFile.GetString("GNS", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        IniFile.GetInt("GNS", "GnsShmKey", 0, &GnsShmKey);
        IniFile.GetInt("GNS", "GnsShmSize", 0, &GnsShmSize);
        IniFile.GetInt("GNS", "NodeNum", 0, &m_MaxGnsNodeNum);
        IniFile.GetInt("GNS", "StateTime", 0, &m_StateTime);
        IniFile.GetString("LOG", "ModuleName", "gns", ModuleName, sizeof(ModuleName));
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


    if(!m_pSendBuff)
    {
        m_pSendBuff = (char*)malloc(XY_PKG_MAX_LEN);
    }
    
    if (GnsShmKey == 0)
    {
        printf("ERR:conf GNS/GnsShmKey is not valid\n");
        return -1;
    }

    if (GnsShmSize == 0)
    {
        printf("ERR:conf GNS/GnsShmSize is not valid\n");
        return -1;
    }
    
    if (m_MaxGnsNodeNum == 0)
    {
        printf("ERR:conf GNS/NodeNum is not valid\n");
        return -1;
    }

    int GnsMemHeadSize = GNS_MEM_HEAD_SIZE;
    int GnsInfoMemSize = m_GnsInfoMap.CalcSize(m_MaxGnsNodeNum, m_MaxGnsNodeNum);

    int GnsMemNeed =  GnsInfoMemSize + GnsMemHeadSize;
    if (GnsMemNeed > GnsShmSize)
    {
        printf("ERR:conf GNS/GnsShmSize[%d] is not enough, need %d\n", GnsShmSize, GnsMemNeed);
        return -1;
    }

    printf("GnsMemNeed=%d, GnsShmSize=%d\n", GnsMemNeed, GnsShmSize);

    Ret = m_GnsMem.Create(GnsShmKey, GnsShmSize, 0666);
    if ((Ret != m_GnsMem.SUCCESS)&&(Ret != m_GnsMem.SHM_EXIST))
    {
        printf("ERR:create gns shm failed, key=%d, size=%d, ret=%d\n", GnsShmKey, GnsShmSize, Ret);
        return -1;
    }

    Ret = m_GnsMem.Attach();
    if (Ret != m_GnsMem.SUCCESS)
    {
        printf("ERR:attach gns shm failed, key=%d, size=%d, ret=%d\n", GnsShmKey, GnsShmSize, Ret);
        return -1;
    }

    printf("INFO:gns shm create succ\n");

    m_pGnsHead = (tagGnsMemHead *)m_GnsMem.GetMem();
    void *pGnsInfoMem = ((char *)m_GnsMem.GetMem()) + GnsMemHeadSize;

    if (m_pGnsHead == NULL)
    {
        printf("ERR:create shm failed\n");
        return -1;
    }

    bool ClearFlag = false;
    if (memcmp(m_pGnsHead->Magic, GNS_MEM_MAGIC, sizeof(m_pGnsHead->Magic)) != 0)
    {
        ClearFlag = true;
        memset(m_pGnsHead, 0, GNS_MEM_HEAD_SIZE);
        memcpy(m_pGnsHead->Magic, GNS_MEM_MAGIC, sizeof(m_pGnsHead->Magic));
        printf("WARN:gns map shoud clear\n");
    }

    Ret = m_GnsInfoMap.Init(pGnsInfoMem, GnsInfoMemSize, m_MaxGnsNodeNum, m_MaxGnsNodeNum);
    if (Ret != 0)
    {
        printf("ERR:init gns info shm failed, ret=%d\n", Ret);
        return -1;
    }

    printf("INFO:gns info map init succ\n");

    if (ClearFlag)
    {
        m_GnsInfoMap.Clear();
        ClearFlag = false;
    }

    Ret = m_GnsInfoMap.Verify();
    if (Ret != 0)
    {
        printf("WARN:gns info verify failed, Ret = %d\n", Ret);
        return -1;
    }
    else
    {
        printf("INFO:gns info map verify succ\n");
    }

    printf("svr init success\n");

    return 0;
}


int CGns::Run()
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


int CGns::DealPkg(const char *pCurBuffPos, int PkgLen)
{
    int Ret = 0;
    
    XYHeaderIn HeaderIn;
    HeaderIn.Read(pCurBuffPos);
    int HeaderInLen = HeaderIn.GetHeaderLen();
    
    switch(HeaderIn.CmdID)
    {
        case Cmd_GNS_Register_Req:
        {
            mm::GNSRegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", HeaderIn.CmdID);
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            int ServerID = CurReq.serverid();
            int ConnPos = CurReq.connpos();

            ShmGnsInfo Info;
            Info.UserID = UserID;
            Info.ServerID = ServerID;
            Info.ConnPos = ConnPos;
            Info.Status = GNS_USER_STATUS_ACTIVE;

            ShmGnsInfo tmp;
            Ret = m_GnsInfoMap.Get(UserID, tmp);
            if(Ret == 0)
            {
                // 通知其它连接层断开连接，不需要对方确认
                if(tmp.Status == GNS_USER_STATUS_ACTIVE)
                {
                    mm::DisconnectReq CurReq2;
                    CurReq2.set_userid(tmp.UserID);
                    CurReq2.set_serverid(tmp.ServerID);
                    CurReq2.set_connpos(tmp.ConnPos);

                    XYHeaderIn Header;
                    Header.SrcID = GetServerID();
                    Header.CmdID = Cmd_Disconnect_Req;
                    Header.SN = 0;
                    Header.ConnPos = tmp.ConnPos;
                    Header.UserID = tmp.UserID;
                    Header.PkgTime = time(NULL);
                    Header.Ret = 0;
                    
                    Send2Server(Header, tmp.ServerID, TO_SRV, 0, CurReq2);
                }

                Ret = m_GnsInfoMap.Update(UserID, Info);
                if(Ret != 0)
                {
                    XF_LOG_WARN(0, 0, "m_GnsInfoMap update failed, Ret=%d, UserID=%ld", Ret, UserID);
                    return -1;
                }
            }
            else
            {
                Ret = m_GnsInfoMap.Insert(UserID, Info);
                if(Ret != 0)
                {
                    XF_LOG_WARN(0, 0, "m_GnsInfoMap insert failed, Ret=%d, UserID=%ld", Ret, UserID);
                    return -1;
                }
            }
            
            mm::GNSRegisterRsp CurRsp;
            CurRsp.set_userid(UserID);
            CurRsp.set_serverid(ServerID);
            CurRsp.set_connpos(ConnPos);
            CurRsp.set_ret(0);

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_GNS_Register_Rsp;
            Header.SN = HeaderIn.SN;
            Header.ConnPos = HeaderIn.ConnPos;
            Header.UserID = HeaderIn.UserID;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, HeaderIn.SrcID, TO_SRV, 0, CurRsp);
            
            break;
        }
        case Cmd_GNS_UnRegister_Req:
        {
            mm::GNSUnRegisterReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", HeaderIn.CmdID);
                return -1;
            }

            uint64_t UserID = CurReq.userid();
            int ServerID = CurReq.serverid();
            int ConnPos = CurReq.connpos();

            ShmGnsInfo Info;
            Info.UserID = UserID;
            Info.ServerID = ServerID;
            Info.ConnPos = ConnPos;
            Info.Status = GNS_USER_STATUS_UNACTIVE;

            ShmGnsInfo tmp;
            Ret = m_GnsInfoMap.Get(UserID, tmp);
            if(Ret == 0)
            {
                if(tmp.Status != GNS_USER_STATUS_UNACTIVE)
                {
                    Ret = m_GnsInfoMap.Update(UserID, Info);
                    if(Ret != 0)
                    {
                        XF_LOG_WARN(0, 0, "m_GnsInfoMap update failed, Ret=%d, UserID=%ld", Ret, UserID);
                        return -1;
                    }
                }
            }
            else
            {
                Ret = m_GnsInfoMap.Insert(UserID, Info);
                if(Ret != 0)
                {
                    XF_LOG_WARN(0, 0, "m_GnsInfoMap insert failed, Ret=%d, UserID=%ld", Ret, UserID);
                    return -1;
                }
            }

            break;
        }
        case Cmd_Disconnect_Rsp:
        {
            mm::DisconnectRsp CurRsp;
            if(!CurRsp.ParseFromArray(pCurBuffPos+HeaderInLen, PkgLen-HeaderInLen))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", HeaderIn.CmdID);
                return -1;
            }

            // 暂时不处理
            
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


int CGns::Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message)
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

int CGns::SendStateMessage()
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

