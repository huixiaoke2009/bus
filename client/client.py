#!/usr/bin/python
import socket
import struct 
import time
import app_pb2


XY_PKG_MAX_LEN = 2048000;
XY_HEADER_LEN = 17;
PACKAGE_HEADER = ">IIIHHb";

class CClient:
    HOST='192.168.206.128'
    PORT=10000
    UserID = 100001
    Passwd = "12345"
    
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM);
    
    def __init__(self, host, port):
        self.HOST = host;
        self.PORT = port;
        
        self.s.connect((self.HOST, self.PORT));
        
    def __del__(self):
        self.s.close();
        
    def SendRecvLoginReq(self):
        CurReq = app_pb2.LoginReq();
        CurReq.userid = self.UserID;
        CurReq.passwd = self.Passwd;
        content = CurReq.SerializeToString();
        headerlen = XY_HEADER_LEN + len(content)
        header = struct.pack(PACKAGE_HEADER, headerlen, 0x00010001, 0, 0, 0, 0);
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
        if CurRsp.ret != 1:
            print 'login error'
            
    def SendRecvRegisterReq(self):
        passwd = '12345';
        CurReq = app_pb2.RegisterReq();
        CurReq.passwd = passwd;
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
        if CurRsp.ret != 1:
            print 'register error'
        
        self.UserID = CurRsp.userid;
        self.Passwd = passwd;
        
    def Run(self):
        self.SendRecvRegisterReq();
        self.SendRecvLoginReq();
    
    
def main():
    c = CClient('192.168.206.128', 10000);
    c.Run();


if __name__ == "__main__":
    main()