
#ifndef _AUTH_PROC_H_
#define _AUTH_PROC_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include <google/protobuf/message.h>
#include "mysql/mysql_wrap.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "header.h"



typedef struct tagMysqlConfig
{
    char Host[32];
    int Port;
    char User[256];
    char Pass[256];
    char DBName[256];
    char TableName[256];
}MysqlConfig;

class CAuth
{
    public:
        CAuth();
        ~CAuth();

        int Init(const char *pConfFile);
        int Run();
        int GetServerID(){return m_ServerID;}
        
    private:
        int DealPkg(const char *pCurBuffPos, int PkgLen);
        int Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);
        int LoginCheck(uint64_t UserID, const std::string& strPasswd, int Plat);
        int Register(const std::string& strPasswd, uint64_t& UserID);
        int SendStateMessage();
        
    private:
        unsigned int m_ServerID;
        int m_StateTime;

        // ???????????????
        MysqlConfig m_DBConfig[DATABASE_NUM];
        mmlib::CMySQL m_DBConn[DATABASE_NUM];
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        char* m_pSendBuff;
};


#endif
