
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

const char USER_MEM_MAGIC[] = "THIS IS USER MEM AAAAAAAAAAA";
const short USER_MEM_HEAD_SIZE = 256;
typedef struct tagUserMemHead
{
    char Magic[32];
    unsigned int CurUserSN;
}UserMemHead;

enum
{
    GNS_USER_STATUS_ACTIVE = 0,
    GNS_USER_STATUS_UNACTIVE = 1,
};


typedef struct tagShmUserInfo
{
    uint64_t UserID;
    int ServerID;
    int ConnPos;
    int Status;

    tagShmUserInfo()
    {
        memset(this, 0x0, sizeof(tagShmUserInfo));
    }
}ShmUserInfo;

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
        int Send2Server(const XYHeaderIn& Header, unsigned int DstID, char SendType, char Flag, const google::protobuf::Message& Message);

    private:
        unsigned int m_ServerID;
        
        mmlib::CShmQueue m_SendQueue;
        mmlib::CShmQueue m_RecvQueue;

        char* m_pSendBuff;

        int m_MaxUserNodeNum;
        mmlib::CShareMem m_UserMem;
        UserMemHead* m_pUserHead;
        mmlib::CHashListNoLock<uint64_t, ShmUserInfo> m_UserInfoMap;
};


#endif
