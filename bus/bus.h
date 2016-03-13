/**
    *note: m_ProxyReqQueue过来的数据必须封好了包头
           m_ProxyRspQueue发出去的数据已经去掉包头
*/


#ifndef _BUS_H_
#define _BUS_H_

#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <map>
#include <string.h>
#include <vector>
#include <google/protobuf/message.h>
#include "bus.pb.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "log/log.h"
#include "util/util.h"


class CConnInfo
{
    public:
        CConnInfo(unsigned int ConnPos, int SockID, const struct sockaddr_in *pRemoteAddr, const struct sockaddr_in *pLocalAddr);
        ~CConnInfo();

        // 接收数据
        int Recv(char *pBuff, unsigned int *pLen);

        // 发送数据
        int Send(const char *pBuff, unsigned int Len);

        // 缓存接收到的数据
        int AddRecvData(const char *pBuff, unsigned int Len);

        //缓存需要发送的数据
        int AddSendData(const char *pBuff, unsigned int Len);

        //发送缓存的数据
        int SendRemainData();

        // 获得上次活跃时间
        time_t GetLastActTime() {return m_LastActTime;}

        // 客户端地址
        unsigned int RemoteAddr() { return m_RemoteAddr.sin_addr.s_addr; }
        const char *RemoteAddrStr() {return inet_ntoa(m_RemoteAddr.sin_addr);}
        unsigned short RemotePort() {return ntohs(m_RemoteAddr.sin_port);}

        //本机地址
        unsigned int LocalAddr() { return m_LocalAddr.sin_addr.s_addr; }
        const char *LocalAddrStr() {return inet_ntoa(m_LocalAddr.sin_addr);}
        unsigned short LocalPort() {return ntohs(m_LocalAddr.sin_port);}

        // 获取SockID
        int GetSockID() {return m_SockID;}

        //设置服务器ID
        void SetClusterID(unsigned int ClusterID){m_ClusterID = ClusterID;}

        //获取服务器ID
        unsigned int GetClusterID(){return m_ClusterID;}

        //获取连接编号
        unsigned int GetConnPos(){return m_ConnPos;}

    protected:
        // 客户端唯一ID
        unsigned int m_ConnPos;

        // 套接字
        int m_SockID;

        // 地址
        struct sockaddr_in m_RemoteAddr;
        struct sockaddr_in m_LocalAddr;

        // 上次活动时间
        time_t m_LastActTime;

        // 暂存数据的buff
        std::string m_RemainSendData;
        std::string m_RemainRecvData;

        unsigned int m_ClusterID;
};


typedef struct tagClusterInfo
{
    unsigned int ClusterIP;
    short ClusterPort;
    unsigned int ClusterID;
    int SocketID;
    int ConnPos;
    CConnInfo* pConnInfo;
    
    tagClusterInfo()
    {
        ClusterIP = 0;
        ClusterPort = 0;
        ClusterID = 0;
        SocketID = -1;
        ConnPos = 0;
        pConnInfo = NULL;
    }
}ClusterInfo;


class CBus
{
    public:
        CBus();
        ~CBus();

        int Init(const char *pConfFile);
        int Run();
        unsigned int GetClusterID(){return m_ClusterID;}

    private:
        int Listen(const char* pAddr, int Port);
        int AcceptConn(int ConnPos);
        void CheckConn();
        void ReleaseConn(std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap);
        
    private:
        // 与其他cluster相连使用
        int ConnectCluster(unsigned int ClusterID);
        int DisconnetCluster(unsigned int ClusterID);
        void CheckCluster();
    
        int ProcessPkg(const char *pCurBuffPos, int RecvLen, std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap);
    
    private:
        int m_EpollFD;
        unsigned int m_ClusterID;
        
        // 生成cpos的计数
        unsigned int m_ConnPosCnt;
        
        //接收缓冲区
        char *m_pProcessBuff;
        int m_ClusterNum;
        
        std::map<unsigned int, CConnInfo*> m_PosConnMap;
        std::map<unsigned int, CConnInfo*>::iterator m_itrCurCheckConn;
        
        ClusterInfo m_ClusterInfo[XY_MAX_CLUSTER_NUM];
        
        // SvrID与Cluster的关联
        std::map<unsigned int, int> m_mapSvrID;
        
        mmlib::CShmQueue m_ClusterQueue;
};


#endif