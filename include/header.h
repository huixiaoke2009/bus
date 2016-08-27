#include <stdint.h>
#include <string.h>
#include "util/util.h"

#ifndef _HEADER_H_
#define _HEADER_H_

#pragma pack(1)
//[PkgSize:4Byte][CmdID:4Byte][SN:4Byte][CkSum:2Byte][Ret:2Byte][Compresse:1Byte][PkgBody:PkgSize-25]
typedef struct tagXYHeader
{
    unsigned int PkgLen;
    unsigned int CmdID;
    unsigned int SN;
    unsigned short CkSum;
    unsigned short Ret;
    char Compresse;

    int Write(char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, SN);
        Offset += mmlib::CBuffTool::WriteShort(Buff + Offset, CkSum);
        Offset += mmlib::CBuffTool::WriteShort(Buff + Offset, Ret);
        Offset += mmlib::CBuffTool::WriteByte(Buff + Offset, Compresse);
        return Offset;
    }

    int Read(const char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, SN);
        Offset += mmlib::CBuffTool::ReadShort(Buff + Offset, CkSum);
        Offset += mmlib::CBuffTool::ReadShort(Buff + Offset, Ret);
        Offset += mmlib::CBuffTool::ReadByte(Buff + Offset, Compresse);
        return Offset;
    }

    int GetHeadLen()
    {
        return 17;
    }

    tagXYHeader()
    {
        PkgLen = 0;
        CmdID = 0;
        SN = 0;
        CkSum = 0;
        Ret = 0;
        Compresse = 0;
    }

}XYHeader;
#pragma pack()

typedef struct tagXYHeaderIn
{
    unsigned int SrcID;
    unsigned int CmdID;
    unsigned int SN;
    unsigned int ConnPos;
    unsigned long long UserID;
    unsigned long long PkgTime;
    unsigned short Ret;

    XYHeader CoverToXYHeader(int PkgLen)
    {
        XYHeader TmpHeader;
        TmpHeader.PkgLen = PkgLen - sizeof(tagXYHeaderIn) + TmpHeader.GetHeadLen();
        TmpHeader.CmdID = CmdID;
        TmpHeader.SN = SN;
        TmpHeader.Ret = Ret;

        return TmpHeader;
    }

    void Copy(const tagXYHeaderIn& o)
    {
        SrcID = o.SrcID;
        CmdID = o.CmdID;
        SN = o.SN;
        ConnPos = o.ConnPos;
        UserID = o.UserID;
        PkgTime = o.PkgTime;
        Ret = o.Ret;
    }

    tagXYHeaderIn()
    {
        memset(this, 0x0, sizeof(tagXYHeaderIn));
    }

    tagXYHeaderIn(const tagXYHeaderIn& o)
    {
        SrcID = o.SrcID;
        CmdID = o.CmdID;
        SN = o.SN;
        ConnPos = o.ConnPos;
        UserID = o.UserID;
        PkgTime = o.PkgTime;
        Ret = o.Ret;
    }
    
}XYHeaderIn;


#endif

