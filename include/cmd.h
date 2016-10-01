
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

// 服务器状态变更通知
const unsigned int Cmd_StateChange = 0x00000004;


/******************* CONN模块 *******************/
// 断开连接
const unsigned int Cmd_Disconnect = 0x00010003;
// 登录通知
const unsigned int Cmd_LoginNotice = 0x00010004;

/******************* AUTH模块 *******************/
// 注册,app使用
const unsigned int Cmd_Auth_Register_Req = 0x00020001;
const unsigned int Cmd_Auth_Register_Rsp = 0x00020002;

// 登录协议,app使用
const unsigned int Cmd_Auth_Login_Req = 0x00020003;
const unsigned int Cmd_Auth_Login_Rsp = 0x00020004;



/******************* USER模块 *******************/
const unsigned int Cmd_User_Register_Req = 0x00040001;
const unsigned int Cmd_User_Register_Rsp = 0x00040002;

// 请求添加好友信息 app使用
const unsigned int Cmd_User_AddFriend_Req = 0x00040003;
const unsigned int Cmd_User_AddFriend_Rsp = 0x00040004;

// 请求添加好友信息 内部使用
const unsigned int Cmd_User_AddFriend_Req2 = 0x00040005;
const unsigned int Cmd_User_AddFriend_Rsp2 = 0x00040006;

#endif