#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <map>
#include "shm_queue/shm_queue.h"
#include "common.h"

#ifndef _CONN_H_
#define _CONN_H_

enum CONNTYPE
{
    //连接类型：未认证，认证，webpcl
    CONN_UNAUTH = 0,
    CONN_AUTH,
    CONN_WEBPCL,
};

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

class CConn
{
    public:
        CConn();
        ~CConn();

        int Init(const char *pConfFile);
        int Run();
        int GetServerID(){return m_ServerID;}
        const char *GetErrMsg() { return m_szErrMsg;};

    private:
        void ReleaseConn(std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap);
        int LogoutUser(uint64_t UserID);
        int ConnRun();
        int AcceptConn(int ConnPos, int type);
        void CheckConn();
        int ProcessPkg(const char *pCurBuffPos, int RecvLen, std::map<unsigned int, CConnInfo*>::iterator &pConnInfoMap);
        int Send2Client(int CurConnPos,const char *pSendBuff, int SendBuffLen);
        int GetUserConnPos(uint64_t UserID, unsigned int &ConnPos);

    private:
        // 监听SOCK句柄
        std::vector<ConnMeta> m_ListenConnMetaList;
        // EPOLL句柄
        int m_EpollFD;
        int m_ServerID;
        // 生成cpos的计数
        unsigned int m_ConnPosCnt;

        std::map<unsigned int, CConnInfo*> m_PosConnMap;
        std::map<unsigned long long, CConnInfo*> m_UserIDConnMap;
        std::map<unsigned int, CConnInfo*>::iterator m_itrCurCheckConn;


        //连接超时时间（指定时间内没有数据包，将视为超时），单位：秒
        int m_TimeOut;

        mmlib::CShmQueue m_RecvQueue;

        char m_ConfFile[256];
        char m_szErrMsg[256];

        char *m_pProcessBuff;
};

#endif