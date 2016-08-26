
#ifndef _AUTH_PROC_H_
#define _AUTH_PROC_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include <google/protobuf/message.h>
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "header.h"

class CAuth
{
    public:
        CAuth();
        ~CAuth();

        int Init(const char *pConfFile);
        int Run();

    private:
        int DealPkg(const char *pCurBuffPos, int PkgLen);
        int Send2Server(const XYHeaderIn& Header, unsigned int CmdID, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);

    private:
        unsigned int m_ServerID;
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        char* m_pSendBuff;
};


#endif
