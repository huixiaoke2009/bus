
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



typedef struct tagAuthDBConfig
{
    char Host[32];
    int Port;
    char User[256];
    char Pass[256];
    char DBName[256];
    char TableName[256];

    tagAuthDBConfig()
    {
        memset(this, 0x0, sizeof(tagAuthDBConfig));
    }
}AuthDBConfig;

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

        AuthDBConfig m_DBConfig[AUTH_DATABASE_NUM];
        mmlib::CMySQL m_DBConn[AUTH_DATABASE_NUM];
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        char* m_pSendBuff;
};


#endif
