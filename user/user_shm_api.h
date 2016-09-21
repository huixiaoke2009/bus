
#ifndef _USER_SHM_API_H_
#define _USER_SHM_API_H_

#include <stdint.h>
#include <string>
#include <string.h>
#include "hash_list/hash_list_nolock.h"
#include "shm_queue/shm_queue.h"
#include "common.h"
#include "header.h"
#include "filelock.h"


const char USER_MEM_MAGIC[] = "THIS IS USER MEM AAAAAAAAAAA";
const short USER_MEM_HEAD_SIZE = 256;

typedef struct tagUserMemHead
{
    char Magic[32];
    unsigned int CurUserSN;
}UserMemHead;

#pragma pack(1)
typedef struct tagFriendInfo
{
    uint64_t UserID;
    char RemarkName[MAX_NAME_LENGTH];
    char SeeStatus; // 可见情况

    tagFriendInfo()
    {
        memset(this, 0x0, sizeof(tagFriendInfo));
    }
}FriendInfo;

typedef struct tagRequestInfo
{
    uint64_t UserID;
    char NickName[MAX_NAME_LENGTH];
    uint64_t RequestTime;

    tagRequestInfo()
    {
        memset(this, 0x0, sizeof(tagRequestInfo));
    }
}RequestInfo;

typedef struct tagShmUserInfo
{
    uint64_t UserID;
    int Status;
    char NickName[MAX_NAME_LENGTH];
    int Level;
    int VipLevel;
    char Sex;
    uint64_t Birthday;
    char TelNo[MAX_TELNO_LENGTH];
    char Address[MAX_ADDR_LENGTH];
    char EMail[MAX_EMAIL_LENGTH];
    char PersonalNote[MAX_PERSONAL_NOTE_LENGTH];
    
    FriendInfo FriendList[MAX_FRIEND_NUM];
    RequestInfo RequestList[MAX_REQUEST_NUM];
    
    tagShmUserInfo()
    {
        memset(this, 0x0, sizeof(tagShmUserInfo));
    }
    
}ShmUserInfo;
#pragma pack()


class CUserShmApi
{
    public:
        CUserShmApi();
        ~CUserShmApi();

        int Init(const char *pConfFile);
        int InsertUserInfo(const ShmUserInfo& Info);
        int RemoveUserInfo(uint64_t UserID);
        int GetUserInfo(uint64_t UserID, ShmUserInfo& Info);
        int UpdateUserInfo(const ShmUserInfo &newUserInfo);
   private:

        mmlib::CFileLock m_UserInfoLock;
        
        int m_MaxUserNodeNum;
        mmlib::CShareMem m_UserMem;
        UserMemHead* m_pUserHead;
        mmlib::CHashListNoLock<uint64_t, ShmUserInfo> m_UserInfoMap;
};


#endif

