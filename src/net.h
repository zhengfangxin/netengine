#ifndef net_h
#define net_h

#include "os.h"
#include "net_def.h"
#include "netengine.h"


// 获取pszHost,port 表示的sockaddr，获取此通过后可以用来使用sendto();
// 返回0表示成功，-1其它表示地址解析失败,-2表示缓冲区不够，pAddr缓冲区长度建议为sizeof(sockaddr_in6)
int make_addr(const char *pszHost, int port, struct sockaddr * pAddr, int *iaddrBufLen, int protocol, bool bipv6only, int *family);

void close_fd(struct netengine *net, struct socket *s);

void socket_free(struct netengine *net, struct socket *s);

struct socket * new_fd(struct netengine *net, int id, socket_t fd, int protocol);

struct socket *	new_fd(struct netengine *net, int id, socket_t fd, int protocol);

int reserve_id(struct netengine *net);

void show_msg(struct netengine *pstnetengine, const char *pszFormat, ...);

void send_request(struct netengine *net, struct request_package *preq, int type, int len);

void pro_set_callback(struct netengine *net, struct request_package *cmd);

int pro_close(struct netengine *net, struct request_package *cmd, int *id);

void pro_connect(struct netengine *net, struct request_package *cmd);

int pro_send(struct netengine *net, struct request_package *cmd, int *id);

int pro_start(struct netengine *net, struct request_package *cmd, int *id);

int pro_cmd(struct netengine *net, int type, struct request_package *cmd, int *id);

void net_close_all(struct netengine *net);
void net_free_all(struct netengine *net);

int close_socket(struct netengine *net, struct socket *s, bool peaceful);

// update time millisec
void net_update_time(struct netengine *net);


#endif