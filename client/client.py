#!/usr/bin/python
import socket
import struct 
import time
import app_pb2

HOST='192.168.206.128'
PORT=10000
XY_PKG_MAX_LEN = 2048000
XY_HEADER_LEN = 17

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM);
s.connect((HOST, PORT));
while(1):
    CurReq = app_pb2.LoginReq();
    CurReq.userid = 100001
    CurReq.passwd = "12345"
    content = CurReq.SerializeToString();
    headerlen = XY_HEADER_LEN + len(content)
    header = struct.pack(">IIIHHb", headerlen, 0x00010001, 0, 0, 0, 0);
    data = header + content
    s.sendall(data);
    recv_data = s.recv(XY_PKG_MAX_LEN);
    recv_header = recv_data[0:XY_HEADER_LEN]
    recv_content = recv_data[XY_HEADER_LEN:];
    PkgLen, CmdID, SN, CkSum, Ret, Compresse = struct.unpack(">IIIHHb", recv_header);
    print PkgLen, CmdID, SN, CkSum, Ret, Compresse
    CurRsp = app_pb2.LoginRsp();
    CurRsp.ParseFromString(recv_content)
    print CurRsp.ret;
    if CurRsp.ret != 1:
        print 'login error'
    time.sleep(3)
s.close();