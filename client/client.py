#!/usr/bin/python
# -*- coding: utf-8 -*-
import socket
import struct 
import time
import app_pb2


XY_PKG_MAX_LEN = 2048000;
XY_HEADER_LEN = 17;
PACKAGE_HEADER = ">IIIHHb";
SERVER_HOST = '192.168.206.128';
SERVER_PORT = 10000;

class CClient:
    HOST=SERVER_HOST;
    PORT=SERVER_PORT;
    UserID = 1472978293;
    Passwd = "12345";
    
    s = None;
    
    def __init__(self, host, port):
        self.HOST = host;
        self.PORT = port;
        
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM);
        self.s.connect((self.HOST, self.PORT));
        
    def __del__(self):
        self.s.close();
        
    def SendRecvLoginReq(self):
        CurReq = app_pb2.LoginReq();
        CurReq.userid = self.UserID;
        CurReq.passwd = self.Passwd;
        CurReq.plat = 1;
        content = CurReq.SerializeToString();
        headerlen = XY_HEADER_LEN + len(content)
        header = struct.pack(PACKAGE_HEADER, headerlen, 0x00020003, 0, 0, 0, 0);
        data = header + content
        self.s.sendall(data);
        recv_data = self.s.recv(XY_PKG_MAX_LEN);
        recv_header = recv_data[0:XY_HEADER_LEN]
        recv_content = recv_data[XY_HEADER_LEN:];
        PkgLen, CmdID, SN, CkSum, Ret, Compresse = struct.unpack(PACKAGE_HEADER, recv_header);
        print PkgLen, CmdID, SN, CkSum, Ret, Compresse
        CurRsp = app_pb2.LoginRsp();
        CurRsp.ParseFromString(recv_content)
        print CurRsp.ret;
        if CurRsp.ret != 0:
            print 'login error'
            return -1;
        else:
            print '%d login success!!!'%self.UserID
            
        return 0;
            
    def SendRecvRegisterReq(self):
        passwd = '12345';
        CurReq = app_pb2.RegisterReq();
        CurReq.passwd = passwd;
        CurReq.nickname = 'mama';
        CurReq.sex = 0;
        CurReq.birthday = 1472978293;
        CurReq.telno = '13724872174';
        CurReq.address = u'广东省揭阳市惠来县仙庵镇京陇乡京东古祖东巷18号广东省揭阳市惠来县仙庵镇京陇乡京东古祖东巷18号';
        CurReq.email = 'huixiaoke2009huixiaoke2009@qq.com'
        content = CurReq.SerializeToString();
        headerlen = XY_HEADER_LEN + len(content)
        header = struct.pack(PACKAGE_HEADER, headerlen, 0x00020001, 0, 0, 0, 0);
        data = header + content
        self.s.sendall(data);
        recv_data = self.s.recv(XY_PKG_MAX_LEN);
        recv_header = recv_data[0:XY_HEADER_LEN]
        recv_content = recv_data[XY_HEADER_LEN:];
        PkgLen, CmdID, SN, CkSum, Ret, Compresse = struct.unpack(PACKAGE_HEADER, recv_header);
        print PkgLen, CmdID, SN, CkSum, Ret, Compresse
        CurRsp = app_pb2.RegisterRsp();
        CurRsp.ParseFromString(recv_content)
        print CurRsp.ret;
        if CurRsp.ret != 0:
            print 'register error'
            return -1;
        else:
            print '%d register success!!!'%CurRsp.userid
        
        self.UserID = CurRsp.userid;
        self.Passwd = passwd;
        
        return 0;
    
    def SendRecvAddFriendReq(self):
        CurReq = app_pb2.AddFriendReq();
        CurReq.userid = 1472978286;
        
        content = CurReq.SerializeToString();
        headerlen = XY_HEADER_LEN + len(content)
        header = struct.pack(PACKAGE_HEADER, headerlen, 0x00040003, 0, 0, 0, 0);
        data = header + content
        self.s.sendall(data);
        recv_data = self.s.recv(XY_PKG_MAX_LEN);
        recv_header = recv_data[0:XY_HEADER_LEN]
        recv_content = recv_data[XY_HEADER_LEN:];
        PkgLen, CmdID, SN, CkSum, Ret, Compresse = struct.unpack(PACKAGE_HEADER, recv_header);
        print PkgLen, CmdID, SN, CkSum, Ret, Compresse
        CurRsp = app_pb2.AddFriendRsp();
        CurRsp.ParseFromString(recv_content)
        print CurRsp.ret;
        if CurRsp.ret != 0:
            print 'add friend error'
            return -1;
        
        return 0;
            
    def Run(self):
        Ret = 0;
        print '-------------------------'
        Ret = self.SendRecvRegisterReq();
        if Ret != 0:
            return -1;
        
        Ret = self.SendRecvLoginReq();
        if Ret != 0:
            return -1;
        #self.SendRecvAddFriendReq();
        
        return 0;
    
def main():
    c1 = CClient(SERVER_HOST, SERVER_PORT);
    c1.Run();
    time.sleep(2);
    c2 = CClient(SERVER_HOST, SERVER_PORT);
    c2.Run();
    time.sleep(2);
    
    c3 = CClient(SERVER_HOST, SERVER_PORT);
    c3.Run();
    time.sleep(2);
    c4 = CClient(SERVER_HOST, SERVER_PORT);
    c4.Run();
    time.sleep(2);
    
    time.sleep(100000);

if __name__ == "__main__":
    main()