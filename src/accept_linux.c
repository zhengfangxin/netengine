#include "accept.h"
#include "net_malloc.h"
#include "net.h"
#include "net_def_linux.h"
#include "socket_poll.h"
#include "pubfunc.h"


static int _on_accept(struct netengine *net, struct socket *s) {
	union sockaddr_all addr_peer;
	socklen_t sa_len = sizeof(addr_peer);
	socket_t newfd = accept(s->fd, &addr_peer.s, &sa_len);
	if (newfd < 0) {
		int err = errno;
		if (err == EMFILE || err == ENFILE) {
			show_msg(net, "accept error %d %s", err, strerror(err));
			return -1;
		} else {
			return 0;
		}
	}

	int newid = reserve_id(net);
	if (newid < 0) {
		show_msg(net, "accept get id failed, may too much connection\n");
		return 0;
	}
	struct socket *news = new_fd(net, newid, newfd, s->protocol);
	news->type = SOCKET_TYPE_PACCEPT;

	// default value
	news->close_time = s->close_time;
	news->recv_updatetime = s->recv_updatetime;
	news->send_updatetime = s->send_updatetime;

	news->def_recv = s->def_recv;
	news->max_recv = s->max_recv;
	news->max_send = s->max_send;

	void * sin_addr = (addr_peer.s.sa_family == AF_INET) ? (void*)&addr_peer.v4.sin_addr : (void *)&addr_peer.v6.sin6_addr;
	inet_ntop(addr_peer.s.sa_family, sin_addr, news->szaddr, sizeof(news->szaddr));
	news->port = ntohs((addr_peer.s.sa_family == AF_INET) ? addr_peer.v4.sin_port : addr_peer.v6.sin6_port);

	bool bSuc = false;
	if (NULL != s->pfon_accept) {
		bSuc = s->pfon_accept(net, s->id, newid, newfd, news->szaddr, news->port, s->puser);
	}
	if (!bSuc) 	{
		socket_close(newfd);
	}
	else {
		
	}

	return 0;
}

int accept_start(struct netengine *net, struct socket *s) {
	bool error = false;
	if (sp_add(net->event_fd, s->fd, s)) {
		error = true;
	}
	if (error) {
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, -1, s->puser);
		}
		close_fd(net, s);
		socket_free(net, s);		
	}
	else {
		if (s->pfon_listen != NULL) {
			s->pfon_listen(net, s->id, s->szaddr, s->port, s->puser);
		}
	}
	return error ? -1 : 0;
}

void accept_on_accecpted(struct netengine *net, struct socket *s, bool berror) {
	if (!berror) {
		// linux don't reach error
	}
	if (_on_accept(net, s) != 0) {
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, -1, s->puser);
		}
		close_fd(net, s);
		socket_free(net, s);
	}
}

void accept_close(struct netengine *net, struct socket *s) {	
	sp_del(net->event_fd, s->fd);
}

void accept_free(struct netengine *net, struct socket *s) {
	
}

// 创建侦听socket
int ng_listen(struct netengine *net, const char *pszaddr, int iPort, int backlog) {
	int family;
	int status;
	int reuse = 1;
	
	union sockaddr_all addr;
	int addrlen = sizeof(addr);
	status = make_addr(pszaddr, iPort, (struct sockaddr *)&addr, &addrlen, IPPROTO_TCP, false, &family);
	if (status < 0)
	{
		int iErr = errno;
		show_msg(net, "GetAddrinfo failed, error:%d", iErr);

		return -1;
	}

	socket_t fd = socket(family, SOCK_STREAM, 0);
	if (fd < 0)
	{
		int iErr = errno;
		show_msg(net, "socket failed, error:%d", iErr);

		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int))==-1) {
		int iErr = errno;
		show_msg(net, "socket reuse failed, error:%d", iErr);
		return -1;
	}

	char listen_addr[64];
	void * sin_addr = (addr.s.sa_family == AF_INET) ? (void*)&addr.v4.sin_addr : (void *)&addr.v6.sin6_addr;
	inet_ntop(addr.s.sa_family, sin_addr, listen_addr, sizeof(listen_addr));
	int listen_port = ntohs((addr.s.sa_family == AF_INET) ? addr.v4.sin_port : addr.v6.sin6_port);

	show_msg(net, "bind addr%s:%d", listen_addr, listen_port);

	if (bind(fd, (struct sockaddr*)&addr, addrlen) != 0)
	{
		int iErr = errno;
		show_msg(net, "StartListen bind failed, error:%d,%s, addr %s:%d", 
			iErr, strerror(iErr), listen_addr, listen_port);

		socket_close(fd);
		return -1;
	}

	if (listen(fd, backlog) != 0)
	{
		int iErr = errno;
		show_msg(net, "StartListen listen failed, error:%d", iErr);

		socket_close(fd);
		return -1;
	}

	int id = reserve_id(net);
	if (id < 0) {
		socket_close(fd);
		return id;
	}

	struct request_package request;
	memset(&request, 0, sizeof(request));

	if (NULL != pszaddr) {
		StringCbCopy(request.u.listen.szaddr, sizeof(request.u.listen.szaddr), pszaddr);
	}
	request.u.listen.port = iPort;
	request.u.listen.protocol = family;
	request.u.listen.id = id;
	request.u.listen.fd = fd;
	send_request(net, &request, req_listen, sizeof(request.u.listen));
	return id;
}
