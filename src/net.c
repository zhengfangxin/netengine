#include "net.h"
#include "netengine.h"
#include "net_malloc.h"
#include "thread_api.h"
#include "pubfunc.h"
#include "net_def.h"
#include "client.h"
#include "accept.h"
#include "connect.h"

#ifdef _WINDOWS
#include "net_def_win.h"
#else
#include "net_def_linux.h"
#endif


void show_msg(struct netengine *pstnetengine, const char *format, ...) {

	char szMsg[256];
	const int iBufferLen = sizeof(szMsg);
		
	char * buffer = NULL;
	bool bmalloc = false;

#ifdef _WINDOWS
	int len;
	{
		va_list args;
		va_start(args, format);
		// add \0
		len = _vscprintf(format, args) + 1;
		va_end(args);
	}

	if (len > iBufferLen)
	{
		buffer = net_malloc(len);
		bmalloc = true;
	}
	else
	{
		buffer = szMsg;
	}
	{
		va_list args;
		va_start(args, format);
		vsprintf_s(buffer, len, format, args);
		va_end(args);
	}
#else
	va_list ap;

	buffer = szMsg;
	va_start(ap, format);
	int len = vsnprintf(buffer, iBufferLen, format, ap);
	va_end(ap);
	if (len >= iBufferLen) {
		bmalloc = true;
		int max_size = iBufferLen;
		for (;;) {
			max_size *= 2;
			buffer = net_malloc(max_size);
			va_start(ap, format);
			len = vsnprintf(buffer, max_size, format, ap);
			va_end(ap);
			if (len < max_size) {
				break;
			}
			net_free(buffer);
		}
	}
#endif
	if (NULL != pstnetengine->pfon_msg)
	{
		pstnetengine->pfon_msg(buffer);
	}

	if (bmalloc)
	{
		net_free(buffer);
	}
}

// 关闭连接
void ng_close(struct netengine *net, int id, bool peaceful) {
	struct request_package request;

	request.u.close.id = id;
	request.u.close.peaceful = peaceful;
	send_request(net, &request, req_close, sizeof(request.u.close));
}


// 创建socket，用来建立连接
int ng_connect(struct netengine *net, const char *phost, int port) {
	struct request_package request;
	if (strlen(phost) >= sizeof(request.u.open.host) - 1) {
		return -1;
	}

	int id = reserve_id(net);
	if (id < 0) {
		return id;
	}

	request.u.open.id = id;
	strcpy_s(request.u.open.host, sizeof(request.u.open.host), phost);
	request.u.open.port = port;
	send_request(net, &request, req_connect, sizeof(request.u.open));

	return id;
}

void pro_listen(struct netengine *net, struct request_package *cmd, int *id) {
	struct request_listen *req = &cmd->u.listen;
	*id = req->id;

	struct socket *s = new_fd(net, req->id, req->fd, req->protocol);

	s->type = SOCKET_TYPE_PLISTEN;

	StringCbCopy(s->szaddr, sizeof(s->szaddr), req->szaddr);
	s->port = req->port;

	s->close_time = DEF_CLOSE_TIME;
	s->recv_updatetime = true;
	s->send_updatetime = true;

	s->def_recv = DEF_BUF;
	s->max_recv = DEF_MAX_BUF;
	s->max_send = DEF_MAX_BUF;
}

int make_addr(const char *pszHost, int port, struct sockaddr * pAddr, int *iaddrBufLen, int protocol, bool bipv6only, int *family)
{
	int status;

	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	char portstr[16];

	if (pszHost == NULL || pszHost[0] == 0)
	{
		if (bipv6only)
		{
			// 这个可以获取到ipv6
			pszHost = "::";
		}
		else
		{
			// 这个只会获取到ipv4地址
			pszHost = "0.0.0.0";
		}
	}
	snprintf(portstr, sizeof(portstr), "%d", port);
	memset(&ai_hints, 0, sizeof(ai_hints));
	ai_hints.ai_family = AF_UNSPEC;
	if (bipv6only)
	{
		ai_hints.ai_family = AF_INET6;
	}
	if (protocol == IPPROTO_TCP) {
		ai_hints.ai_socktype = SOCK_STREAM;
	}
	else {
		assert(protocol == IPPROTO_UDP);
		ai_hints.ai_socktype = SOCK_DGRAM;
	}
	ai_hints.ai_protocol = protocol;

	status = getaddrinfo(pszHost, portstr, &ai_hints, &ai_list);
	if (status != 0) {
		return -1;
	}

	*family = ai_list->ai_family;

	int ilen = (int)ai_list->ai_addrlen;
	if (ilen > *iaddrBufLen)
	{
		return -2;
	}

	memcpy(pAddr, ai_list->ai_addr, ilen);
	*iaddrBufLen = ilen;

	return 0;
}

void close_fd(struct netengine *net, struct socket *s) {
	if (s->fd == -1) {
		return;
	}
	if (s->type == SOCKET_TYPE_LISTEN) {
		accept_close(net, s);
	}
	else if (s->type == SOCKET_TYPE_CONNECTING) {
		connect_close(net, s);
	}
	else if (s->type == SOCKET_TYPE_CONNECTED || s->type == SOCKET_TYPE_HALFCLOSE) {
		client_close(net, s);
	}

	socket_close(s->fd);
	s->fd = -1;	
}

void socket_free(struct netengine *net, struct socket *s) {
	if (s->type == SOCKET_TYPE_INVALID) {
		return;
	}
	accept_free(net, s);
	client_free(net, s);

	// 释放内存
	memset(s, 0, sizeof(*s));

	s->type = SOCKET_TYPE_INVALID;
	s->id = -1;
	s->protocol = -1;
}

int
reserve_id(struct netengine *net) {
	int i;
	for (i = 0; i<MAX_SOCKET; i++) {
		int id = __sync_add_and_fetch(&(net->alloc_id), 1);
		if (id < 0) {
			id = __sync_and_and_fetch(&(net->alloc_id), 0x7fffffff);
		}
		struct socket *s = GETSOCKET(net, id);
		if (s->type == SOCKET_TYPE_INVALID) {
			if (__sync_bool_compare_and_swap(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE)) {
				s->id = id;
				s->fd = -1;
				return id;
			}
			else {
				// retry
				--i;
			}
		}
	}
	return -1;
}

struct socket *
	new_fd(struct netengine *net, int id, socket_t fd, int protocol) {
	struct socket * s = GETSOCKET(net, id);
	assert(s->type == SOCKET_TYPE_RESERVE);

	s->id = id;
	s->fd = fd;
	s->protocol = protocol;

	return s;
}

void *ng_get_net_user(struct netengine *net) {
	return net->puser;
}

// 设置侦听回调函数
void ng_set_listen(struct netengine *net, int id, fonlisten *pfonlisten, fonaccept *pfonaccept, fonerror *pfonerror) {
	struct request_package request;
	memset(&request, 0, sizeof(request));

	request.u.setcallback.id = id;
	request.u.setcallback.pfonlisten = pfonlisten;
	request.u.setcallback.pfonaccept = pfonaccept;
	request.u.setcallback.pfonerror = pfonerror;
	send_request(net, &request, req_setcallback, sizeof(request.u.setcallback));
}

// 设置接收回调函数
void ng_set_recv(struct netengine *net, int id, fonrecv *pfonrecv, fonclose *pfonclose, fonerror *pfonerror) {
	struct request_package request;
	memset(&request, 0, sizeof(request));

	request.u.setcallback.id = id;
	request.u.setcallback.pfonrecv = pfonrecv;
	request.u.setcallback.pfonclose = pfonclose;
	request.u.setcallback.pfonerror = pfonerror;
	send_request(net, &request, req_setcallback, sizeof(request.u.setcallback));
}

// 设置建立连接回调函数
void ng_set_connect(struct netengine *net, int id, fonrecv *pfonrecv, fonconnect *pfonconnect,
	fonclose *pfonclose, fonerror *pfonerror) {
	struct request_package request;
	memset(&request, 0, sizeof(request));

	request.u.setcallback.id = id;
	request.u.setcallback.pfonrecv = pfonrecv;
	request.u.setcallback.pfonconnect = pfonconnect;
	request.u.setcallback.pfonclose = pfonclose;
	request.u.setcallback.pfonerror = pfonerror;
	send_request(net, &request, req_setcallback, sizeof(request.u.setcallback));
}

// 设置用户数
void ng_set_userdata(struct netengine *net, int id, void *puser) {
	struct request_package request;

	request.u.userdata.id = id;
	request.u.userdata.puser = puser;
	send_request(net, &request, req_setuser, sizeof(request.u.userdata));
}

// 发送数据，不在网络线程时调用此函数，发送失败会调用onerror
void ng_sendto(struct netengine *net, int id, char *pdata, int size, ffreedata *pfree) {
	struct request_package request;

	request.u.send.id = id;
	request.u.send.sz = size;
	request.u.send.buffer = pdata;
	request.u.send.pfree = pfree;
	send_request(net, &request, req_send, sizeof(request.u.send));
}

void ng_set_net_user(struct netengine *net, void *puser) {
	net->puser = puser;
}

// 开始侦听，连接，或收发数据
void ng_start(struct netengine *net, int id) {
	struct request_package request;

	request.u.start.id = id;
	send_request(net, &request, req_start, sizeof(request.u.start));
}

// 设置日志函数
void ng_setmsg(struct netengine *net, fonmsg *pfonmsg) {
	net->pfon_msg = pfonmsg;
}

// 设置发送和接收是否更新有效时间，超过多少秒关闭连接
void ng_set_valid_time(struct netengine *net, int id, bool send, bool recv, int close_time) {
	struct request_package request;

	request.u.time.id = id;
	request.u.time.recv = recv;
	request.u.time.send = send;
	request.u.time.close_time = close_time;
	send_request(net, &request, req_set_valid_time, sizeof(request.u.time));
}

// 设置缓冲区参数
void ng_set_buf(struct netengine *net, int id, int def_recv, int max_recv, int max_send) {
	struct request_package request;

	request.u.buf.id = id;
	request.u.buf.def_recv = def_recv;
	request.u.buf.max_recv = max_recv;
	request.u.buf.max_send = max_send;
	send_request(net, &request, req_set_buf, sizeof(request.u.buf));
}

void ng_run_func(struct netengine *net, pfrunfunc *pfrun, void *user) {

	struct request_package request;

	request.u.run.pfrun = pfrun;
	request.u.run.user = user;
	send_request(net, &request, req_run_func, sizeof(request.u.run));
}

void ng_break(struct netengine *net) {
	struct request_package request;

	send_request(net, &request, req_exit, 0);
}

DLL_EXPORT int64 ng_get_valid_time(struct netengine *net, int id) {
	struct socket *s = GETSOCKET(net, id);
	return client_get_valid_time(s);
}

bool ng_get_remote_addr(struct netengine *net, int id, char *host, int hostbuflen, int *port) {
	struct socket *s = GETSOCKET(net, id);
	if (s->type == SOCKET_TYPE_INVALID) {
		return false;
	}
	StringCbCopy(host, hostbuflen, s->szaddr);
	if (NULL != port) {
		*port = s->port;
	}
	return true;
}

void pro_set_callback(struct netengine *net, struct request_package *cmd) {
	struct request_setcallback *req = &cmd->u.setcallback;
	struct socket *s = GETSOCKET(net, req->id);
	assert(s->type == SOCKET_TYPE_PLISTEN || s->type == SOCKET_TYPE_PCONNECT || s->type == SOCKET_TYPE_PACCEPT);

	s->pfon_listen = req->pfonlisten;
	s->pfon_accept = req->pfonaccept;
	s->pfon_close = req->pfonclose;
	s->pfon_error = req->pfonerror;
	s->pfon_recv = req->pfonrecv;
	s->pfon_connect = req->pfonconnect;
}

void pro_set_user(struct netengine *net, struct request_package *cmd) {
	struct request_setuser *req = &cmd->u.userdata;
	struct socket *s = GETSOCKET(net, req->id);

	s->puser = req->puser;
}

int pro_close(struct netengine *net, struct request_package *cmd, int *id) {
	struct request_close *req = &cmd->u.close;
	struct socket *s = GETSOCKET(net, req->id);
	*id = req->id;

	return close_socket(net, s, req->peaceful);
}

int close_socket(struct netengine *net, struct socket *s, bool peaceful) {
	if (s->type == SOCKET_TYPE_INVALID) {
		return 0;
	}
	if (s->type == SOCKET_TYPE_RESERVE) {
		s->type = SOCKET_TYPE_INVALID;
		return 0;
	}
	if (s->type == SOCKET_TYPE_PCONNECT || s->type == SOCKET_TYPE_PLISTEN || s->type == SOCKET_TYPE_PACCEPT) {
		close_fd(net, s);
		socket_free(net, s);
		return 0;
	}
	if (!peaceful) {
		close_fd(net, s);
		return -1;
	}
	if (s->type == SOCKET_TYPE_HALFCLOSE) {
		return 0;
	}
	if (s->type == SOCKET_TYPE_LISTEN || s->type == SOCKET_TYPE_CONNECTING) {
		close_fd(net, s);
		return -1;
	}
	// 当前也只有SOCKET_TYPE_CONNECTED
	if (client_is_data_empty(s)) {
		close_fd(net, s);
		return -1;
	}
	// 否则，等发完数据再close
	s->type = SOCKET_TYPE_HALFCLOSE;
	return 0;
}

void net_close_all(struct netengine *net)
{
	int i;
	for (i = 0; i < MAX_SOCKET; ++i) {
		close_socket(net, net->slot + i, false);
	}
}

void net_free_all(struct netengine *net) {
	int i;
	for (i = 0; i < MAX_SOCKET; ++i) {
		socket_free(net, net->slot + i);
	}
}

void pro_connect(struct netengine *net, struct request_package *cmd) {
	struct request_open *req = &cmd->u.open;
	struct socket *s = GETSOCKET(net, req->id);

	StringCbCopy(s->szaddr, sizeof(s->szaddr), req->host);
	s->port = req->port;
	s->fd = -1;
	s->type = SOCKET_TYPE_PCONNECT;

	s->close_time = DEF_CLOSE_TIME;
	s->recv_updatetime = true;
	s->send_updatetime = true;

	s->def_recv = DEF_BUF;
	s->max_recv = DEF_MAX_BUF;
	s->max_send = DEF_MAX_BUF;
}

int pro_start(struct netengine *net, struct request_package *cmd, int *id) {
	struct request_start *req = &cmd->u.start;
	struct socket *s = GETSOCKET(net, req->id);
	assert(s->id == req->id);
	*id = req->id;

	assert(s->type == SOCKET_TYPE_PLISTEN || s->type == SOCKET_TYPE_PCONNECT || s->type == SOCKET_TYPE_PACCEPT);

	if (s->type == SOCKET_TYPE_PACCEPT) {
		s->type = SOCKET_TYPE_CONNECTED;
		client_start_recv_send(net, s, true);
		return 0;
	}
	else if (s->type == SOCKET_TYPE_PLISTEN) {
		s->type = SOCKET_TYPE_LISTEN;
		return accept_start(net, s);
	}
	else if (s->type == SOCKET_TYPE_PCONNECT) {
		s->type = SOCKET_TYPE_CONNECTING;
		return connect_start(net, s);
	}
	else {
		assert(false);
		show_msg(net, "start invalid socket %d", req->id);
	}
	return 0;
}

int pro_send(struct netengine *net, struct request_package *cmd, int *id) {
	struct request_send *req = &cmd->u.send;
	*id = req->id;

	return ng_send(net, req->id, req->buffer, req->sz, true, req->pfree);
}

// 发送数据，当在网络线程时调用此函数，发送失败会调用onerror
int ng_send(struct netengine *net, int id,  void *pdata, int idatalen, bool needfree, ffreedata *pfree) {
	struct socket *s = GETSOCKET(net, id);
	
	return client_send(net, s, pdata, idatalen, needfree, pfree);
}

void pro_set_valid_time(struct netengine *net, struct request_package *cmd) {
	struct request_set_valid_time *req = &cmd->u.time;
	struct socket *s = GETSOCKET(net, req->id);

	s->send_updatetime = req->send;
	s->recv_updatetime = req->recv;
	s->close_time = req->close_time;

	net->check_time = min(net->check_time, s->close_time / 3);
	net->check_time = max(net->check_time, 1);
}

void pro_set_buf(struct netengine *net, struct request_package *cmd) {
	struct request_set_buf *req = &cmd->u.buf;
	struct socket *s = GETSOCKET(net, req->id);

	s->def_recv = req->def_recv;
	s->max_recv = req->max_recv;
	s->max_send = req->max_send;
}

void pro_run_func(struct netengine *net, struct request_package *cmd) {
	struct request_run_func *req = &cmd->u.run;

	req->pfrun(net, req->user);
}
int pro_cmd(struct netengine *net, int type, struct request_package *cmd, int *id) {
	switch (type) {
	case req_exit:
		break;
	case req_listen:
		pro_listen(net, cmd, id);
		break;
	case req_setcallback:
		pro_set_callback(net, cmd);
		break;
	case req_start:
		return pro_start(net, cmd, id);
		break;
	case req_close:
		return pro_close(net, cmd, id);
		break;
	case req_setuser:
		pro_set_user(net, cmd);
		break;
	case req_send:
		return pro_send(net, cmd, id);
		break;
	case req_connect:
		pro_connect(net, cmd);
		break;
	case req_set_valid_time:
		pro_set_valid_time(net, cmd);
		break;
	case req_set_buf:
		pro_set_buf(net, cmd);
		break;
	case req_run_func:
		pro_run_func(net, cmd);
		break;
	case req_close_all:
		net_close_all(net);
		break;
	default:
		break;
	}
	return 0;
}

// update time sec
void net_update_time(struct netengine *net) {
	net->cache_time = gethostruntime_millisec();
}

// get time sec
int64 ng_get_time(struct netengine *net) {
	return net->cache_time;
}

void ng_close_all(struct netengine *net) {
	struct request_package request;
	memset(&request, 0, sizeof(request));

	send_request(net, &request, req_close_all, sizeof(request.u.close_all));
}