
#ifndef _COMMON_H_
#define _COMMON_H_

// 公用常量
const int XY_MAXBUFF_LEN = 2048000;   //最大允许接收数据的长度为2M
const int XY_PKG_MAX_LEN = 2048000;   //单个协议包最大的长度为2M
const int XY_MAX_CONN_NUM = 100000;   //最大连接数


//user常量
const int MAX_NAME_LENGTH = 24;
const int MAX_FRIEND_NUM = 200;


// 服务器与组
const int GROUP_CONN = 100;
const int GROUP_AUTH = 200;
const int GROUP_GNS = 300;
const int GROUP_USER = 400;

const int SERVER_GNS = 301; //目前的设定是GNS只有一台


// 命令字前缀
const unsigned int CMD_PREFIX_SYS = 0x0000;
const unsigned int CMD_PREFIX_CONN = 0x0001;
const unsigned int CMD_PREFIX_AUTH = 0x0002;
const unsigned int CMD_PREFIX_GNS = 0x0003;
const unsigned int CMD_PREFIX_USER = 0x0004;



/********************* 下面是一些enum结构 **************************/

//连接类型：未认证，认证，webpcl
enum
{
    CONN_UNAUTH = 0,
    CONN_AUTH = 1,
};

// GNS用户状态: 活跃，非活跃
enum
{
    GNS_USER_STATUS_ACTIVE = 0,
    GNS_USER_STATUS_UNACTIVE = 1,
};

//服务器状态：在线，休眠，离开
enum
{
    BUS_SVR_ONLINE = 0,
    BUS_SVR_SLEEP = 1,
    BUS_SVR_OFFLINE = 2,
};




#endif
