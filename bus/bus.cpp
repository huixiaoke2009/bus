
#include <functional>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>

#include "ini_file/ini_file.h"
#include "bus.h"
#include "bus_header.h"
#include "cmd.h"

const unsigned int LISTEN_SOCK_CPOS_MIN = 100;    //100-149的ConnPos用于监听TCP的Socket
const unsigned int LISTEN_SOCK_CPOS_MAX = 149;

const unsigned int LISTEN_UDP_CPOS_MIN = 150;    //150-199的ConnPos用于监听UDP的Socket
const unsigned int LISTEN_UDP_CPOS_MAX = 199;

const unsigned int CLUSTER_SOCK_CPOS_MIN = 200000;   //200000-299999的ConnPos用于主动与其他cluster连接
const unsigned int CLUSTER_SOCK_CPOS_MAX = 299999;

const unsigned int CLIENT_SOCK_CPOS_MIN = 300000;    //300000以上是其它cluster连接上来的Socket

const unsigned int MAX_EPOLL_RET_EVENTS = 1024;

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



CConnInfo::CConnInfo(unsigned int ConnPos, int SockID, const struct sockaddr_in *pRemoteAddr, const struct sockaddr_in *pLocalAddr)
{
    m_ConnPos = ConnPos;
    m_SockID = SockID;
    m_RemoteAddr = *pRemoteAddr;
    m_LocalAddr = *pLocalAddr;
    m_RemainSendData.clear();
    m_RemainRecvData.clear();
    m_LastActTime = time(NULL);
    m_ClusterID = 0;
}

CConnInfo::~CConnInfo()
{

}

// 接收数据
//@return 0-成功接收数据 1-连接关闭 <0-接受出错
int CConnInfo::Recv(char *pBuff, unsigned int *pLen)
{
    char *pRecvPtr = pBuff;
    unsigned int RecvBuffLen = *pLen;
    if (!m_RemainRecvData.empty())
    {
        if (m_RemainRecvData.length() > (unsigned int)XY_MAXBUFF_LEN)
        {
            XF_LOG_WARN(0, 0, "%s|recv remain len %lu", RemoteAddrStr(), (unsigned long)m_RemainRecvData.length());
            return -1;
        }

        pRecvPtr += m_RemainRecvData.length();
        RecvBuffLen -= m_RemainRecvData.length();
        XF_LOG_DEBUG(0, 0, "%s|%s|with recv remain len %lu", __FUNCTION__, RemoteAddrStr(), (unsigned long)m_RemainRecvData.length());
    }

    int RecvBytes = read(m_SockID, pRecvPtr, RecvBuffLen);
    if (RecvBytes > 0)
    {
        m_LastActTime = time(NULL);

        XF_LOG_TRACE(0, 0, "%s|recv|%d|%s", RemoteAddrStr(), RecvBytes, CStrTool::Str2Hex(pRecvPtr, RecvBytes));

        *pLen = RecvBytes;
        if (!m_RemainRecvData.empty())
        {
            *pLen += m_RemainRecvData.length();
            memcpy(pBuff, m_RemainRecvData.c_str(), m_RemainRecvData.length());
            m_RemainRecvData.clear();
        }

        return 0;
    }
    else if (RecvBytes == 0)
    {
        //连接被终止
        XF_LOG_DEBUG(0, 0, "conn close|%s|%d|%d", RemoteAddrStr(), RemotePort(), m_ConnPos);
        return 1;
    }
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
        //相当于没有接收到数据
        *pLen = 0;
        if (!m_RemainRecvData.empty())
        {
            *pLen += m_RemainRecvData.length();
            memcpy(pBuff, m_RemainRecvData.c_str(), m_RemainRecvData.length());
            m_RemainRecvData.clear();
        }

        return 0;
    }
    else
    {
        XF_LOG_WARN(0, 0, "%s|recv failed, ret=%d, errno=%d, errmsg=%s", RemoteAddrStr(), RecvBytes, errno, strerror(errno));
        return -1;
    }
}

// 发送数据
//@return 0-成功发送数据 <0-发送失败
int CConnInfo::Send(const char *pBuff, unsigned int Len)
{
    int WriteBytes = 0;
    if (!m_RemainSendData.empty())
    {
        WriteBytes = write(m_SockID, m_RemainSendData.c_str(), m_RemainSendData.length());
        if (WriteBytes > 0 && WriteBytes < (int)m_RemainSendData.length())
        {
            XF_LOG_TRACE(0, 0, "Send|%d|%s", WriteBytes, CStrTool::Str2Hex(m_RemainSendData.c_str(), WriteBytes));
            m_LastActTime = time(NULL);
            //说明Reamin的数据还没有发完，需要继续发Remain的数据
            if ((int)m_RemainSendData.length() > 5120000)
            {
                //如果存在超过5MB的数据没有发送出去，输出warn日志
                XF_LOG_WARN(0, 0, "%s|send reamin data failed, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
            }
            else
            {
                XF_LOG_DEBUG(0, 0, "%s|send reamin data failed, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
            }
            m_RemainSendData.erase(0, WriteBytes);
            return 0;
        }
        else if (WriteBytes == (int)m_RemainSendData.length())
        {
            XF_LOG_TRACE(0, 0, "Send|%d|%s", WriteBytes,  CStrTool::Str2Hex(m_RemainSendData.c_str(), WriteBytes));
            m_RemainSendData.clear();
        }
        else if (WriteBytes <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 0;
            }
            else
            {
                XF_LOG_WARN(0, 0, "%s|send remain data failed, ret=%d, errno=%d, errmsg=%s", RemoteAddrStr(), WriteBytes, errno, strerror(errno));
                return -1;
            }
        }
    }

    WriteBytes = write(m_SockID, pBuff, Len);
    if (WriteBytes > 0)
    {
        XF_LOG_TRACE(0, 0, "Send|%d|%s", Len, CStrTool::Str2Hex(pBuff, Len));
        m_LastActTime = time(NULL);
        return WriteBytes;
    }
    else if (WriteBytes <= 0)
    {
        //连接被终止
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        else
        {
            XF_LOG_WARN(0, 0, "%s|send data failed, ret=%d, errno=%d, errmsg=%s", RemoteAddrStr(), WriteBytes, errno, strerror(errno));
            return -1;
        }
    }

    return 0;
}

// 缓存接收到的数据
int CConnInfo::AddRecvData(const char *pBuff, unsigned int Len)
{
    XF_LOG_DEBUG(0, 0, "%s|%s|%u|%s|%lu|%d", __FUNCTION__, RemoteAddrStr(), m_ConnPos, __FUNCTION__, (unsigned long)m_RemainRecvData.length(), Len);

    if (!m_RemainRecvData.empty())
    {
        if (m_RemainRecvData.length() + Len > (unsigned int)XY_MAXBUFF_LEN)
        {
            XF_LOG_WARN(0, 0, "%s|add recv failed, len=%lu, add_len=%d", RemoteAddrStr(), (unsigned long)m_RemainRecvData.length(), Len);
            return -1;
        }

        //TODO 错误处理
        m_RemainRecvData.append(pBuff, Len);
        XF_LOG_DEBUG(0, 0, "%s|%s|%u|after add string|%lu", __FUNCTION__, RemoteAddrStr(), m_ConnPos, (unsigned long)m_RemainRecvData.length());
    }
    else
    {
        if (Len > (unsigned int)XY_MAXBUFF_LEN)
        {
            XF_LOG_WARN(0, 0, "%s|add recv failed, add_len=%d", RemoteAddrStr(), Len);
            return -1;
        }

        //TODO 错误处理
        m_RemainRecvData.assign(pBuff, Len);
        XF_LOG_DEBUG(0, 0, "%s|%s|%u|after new string|%lu", __FUNCTION__, RemoteAddrStr(), m_ConnPos, (unsigned long)m_RemainRecvData.length());
    }

    return 0;
}

//缓存需要发送的数据
int CConnInfo::AddSendData(const char *pBuff, unsigned int Len)
{
    XF_LOG_DEBUG(0, 0, "%s|%s|%u|%s|%lu|%d", __FUNCTION__, RemoteAddrStr(), m_ConnPos, __FUNCTION__, (unsigned long)m_RemainSendData.length(), Len);

    if (!m_RemainSendData.empty())
    {
        if (m_RemainSendData.length() + Len > (unsigned int)XY_MAXBUFF_LEN)
        {
            XF_LOG_WARN(0, 0, "%s|add recv failed, len=%lu, add_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), Len);
            return -1;
        }

        //TODO 错误处理
        m_RemainSendData.append(pBuff, Len);
        XF_LOG_DEBUG(0, 0, "%s|%s|%u|after add string|%lu", __FUNCTION__, RemoteAddrStr(), m_ConnPos, (unsigned long)m_RemainSendData.length());
    }
    else
    {
        if (Len > (unsigned int)XY_MAXBUFF_LEN)
        {
            XF_LOG_WARN(0, 0, "%s|add send failed, add_len=%d", RemoteAddrStr(), Len);
            return -1;
        }

        //TODO 错误处理
        m_RemainSendData.assign(pBuff, Len);
        XF_LOG_DEBUG(0, 0, "%s|%s|%u|after new string|%lu", __FUNCTION__, RemoteAddrStr(), m_ConnPos, (unsigned long)m_RemainSendData.length());
    }

    return 0;
}

//发送缓存的数据
int CConnInfo::SendRemainData()
{
    int WriteBytes = 0;
    if (!m_RemainSendData.empty())
    {
        WriteBytes = write(m_SockID, m_RemainSendData.c_str(), m_RemainSendData.length());
        if (WriteBytes > 0 && WriteBytes < (int)m_RemainSendData.length())
        {
            m_LastActTime = time(NULL);
            //说明Reamin的数据还没有发完，需要继续发Remain的数据
            if ((int)m_RemainSendData.length() > 5120000)
            {
                //如果存在超过5MB的数据没有发送出去，输出warn日志
                XF_LOG_WARN(0, 0, "%s|send reamin data success, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
            }
            else
            {
                XF_LOG_DEBUG(0, 0, "%s|send reamin data success, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
            }


            m_RemainSendData.erase(0, WriteBytes);
            return 1;   //数据未发完
        }
        else if (WriteBytes == (int)m_RemainSendData.length())
        {
            m_RemainSendData.clear();
        }
        else if (WriteBytes <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 1;   //数据未发完
            }
            else
            {
                XF_LOG_WARN(0, 0, "%s|send remain data failed, ret=%d, errno=%d, errmsg=%s", RemoteAddrStr(), WriteBytes, errno, strerror(errno));
                return -1;
            }
        }
    }

    return 0;
}


CBus::CBus()
{
    m_ClusterID = 0;
    m_EpollFD = -1;
    m_ConnPosCnt = 0;
    m_ClusterPosCnt = 0;
    m_UDPHelloTime = 0;
    m_TCPHelloTime = 0;
    m_PosConnMap.clear();
    m_pProcessBuff = NULL;
}

CBus::~CBus()
{
    if (m_EpollFD >= 0)
    {
        close(m_EpollFD);
        m_EpollFD = -1;
    }

    if(m_pProcessBuff)
    {
        free(m_pProcessBuff);
    }
}


int CBus::Init(const char *pConfFile)
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
        IniFile.GetInt("BUS", "ClusterID", 0, (int*)&m_ClusterID);
        IniFile.GetInt("BUS", "UDPHelloTime", 0, (int*)&m_UDPHelloTime);
        IniFile.GetInt("BUS", "TCPHelloTime", 0, (int*)&m_TCPHelloTime);
        
        IniFile.GetString("BUS", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));
        
        IniFile.GetString("LOG", "ModuleName", "bus", ModuleName, sizeof(ModuleName));
        IniFile.GetInt("LOG", "LogLocal", 1, &LogLocal);
        IniFile.GetInt("LOG", "LogLevel", 3, &LogLevel);
        IniFile.GetString("LOG", "LogPath", "/dev/null", LogPath, sizeof(LogPath));
    }
    else
    {
        printf("ERR:conf file [%s] is not valid\n", pConfFile);
        return -1;
    }
    
    if(m_ClusterID == 0)
    {
        printf("ERR:BUS/ClusterID is not valid\n");
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

    OpenLog(CStrTool::Format("%s_%d", ModuleName, m_ClusterID).c_str());
    if (LogLocal == 1)
    {
        SetLogLocal(1, LogLevel, LogPath);
    }
    
    //分配空间
    if (!m_pProcessBuff)
    {
        m_pProcessBuff = (char *)malloc(XY_PKG_MAX_LEN);
    }

    //创建EPOLL
    m_EpollFD = epoll_create(XY_MAX_CONN_NUM);
    if (m_EpollFD == -1)
    {
        printf("ERR:epoll create failed|%d|%d|%s\n", XY_MAX_CONN_NUM, errno, strerror(errno));
        return -1;
    }
    
    CIniFile BusFile(BusConfPath);
    if (!BusFile.IsValid())
    {
        printf("ERR:conf file [%s] is not valid\n", BusConfPath);
        return -1;
    }
    
    int GCIMKey = 0;
    int GCIMSize = 0;
    char aszClusterList[1024] = {0};
    
    BusFile.GetInt("BUS_GLOBAL", "GCIMKey", 0, &GCIMKey);
    BusFile.GetInt("BUS_GLOBAL", "GCIMSize", 0, &GCIMSize);
    BusFile.GetString("BUS_GLOBAL", "ClusterList", "", aszClusterList, sizeof(aszClusterList));

    if (0 == GCIMKey || 0 == GCIMSize)
    {
        printf("Error 0 == GCIMKey(%x) || 0 == GCIMSize(%d)", GCIMKey, GCIMSize);
        return -1;
    }
    
    Ret = m_ClusterQueue.Init(GCIMKey, GCIMSize);
    if (Ret != 0)
    {
        printf("ERR:init m_ClusterQueue failed, key=%d, size=%d, err=%s\n",
                GCIMKey, GCIMSize, m_ClusterQueue.GetErrMsg());
        return -1;
    }
    
    printf("m_ClusterQueue Init success\n");
    
    //解析Cluster
    char *pszOneClusterID = NULL;
    int iCurListNum = 0;
    vector<unsigned int> vctClusterID;
    for(char *pszSecVal = aszClusterList; (pszOneClusterID = strtok(pszSecVal, ",")) != NULL; pszSecVal=NULL)
    {
        //去掉空格
        while ((*pszOneClusterID) == ' ')
        {
            pszOneClusterID++;
        }

        unsigned int ClusterID = atoi(pszOneClusterID);
        
        vctClusterID.push_back(ClusterID);
        
        iCurListNum++;
    }
    
    if(iCurListNum == 0)
    {
        printf("iCurListNum == 0\n");
        return -1;
    }
    
    
    for(unsigned int index = 0; index < vctClusterID.size(); index++)
    {
        unsigned int ClusterID  = vctClusterID[index];
        
        string strCluster = CStrTool::Format("CLUSTER_%d", ClusterID);
        
        char CurIPStr[16] = {0};
        int CurPort = 0;
        unsigned int tmpClusterID = 0;
        BusFile.GetString(strCluster.c_str(), "ListenIP", "", CurIPStr, sizeof(CurIPStr));
        BusFile.GetInt(strCluster.c_str(), "ListenPort", 0, &CurPort);
        BusFile.GetInt(strCluster.c_str(), "ClusterID", 0, (int *)&tmpClusterID);
        
        if (CurIPStr[0] == 0 || CurPort == 0)
        {
            printf("%s/ListenIP or ListenPort is not valid, addr[%s] or port [%d] is not valid", strCluster.c_str(), CurIPStr, CurPort);
            return -1;
        }
        
        if(tmpClusterID != ClusterID || ClusterID == 0)
        {
            printf("%s/ClusterID is not valid, %d, %d\n", strCluster.c_str(), tmpClusterID, ClusterID);
            return -1;
        }

        ClusterInfo CInfo;
        CInfo.ClusterIP = inet_addr(CurIPStr);
        CInfo.ClusterPort = CurPort;
        CInfo.ClusterID = ClusterID;
        CInfo.SocketID = -1;
        CInfo.ConnPos = 0;
        if(m_mapClusterInfo.insert(std::pair<unsigned int, ClusterInfo>(ClusterID, CInfo)).second == false)
        {
            printf("m_mapClusterInfo insert failed\n");
            return -1;
        }

        // 自己的配置，用于启用监听端口
        if(ClusterID == GetClusterID())
        {
            Ret = ListenTcp(CurIPStr, CurPort);
            if(Ret != 0)
            {
                printf("socket listen failed, %s, %d\n", CurIPStr, CurPort);
                return -1;
            }
            
            Ret = ListenUdp(CurIPStr, CurPort);
            if(Ret != 0)
            {
                printf("socket listen failed, %s, %d\n", CurIPStr, CurPort);
                return -1;
            }
        }
        
        //解析SvrList
        char aszSvrList[1024] = {0};
        BusFile.GetString(strCluster.c_str(), "SrvList", "", aszSvrList, sizeof(aszSvrList));
        
        char *pszOneSvrID = NULL;
        for(char *pszSecVal2 = aszSvrList; (pszOneSvrID = strtok(pszSecVal2, ",")) != NULL; pszSecVal2 = NULL)
        {            
            //去掉空格
            while ((*pszOneSvrID) == ' ')
            {
                pszOneSvrID++;
            }

            unsigned int ServerID = atoi(pszOneSvrID);
            
            ServerInfo Info;
            Info.ClusterID = ClusterID;
            Info.GroupID = 0;
            Info.QueueKey = 0;
            Info.QueueSize = 0;
            Info.pQueue = NULL;
            if(ClusterID == GetClusterID())
            {
                string strSvr = CStrTool::Format("SERVER_%d", ServerID);
                unsigned int tmpClusterID = 0;
                unsigned int tmpSvrID = 0;
                unsigned int tmpGroupID = 0;
                int QueueKey = 0;
                int QueueSize = 0;
            
                BusFile.GetInt(strSvr.c_str(), "ClusterID", 0, (int*)&tmpClusterID);
                BusFile.GetInt(strSvr.c_str(), "ServerID", 0, (int*)&tmpSvrID);
                BusFile.GetInt(strSvr.c_str(), "GroupID", 0, (int*)&tmpGroupID);
                BusFile.GetInt(strSvr.c_str(), "QueueKey", 0, &QueueKey);
                BusFile.GetInt(strSvr.c_str(), "QueueSize", 0, &QueueSize);
                
                if(tmpClusterID == 0 || tmpClusterID != ClusterID || tmpSvrID == 0 || tmpSvrID != ServerID || tmpGroupID == 0)
                {
                    printf("ClusterID or ServerID or GroupID is illage, %d|%d|%d|%d|%d\n", 
                                tmpClusterID, ClusterID, tmpSvrID, tmpGroupID, ServerID);
                    return -1;
                }

                map<unsigned int, vector<unsigned int> >::iterator iter_map = m_mapGrpInfo.find(tmpGroupID);
                if(iter_map == m_mapGrpInfo.end())
                {
                    vector<unsigned int> tmp;
                    tmp.push_back(ServerID);
                    
                    if(m_mapGrpInfo.insert(std::pair<unsigned int, vector<unsigned int> >(tmpGroupID, tmp)).second == false)
                    {
                        printf("m_mapGrpInfo insert failed\n");
                        return -1;
                    }
                }
                else
                {
                    vector<unsigned int>& iter_vct = iter_map->second;
                    iter_vct.push_back(ServerID);
                }
                
                if(QueueKey == 0 || QueueSize == 0)
                {
                    printf("QueueKey or QueueSize is illeage, %d|%0x|%d\n", ServerID, QueueKey, QueueSize);
                    return -1;
                }
                
                mmlib::CShmQueue* pQueue = new mmlib::CShmQueue();
                if(pQueue->Init(QueueKey, QueueSize) != 0)
                {
                    printf("pQueue Init failed, %0x|%d\n", QueueKey, QueueSize);
                    return -1;
                }

                Info.GroupID = tmpGroupID;
                Info.QueueKey = QueueKey;
                Info.QueueSize = QueueSize;
                Info.pQueue = pQueue;
                
                if(m_mapSvrInfo.insert(std::pair<unsigned int, ServerInfo>(ServerID, Info)).second == false)
                {
                    printf("m_mapSvrInfo insert failed\n");
                    delete(pQueue);
                    return -1;
                }
            }
            else
            {
                string strSvr = CStrTool::Format("SERVER_%d", ServerID);
                unsigned int tmpClusterID = 0;
                unsigned int tmpGroupID = 0;
            
                BusFile.GetInt(strSvr.c_str(), "ClusterID", 0, (int*)&tmpClusterID);
                BusFile.GetInt(strSvr.c_str(), "GroupID", 0, (int*)&tmpGroupID);

                
                if(tmpClusterID == 0 || tmpClusterID != ClusterID || tmpGroupID == 0)
                {
                    printf("ClusterID or GroupID is illage, %d|%d|%d\n", 
                                tmpClusterID, ClusterID, tmpGroupID);
                    return -1;
                }

                map<unsigned int, vector<unsigned int> >::iterator iter_map = m_mapGrpInfo.find(tmpGroupID);
                if(iter_map == m_mapGrpInfo.end())
                {
                    vector<unsigned int> tmp;
                    tmp.push_back(ServerID);
                    
                    if(m_mapGrpInfo.insert(std::pair<unsigned int, vector<unsigned int> >(tmpGroupID, tmp)).second == false)
                    {
                        printf("m_mapGrpInfo insert failed\n");
                        return -1;
                    }
                }
                else
                {
                    vector<unsigned int>& iter_vct = iter_map->second;
                    iter_vct.push_back(ServerID);
                }

                Info.GroupID = tmpGroupID;
                
                if(m_mapSvrInfo.insert(std::pair<unsigned int, ServerInfo>(ServerID, Info)).second == false)
                {
                    printf("m_mapSvrInfo insert failed\n");
                    return -1;
                }
            }
        } 
    }
    
    printf("svr init success\n");
    
    return 0;
}


int CBus::Run()
{
    int Ret = 0;
    StopFlag = false;

    struct epoll_event RetEvent[MAX_EPOLL_RET_EVENTS];
    int RetEventNum = 0;

    char *pRecvBuff = (char *)malloc(XY_MAXBUFF_LEN);
    int RecvLen = 0;
    
    time_t LastHelloTime = time(NULL);
    time_t LastHeartBeetTime = time(NULL);
    
    while(!StopFlag)
    {
        int EmptyFlag = 1;  //没有数据标志位

        RetEventNum = epoll_wait(m_EpollFD, RetEvent, MAX_EPOLL_RET_EVENTS, 1);
        if(RetEventNum > 0)
        {
            EmptyFlag = 0;
            unsigned int CurConnPos = 0;
            for(int i = 0; i < RetEventNum; ++i)
            {
                XF_LOG_TRACE(0, 0, "epoll_wait return, event_num=%d, cur_event=%d, event=%d, conn_pos=%u", RetEventNum, i, RetEvent[i].events, RetEvent[i].data.u32);

                CurConnPos = RetEvent[i].data.u32;
                if(CurConnPos >= LISTEN_SOCK_CPOS_MIN && CurConnPos <= LISTEN_SOCK_CPOS_MAX)
                {
                    if(RetEvent[i].events & EPOLLIN)
                    {
                        AcceptConn(CurConnPos);
                    }
                    else
                    {
                        XF_LOG_WARN(0, 0, "listening socket recv event %d", RetEvent[i].events);
                    }
                }
                else if(CurConnPos >= LISTEN_UDP_CPOS_MIN && CurConnPos <= LISTEN_UDP_CPOS_MAX)
                {
                    RecvHelloMessage(CurConnPos);
                }
                else
                {
                    std::map<unsigned int, CConnInfo*>::iterator pConnInfoMap = m_PosConnMap.find(CurConnPos);
                    if (pConnInfoMap == m_PosConnMap.end())
                    {
                        XF_LOG_WARN(0, 0, "EPOLL find invalid cpos|%u", CurConnPos);
                        continue;
                    }

                    CConnInfo *pCurConnInfo = pConnInfoMap->second;
                    if(RetEvent[i].events & EPOLLERR)
                    {
                        XF_LOG_WARN(0, 0, "EPOLL return EPOLLERR|%u", CurConnPos);
                        ReleaseConn(pConnInfoMap);
                        continue;
                    }

                    if(RetEvent[i].events & EPOLLHUP)
                    {
                        XF_LOG_WARN(0, 0, "EPOLL return EPOLLHUP|%u", CurConnPos);
                        ReleaseConn(pConnInfoMap);
                        continue;
                    }

                    if(RetEvent[i].events & EPOLLIN)
                    {
                        //读取数据
                        //流程：1)读取 2)判断是否OK 3)如果OK就入Queue 4)如果不OK就将数据缓存或关闭连接
                        RecvLen = XY_MAXBUFF_LEN;
                        Ret = pCurConnInfo->Recv(pRecvBuff, (unsigned int *)&RecvLen);
                        if (Ret == 1)
                        {
                            //连接被终止
                            ReleaseConn(pConnInfoMap);
                            continue;
                        }
                        else if (Ret < 0)
                        {
                            //接收失败
                            XF_LOG_WARN(0, 0, "%u|recv failed", CurConnPos);
                            ReleaseConn(pConnInfoMap);
                            continue;
                        }

                        if (RecvLen > XY_MAXBUFF_LEN)
                        {
                            XF_LOG_WARN(0, 0, "%u|%u|recv len is not valid", CurConnPos, RecvLen);
                            ReleaseConn(pConnInfoMap);
                            continue;
                        }

                        XF_LOG_TRACE(0, 0, "Recv|%d|%s", RecvLen, CStrTool::Str2Hex(pRecvBuff, RecvLen));

                        char *pCurBuffPos = pRecvBuff;
                        while(RecvLen > 0)
                        {
                            int ProcLen = ProcessPkg(pCurBuffPos, RecvLen, pConnInfoMap);
                            if ((ProcLen > RecvLen)||(ProcLen == 0))
                            {
                                //需要暂存数据
                                //ProcLen返回0表示根据现有的字段还不能计算长度
                                int Ret2 = pCurConnInfo->AddRecvData(pCurBuffPos, RecvLen);
                                if (Ret2 != 0)
                                {
                                    XF_LOG_WARN(0, 0, "%u|%u|store recv data failed", CurConnPos, RecvLen);
                                    ReleaseConn(pConnInfoMap);
                                }
                                RecvLen = 0;
                            }
                            else if (ProcLen == -1)
                            {
                                //直接关闭连接
                                XF_LOG_WARN(0, 0, "%u|%u|handle_input ret -1", CurConnPos, RecvLen);
                                ReleaseConn(pConnInfoMap);
                                RecvLen = 0;
                            }
                            else if (ProcLen < -1)
                            {
                                XF_LOG_WARN(0, 0, "%u|%u|handle_input ret %d", CurConnPos, RecvLen, ProcLen);
                                ReleaseConn(pConnInfoMap);
                                RecvLen = 0;
                            }
                            else
                            {
                                //数据包处理成功
                                XF_LOG_TRACE(0, 0, "process pkg succ");

                                pCurBuffPos += ProcLen;
                                RecvLen -= ProcLen;
                            }
                        }

                        continue;
                    }

                    if(RetEvent[i].events & EPOLLOUT)
                    {
                        //发送数据
                        //流程：1)发送 2)判断是否需要等待 3)如果不需要等待，直接就OK 4)如果需要等待，将数据缓存
                        int Ret = pCurConnInfo->SendRemainData();
                        if (Ret == 1)
                        {
                            //数据未发完
                            continue;
                        }
                        else if (Ret != 0)
                        {
                            XF_LOG_WARN(0, 0, "send remain data failed|%u", CurConnPos);
                            ReleaseConn(pConnInfoMap);
                            continue;
                        }
                        else
                        {
                            //删除EPOLLOUT事件
                            struct epoll_event ev;
                            ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                            ev.data.u32 = CurConnPos;

                            int Ret2 = epoll_ctl(m_EpollFD, EPOLL_CTL_MOD, pCurConnInfo->GetSockID(), &ev);
                            if(Ret2 != 0)
                            {
                                XF_LOG_WARN(0, 0, "epoll mod del epoll_out failed|%d|%s", errno, strerror(errno));
                                //TODO 这里需要考虑后续处理
                            }
                        }
                    }
                }
            }
        }
        else if (RetEventNum == -1)
        {
            if(errno == EINTR)
            {
                XF_LOG_WARN(0, 0, "epoll_wait signal interruption");
            }
            else
            {
                XF_LOG_WARN(0, 0, "epoll_wait failed, errno=%d, errmsg=%s", errno, strerror(errno));
            }
        }
        else
        {
            //没有任何事件
        }
        
        
        // 从自己的通道获取数据进行转发
        RecvLen = XY_MAXBUFF_LEN;
        Ret = m_ClusterQueue.OutQueue(pRecvBuff, &RecvLen);
        if(Ret == m_ClusterQueue.E_SHM_QUEUE_EMPTY)
        {
            
        }
        else if(Ret != 0)
        {
            XF_LOG_WARN(0, 0, "ClusterQueue OutQueue failed");
        }
        else
        {
            XF_LOG_TRACE(0, 0, "ClusterQueue OutQueue success");
            Ret = ForwardMsg(pRecvBuff, RecvLen);
            if(Ret != 0)
            {
                XF_LOG_WARN(0, 0, "ForwardMsg failed, Ret = %d", Ret);
            }
        }

        // 向对方的UDP端口发送hello数据包
        time_t NowTime = time(NULL);
        if(NowTime - LastHelloTime >= m_UDPHelloTime)
        {
            LastHelloTime = NowTime;
            SendHelloMessage();
        }
        
        // 向每个cluster发送心跳包，保持TCP连接活跃
        if(NowTime - LastHeartBeetTime >= m_TCPHelloTime)
        {
            LastHeartBeetTime = NowTime;
            SendHeartBeetMessage();
        }
        
        if(EmptyFlag)
        {
            usleep(1000);
        }
    }

    return 0;
}


int CBus::ListenTcp(const char* pAddr, int Port)
{
    unsigned int LocalIP = inet_addr(pAddr);
    int LocalPort = Port;

    int CurListenSockID = socket(AF_INET, SOCK_STREAM, 0);

    if(-1 == CurListenSockID)
    {
        printf("ERR:create listen socket failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }

    int ReuseVal = 1;
    if(-1 == setsockopt(CurListenSockID, SOL_SOCKET, SO_REUSEADDR, &ReuseVal, sizeof(ReuseVal)))
    {
        printf("ERR:setsockopt failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0x0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = LocalIP;
    addr.sin_port = htons(LocalPort);

    if(-1 == TEMP_FAILURE_RETRY(::bind(CurListenSockID, (struct sockaddr*)&addr, sizeof(addr))))
    {
        printf("ERR:bind failed|%s|%u|%d|%s\n", CStrTool::IPString(addr.sin_addr.s_addr), ntohs(addr.sin_port), errno, strerror(errno));
        return -1;
    }

    if(-1 == TEMP_FAILURE_RETRY(::listen(CurListenSockID, SOMAXCONN)))
    {
        printf("ERR:listen failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u32 = LISTEN_SOCK_CPOS_MIN;

    if(0 != epoll_ctl(m_EpollFD, EPOLL_CTL_ADD, CurListenSockID, &ev))
    {
        printf("ERR:epoll_ctl failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0x0, sizeof(remote_addr));

    CConnInfo *pCurConnInfo = new CConnInfo(LISTEN_SOCK_CPOS_MIN, CurListenSockID, &remote_addr, &addr);

    if(m_PosConnMap.insert(std::pair<unsigned int, CConnInfo*>(LISTEN_SOCK_CPOS_MIN, pCurConnInfo)).second == false)
    {
        printf("conn map add failed\n");
        epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, CurListenSockID, NULL);
        close(CurListenSockID);
        SAFE_DELETE(pCurConnInfo);
        return -1;
    }

    printf("INFO:service tcp listen ok, ip=%s, port=%d\n", inet_ntoa(*(struct in_addr *)(&addr.sin_addr.s_addr)), ntohs(addr.sin_port));
    
    return 0;
}


int CBus::ListenUdp(const char* pAddr, int Port)
{
    unsigned int LocalIP = inet_addr(pAddr);
    int LocalPort = Port;

    int CurListenSockID = socket(AF_INET, SOCK_DGRAM, 0);

    if(-1 == CurListenSockID)
    {
        printf("ERR:create listen socket failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0x0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = LocalIP;
    addr.sin_port = htons(LocalPort);

    if(-1 == TEMP_FAILURE_RETRY(::bind(CurListenSockID, (struct sockaddr*)&addr, sizeof(addr))))
    {
        printf("ERR:bind failed|%s|%u|%d|%s\n", CStrTool::IPString(addr.sin_addr.s_addr), ntohs(addr.sin_port), errno, strerror(errno));
        return -1;
    }
    
    struct timeval tv_out;
    tv_out.tv_sec = 2;
    tv_out.tv_usec = 0;
    
    if(-1 == setsockopt(CurListenSockID, SOL_SOCKET, SO_RCVTIMEO, &tv_out, sizeof(tv_out)))
    {
        printf("ERR:setsockopt failed!\n");
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u32 = LISTEN_UDP_CPOS_MIN;

    if(0 != epoll_ctl(m_EpollFD, EPOLL_CTL_ADD, CurListenSockID, &ev))
    {
        printf("ERR:epoll_ctl failed|%d|%s\n", errno, strerror(errno));
        return -1;
    }
    
    // 下面这一段只是为了方便epoll监听管理，不能用CConnInfo里面的send和recieve接口
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0x0, sizeof(remote_addr));

    CConnInfo *pCurConnInfo = new CConnInfo(LISTEN_UDP_CPOS_MIN, CurListenSockID, &remote_addr, &addr);

    if(m_PosConnMap.insert(std::pair<unsigned int, CConnInfo*>(LISTEN_UDP_CPOS_MIN, pCurConnInfo)).second == false)
    {
        printf("conn map add failed\n");
        epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, CurListenSockID, NULL);
        close(CurListenSockID);
        SAFE_DELETE(pCurConnInfo);
        return -1;
    }

    printf("INFO:service udp listen ok, ip=%s, port=%d\n", inet_ntoa(*(struct in_addr *)(&addr.sin_addr.s_addr)), ntohs(addr.sin_port));
    
    return 0;
}


int CBus::AcceptConn(unsigned int ConnPos)
{
    int Ret = 0;

    struct sockaddr_in RemoteAddr;
    memset(&RemoteAddr, 0x0, sizeof(RemoteAddr));
    socklen_t RemoteAddrLen = sizeof(RemoteAddr);

    std::map<unsigned int, CConnInfo*>::iterator pConnInfoMap = m_PosConnMap.find(ConnPos);
    if (pConnInfoMap == m_PosConnMap.end())
    {
        XF_LOG_WARN(0, 0, "Accept find invalid cpos|%u", ConnPos);
        return -1;
    }

    CConnInfo *pCurConnInfo = pConnInfoMap->second;

    int NewSockID = TEMP_FAILURE_RETRY(::accept(pCurConnInfo->GetSockID(), (struct sockaddr*)&RemoteAddr, &RemoteAddrLen));
    if(NewSockID > 0)
    {
        //经过一定的周期，可能产生的ConnPos正在使用中
        m_ConnPosCnt++;
        unsigned int CurConnPos;
        if (m_ConnPosCnt < CLIENT_SOCK_CPOS_MIN)
        {
            CurConnPos = CLIENT_SOCK_CPOS_MIN+1;
            m_ConnPosCnt = CurConnPos;
        }
        else
        {
            CurConnPos = m_ConnPosCnt;
        }

        //检索当前的map，防止出现冲突的ConnPos
        while (true)
        {
            if (m_PosConnMap.find(CurConnPos) == m_PosConnMap.end())
            {
                break;
            }

            CurConnPos++;

            if (CurConnPos < CLIENT_SOCK_CPOS_MIN)
            {
                CurConnPos = CLIENT_SOCK_CPOS_MIN + 1;
            }
        }

        //设置为非阻塞模式
        int FdFlags = 0;
        FdFlags = fcntl(NewSockID, F_GETFL, 0);
        if (FdFlags == -1)
        {
            XF_LOG_WARN(0, 0, "fcntl get flags failed|%d|%s", errno, strerror(errno));
            close(NewSockID);
            return -1;
        }

        Ret = fcntl(NewSockID, F_SETFL, FdFlags | O_NONBLOCK);
        if (Ret == -1)
        {
            XF_LOG_WARN(0, 0, "fcntl set flags failed|%d|%s", errno, strerror(errno));
            close(NewSockID);
            return -1;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
        ev.data.u32 = CurConnPos;

        Ret = epoll_ctl(m_EpollFD, EPOLL_CTL_ADD, NewSockID, &ev);
        if(Ret != 0)
        {
            XF_LOG_WARN(0, 0, "epoll add failed|%d|%s", errno, strerror(errno));
            close(NewSockID);
            return -1;
        }

        struct sockaddr_in local_addr;
        memset(&local_addr, 0x0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = pCurConnInfo->LocalAddr();
        local_addr.sin_port = pCurConnInfo->LocalPort();

        CConnInfo *pCurConnInfo = new CConnInfo(CurConnPos, NewSockID, &RemoteAddr, &local_addr);

        if(m_PosConnMap.insert(std::pair<unsigned int, CConnInfo*>(CurConnPos, pCurConnInfo)).second == false)
        {
            XF_LOG_WARN(0, 0, "conn map add failed");
            epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, NewSockID, NULL);
            close(NewSockID);
            SAFE_DELETE(pCurConnInfo);
            return -1;
        }

        XF_LOG_DEBUG(0, 0, "conn in|%s|%u|%s|%d|%u", mmlib::CStrTool::IPString(pCurConnInfo->LocalAddr()), ntohs(pCurConnInfo->LocalPort()), mmlib::CStrTool::IPString(RemoteAddr.sin_addr.s_addr), ntohs(RemoteAddr.sin_port), CurConnPos);
    }
    else
    {
        XF_LOG_WARN(0, 0, "accept conn failed|%d|%s", errno, strerror(errno));
        return -1;
    }

    return 0;
}



//返回值:0~PkgLen-1表示包不够大，-1表示出错要删除链接， PkgLen表示正常。如果要删除链接不要在这里删，返回-1即可
int CBus::ProcessPkg(const char *pCurBuffPos, int RecvLen, std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap)
{
    int Ret = 0;
    
    BusHeader CurHeader;
    
    int HeaderLen = CurHeader.GetHeaderLen();
    // 接受到的数据不够包头长度
    if(RecvLen < HeaderLen)
    {
        return HeaderLen;
    }

    CurHeader.Read(pCurBuffPos);
    
    int PkgLen = CurHeader.PkgLen;
    //接收到的数据不够一个包
    if(PkgLen > RecvLen)
    {
        return PkgLen;
    }

    //判断包长是否异常
    if(PkgLen > XY_PKG_MAX_LEN)
    {
        //相当于不要这个包了
        return PkgLen;
    }
    
    // ------------------------- bus内部协议 begin ---------------------
    if(CurHeader.CmdID == Cmd_Heartbeat)
    {
        bus::HeartbeatMsg CurHeartbeatReq;
        if (!CurHeartbeatReq.ParseFromArray(pCurBuffPos+HeaderLen, PkgLen-HeaderLen))
        {
            //相当于不要这个包了
            XF_LOG_WARN(0, 0, "Parse Pkg failed, CmdID=%0x", CurHeader.CmdID);
            return PkgLen;
        }
        
        unsigned int ClusterID = CurHeartbeatReq.clusterid();
        
        XF_LOG_TRACE(0, 0, "Recieve Heartbeat from ClusterID %d", ClusterID);

        return PkgLen;
    }
    
    // ------------------------- bus内部协议 end ---------------------
    
    unsigned int DstID = CurHeader.DstID;
    unsigned int SrcID = CurHeader.SrcID;
    unsigned int CmdID = CurHeader.CmdID;
    char SendType = CurHeader.SendType;

    XF_LOG_DEBUG(0, 0, "%d|%d|%d|%0x", SrcID, DstID, SendType, CmdID);

    if(SendType != TO_SRV)
    {
        //相当于不要这个包了
        XF_LOG_WARN(0, 0, "SendType is not TO_SRV:%d,%d|%d|%0x", TO_SRV, SrcID, DstID, CmdID);
        return PkgLen;
    }
    
    map<unsigned int, ServerInfo>::iterator iter = m_mapSvrInfo.find(DstID);
    if(iter == m_mapSvrInfo.end())
    {
        //相当于不要这个包了
        XF_LOG_WARN(0, 0, "Unknow DstID %d, %d|%0x", DstID, SrcID, CmdID);
        return PkgLen;
    }
    
    const ServerInfo& Info = iter->second;
    if(Info.ClusterID == GetClusterID())
    {
        if(Info.pQueue != NULL)
        {
            Ret = Info.pQueue->InQueue(pCurBuffPos, RecvLen);
            if(Ret == Info.pQueue->E_SHM_QUEUE_FULL)
            {
                //相当于不要这个包了
                XF_LOG_WARN(0, 0, "ShmQueue is full, %0x|%d", Info.QueueKey, Info.QueueSize);
                return PkgLen;
            }
            else if(Ret != 0)
            {
                XF_LOG_WARN(0, 0, "InQueue failed, %d|%0x|%d", Ret, Info.QueueKey, Info.QueueSize);
                //相当于不要这个包了
                return PkgLen;
            }
            else
            {
                XF_LOG_TRACE(0, 0, "InQueue success, %d|%0x|%d", Ret, Info.QueueKey, Info.QueueSize);
            }
        }
        else
        {
            XF_LOG_WARN(0, 0, "Queqe is null, %d|%0x|%d", Info.ClusterID, Info.QueueKey, Info.QueueSize);
        }
    }
    else
    {
        // 这里需要进行转发吗?
        /*
        for(int i = 0; i < m_ClusterNum && i < XY_MAX_CLUSTER_NUM; i++)
        {
            if(m_ClusterInfo[i].ClusterID == Info.ClusterID)
            {
                Ret = Send2Cluster(m_ClusterInfo[i], pCurBuffPos, RecvLen);
                if(Ret != 0)
                {
                    XF_LOG_WARN(0, 0, "Send2Cluster failed, %d", Info.ClusterID);
                    return -1;
                }
                
                break;
            }
        }
        */
        XF_LOG_WARN(0, 0, "unbelievable, DstID=%d, SrcID=%d, CmdID=%0x", DstID, SrcID, CmdID);
    }
    
    return PkgLen;
}


int CBus::ForwardMsg(const char *pCurBuffPos, int RecvLen)
{
    int Ret = 0;
    
    BusHeader CurHeader;
    
    int HeaderLen = CurHeader.GetHeaderLen();
    // 接受到的数据不够包头长度
    if(RecvLen < HeaderLen)
    {
        XF_LOG_WARN(0, 0, "RecvLen < HeaderLen, %d|%d", RecvLen, HeaderLen);
        return -1;
    }

    CurHeader.Read(pCurBuffPos);
    
    int PkgLen = CurHeader.PkgLen;
    //接收到的数据不够一个包
    if(RecvLen < PkgLen)
    {
        XF_LOG_WARN(0, 0, "RecvLen < PkgLen, %d|%d", RecvLen, PkgLen);
        return -1;
    }

    //判断包长是否异常
    if(PkgLen > XY_PKG_MAX_LEN)
    {
        XF_LOG_WARN(0, 0, "PkgLen > XY_PKG_MAX_LEN, %d|%d", RecvLen, XY_PKG_MAX_LEN);
        return -1;
    }
    
    unsigned int DstID = CurHeader.DstID;
    unsigned int SrcID = CurHeader.SrcID;
    unsigned int CmdID = CurHeader.CmdID;
    char SendType = CurHeader.SendType;

    XF_LOG_DEBUG(0, 0, "%d|%d|%d|%0x", SrcID, DstID, SendType, CmdID);

    if(SendType == TO_GRP)
    {
        int Rand = 0;
        map<unsigned int, vector<unsigned int> >::iterator iter_map = m_mapGrpInfo.find(DstID);
        if(iter_map == m_mapGrpInfo.end())
        {
            XF_LOG_WARN(0, 0, "Unknow DstID %d, %d|%0x", DstID, SrcID, CmdID);
            return -1;
        }

        const vector<unsigned int>& iter_vct = iter_map->second;
        int Size = iter_vct.size();
        if(Size == 0)
        {
            XF_LOG_WARN(0, 0, "Group %d do not have any server", DstID);
            return -1;
        }
        
        Rand = CRandomTool::Instance()->Get(0, Size);
        DstID = iter_vct[Rand];
        
        XF_LOG_DEBUG(0, 0, "Size, Rand, DstID, %d|%d|%d", Size, Rand, DstID);
    }
    
    map<unsigned int, ServerInfo>::iterator iter = m_mapSvrInfo.find(DstID);
    if(iter == m_mapSvrInfo.end())
    {
        XF_LOG_WARN(0, 0, "Unknow DstID %d, %d|%0x", DstID, SrcID, CmdID);
        return -1;
    }
    
    const ServerInfo& Info = iter->second;
    if(Info.ClusterID == GetClusterID())
    {
        if(Info.pQueue != NULL)
        {
            Ret = Info.pQueue->InQueue(pCurBuffPos, RecvLen);
            if(Ret == Info.pQueue->E_SHM_QUEUE_FULL)
            {
                XF_LOG_WARN(0, 0, "ShmQueue is full, %0x|%d", Info.QueueKey, Info.QueueSize);
                return -1;
            }
            else if(Ret != 0)
            {
                XF_LOG_WARN(0, 0, "InQueue failed, %d|%0x|%d", Ret, Info.QueueKey, Info.QueueSize);
                return -1;
            }
            else
            {
                XF_LOG_TRACE(0, 0, "InQueue success, %d|%0x|%d", Ret, Info.QueueKey, Info.QueueSize);
            }
        }
        else
        {
            XF_LOG_WARN(0, 0, "Queqe is null, %d|%0x|%d", Info.ClusterID, Info.QueueKey, Info.QueueSize);
        }
    }
    else
    {
        map<unsigned int, ClusterInfo>::iterator iter2 = m_mapClusterInfo.find(Info.ClusterID);
        if(iter2 != m_mapClusterInfo.end())
        {
            Ret = Send2Cluster(iter2->second, pCurBuffPos, RecvLen);
            if(Ret != 0)
            {
                XF_LOG_WARN(0, 0, "Send2Cluster failed, %d", Info.ClusterID);
                return -1;
            }
        }
    }
    
    return 0;
}

void CBus::ReleaseConn(std::map<unsigned int, CConnInfo*>::iterator &itrConnInfoMap)
{
    unsigned int CurConnPos = itrConnInfoMap->first;
    
    CConnInfo *pCurConnInfo = itrConnInfoMap->second;
    epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, pCurConnInfo->GetSockID(), NULL);
    close(pCurConnInfo->GetSockID());

    XF_LOG_INFO(0, 0, "DEL_POS_CONN_MAP|%d", itrConnInfoMap->first);
    m_PosConnMap.erase(itrConnInfoMap);
    
    SAFE_DELETE(pCurConnInfo);
    
    if (CurConnPos > CLUSTER_SOCK_CPOS_MIN && CurConnPos < CLUSTER_SOCK_CPOS_MAX)
    {
        map<unsigned int, ClusterInfo>::iterator iter = m_mapClusterInfo.begin();
        for(; iter != m_mapClusterInfo.end(); iter++)
        {
            if(iter->second.ConnPos == CurConnPos)
            {
                DisconnetCluster(iter->second.ClusterID);
                break;
            }
        }
    }
}

void CBus::RecvHelloMessage(unsigned int ConnPos)
{
    std::map<unsigned int, CConnInfo*>::iterator iter = m_PosConnMap.find(ConnPos);
    if(iter == m_PosConnMap.end())
    {
        XF_LOG_WARN(0, 0, "Unknow ConnPos:%d", ConnPos);
        return;
    }
    
    int SocketID = iter->second->GetSockID();
    
    char buf[1024];
    int bufLen = sizeof(buf);
    
    int RecvSize = recvfrom(SocketID, buf, bufLen, 0, NULL, 0);
    if(RecvSize <= 0)
    {
        XF_LOG_WARN(0, 0, "recvfrom failed, %d:%d:%d", SocketID, RecvSize, ConnPos);
        return;
    }
    
    BusHeader CurHeader;
    int Offset = CurHeader.Read(buf);
    if (CurHeader.CmdID != Cmd_HelloMessage)
    {
        XF_LOG_WARN(0, 0, "Unknow CmdID %d", CurHeader.CmdID);
        return;
    }
    
    bus::HelloMessage Msg;
    if (!Msg.ParseFromArray(buf + Offset, RecvSize - Offset))
    {
        XF_LOG_WARN(0, 0, "Parse HelloMessage Pkg failed");
        return;
    }
    
    unsigned int ClusterID = Msg.clusterid();
    
    XF_LOG_TRACE(0, 0, "Recv hello message from cluster:%d", ClusterID);
    
    map<unsigned int, ClusterInfo>::iterator iter2 = m_mapClusterInfo.find(ClusterID);
    if (ClusterID != GetClusterID() && iter2 != m_mapClusterInfo.end())
    {
        //检查是否需要连接
        if(iter2->second.SocketID == -1 || iter2->second.ConnPos == 0)
        {
            ConnectCluster(ClusterID);
        }
    }
    
    return;
}


void CBus::SendHelloMessage()
{
    map<unsigned int, ClusterInfo>::iterator iter = m_mapClusterInfo.begin();
    for(; iter != m_mapClusterInfo.end(); iter++)
    {
        if(iter->second.ClusterID == GetClusterID())
        {
            continue;
        }
        
        char buf[1024] = {0};
        int bufLen = sizeof(buf);
        
        BusHeader CurHeader;
        int HeaderLen = CurHeader.GetHeaderLen();
        
        bus::HelloMessage Msg;
        Msg.set_clusterid(GetClusterID());
        
        if (!Msg.SerializeToArray(buf + HeaderLen, bufLen - HeaderLen))
        {
            XF_LOG_WARN(0, 0, "SerializeToArray failed, %s", Msg.ShortDebugString().c_str());
            return;
        }
        
        CurHeader.PkgLen = HeaderLen + Msg.ByteSize();
        CurHeader.CmdID = Cmd_HelloMessage;
        CurHeader.SrcID = 0;
        CurHeader.DstID = 0;
        CurHeader.SN = 0;
        CurHeader.Ret = 0;
        
        CurHeader.Write(buf);
        
        int CurSockID = socket(AF_INET, SOCK_DGRAM, 0);

        if(-1 == CurSockID)
        {
            printf("ERR:create listen socket failed|%d|%s\n", errno, strerror(errno));
            return;
        }

        struct sockaddr_in addr;
        memset(&addr, 0x0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = iter->second.ClusterIP;
        addr.sin_port = htons(iter->second.ClusterPort);
        
        sendto(CurSockID, buf, CurHeader.PkgLen, 0, (sockaddr*)&addr, sizeof(addr));
        
        close(CurSockID);
        
        XF_LOG_TRACE(0, 0, "Send hello message to cluster:%d", iter->second.ClusterID);
    }
}

void CBus::SendHeartBeetMessage()
{
    map<unsigned int, ClusterInfo>::iterator iter = m_mapClusterInfo.begin();
    for(; iter != m_mapClusterInfo.end(); iter++)
    {
        if(iter->second.ClusterID == GetClusterID())
        {
            continue;
        }
        
        bus::HeartbeatMsg Msg;
        Msg.set_clusterid(GetClusterID());
        
        Send2ClusterByMsg(iter->second, Cmd_Heartbeat, Msg);

        XF_LOG_TRACE(0, 0, "Send Heartbeat to ClusterID:%d", iter->second.ClusterID); 
    }
    
}


int CBus::Send2ClusterByMsg(const ClusterInfo& Info, unsigned int CmdID, const google::protobuf::Message &Rsp)
{
    BusHeader CurHeader;
    int HeaderLen = CurHeader.GetHeaderLen();
    char acSendBuff[XY_PKG_MAX_LEN] = {0};
    int BufLen = sizeof(acSendBuff) - HeaderLen;
    char *pSendData = acSendBuff + HeaderLen;
    if (!Rsp.SerializeToArray(pSendData, BufLen))
    {
        XF_LOG_WARN(0, 0, "SerializeToArray failed, %s", Rsp.ShortDebugString().c_str());
        return -1;
    }
    
    CurHeader.PkgLen = HeaderLen + Rsp.ByteSize();
    CurHeader.CmdID = CmdID;
    CurHeader.DstID = 0;
    CurHeader.SrcID = 0;
    CurHeader.SN = 0;
    CurHeader.Ret = 0;
    CurHeader.Write(acSendBuff);
    
    return Send2Cluster(Info, acSendBuff, CurHeader.PkgLen);
}


int CBus::Send2Cluster(const ClusterInfo& Info, const char *pSendBuff, int SendBuffLen)
{
    if(Info.SocketID == -1 || Info.ConnPos == 0)
    {
        XF_LOG_TRACE(0, 0, "ClusterID:%d is not active", Info.ClusterID);
        return 0;
    }
    
    unsigned int CurConnPos = Info.ConnPos;
    std::map<unsigned int, CConnInfo*>::iterator pConnInfoMap = m_PosConnMap.find(CurConnPos);
    if(pConnInfoMap == m_PosConnMap.end())
    {
        XF_LOG_WARN(0, 0, "find invalid connpos %d", CurConnPos);
        return -1;
    }
    
    CConnInfo *pCurConnInfo = pConnInfoMap->second;

    int Ret2 = pCurConnInfo->Send(pSendBuff, SendBuffLen);
    if (Ret2 >= 0 && Ret2 < SendBuffLen)
    {
        //发送数据被阻塞了
        int Ret3 = pCurConnInfo->AddSendData(pSendBuff + Ret2, SendBuffLen - Ret2);
        if (Ret3 != 0)
        {
            XF_LOG_WARN(0, 0, "add send data failed|%u", CurConnPos);
            ReleaseConn(pConnInfoMap);
            return -2;
        }

        //添加EPOLLOUT事件
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLOUT;
        ev.data.u32 = CurConnPos;

        Ret3 = epoll_ctl(m_EpollFD, EPOLL_CTL_MOD, pCurConnInfo->GetSockID(), &ev);
        if(Ret3 != 0)
        {
            XF_LOG_WARN(0, 0, "epoll mod add epoll_out failed|%d|%s", errno, strerror(errno));
            //TODO 这里需要考虑后续处理
        }

        XF_LOG_TRACE(0, 0, "Send2Proxy|%d|%d|Add epoll out event", CurConnPos, pCurConnInfo->GetSockID());
    }
    else if (Ret2 < 0)
    {
        //发送数据失败
        XF_LOG_WARN(0, 0, "send data failed|%u", CurConnPos);
        ReleaseConn(pConnInfoMap);
        return -3;
    }
    else
    {
        //发送数据成功
    }

    return 0;
}

int CBus::ConnectCluster(unsigned int ClusterID)
{
    if (ClusterID == GetClusterID())
    {
        return 0;
    }

    map<unsigned int, ClusterInfo>::iterator iter = m_mapClusterInfo.find(ClusterID);

    if(iter != m_mapClusterInfo.end())
    {
        if(iter->second.SocketID != -1 || iter->second.ConnPos != 0)
        {
            epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, iter->second.SocketID, NULL);
            close(iter->second.SocketID);
            std::map<unsigned int, CConnInfo*>::iterator iter2 = m_PosConnMap.find(iter->second.ConnPos);
            if (iter2 != m_PosConnMap.end())
            {
                m_PosConnMap.erase(iter2);
            }

            iter->second.SocketID = -1;
            iter->second.ConnPos = 0;
        }
        
        int SocketID = socket(AF_INET, SOCK_STREAM, 0);
        if (-1 == SocketID)
        {
            XF_LOG_WARN(0, 0,"create socket faile|%d|%s", errno, strerror(errno));
            return -1;
        }
        
        struct sockaddr_in stSockAddr;
        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(iter->second.ClusterPort);
        stSockAddr.sin_addr.s_addr = iter->second.ClusterIP;

        if (-1 == connect(SocketID, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)))
        {
            // 进入这个分支表示超时或者出错
            close(SocketID);
            XF_LOG_WARN(0, 0, "select return error|%d", ClusterID);
            return -1;
        }
        
        XF_LOG_INFO(0 , 0, "ConnetCluster:%d", ClusterID);
        
        struct sockaddr_in local_addr;
        socklen_t local_len = sizeof(local_addr);
        memset(&local_addr, 0x0, sizeof(local_addr));
        int Ret = getsockname(SocketID, (struct sockaddr *)&local_addr, &local_len);
        if (Ret != 0)
        {
            XF_LOG_WARN(0, 0, "getsockname failed");
            return -1;
        }

        //经过一定的周期，可能产生的ConnPos正在使用中
        m_ClusterPosCnt++;
        unsigned int CurConnPos;
        if (m_ClusterPosCnt < CLUSTER_SOCK_CPOS_MIN || m_ClusterPosCnt > CLUSTER_SOCK_CPOS_MAX)
        {
            m_ClusterPosCnt = CLUSTER_SOCK_CPOS_MIN+1;
        }
   
        CurConnPos = m_ClusterPosCnt;

        //检索当前的map，防止出现冲突的ConnPos，满的时候会进入死循环
        while (true)
        {
            if (m_PosConnMap.find(CurConnPos) == m_PosConnMap.end())
            {
                break;
            }

            CurConnPos++;

            if (CurConnPos < CLUSTER_SOCK_CPOS_MIN || CurConnPos > CLUSTER_SOCK_CPOS_MAX)
            {
                CurConnPos = CLUSTER_SOCK_CPOS_MIN + 1;
            }
        }
        
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
        ev.data.u32 = CurConnPos;

        if(epoll_ctl(m_EpollFD, EPOLL_CTL_ADD, SocketID, &ev) != 0)
        {
            XF_LOG_WARN(0, 0, "epoll add failed|%d|%s", errno, strerror(errno));
            close(SocketID);
            return -1;
        }
        
        CConnInfo *pCurConnInfo = new CConnInfo(CurConnPos, SocketID, &stSockAddr, &local_addr);

        if(m_PosConnMap.insert(std::pair<unsigned int, CConnInfo*>(CurConnPos, pCurConnInfo)).second == false)
        {
            XF_LOG_WARN(0, 0, "conn map add failed");
            epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, SocketID, NULL);
            close(SocketID);
            SAFE_DELETE(pCurConnInfo);
            return -1;
        }
        
        iter->second.SocketID = SocketID;
        iter->second.ConnPos = CurConnPos;
    }
    
    return 0;
}


int CBus::DisconnetCluster(unsigned int ClusterID)
{
    if (ClusterID == GetClusterID())
    {
        return 0;
    }

    map<unsigned int, ClusterInfo>::iterator iter = m_mapClusterInfo.find(ClusterID);
    if(iter != m_mapClusterInfo.end())
    {
        if(iter->second.SocketID != -1 || iter->second.ConnPos != 0)
        {
            // 这几步执行多一次也没关系
            epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, iter->second.SocketID, NULL);
            close(iter->second.SocketID);
            std::map<unsigned int, CConnInfo*>::iterator iter2 = m_PosConnMap.find(iter->second.ConnPos);
            if (iter2 != m_PosConnMap.end())
            {
                m_PosConnMap.erase(iter2);
            }
           
            iter->second.SocketID = -1;
            iter->second.ConnPos = 0;
            
            XF_LOG_INFO(0 , 0, "DisconnetCluster:%d", ClusterID);
        }
    }
    
    return 0;
}
