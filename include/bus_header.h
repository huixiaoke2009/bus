
#ifndef __BUS_HEADER__
#define __BUS_HEADER__

typedef struct tagBusHeader
{
    unsigned int PkgLen; // 4
    unsigned int CmdID;  // 4
    unsigned int SrcID;  // 4
    unsigned int DstID;  // 4
    unsigned int SN;     // 4
    short Ret;           // 2

    tagBusHeader()
    {
        memset(this, 0x0, sizeof(tagBusHeader));
    }
    
    int GetHeaderLen()
    {
        return 22;
    }
    
    int Write(char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, SrcID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, DstID);
        Offset += mmlib::CBuffTool::WriteInt(Buff + Offset, SN);
        Offset += mmlib::CBuffTool::WriteShort(Buff + Offset, Ret);
        
        return Offset;
    }

    int Read(const char *Buff)
    {
        int Offset = 0;
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, PkgLen);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, CmdID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, SrcID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, DstID);
        Offset += mmlib::CBuffTool::ReadInt(Buff + Offset, SN);
        Offset += mmlib::CBuffTool::ReadShort(Buff + Offset, Ret);
        
        return Offset;
    }
    
}BusHeader;


typedef struct tagAppHeader
{
    unsigned int CmdID;
    unsigned int SrcID;
    unsigned int DstID;
    
    tagAppHeader()
    {
        memset(this, 0x0, sizeof(tagAppHeader));
    }
}AppHeader;

#endif