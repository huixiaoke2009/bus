
#ifndef _CMD_H_
#define _CMD_H_

/*
*  0x0000:为bus系统模块
*  0x0001:为连接层使用，处理与登录连接相关的事务
*
*/

/******************* 系统模块 *******************/
// udp hello 数据包
const unsigned int Cmd_HelloMessage = 0x00000001;

// 心跳包
const unsigned int Cmd_Heartbeat = 0x00000002;

// 转发数据包
const unsigned int Cmd_Transfer = 0x00000003;


/******************* 连接模块 *******************/
// 登录协议
const unsigned int Cmd_Login_Req = 0x00010001;
const unsigned int Cmd_Login_Rsp = 0x00010002;

// 断开连接
const unsigned int Cmd_Disconnect_Req = 0x00010003;
const unsigned int Cmd_Disconnect_Rsp = 0x00010004;


/******************* GNS模块 *******************/
const unsigned int Cmd_GNS_Register_Req = 0x00020001;
const unsigned int Cmd_GNS_Register_Rsp = 0x00020002;


#endif