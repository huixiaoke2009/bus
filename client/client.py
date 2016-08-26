#!/usr/bin/python
import socket
import struct 
import time
import app_pb2

HOST='192.168.206.128'
PORT=10000

content = app_pb2.LoginReq();
content.userid = 100001
content.passwd = "12345"
content = content.SerializeToString();
headerlen = 25 + len(content)
data = struct.pack(">IIQIHHb", headerlen, 0x00010001, 0, 0, 0, 0, 0);
data = data + content
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM);
s.connect((HOST, PORT));
s.sendall(data)
s.close();
time.sleep(1000);

