#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <map>
#include <google/protobuf/message.h>
#include "hash_list/hash_list_nolock.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "../include/header.h"

#ifndef _CONN_H_
#define _CONN_H_


typedef struct tagConnMeta
{
    unsigned int ConnPos;   //用来存储连接序号
    int RetVal;
    long long RecvTime;
    long long SendTime;

    unsigned int LocalIP;
    unsigned short LocalPort;
    unsigned int RemoteIP;
    unsigned short RemotePort;
}ConnMeta;

const char GNS_MEM_MAGIC[] = "THIS IS GNS MEM AAAAAAAAAAA";
const short GNS_MEM_HEAD_SIZE = 256;
typedef struct tagGnsMemHead
{
    char Magic[32];
    unsigned int CurUserSN;
}GnsMemHead;


typedef struct tagShmGnsInfo
{
    uint64_t UserID;
    int ServerID;
    unsigned int ConnPos;
    int Status;
    uint64_t LastActiveTime;
    
    tagShmGnsInfo()
    {
        memset(this, 0x0, sizeof(tagShmGnsInfo));
    }
}ShmGnsInfo;

class CConnInfo
{
    public:
        CConnInfo(unsigned int ConnPos, int SockID, const struct sockaddr_in *pRemoteAddr, const struct sockaddr_in *pLocalAddr, const int flag);
        ~CConnInfo();

        // 接收数据
        int Recv(char *pBuff, unsigned int *pLen);

        // 发送数据
        int Send(const char *pBuff, unsigned int Len);

        // 获得上次活跃时间
        time_t GetLastActTime() { return m_LastActTime; }

        // 客户端地址
        unsigned int RemoteAddr() { return m_RemoteAddr.sin_addr.s_addr; }
        const char *RemoteAddrStr() {return inet_ntoa(*(struct in_addr *)(&m_RemoteAddr.sin_addr.s_addr));}
        unsigned short RemotePort() { return m_RemoteAddr.sin_port; }

        unsigned int LocalAddr() { return m_LocalAddr.sin_addr.s_addr; }
        const char *LocalAddrStr() {return inet_ntoa(*(struct in_addr *)(&m_LocalAddr.sin_addr.s_addr));}
        unsigned short LocalPort() { return m_LocalAddr.sin_port; }

        // 获取FD
        int GetSockID() {return m_SockID;}

        // 缓存接收到的数据
        int AddRecvData(const char *pBuff, unsigned int Len);

        //缓存需要发送的数据
        int AddSendData(const char *pBuff, unsigned int Len);

        //发送缓存的数据
        int SendRemainData();

        //设置用户ID
        int SetUserID(unsigned long long UserID)
        {
            m_UserID = UserID;
            return 0;
        }

        //获取用户ID
        unsigned long long GetUserID()
        {
            return m_UserID;
        }

        int GetConnType()
        {
            return m_ConnType;
        }
        
        void SetConnType(int type)
        {
            m_ConnType = type;
        }

        unsigned int GetConnPos()
        {
            return m_ConnPos;
        }

    protected:
        // 客户端唯一ID
        unsigned int m_ConnPos;

        // 套接字
        int m_SockID;

        // 认证类型
        int m_ConnType;  //0未认证，1已认证，2webpcl

        // 用户ID
        unsigned long long m_UserID;
        
        // 地址
        struct sockaddr_in m_RemoteAddr;
        struct sockaddr_in m_LocalAddr;

        // 上次活动时间
        time_t m_LastActTime;

        // 暂存数据的buff
        std::string m_RemainSendData;
        std::string m_RemainRecvData;
};

class CConn;
class CConn
{
    public:
        CConn();
        ~CConn();

        int Init(const char *pConfFile);
        int Run();
        int GetServerID(){return m_ServerID;}
        const char* GetErrMsg(){return m_szErrMsg;}
        static CConn* m_pSelf;
    private:
        int AcceptConn(int ConnPos, int type);
        void CheckConn();
        void ReleaseConn(std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap, bool bSend = true);
        int Send2Client(int CurConnPos,const char *pSendBuff, int SendBuffLen);
        int Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);
        int Send2Server(unsigned int DstID, char SendType, char Flag, const char* pCurBuff, int PkgLen);
        int SendErrMsg2Server(unsigned int DstID, unsigned int CmdID, int ErrCode);
        int ProcessPkg(const char *pCurBuffPos, int RecvLen, std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap);
        int DealPkg(const char *pCurBuffPos, int RecvLen);
        int SendStateMessage();
        static void MemFullCallBackAll(uint64_t UserID, ShmGnsInfo &CurGnsInfo, void* p);
        static void MemFullCallBack(uint64_t UserID, ShmGnsInfo &CurGnsInfo, void* p);
        int CheckValid();
        
    private:
        // EPOLL句柄
        int m_EpollFD;
        int m_ServerID;
        int m_StateTime;
        // 生成cpos的计数
        unsigned int m_ConnPosCnt;

        std::map<unsigned int, CConnInfo*> m_PosConnMap;
        std::map<unsigned int, CConnInfo*>::iterator m_itrCurCheckConn;
        int m_CheckConnPos;

        //连接超时时间（指定时间内没有数据包，将视为超时），单位：秒
        int m_TimeOut;

        mmlib::CShmQueue m_RecvQueue;
        mmlib::CShmQueue m_SendQueue;

        char m_ConfFile[256];
        char m_szErrMsg[256];

        char *m_pProcessBuff;
        char *m_pSendBuff;

        int m_MaxGnsNodeNum;
        mmlib::CShareMem m_GnsMem;
        GnsMemHead* m_pGnsHead;
        mmlib::CHashListNoLock<uint64_t, ShmGnsInfo> m_GnsInfoMap;
};

#endif
