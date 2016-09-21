
#ifndef _COMMON_H_
#define _COMMON_H_

// 公用常量
const int XY_MAXBUFF_LEN = 2048000;   //最大允许接收数据的长度为2M
const int XY_PKG_MAX_LEN = 2048000;   //单个协议包最大的长度为2M
const int XY_MAX_CONN_NUM = 100000;   //最大连接数


//user常量
const int MAX_NAME_LENGTH = 24;     // 用户名，昵称等长度
const int MAX_FRIEND_NUM = 200;     // 最大好友数量
const int MAX_REQUEST_NUM = 50;     // 最多用户请求
const int MAX_PERSONAL_NOTE_LENGTH = 256;   // 个人描述长度
const int MAX_TELNO_LENGTH = 11;
const int MAX_ADDR_LENGTH = 256;
const int MAX_EMAIL_LENGTH = 64;
const int MAX_USER_SERVER_NUM = 1;  // user服务器数量，决定某个userid去哪台服务器处理
const int USER_DATABASE_NUM = 2;
const int USER_TABLE_NUM = 2;

// auth常量
const int AUTH_DATABASE_NUM = 4;    // auth分库分表的库数量
const int AUTH_TABLE_NUM = 2;            // auth分库分表的表数量

// 服务器与组
const int GROUP_CONN = 100;
const int GROUP_AUTH = 200;
const int GROUP_GNS = 300;
const int GROUP_USER = 400;

const int SERVER_GNS = 301; //目前的设定是GNS只有一台
const int SERVER_USER_BEGIN = 400;

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

// 用户状态
enum
{
    USER_STATUS_INIT    = 0,
    USER_STATUS_LOADING = 1,
    USER_STATUS_LOADED  = 2,
};

typedef struct tagDBConfig
{
    char Host[32];
    int Port;
    char User[256];
    char Pass[256];
    char DBName[256];
    char TableName[256];

    tagDBConfig()
    {
        memset(this, 0x0, sizeof(tagDBConfig));
    }
}DBConfig;


#endif
