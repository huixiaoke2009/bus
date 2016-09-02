
#ifndef _GNS_PROC_H_
#define _GNS_PROC_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include <google/protobuf/message.h>
#include "hash_list/hash_list_nolock.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "header.h"

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

    tagShmGnsInfo()
    {
        memset(this, 0x0, sizeof(tagShmGnsInfo));
    }
}ShmGnsInfo;

class CGns
{
    public:
        CGns();
        ~CGns();

        int Init(const char *pConfFile);
        int Run();
        int GetServerID(){return m_ServerID;}
        
    private:
        int DealPkg(const char *pCurBuffPos, int PkgLen);
        int Send2Server(XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);
        int SendStateMessage();
        
    private:
        unsigned int m_ServerID;
        int m_StateTime;
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        char* m_pSendBuff;

        int m_MaxGnsNodeNum;
        mmlib::CShareMem m_GnsMem;
        GnsMemHead* m_pGnsHead;
        mmlib::CHashListNoLock<uint64_t, ShmGnsInfo> m_GnsInfoMap;
};


#endif
