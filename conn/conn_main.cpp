#include <stdio.h>
#include "conn.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("usage: %s <conf_file>\n", argv[0]);
        return -1;
    }

    daemon(1, 1);

    int Ret = 0;
    CConn Conn;

    Ret = Conn.Init(argv[1]);
    if (Ret != 0)
    {
        printf("ERR:proc init failed, ret=%d|err=%s\n", Ret, Conn.GetErrMsg());
        return -1;
    }

    Ret = Conn.Run();

    return Ret;
}
