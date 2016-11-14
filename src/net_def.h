#ifndef net_def_h
#define net_def_h

#include "os.h"
#include "socket_def.h"
#include "netengine.h"


#define MAX_SOCKET (50000)

#define SOCKET_TYPE_INVALID 0
#define SOCKET_TYPE_RESERVE 1
#define SOCKET_TYPE_PLISTEN 2
#define SOCKET_TYPE_LISTEN 3
#define SOCKET_TYPE_CONNECTING 4
#define SOCKET_TYPE_CONNECTED 5
#define SOCKET_TYPE_HALFCLOSE 6
#define SOCKET_TYPE_PACCEPT 7
#define SOCKET_TYPE_PCONNECT 8

#define HASH_ID(id) (((unsigned int)id) % MAX_SOCKET)

union sockaddr_all {
	struct sockaddr s;
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
};

struct request_open {
	int id;
	int port;
	char host[128];
};

struct request_send {
	int id;
	int sz;
	char * buffer;
	ffreedata *pfree;
};


struct request_close {
	int id;
	int peaceful;
};

struct request_listen {
	int id;
	socket_t fd;
	int protocol;	// ipv4,ipv6
	char szaddr[64];
	int port;
};

struct request_start {
	int id;
};

struct request_setopt {
	int id;
	int what;
	int value;
};

struct request_setcallback {
	int id;
	fonerror *pfonerror;
	fonclose *pfonclose;
	fonlisten *pfonlisten;
	fonaccept	*pfonaccept;
	fonconnect *pfonconnect;
	fonrecv *pfonrecv;
};

struct request_setuser {
	int		id;
	void	*puser;
};

struct request_set_valid_time {
	int		id;
	bool	recv;
	bool	send;
	int		close_time;
};

struct request_set_buf {
	int		id;
	int def_recv;
	int max_recv;
	int max_send;
};

struct request_run_func {
	pfrunfunc *pfrun;
	void *user;
};

struct request_close_all {
	int res;
};

enum req_type {
	req_none,
	req_exit,
	req_listen,
	req_setcallback,
	req_start,
	req_close,
	req_setuser,
	req_send,
	req_connect,
	req_set_valid_time,
	req_set_buf,
	req_run_func,
	req_close_all,
};

struct request_package {
	uchar header[8];	// 6 bytes dummy, 6len,7type
	union {
		char buffer[256];
		struct request_open open;
		struct request_send send;
		struct request_close close;
		struct request_listen listen;
		struct request_start start;
		struct request_setopt setopt;
		struct request_setcallback setcallback;
		struct request_setuser	userdata;
		struct request_set_valid_time time;
		struct request_set_buf buf;
		struct request_run_func run;
		struct request_close_all close_all;
	} u;
};

struct socket;

#define	DEF_CLOSE_TIME 120
#define DEF_BUF (5*1024)
#define DEF_MAX_BUF (10 * 1024 * 1024)

struct write_buffer {
	struct write_buffer * next;
	void *buffer;
	int size;
	int cursz;
	ffreedata *pfree;
};

struct wb_list {
	struct write_buffer * head;
	struct write_buffer * tail;
};

#endif