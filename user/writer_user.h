
#ifndef _WRITER_USER_H_
#define _WRITER_USER_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include "shm_queue/shm_queue.h"
#include "mysql/mysql_wrap.h"
#include "user_shm_api.h"
#include "process_manager/process_manager.h"

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
    DBConfig m_DBConfig[USER_DATABASE_NUM];
    mmlib::CMySQL m_DBConn[USER_DATABASE_NUM];
    
    mmlib::CShmQueue m_WriterQueue;
    CUserShmApi m_UserShm;
};


#endif
