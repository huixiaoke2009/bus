#include "filelock.h"
#include "log/log.h"

CShmUserLock::CShmUserLock(mmlib::CFileLock &FileLock, int LockType, int OffSet, int LockSize, int NoLockFlag /*= 0*/, int LockedOffSet /*= 0*/)
{
    m_NoLockFlag = NoLockFlag;
    m_LockedOffSet = LockedOffSet;
    m_OffSet = OffSet;
    m_LockSize = LockSize;
    m_pFileLock = &FileLock;

    XF_LOG_TRACE(0, 1, "%s %p Lock type=%d, offset=%d, size=%d, nolock=%d, locked_offset=%d",
            __func__,
            this,
            LockType,
            OffSet,
            LockSize,
            NoLockFlag,
            LockedOffSet);

    if ((m_NoLockFlag == 1)&&(m_OffSet == m_LockedOffSet))
    {
        return;
    }
    else
    {
        m_pFileLock->Lock(LockType, m_OffSet, m_LockSize);
    }
    return;
}

CShmUserLock::~CShmUserLock()
{
    XF_LOG_TRACE(0, 2, "%s %p UnLock offset=%d, size=%d, nolock=%d, locked_offset=%d",
            __func__,
            this,
            m_OffSet,
            m_LockSize,
            m_NoLockFlag,
            m_LockedOffSet);

    if ((m_NoLockFlag == 1)&&(m_OffSet == m_LockedOffSet))
    {
        return;
    }
    else
    {
        m_pFileLock->UnLock(m_OffSet, m_LockSize);
    }
    return;
}
