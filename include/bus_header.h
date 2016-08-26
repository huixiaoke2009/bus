
#ifndef __BUS_HEADER__
#define __BUS_HEADER__

enum
{
    TO_SRV = 0,
    TO_GRP = 1,
};

#pragma pack(1)
typedef struct tagBusHeader
{
    unsigned int PkgLen; // 4
    unsigned int CmdID;  // 4
    unsigned int SrcID;  // 4
    unsigned int DstID;  // 4
    unsigned long long PkgTime; // 8
    char SendType;       // 1
    char Flag;           // 1

    tagBusHeader()
    {
        PkgLen = 0;
        CmdID = 0;
        SrcID = 0;
        DstID = 0;
        PkgTime = 0;
        SendType = 0;
        Flag = 0;
    }
    
    int GetHeaderLen()
    {
        return 26;
    }
    
    int Write(char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, SrcID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, DstID);
        Offset += mmlib::CBuffTool::WriteLongLong(Buff + Offset, PkgTime);
        Offset += mmlib::CBuffTool::WriteByte(Buff + Offset, SendType);
        Offset += mmlib::CBuffTool::WriteByte(Buff + Offset, Flag);
        
        return Offset;
    }

    int Read(const char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, SrcID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, DstID);
        Offset += mmlib::CBuffTool::ReadLongLong(Buff + Offset, PkgTime);
        Offset += mmlib::CBuffTool::ReadByte(Buff + Offset, SendType);
        Offset += mmlib::CBuffTool::ReadByte(Buff + Offset, Flag);
        return Offset;
    }
    
}BusHeader;
#pragma pack()

#endif