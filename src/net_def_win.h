#ifndef net_def_win_h
#define net_def_win_h

#include "os.h"
#include "socket_def.h"
#include "netengine.h"
#include "net_def.h"
#include "net_buf.h"

enum iocp_type {
	iocp_none,
	iocp_listen,
	iocp_recv,
	iocp_send,
	iocp_connect
};
struct iocpnode {
	OVERLAPPED overlapped;
	int			id;
	enum iocp_type	type;
};

struct send_node {
	struct iocpnode iocp_node;
	int allsize;
	ffreedata *pfree;
	WSABUF wsaBuf;
};
struct socket {
	int				id;
	socket_t		fd;
	long volatile	type;
	void			*puser;
	// 远程地址
	int				protocol;		// 网络类型，ipv4,ipv6
	char			szaddr[64];		// accept和connect时，远程的地址
	int				port;

	fonlisten		*pfon_listen;
	fonaccept		*pfon_accept;
	fonerror		*pfon_error;
	fonclose		*pfon_close;
	fonconnect		*pfon_connect;
	fonrecv			*pfon_recv;

	int				def_recv;
	int				max_recv;
	int				max_send;

	bool			send_updatetime;
	bool			recv_updatetime;
	int				close_time;			// 多少秒无效时间则关闭连接

	socket_t		newfd;

	int64			valid_time; // milli sec
	bool			is_error;

	struct wb_list	send_list;
	int				wb_len;	// no only in send_list, but wsasend sending
	int				sending_count;

	char			*pacceptbuf;

	struct iocpnode		iocp_accept;
	struct iocpnode		iocp_connect;
	struct iocpnode		iocp_recv;

	WSABUF				recv_buf;
	bool				recving;
	struct netbuf		*recv_stream;	
};

struct netengine {
	HANDLE			iocp;
	fonmsg			*pfon_msg;
	long volatile	alloc_id;
	void			*puser;

	struct socket slot[MAX_SOCKET];

	int64			cache_time;
	int				check_time;			// check close
};

#define GETSOCKET(net,id) (&net->slot[HASH_ID((id))])

#endif