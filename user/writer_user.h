
#ifndef _WRITER_USER_H_
#define _WRITER_USER_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include "shm_queue/shm_queue.h"
#include "mysql/mysql_wrap.h"
#include "user_shm_api.h"
#include "process_manager/process_manager.h"

typedef struct tagUserDBConfig
{
    char Host[32];
    int Port;
    char DBName[256];
    char User[256];
    char Pass[256];
    char TableName[256];

    tagUserDBConfig()
    {
        memset(this, 0x0, sizeof(tagUserDBConfig));
    }
}UserDBConfig;


class CWriterUser: public mmlib::CProcessManager
{

public:
    CWriterUser();
    ~CWriterUser();
    int Init(const char* pConfFile);
    int Entity(int argc, char *argv[]);
    int Struct2ProtoStringUser(const ShmUserInfo &CurUserInfo,std::string &strProto, uint64_t UserID);
    int CheckUserInfoValid(uint64_t UserID, const ShmUserInfo &CurUserInfo);
    int ProcessWriterUserInfo(uint64_t UserID);

private:
    UserDBConfig m_DBConfig[USER_DATABASE_NUM];
    mmlib::CMySQL m_DBConn[USER_DATABASE_NUM];
    
    mmlib::CShmQueue m_WriterQueue;
    CUserShmApi m_UserShm;
};


#endif
