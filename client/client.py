#!/usr/bin/python
import socket
import struct 
import time
import app_pb2

HOST='192.168.206.128'
PORT=10000
XY_PKG_MAX_LEN = 2048000

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM);
s.connect((HOST, PORT));
while(1):
    CurReq = app_pb2.LoginReq();
    CurReq.userid = 100001
    CurReq.passwd = "12345"
    content = CurReq.SerializeToString();
    headerlen = 25 + len(content)
    header = struct.pack(">IIQIHHb", headerlen, 0x00010001, 0, 0, 0, 0, 0);
    data = header + content
    s.sendall(data);
    recv_data = s.recv(XY_PKG_MAX_LEN);
    recv_header = recv_data[0:25]
    recv_content = recv_data[25:];
    PkgLen, CmdID, UserID, SN, CkSum, Ret, Compresse = struct.unpack(">IIQIHHb", recv_header);
    #print PkgLen, CmdID, UserID, SN, CkSum, Ret, Compresse
    CurRsp = app_pb2.LoginRsp();
    CurRsp.ParseFromString(recv_content)
    if CurRsp.ret != 1:
        print 'error'
    time.sleep(1)
s.close();