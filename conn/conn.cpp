#include <functional>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>

#include "conn.h"
#include "log/log.h"
#include "util/util.h"
#include "ini_file/ini_file.h"
#include "../include/cmd.h"
#include "md5/md5.h"
#include "bus_header.h"

#include "bus.pb.h"
#include "app.pb.h"
#include "mm.pb.h"


using namespace mmlib;
using namespace std;

const unsigned int LISTEN_SOCK_CPOS_MIN = 100;    //100-199的ConnPos用于监听的Socket
const unsigned int LISTEN_SOCK_CPOS_MAX = 199;

const unsigned int WLISTEN_SOCK_CPOS_MIN = 200;    //200-299的ConnPos用于webpcl监听的Socket
const unsigned int WLISTEN_SOCK_CPOS_MAX = 299;

const unsigned int CLIENT_SOCK_CPOS_MIN = 500;    //500以上才是客户端连接上来的Socket

const unsigned int MAX_EPOLL_RET_EVENTS = 1024;


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


CConnInfo::CConnInfo(unsigned int ConnPos, int SockID, const struct sockaddr_in *pRemoteAddr, const struct sockaddr_in *pLocalAddr, const int flag)
{

    m_ConnPos = ConnPos;
    m_SockID = SockID;
    m_RemoteAddr = *pRemoteAddr;
    m_LocalAddr = *pLocalAddr;
    m_RemainSendData.clear();
    m_RemainRecvData.clear();
    m_LastActTime = time(NULL);
    m_UserID = 0;
    m_ConnType = flag;
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
        XF_LOG_DEBUG(0, 0, "conn close|%s|%d|%d", RemoteAddrStr(), ntohs(RemotePort()), m_ConnPos);
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
            XF_LOG_TRACE(0, 0, "Send|%d|%s", WriteBytes,  CStrTool::Str2Hex(m_RemainSendData.c_str(), WriteBytes));
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
                XF_LOG_WARN(0, 0, "%s|send reamin data failed, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
            }
            else
            {
                XF_LOG_DEBUG(0, 0, "%s|send reamin data failed, remain_send_data_len=%lu, send_len=%d", RemoteAddrStr(), (unsigned long)m_RemainSendData.length(), WriteBytes);
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

CConn *CConn::m_pSelf = NULL;


CConn::CConn()
{
    m_ServerID = 0;
    m_StateTime = 0;
    m_EpollFD = -1;
    m_ConnPosCnt = 0;

    m_pSelf = this;

    m_PosConnMap.clear();
    m_itrCurCheckConn = m_PosConnMap.begin();
    m_CheckConnPos = 0;
    m_TimeOut = 0;

    m_pProcessBuff = NULL;
    m_pSendBuff = NULL;
    memset(&m_ConfFile, 0, sizeof(m_ConfFile));
}

CConn::~CConn()
{
    if (m_EpollFD >= 0)
    {
        close(m_EpollFD);
        m_EpollFD = -1;
    }

    if(m_pProcessBuff)
    {
        SAFE_DELETE(m_pProcessBuff);
    }

    if(m_pSendBuff)
    {
        SAFE_DELETE(m_pSendBuff);
    }
}

int CConn::Init(const char *pConfFile)
{
    int Ret = 0;

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
    signal(SIGPIPE, SIG_IGN);

    //读取配置文件
    CIniFile IniFile(pConfFile);

    char ListenStr[1024] = {0};
    char BusConfPath[256] = {0};

    int GnsShmKey = 0;
    int GnsShmSize = 0;
    
    char ModuleName[256] = {0};
    int LogLocal = 0;
    int LogLevel = 0;
    char LogPath[1024] = {0};

    snprintf(m_ConfFile, sizeof(m_ConfFile), "%s", pConfFile);

    if (IniFile.IsValid())
    {
        IniFile.GetInt("CONN", "ServerID", 0, &m_ServerID);
        IniFile.GetString("CONN", "Listen", "", ListenStr, sizeof(ListenStr));
        IniFile.GetInt("CONN", "TimeOut", 600, &m_TimeOut);    //默认10分钟没有请求，将断开链接
        IniFile.GetInt("CONN", "StateTime", 0, &m_StateTime);
        IniFile.GetString("CONN", "BusConfPath", "", BusConfPath, sizeof(BusConfPath));

        IniFile.GetInt("CONN", "GnsShmKey", 0, &GnsShmKey);
        IniFile.GetInt("CONN", "GnsShmSize", 0, &GnsShmSize);
        IniFile.GetInt("CONN", "NodeNum", 0, &m_MaxGnsNodeNum);
        
        IniFile.GetString("LOG", "ModuleName", "conn", ModuleName, sizeof(ModuleName));
        IniFile.GetInt("LOG", "LogLocal", 1, &LogLocal);
        IniFile.GetInt("LOG", "LogLevel", 3, &LogLevel);
        IniFile.GetString("LOG", "LogPath", "./log", LogPath, sizeof(LogPath));
    }
    else
    {
        printf("ERR:conf file [%s] is not valid\n", pConfFile);
        return -1;
    }

    printf("INFO:read conf succ, ServerID=%d\n", m_ServerID);

    if(BusConfPath[0] == 0)
    {
        printf("ERR:BUS/BusConfPath is not valid\n");
        return -1;
    }

    int ConnRecvQueueKey= 0;
    int ConnRecvQueueSize = 0;
    int ConnSendQueueKey = 0;
    int ConnSendQueueSize = 0;
    string strServerID = CStrTool::Format("SERVER_%d", m_ServerID);
    
    CIniFile BusFile(BusConfPath);
    if (!BusFile.IsValid())
    {
        printf("ERR:conf file [%s] is not valid\n", BusConfPath);
        return -1;
    }

    BusFile.GetInt("BUS_GLOBAL", "GCIMKey", 0, &ConnSendQueueKey);
    BusFile.GetInt("BUS_GLOBAL", "GCIMSize", 0, &ConnSendQueueSize);
    BusFile.GetInt(strServerID.c_str(), "QueueKey", 0, &ConnRecvQueueKey);
    BusFile.GetInt(strServerID.c_str(), "QueueSize", 0, &ConnRecvQueueSize);
    
    if (ModuleName[0] == 0)
    {
        printf("ERR:LOG/ModuleName is not valid\n");
        return -1;
    }

    OpenLog(CStrTool::Format("%s_%d", ModuleName, m_ServerID).c_str());
    if (LogLocal == 1)
    {
        SetLogLocal(1, LogLevel, LogPath);
    }

    //分配空间
    if (!m_pProcessBuff)
    {
        m_pProcessBuff = (char *)malloc(XY_PKG_MAX_LEN);
    }

    if (!m_pSendBuff)
    {
        m_pSendBuff = (char *)malloc(XY_PKG_MAX_LEN);
    }

    //解析监听地址
    char *pszOneIPAddr = NULL;
    int iCurAddrNum = 0;
    std::vector<ConnMeta> vctListenConnMetaList;
    for(char *pszSecVal = ListenStr; (pszOneIPAddr = strtok(pszSecVal, ",")) != NULL; pszSecVal=NULL)
    {
        //去掉空格
        while ((*pszOneIPAddr) == ' ')
        {
            pszOneIPAddr++;
        }

        ConnMeta CurAddr;
        memset(&CurAddr, 0, sizeof(CurAddr));
        char *AddrDiv = strstr(pszOneIPAddr, ":");
        if (AddrDiv == NULL)
        {
            snprintf(m_szErrMsg, sizeof(m_szErrMsg), "CONN/Listen is not valid, str=%s, addr_idx=%d", ListenStr, iCurAddrNum);
            return -1;
        }

        char CurIPStr[32];
        char CurPortStr[16];
        memset(CurIPStr, 0, sizeof(CurIPStr));
        memset(CurPortStr, 0, sizeof(CurPortStr));

        memcpy(CurIPStr, pszOneIPAddr, (AddrDiv - pszOneIPAddr));
        memcpy(CurPortStr, AddrDiv+1, (strlen(pszOneIPAddr) - (AddrDiv - pszOneIPAddr) - 1));
        printf("CONN_INFO|%s|%s|%s\n", pszOneIPAddr, CurIPStr, CurPortStr);
        CurAddr.LocalIP = inet_addr(CurIPStr);
        CurAddr.LocalPort = atoi(CurPortStr);

        //if (CurAddr.LocalIP == 0||CurAddr.LocalPort == 0)
        if (CurAddr.LocalPort == 0)
        {
            snprintf(m_szErrMsg, sizeof(m_szErrMsg), "CONN/Listen is not valid, str=%s, addr[%s] or port [%s] is not valid", ListenStr, CurIPStr, CurPortStr);
            return -1;
        }

        iCurAddrNum++;

        vctListenConnMetaList.push_back(CurAddr);
    }

    if (vctListenConnMetaList.size() == 0)
    {
        snprintf(m_szErrMsg, sizeof(m_szErrMsg), "CONN/Listen is not valid, str=%s, addr_num=%d", ListenStr, 0);
        return -1;
    }

    
    //初始化QUEUE
    Ret = m_RecvQueue.Init(ConnRecvQueueKey, ConnRecvQueueSize);
    if (Ret != 0)
    {
        snprintf(m_szErrMsg, sizeof(m_szErrMsg), "init RecvQueue failed, key=%u, size=%u, ret=%d, errmsg=%s", ConnRecvQueueKey, ConnRecvQueueSize, Ret, m_RecvQueue.GetErrMsg());
        return -1;
    }
    XF_LOG_INFO(0, 0,  "init RecvQueue succ, key=0x%x, size=%u", ConnRecvQueueKey, ConnRecvQueueSize);

    
    Ret = m_SendQueue.Init(ConnSendQueueKey, ConnSendQueueSize);
    if (Ret != 0)
    {
        snprintf(m_szErrMsg, sizeof(m_szErrMsg), "init SendQueue failed, key=%u, size=%u, ret=%d, errmsg=%s", ConnSendQueueKey, ConnSendQueueSize, Ret, m_SendQueue.GetErrMsg());
        return -1;
    }
    XF_LOG_INFO(0, 0,  "init SendQueue succ, key=0x%x, size=%u", ConnSendQueueKey, ConnSendQueueSize);

    //创建EPOLL
    m_EpollFD = epoll_create(XY_MAX_CONN_NUM);
    if (m_EpollFD == -1)
    {
        printf("ERR:epoll create failed|%d|%d|%s\n", XY_MAX_CONN_NUM, errno, strerror(errno));
        return -1;
    }

    //创建监听 和 EPOLL
    for (unsigned int i = 0; i < vctListenConnMetaList.size(); i++)
    {
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
        addr.sin_addr.s_addr = vctListenConnMetaList[i].LocalIP;
        addr.sin_port = htons((unsigned short)vctListenConnMetaList[i].LocalPort);

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
        ev.data.u32 = LISTEN_SOCK_CPOS_MIN + i;

        if(0 != epoll_ctl(m_EpollFD, EPOLL_CTL_ADD, CurListenSockID, &ev))
        {
            printf("ERR:epoll_ctl failed|%d|%s\n", errno, strerror(errno));
            return -1;
        }

        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0x0, sizeof(remote_addr));

        CConnInfo *pCurConnInfo = new CConnInfo(LISTEN_SOCK_CPOS_MIN + i, CurListenSockID, &remote_addr, &addr, CONN_UNAUTH);

        if(m_PosConnMap.insert(std::pair<unsigned int, CConnInfo*>(LISTEN_SOCK_CPOS_MIN + i, pCurConnInfo)).second == false)
        {
            XF_LOG_WARN(0, 0, "conn map add failed");
            epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, CurListenSockID, NULL);
            close(CurListenSockID);
            SAFE_DELETE(pCurConnInfo);
            return -1;
        }

        printf("INFO:service listen ok, ip=%s, port=%d\n", inet_ntoa(*(struct in_addr *)(&addr.sin_addr.s_addr)), ntohs(addr.sin_port));
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

    vector<LoginData> vctData;
    for(int i = 0; i < m_MaxGnsNodeNum; i++)
    {
        m_GnsInfoMap.ProcessAll(i, MemFullCallBackAll, &vctData);
    }

    int UserSize = vctData.size();
    printf("vctUserID Size = %d\n", UserSize);
    
    mm::LoginDisconnect CurReq;
    int j = 0;
    for(int i = 0; i < UserSize; i++)
    {
        mm::LoginData data;
        data.set_userid(vctData[i].UserID);
        data.set_connpos(vctData[i].ConnPos);
        data.set_serverid(vctData[i].ServerID);
        data.set_time(vctData[i].LastActiveTime);
        CurReq.add_data()->CopyFrom(data);
        
        j++;
        if(j > 1000 || i == UserSize-1)
        {
            j = 0;

            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_Disconnect;
            Header.SN = 0;
            Header.ConnPos = 0;
            Header.UserID = 0;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, GROUP_CONN, TO_GRP_ALLNOME, 0, CurReq);
        }
    }

    m_GnsInfoMap.Clear();

    mm::LoginSyncReq CurReq2;
    CurReq2.set_serverid(GetServerID());
    
    XYHeaderIn Header;
    Header.SrcID = GetServerID();
    Header.CmdID = Cmd_LoginSync_Req;
    Header.SN = 0;
    Header.ConnPos = 0;
    Header.UserID = 0;
    Header.PkgTime = time(NULL);
    Header.Ret = 0;
    
    Send2Server(Header, GROUP_CONN, TO_GRP_ALLNOME, 0, CurReq2);

    printf("LoginSync Requrest Send\n");
    
    printf("svr init success\n");
    
    return 0;
}

int CConn::AcceptConn(int ConnPos, int type)
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

        CConnInfo *pCurConnInfo = new CConnInfo(CurConnPos, NewSockID, &RemoteAddr, &local_addr, type);

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

void CConn::CheckConn()
{
    //每次只检查一个链接是否超时
    if (m_itrCurCheckConn == m_PosConnMap.end())
    {
        m_itrCurCheckConn = m_PosConnMap.begin();
        return;
    }

    if ((m_itrCurCheckConn->first >= LISTEN_SOCK_CPOS_MIN) && (m_itrCurCheckConn->first <= LISTEN_SOCK_CPOS_MAX))
    {
        m_itrCurCheckConn++;
        return;
    }

    if ((m_itrCurCheckConn->first >= WLISTEN_SOCK_CPOS_MIN) && (m_itrCurCheckConn->first <= WLISTEN_SOCK_CPOS_MAX))
    {
        m_itrCurCheckConn++;
        return;
    }
    //XF_LOG_DEBUG(0, 0, "check conn|%s|%d|%u|%s", m_itrCurCheckConn->second->RemoteAddrStr(), m_itrCurCheckConn->second->RemotePort(), m_itrCurCheckConn->first, CStrTool::TimeString(m_itrCurCheckConn->second->GetLastActTime()));

    time_t TineNow = time(NULL);
    time_t TimeDiff = TineNow - m_itrCurCheckConn->second->GetLastActTime();
    if (TimeDiff > m_TimeOut)
    {
        XF_LOG_WARN(0, 0, "conn timeout|%s|%d|%u|%s", m_itrCurCheckConn->second->RemoteAddrStr(), m_itrCurCheckConn->second->RemotePort(), m_itrCurCheckConn->first, CStrTool::TimeString(m_itrCurCheckConn->second->GetLastActTime()));
        std::map<unsigned int, CConnInfo*>::iterator itrTmpConn = m_itrCurCheckConn;
        ReleaseConn(itrTmpConn);
    }
    else
    {
        m_itrCurCheckConn++;
    }

    return;
}

void CConn::ReleaseConn(std::map<unsigned int, CConnInfo*>::iterator &itrConnInfoMap, bool bSend)
{
    CConnInfo *pCurConnInfo = itrConnInfoMap->second;
    epoll_ctl(m_EpollFD, EPOLL_CTL_DEL, pCurConnInfo->GetSockID(), NULL);
    close(pCurConnInfo->GetSockID());
    if (itrConnInfoMap == m_itrCurCheckConn)
    {
        m_itrCurCheckConn++;
    }

    uint64_t UserID = pCurConnInfo->GetUserID();
    
    if (UserID != 0)
    {
        time_t nowTime = time(NULL);
        ShmGnsInfo* pCurGnsInfo = m_GnsInfoMap.Get(UserID);
        if(pCurGnsInfo != NULL)
        {
            pCurGnsInfo->ServerID = GetServerID();
            pCurGnsInfo->ConnPos = itrConnInfoMap->first;
            pCurGnsInfo->Status = GNS_USER_STATUS_UNACTIVE;
            pCurGnsInfo->LastActiveTime = nowTime;

            XF_LOG_INFO(0, UserID, "m_GnsInfoMap modify success");
        }
        
        if(bSend)
        {
            // 通知其它CONN连接已断开
            mm::LoginDisconnect CurReq;
            mm::LoginData data;
            data.set_userid(UserID);
            data.set_connpos(itrConnInfoMap->first);
            data.set_serverid(GetServerID());
            data.set_time(nowTime);
            CurReq.add_data()->CopyFrom(data);
            
            XYHeaderIn Header;
            Header.SrcID = GetServerID();
            Header.CmdID = Cmd_Disconnect;
            Header.SN = 0;
            Header.ConnPos = itrConnInfoMap->first;
            Header.UserID = UserID;
            Header.PkgTime = time(NULL);
            Header.Ret = 0;
            
            Send2Server(Header, GROUP_CONN, TO_GRP_ALLNOME, 0, CurReq);
        }
    }

    m_PosConnMap.erase(itrConnInfoMap);
    
    XF_LOG_INFO(0, UserID, "DEL_POS_CONN_MAP|%d", itrConnInfoMap->first);

    SAFE_DELETE(pCurConnInfo);
}


int CConn::Run()
{
    int Ret = 0;
    StopFlag = false;

    struct epoll_event RetEvent[MAX_EPOLL_RET_EVENTS];
    int RetEventNum = 0;

    char *pRecvBuff = (char *)malloc(XY_MAXBUFF_LEN);
    int RecvLen = 0, SendLen = 0;

    time_t LastStateTime = time(NULL);

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
                if((CurConnPos >= LISTEN_SOCK_CPOS_MIN) && (CurConnPos <= LISTEN_SOCK_CPOS_MAX))
                {
                    if(RetEvent[i].events & EPOLLIN)
                    {
                        AcceptConn(CurConnPos, CONN_UNAUTH);
                    }
                    else
                    {
                        XF_LOG_WARN(0, 0, "listening socket recv event %d", RetEvent[i].events);
                    }
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
                            int ProcLen = 0;
                            if(pCurConnInfo->GetConnType() == CONN_UNAUTH || pCurConnInfo->GetConnType() == CONN_AUTH)
                            {
                                ProcLen = ProcessPkg(pCurBuffPos, RecvLen, pConnInfoMap);
                            }
                            else
                            {
                                XF_LOG_WARN(0, 0, "m_ConnType = %d", pCurConnInfo->GetConnType());
                                ProcLen = -1;
                            }
                            
                            if ((ProcLen > (int)RecvLen)||(ProcLen == 0))
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
        

        // 从Queue收包
        const unsigned int MAX_QUEUEPKG_PER_LOOP = 100;
        for(int CurRecvCount=0; CurRecvCount<(int)MAX_QUEUEPKG_PER_LOOP; CurRecvCount++)
        {
            SendLen = XY_PKG_MAX_LEN;
            Ret = m_RecvQueue.OutQueue(m_pProcessBuff, &SendLen);
            if (Ret == 0)
            {
                XF_LOG_TRACE(0, 0, "Run|OutQueue|%d|%s", SendLen, CStrTool::Str2Hex(m_pProcessBuff, SendLen));
                
                //需要发包哦~~
                EmptyFlag = 0;
                BusHeader CurBusHeader;
                CurBusHeader.Read(m_pProcessBuff);
                int PkgLen = SendLen - CurBusHeader.GetHeaderLen();
                char *pSendBuff = m_pProcessBuff + CurBusHeader.GetHeaderLen();
                
                Ret = DealPkg(pSendBuff, PkgLen);
                if(Ret != 0)
                {
                    XF_LOG_WARN(0, 0, "Run|DealPkg failed, Ret=%d", Ret);
                    continue;
                }
            }
            else if (Ret == CShmQueue::E_SHM_QUEUE_EMPTY)
            {
                break;
            }
            else
            {
                //出错了
                XF_LOG_WARN(0, 0, "Run|OutQueue failed, Ret=%d, errmsg=%s", Ret, m_RecvQueue.GetErrMsg());
                break;
            }
        }

        // 向bus更新自己的状态
        time_t NowTime = time(NULL);
        if(NowTime - LastStateTime >= m_StateTime)
        {
            LastStateTime = NowTime;
            SendStateMessage();
        }

        // 对连接进行检查扫描
        CheckConn();

        CheckValid();
    }

    return 0;
}

int CConn::Send2Client(int CurConnPos, const char *pSendBuff, int SendBuffLen)
{

    std::map<unsigned int, CConnInfo*>::iterator pConnInfoMap = m_PosConnMap.find(CurConnPos);
    if (pConnInfoMap == m_PosConnMap.end())
    {
        XF_LOG_WARN(0, 0, "find invalid cpos|%u", CurConnPos);
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


int CConn::Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message)
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


int CConn::Send2Server(unsigned int DstID, char SendType, char Flag, const char* pCurBuff, int PkgLen)
{
    BusHeader CurBusHeader;
    int HeaderLen = CurBusHeader.GetHeaderLen();
    CurBusHeader.PkgLen = HeaderLen + PkgLen;
    CurBusHeader.CmdID = Cmd_Transfer;
    CurBusHeader.SrcID = GetServerID();
    CurBusHeader.DstID = DstID;
    CurBusHeader.SendType = SendType;
    CurBusHeader.Flag = Flag;
    CurBusHeader.Write(m_pSendBuff);

    memcpy(m_pSendBuff+HeaderLen, pCurBuff, PkgLen);

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



int CConn::SendErrMsg2Server(unsigned int DstID, unsigned int CmdID, int ErrCode)
{
    mm::ErrorMsg CurReq;
    CurReq.set_errcode(ErrCode);

    XYHeaderIn Header;
    Header.SrcID = GetServerID();
    Header.CmdID = CmdID;
    Header.SN = 0;
    Header.ConnPos = 0;
    Header.UserID = 0;
    Header.PkgTime = time(NULL);
    Header.Ret = ErrCode;

    return Send2Server(Header, DstID, TO_SRV, 0, CurReq);
}


//返回值:0~PkgLen-1表示包不够大，-1表示出错要删除链接， PkgLen表示正常。如果要删除链接不要在这里删，返回-1即可
int CConn::ProcessPkg(const char *pCurBuffPos, int RecvLen, std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap)
{
    int Offset = 0;
    XYHeader CurHeader;
    XYHeaderIn CurHeaderIn;
    //包头长度还不够
    if (RecvLen < CurHeader.GetHeadLen())
    {
        return CurHeader.GetHeadLen();
    }

    //解析包头,之后offset偏移到data字段（data包括了cmdname）
    Offset = CurHeader.Read(pCurBuffPos);

    //判断包长
    if((int)CurHeader.PkgLen < CurHeader.GetHeadLen())
    {
        XF_LOG_WARN(0, 0, "pkg len is not valid, len=%d", CurHeader.PkgLen);
        return -1;
    }

    if ((int)CurHeader.PkgLen - CurHeader.GetHeadLen() > XY_PKG_MAX_LEN - CurHeaderIn.GetHeaderLen())
    {
        XF_LOG_WARN(0, 0, "pkg len is not valid, len=%d", CurHeader.PkgLen);
        return -1;
    }

    //接收到的数据不够一个包
    if(CurHeader.PkgLen > (unsigned int)RecvLen)
    {
        return CurHeader.PkgLen;
    }

    // 除去所有包头的长度
    int PkgInLen = CurHeader.PkgLen-CurHeader.GetHeadLen();
    int TotalPkgLen = CurHeader.PkgLen-CurHeader.GetHeadLen()+CurHeaderIn.GetHeaderLen();
    memcpy(m_pProcessBuff+CurHeaderIn.GetHeaderLen(), pCurBuffPos+CurHeader.GetHeadLen(), PkgInLen);

    CurHeaderIn.SrcID = GetServerID(); //客户端过来的请求，默认写上自己的serverid
    CurHeaderIn.CmdID = CurHeader.CmdID;
    CurHeaderIn.SN = CurHeader.SN;
    CurHeaderIn.ConnPos = pConnInfoMap->second->GetConnPos();
    CurHeaderIn.PkgTime = time(NULL);
    CurHeaderIn.Ret = CurHeader.Ret;
    CurHeaderIn.UserID = pConnInfoMap->second->GetUserID();
    CurHeaderIn.Write(m_pProcessBuff);
        
    //校验CKSUM
    
    
    //读取包体

    char* pProcessBuff = m_pProcessBuff;
    
    if(CurHeader.Compresse == 1)
    {
        //暂时不考虑
        pProcessBuff = m_pProcessBuff;
    }

    if(CurHeaderIn.CmdID != Cmd_Auth_Login_Req && CurHeaderIn.CmdID != Cmd_Auth_Register_Req && pConnInfoMap->second->GetConnType() != CONN_AUTH)
    {
        return CurHeader.PkgLen;
    }

    unsigned int CmdPrefix = CurHeaderIn.CmdID>>16;
    switch(CmdPrefix)
    {
        case CMD_PREFIX_CONN:
        {
            break;
        }
        case CMD_PREFIX_AUTH:
        {
            if(CurHeaderIn.CmdID == Cmd_Auth_Login_Req)
            {
                app::LoginReq CurReq;
                if(!CurReq.ParseFromArray(pProcessBuff+CurHeaderIn.GetHeaderLen(), PkgInLen))
                {
                    XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", CurHeaderIn.CmdID);
                    return -1;
                }

                uint64_t UserID = CurReq.userid();
                string strPasswd = CurReq.passwd();
                int Plat = CurReq.plat();
                
                mm::AuthLoginReq CurReq2;
                CurReq2.set_userid(UserID);
                CurReq2.set_passwd(strPasswd);
                CurReq2.set_plat(Plat);

                Send2Server(CurHeaderIn, GROUP_AUTH, TO_GRP_RND, 0, CurReq2);
            }
            else if(CurHeaderIn.CmdID == Cmd_Auth_Register_Req)
            {
                app::RegisterReq CurReq;
                if(!CurReq.ParseFromArray(pProcessBuff+CurHeaderIn.GetHeaderLen(), PkgInLen))
                {
                    XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", CurHeaderIn.CmdID);
                    return -1;
                }

                string strPasswd = CurReq.passwd();
                string strNickName = CurReq.nickname();
                int Sex = CurReq.sex();
                uint64_t Birthday = CurReq.birthday();
                string strTelNo = CurReq.telno(); 
                string strAddress = CurReq.address();
                string strEmail = CurReq.email();
                
                mm::AuthRegisterReq CurReq2;
                CurReq2.set_passwd(strPasswd);
                CurReq2.set_nickname(strNickName);
                CurReq2.set_sex(Sex);
                CurReq2.set_birthday(Birthday);
                CurReq2.set_telno(strTelNo);
                CurReq2.set_address(strAddress);
                CurReq2.set_email(strEmail);

                Send2Server(CurHeaderIn, GROUP_AUTH, TO_GRP_RND, 0, CurReq2);
            }
            else
            {
                Send2Server(GROUP_AUTH, TO_GRP_RND, 0, pProcessBuff, TotalPkgLen);
            }
            
            break;
        }
        case CMD_PREFIX_USER:
        {
            int ServerID = GetUserServer(CurHeaderIn.UserID);
            Send2Server(ServerID, TO_SRV, 0, pProcessBuff, TotalPkgLen);
            break;
        }
        default:
        {
            XF_LOG_WARN(0, 0, "unknow command %0x", CurHeaderIn.CmdID);
            break;
        }
    }
    

    return CurHeader.PkgLen;
}

int CConn::DealPkg(const char *pCurBuffPos, int PkgLen)
{
    XYHeaderIn Header;
    Header.Read(pCurBuffPos);
    
    switch(Header.CmdID)
    {
        case Cmd_Auth_Register_Rsp:
        {
            int ConnPos = Header.ConnPos;
            
            mm::AuthRegisterRsp CurRsp;
            if(!CurRsp.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Header.CmdID);
                return -1;
            }

            int Result = CurRsp.ret();
            uint64_t UserID = CurRsp.userid();
            string strNickName = CurRsp.nickname();
            int Sex = CurRsp.sex();
            uint64_t Birthday = CurRsp.birthday();
            string strTelNo = CurRsp.telno(); 
            string strAddress = CurRsp.address();
            string strEmail = CurRsp.email();
            
            if(Result != 0)
            {
                // 不成功,返回客户端失败原因
                app::RegisterRsp CurRsp2;
                CurRsp2.set_ret(Result);
                CurRsp2.set_userid(UserID);
                
                XYHeader CurHeader;
                CurHeader.PkgLen = CurRsp2.ByteSize() + CurHeader.GetHeadLen();
                CurHeader.CmdID = Cmd_Auth_Register_Rsp;
                CurHeader.SN = Header.SN;
                CurHeader.CkSum = 0;
                CurHeader.Ret = Header.Ret;
                CurHeader.Compresse = 0;
                CurHeader.Write(m_pSendBuff);
                int HeaderLen = CurHeader.GetHeadLen();
                if(!CurRsp2.SerializeToArray(m_pSendBuff+HeaderLen, XY_PKG_MAX_LEN-HeaderLen))
                {
                    XF_LOG_WARN(0, 0, "pack err msg failed");
                    return -1;
                }

                Send2Client(ConnPos, m_pSendBuff, CurHeader.PkgLen);
            }
            else
            {
                mm::UserRegisterReq CurReq;
                CurReq.set_userid(UserID);
                CurReq.set_nickname(strNickName);
                CurReq.set_sex(Sex);
                CurReq.set_birthday(Birthday);
                CurReq.set_telno(strTelNo);
                CurReq.set_address(strAddress);
                CurReq.set_email(strEmail);

                XYHeaderIn CurHeader;
                CurHeader.SrcID = GetServerID();
                CurHeader.CmdID = Cmd_User_Register_Req;
                CurHeader.SN = Header.SN;
                CurHeader.ConnPos = ConnPos;
                CurHeader.UserID = Header.UserID;
                CurHeader.PkgTime = time(NULL);
                CurHeader.Ret = 0;

                int ServerID = SERVER_USER_BEGIN + UserID % MAX_USER_SERVER_NUM + 1;
                Send2Server(CurHeader, ServerID, TO_SRV, 0, CurReq);
            }
            
            break;
        }
        case Cmd_User_Register_Rsp:
        {
            int ConnPos = Header.ConnPos;
            
            mm::UserRegisterRsp CurRsp;
            if(!CurRsp.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmd=%0x", Header.CmdID);
                return -1;
            }

            int Result = CurRsp.ret();
            uint64_t UserID = CurRsp.userid();

            app::RegisterRsp CurRsp2;
            CurRsp2.set_userid(UserID);
            CurRsp2.set_ret(Result);

            XYHeader CurHeader;
            CurHeader.PkgLen = CurRsp2.ByteSize() + CurHeader.GetHeadLen();
            CurHeader.CmdID = Cmd_Auth_Register_Rsp;
            CurHeader.SN = Header.SN;
            CurHeader.CkSum = 0;
            CurHeader.Ret = Header.Ret;
            CurHeader.Compresse = 0;
            CurHeader.Write(m_pSendBuff);
            int HeaderLen = CurHeader.GetHeadLen();
            if(!CurRsp2.SerializeToArray(m_pSendBuff+HeaderLen, XY_PKG_MAX_LEN-HeaderLen))
            {
                XF_LOG_WARN(0, 0, "pack err msg failed");
                return -1;
            }

            Send2Client(ConnPos, m_pSendBuff, CurHeader.PkgLen);
            
            break;
        }
        case Cmd_Auth_Login_Rsp:
        {
            int ConnPos = Header.ConnPos;
            
            mm::AuthLoginRsp CurRsp;
            if(!CurRsp.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmd=%0x", Header.CmdID);
                return -1;
            }

            uint64_t UserID = CurRsp.userid();
            int Result = CurRsp.ret();
            
            if(Result == 0)
            {
                // 修改连接信息
                std::map<unsigned int, CConnInfo*>::iterator pConnInfoMap = m_PosConnMap.find(ConnPos);
                if (pConnInfoMap == m_PosConnMap.end())
                {
                    XF_LOG_WARN(0, 0, "invalid cpos|%u", ConnPos);
                    return -1;
                }
            
                CConnInfo *pCurConnInfo = pConnInfoMap->second;
                pCurConnInfo->SetConnType(CONN_AUTH);
                pCurConnInfo->SetUserID(UserID);

                time_t nowTime = time(NULL);
                // 修改GNS信息,自家门口登录的,无条件覆盖
                ShmGnsInfo* pCurGnsInfo = m_GnsInfoMap.Get(UserID);
                if(pCurGnsInfo == NULL)
                {
                    ShmGnsInfo Info;
                    Info.UserID = UserID;
                    Info.ConnPos = ConnPos;
                    Info.ServerID = GetServerID();
                    Info.Status = GNS_USER_STATUS_ACTIVE;
                    Info.LastActiveTime = nowTime;

                    int Ret = m_GnsInfoMap.Insert(UserID, Info);
                    if(Ret != 0)
                    {
                        XF_LOG_WARN(0, UserID, "m_GnsInfoMap Insert failed, Ret=%d", Ret);
                        //return -1;
                    }
                }
                else
                {
                    if(pCurGnsInfo->Status == GNS_USER_STATUS_ACTIVE && pCurGnsInfo->ServerID == GetServerID())
                    {
                        std::map<unsigned int, CConnInfo*>::iterator iter = m_PosConnMap.find(pCurGnsInfo->ConnPos);
                        if(iter != m_PosConnMap.end() && iter->second->GetUserID() == UserID)
                        {
                            ReleaseConn(iter, false);
                        }
                    }
                    
                    pCurGnsInfo->ConnPos = ConnPos;
                    pCurGnsInfo->ServerID = GetServerID();
                    pCurGnsInfo->Status = GNS_USER_STATUS_ACTIVE;
                    pCurGnsInfo->LastActiveTime = nowTime;
                }
                
                // 成功,通知其他CONN
                mm::LoginNotice CurReq;
                mm::LoginData data;
                data.set_userid(UserID);
                data.set_serverid(GetServerID());
                data.set_connpos(ConnPos);
                data.set_time(nowTime);
                CurReq.add_data()->CopyFrom(data);
                
                XYHeaderIn CurHeader;
                CurHeader.SrcID = GetServerID();
                CurHeader.CmdID = Cmd_LoginNotice;
                CurHeader.SN = Header.SN;
                CurHeader.ConnPos = ConnPos;
                CurHeader.UserID = UserID;
                CurHeader.PkgTime = time(NULL);
                CurHeader.Ret = 0;

                Send2Server(CurHeader, GROUP_CONN, TO_GRP_ALLNOME, 0, CurReq);
            }

            app::LoginRsp CurRsp2;
            CurRsp2.set_ret(Result);
            
            XYHeader CurHeader;
            CurHeader.PkgLen = CurRsp2.ByteSize() + CurHeader.GetHeadLen();
            CurHeader.CmdID = Cmd_Auth_Login_Rsp;
            CurHeader.SN = Header.SN;
            CurHeader.CkSum = 0;
            CurHeader.Ret = Header.Ret;
            CurHeader.Compresse = 0;
            CurHeader.Write(m_pSendBuff);
            int HeaderLen = CurHeader.GetHeadLen();
            if(!CurRsp2.SerializeToArray(m_pSendBuff+HeaderLen, XY_PKG_MAX_LEN-HeaderLen))
            {
                XF_LOG_WARN(0, 0, "pack err msg failed");
                return -1;
            }

            Send2Client(ConnPos, m_pSendBuff, CurHeader.PkgLen);
            
            break;
        }
        case Cmd_LoginNotice:
        {
            mm::LoginNotice CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmd=%0x", Header.CmdID);
                return -1;
            }

            for(int j = 0; j < CurReq.data_size(); j++)
            {
                uint64_t UserID = CurReq.data(j).userid();
                int ServerID = CurReq.data(j).serverid();
                unsigned int ConnPos = CurReq.data(j).connpos();
                time_t _time = CurReq.data(j).time();
                
                ShmGnsInfo* pCurGnsInfo = m_GnsInfoMap.Get(UserID);
                if(pCurGnsInfo == NULL)
                {
                    ShmGnsInfo Info;
                    Info.UserID = UserID;
                    Info.ConnPos = ConnPos;
                    Info.ServerID = ServerID;
                    Info.Status = GNS_USER_STATUS_ACTIVE;
                    Info.LastActiveTime = _time;

                    int Ret = m_GnsInfoMap.Insert(UserID, Info);
                    if(Ret != 0)
                    {
                        XF_LOG_WARN(0, UserID, "m_GnsInfoMap Insert failed, Ret=%d", Ret);
                        //return -1;
                    }

                    XF_LOG_INFO(0, UserID, "m_GnsInfoMap Insert success");
                }
                else
                {
                    // 这里要求两台服务器的时间要同步
                    if(pCurGnsInfo->LastActiveTime <= (uint64_t)_time)
                    {
                        if(pCurGnsInfo->ServerID == GetServerID() && pCurGnsInfo->Status == GNS_USER_STATUS_ACTIVE)
                        {
                            std::map<unsigned int, CConnInfo*>::iterator iter = m_PosConnMap.find(pCurGnsInfo->ConnPos);
                            if(iter != m_PosConnMap.end() && iter->second->GetUserID() == UserID)
                            {
                                ReleaseConn(iter ,false);
                            }
                        }
                        
                        pCurGnsInfo->ConnPos = ConnPos;
                        pCurGnsInfo->ServerID = ServerID;
                        pCurGnsInfo->Status = GNS_USER_STATUS_ACTIVE;
                        pCurGnsInfo->LastActiveTime = _time;

                        XF_LOG_INFO(0, UserID, "m_GnsInfoMap modify success");
                    }
                }
            }
            
            break;
        }
        case Cmd_Disconnect:
        {
            mm::LoginDisconnect CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Header.CmdID);
                return -1;
            }

            for(int j = 0; j < CurReq.data_size(); j++)
            {
                uint64_t UserID = CurReq.data(j).userid();
                int ServerID = CurReq.data(j).serverid();
                int ConnPos = CurReq.data(j).connpos();
                time_t _time = CurReq.data(j).time();
                
                ShmGnsInfo* pCurGnsInfo = m_GnsInfoMap.Get(UserID);
                if(pCurGnsInfo != NULL)
                {
                    if(pCurGnsInfo->LastActiveTime <= (uint64_t)_time)
                    {
                        if(pCurGnsInfo->ServerID == GetServerID() && pCurGnsInfo->Status == GNS_USER_STATUS_ACTIVE)
                        {
                            std::map<unsigned int, CConnInfo*>::iterator iter = m_PosConnMap.find(pCurGnsInfo->ConnPos);
                            if(iter != m_PosConnMap.end() && iter->second->GetUserID() == UserID)
                            {
                                ReleaseConn(iter, false);
                            }                            
                        }
                        else
                        {
                            pCurGnsInfo->ServerID = ServerID;
                            pCurGnsInfo->ConnPos = ConnPos;
                            pCurGnsInfo->Status = GNS_USER_STATUS_UNACTIVE;
                            pCurGnsInfo->LastActiveTime = _time;

                            XF_LOG_INFO(0, UserID, "m_GnsInfoMap modify success");
                        }
                    }
                }
            }

            break;
        }
        case Cmd_LoginSync_Req:
        {
            mm::LoginSyncReq CurReq;
            if(!CurReq.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmdid=%0x", Header.CmdID);
                return -1;
            }

            if(CurReq.serverid() == GetServerID())
            {
                XF_LOG_WARN(0, 0, "Can not be here");
                return -1;
            }

            
            vector<LoginData> vctData;
            for(int i = 0; i < m_MaxGnsNodeNum; i++)
            {
                m_GnsInfoMap.ProcessAll(i, MemFullCallBackAll, &vctData);
            }
        
            int UserSize = vctData.size();
            XF_LOG_INFO(0, 0, "vctUserID Size = %d", UserSize);
            
            mm::LoginSyncRsp CurRsp;
            int j = 0;
            for(int i = 0; i < UserSize; i++)
            {
                mm::LoginData data;
                data.set_userid(vctData[i].UserID);
                data.set_connpos(vctData[i].ConnPos);
                data.set_serverid(vctData[i].ServerID);
                data.set_time(vctData[i].LastActiveTime);
                CurRsp.add_data()->CopyFrom(data);
                
                j++;
                if(j > 1000 || i == UserSize-1)
                {
                    j = 0;
        
                    XYHeaderIn CurHeader;
                    CurHeader.SrcID = GetServerID();
                    CurHeader.CmdID = Cmd_LoginSync_Rsp;
                    CurHeader.SN = 0;
                    CurHeader.ConnPos = 0;
                    CurHeader.UserID = 0;
                    CurHeader.PkgTime = time(NULL);
                    CurHeader.Ret = 0;
                    
                    Send2Server(CurHeader, Header.SrcID, TO_SRV, 0, CurRsp);
                }
            }
            
            
            break;
        }
        case Cmd_LoginSync_Rsp:
        {
            mm::LoginSyncRsp CurRsp;
            if(!CurRsp.ParseFromArray(pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen()))
            {
                XF_LOG_WARN(0, 0, "pkg parse failed, cmd=%0x", Header.CmdID);
                return -1;
            }

            for(int j = 0; j < CurRsp.data_size(); j++)
            {
                uint64_t UserID = CurRsp.data(j).userid();
                int ServerID = CurRsp.data(j).serverid();
                unsigned int ConnPos = CurRsp.data(j).connpos();
                time_t _time = CurRsp.data(j).time();
                
                ShmGnsInfo* pCurGnsInfo = m_GnsInfoMap.Get(UserID);
                if(pCurGnsInfo == NULL)
                {
                    ShmGnsInfo Info;
                    Info.UserID = UserID;
                    Info.ConnPos = ConnPos;
                    Info.ServerID = ServerID;
                    Info.Status = GNS_USER_STATUS_ACTIVE;
                    Info.LastActiveTime = _time;

                    int Ret = m_GnsInfoMap.Insert(UserID, Info);
                    if(Ret != 0)
                    {
                        XF_LOG_WARN(0, UserID, "m_GnsInfoMap Insert failed, Ret=%d", Ret);
                        //return -1;
                    }

                    XF_LOG_INFO(0, UserID, "m_GnsInfoMap Insert success");
                }
                else
                {
                    // 这里要求两台服务器的时间要同步
                    if(pCurGnsInfo->LastActiveTime <= (uint64_t)_time)
                    {
                        if(pCurGnsInfo->ServerID == GetServerID() && pCurGnsInfo->Status == GNS_USER_STATUS_ACTIVE)
                        {
                            std::map<unsigned int, CConnInfo*>::iterator iter = m_PosConnMap.find(pCurGnsInfo->ConnPos);
                            if(iter != m_PosConnMap.end() && iter->second->GetUserID() == UserID)
                            {
                                ReleaseConn(iter ,false);
                            }
                        }
                        
                        pCurGnsInfo->ConnPos = ConnPos;
                        pCurGnsInfo->ServerID = ServerID;
                        pCurGnsInfo->Status = GNS_USER_STATUS_ACTIVE;
                        pCurGnsInfo->LastActiveTime = _time;

                        XF_LOG_INFO(0, UserID, "m_GnsInfoMap modify success");
                    }
                }
            }
            
            break;
        }
        default:
        {
            // 正常情况下透传给客户端就行了，其它server需要负责组好包
            XYHeader CurHeader;
            CurHeader = Header.CoverToXYHeader(PkgLen);
            CurHeader.Write(m_pSendBuff);
            memcpy(m_pSendBuff+CurHeader.GetHeadLen(), pCurBuffPos+Header.GetHeaderLen(), PkgLen-Header.GetHeaderLen());

            unsigned int CurConnPos = Header.ConnPos;
            if(CurConnPos == 0)
            {
                break;
            }

            Send2Client(CurConnPos, m_pSendBuff, CurHeader.PkgLen);
            
            break;
        }
    }
    
    return 0;
}

int CConn::SendStateMessage()
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


void CConn::MemFullCallBackAll(uint64_t UserID, ShmGnsInfo &CurGnsInfo, void* p)
{
    if(CurGnsInfo.ServerID == m_pSelf->GetServerID() && CurGnsInfo.Status == GNS_USER_STATUS_ACTIVE)
    {
        vector<LoginData>* pVct = (vector<LoginData>*) p;
        LoginData data;
        data.UserID = CurGnsInfo.UserID;
        data.ServerID = CurGnsInfo.ServerID;
        data.ConnPos = CurGnsInfo.ConnPos;
        data.LastActiveTime = CurGnsInfo.LastActiveTime;
        
        pVct->push_back(data);
    }
}


void CConn::MemFullCallBack(uint64_t UserID, ShmGnsInfo &CurGnsInfo, void* p)
{
    time_t nowTime= time(NULL);
    if(CurGnsInfo.Status == GNS_USER_STATUS_UNACTIVE && (uint64_t)(nowTime - CONN_INVALID_TIME) > CurGnsInfo.LastActiveTime)
    {
        int Ret = m_pSelf->m_GnsInfoMap.Remove(UserID);
        if(Ret != 0)
        {
            XF_LOG_WARN(0, UserID, "m_GnsInfoMap %ld Remove failed, Ret=%d", UserID, Ret);
        }

        XF_LOG_INFO(0, UserID, "m_GnsInfoMap %ld Remove success", UserID);
    }
}

int CConn::CheckValid()
{
    m_GnsInfoMap.ProcessAll(m_CheckConnPos, MemFullCallBack, NULL); 
    m_CheckConnPos = (m_CheckConnPos+1)%m_MaxGnsNodeNum;

    return 0;
}
