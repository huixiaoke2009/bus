
#ifndef _USER_PROC_H_
#define _USER_PROC_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include <google/protobuf/message.h>
#include "user_shm_api.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "header.h"

class CUser
{
    public:
        CUser();
        ~CUser();

        int Init(const char *pConfFile);
        int Run();
        int GetServerID(){return m_ServerID;}
        
    private:
        int DealPkg(const char *pCurBuffPos, int PkgLen);
        int Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);
        int SendStateMessage();
        int LoadUserInfo(uint64_t UserID, const std::string& strRequest);
        int WriteUserInfo(uint64_t UserID);
        
    private:
        unsigned int m_ServerID;
        int m_StateTime;
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        mmlib::CShmQueue m_LoaderQueue;
        mmlib::CShmQueue m_WriterQueue;

        char* m_pSendBuff;

        CUserShmApi m_UserShm;
};


#endif
