
#ifndef _WRITER_USER_H_
#define _WRITER_USER_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include "shm_queue/shm_queue.h"
#include "mysql/mysql_wrap.h"
#include "user_shm_api.h"
#include "process_manager/process_manager.h"

class CLoaderUser: public mmlib::CProcessManager
{

public:
    CLoaderUser();
    ~CLoaderUser();
    int Init(const char* pConfFile);
    int Entity(int argc, char *argv[]);
    int ProcessLoaderUserInfo(uint64_t UserID, const std::string& strRequest);
    int UserProtoString2Struct(ShmUserInfo &CurUserInfo, const std::string &strProto);
private:
    DBConfig m_DBConfig[USER_DATABASE_NUM];
    mmlib::CMySQL m_DBConn[USER_DATABASE_NUM];
    
    mmlib::CShmQueue m_LoaderQueue;
    CUserShmApi m_UserShm;
};


#endif
