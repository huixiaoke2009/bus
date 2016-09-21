#ifndef _FILELOCK_H_
#define _FILELOCK_H_

#include "file_lock/file_lock.h"

//user锁
// 锁住整张表,一般只需要在初始化的时候才用到
#define LOCK_ALL(LockType) CShmUserLock userAllLock(m_UserInfoLock, LockType, 0, 0)
// 锁住第一个字段,对应的就是锁住共享内存头部,一般只有在插入和删除时候才用到
#define LOCK_HASHLIST_HEAD(LockType) CShmUserLock userHashListHeadLock(m_UserInfoLock, LockType, 0, 1)
// 锁住一个bit,每个用户对应一个bit
#define LOCK_USER(LockType, UserID) CShmUserLock userLock(m_UserInfoLock, LockType, 2+(UserID%m_MaxUserNodeNum), 1)

class CShmUserLock
{
private:
    int m_NoLockFlag;
    int m_LockedOffSet;
    int m_OffSet;
    int m_LockSize;
    mmlib::CFileLock *m_pFileLock;

public:
    CShmUserLock(mmlib::CFileLock &FileLock, int LockType, int OffSet, int LockSize, int NoLockFlag = 0, int LockedOffSet = 0);
    ~CShmUserLock();
};

#endif
