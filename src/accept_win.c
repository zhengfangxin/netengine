#include "accept.h"
#include "net_malloc.h"
#include "net.h"
#include "net_def_win.h"
#include "pubfunc.h"

static LPFN_ACCEPTEX lpfnAcceptEx = NULL;

static void _on_accept(struct netengine *net, struct socket *s) {
	// 获取ip地址
	if (setsockopt(s->newfd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&s->fd, sizeof(s->fd)) == SOCKET_ERROR) {
		show_msg(net, "handle_accept setsockopt error %d", GetLastError());
		socket_close(s->newfd);
		return ;
	}
	socket_t newfd = s->newfd;
	if (CreateIoCompletionPort((HANDLE)newfd, net->iocp, 0, 0) == NULL)
	{
		uint iErr = GetLastError();
		show_msg(net, "accept bind iocp failed, error:%u", iErr);
		socket_close(s->newfd);
		return ;
	}

	int newid = reserve_id(net);
	if (newid < 0) {
		show_msg(net, "accept get id failed, may too much connection\n");
		return;
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

	union sockaddr_all addr_peer;
	int sa_len = sizeof(addr_peer);
	getpeername(newfd, (struct sockaddr*)&addr_peer, &sa_len);

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

	s->newfd = -1;
}

static int _do_accept(struct netengine *net, struct socket *s) {
	if (s->newfd != -1) {
		socket_close(s->newfd);
		s->newfd = -1;
	}
	socket_t fd = s->fd;
	socket_t newfd = WSASocket(s->protocol, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == newfd)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "iocp TryAccept WSASocket failed, error:%d", iErr);
		return iErr;
	}

	if (!lpfnAcceptEx)
	{
		GUID guidAcceptEx = WSAID_ACCEPTEX;
		DWORD dwBytes = 0;
		if (WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidAcceptEx, sizeof(guidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx),
			&dwBytes, NULL, NULL))
		{
			int iErr = WSAGetLastError();
			show_msg(net, "iocp TryAccept WSAIoctl failed, error:%d", iErr);

			return iErr;
		}
	}

	DWORD dwBytesReceived = 0;

	const int addrlen = sizeof(union sockaddr_all)+16;	// msdn至少大于协议的16
	if (NULL == s->pacceptbuf) {
		s->pacceptbuf = net_malloc(2 * addrlen);
	}
	s->iocp_accept.id = s->id;
	s->iocp_accept.type = iocp_listen;
	s->newfd = newfd;

retry:
	if (!lpfnAcceptEx(fd, newfd, s->pacceptbuf, 0,
		addrlen, addrlen, (LPDWORD)&dwBytesReceived, (LPOVERLAPPED)&s->iocp_accept))
	{
		int iError = WSAGetLastError();
		if (WSAECONNRESET == iError)
		{
			goto retry;
		}
		if (ERROR_IO_PENDING != iError)
		{
			show_msg(net, "iocp TryAccept AcceptEx %d", iError);
			return iError;
		}
	}
	else
	{
		// iocp will callback
	}
	s->type = SOCKET_TYPE_LISTEN;
	return 0;
}

int accept_start(struct netengine *net, struct socket *s) {
	bool error = false;
	socket_t fd = s->fd;
	s->newfd = -1;
	if (CreateIoCompletionPort((HANDLE)fd, net->iocp, 0, 0) == NULL)
	{
		uint iErr = GetLastError();
		show_msg(net, "iocp StartListen CreateIoCompletionPort failed, error:%u", iErr);

		error = true;
	}
	if (error || _do_accept(net, s) != 0) {
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
		_on_accept(net, s);
	}
	if (_do_accept(net, s) != 0) {
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, -1, s->puser);
		}
		close_fd(net, s);
		socket_free(net, s);
	}
}

void accept_close(struct netengine *net, struct socket *s) {
	if (s->newfd != -1) {
		socket_close(s->newfd);
		s->newfd = -1;
	}
}

void accept_free(struct netengine *net, struct socket *s) {
	if (s->pacceptbuf != NULL) {
		net_free(s->pacceptbuf);
		s->pacceptbuf = NULL;
	}
}


// 创建侦听socket
int ng_listen(struct netengine *net, const char *pszaddr, int iPort, int backlog) {
	int family;
	int status;

	union sockaddr_all addr;
	int addrlen = sizeof(addr);
	status = make_addr(pszaddr, iPort, (struct sockaddr *)&addr, &addrlen, IPPROTO_TCP, false, &family);
	if (status < 0)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "GetAddrinfo failed, error:%d", iErr);

		return -1;
	}

	socket_t fd = WSASocket(family, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (fd == INVALID_SOCKET)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "iocp WSASocket failed, error:%d", iErr);

		return -1;
	}

	if (bind(fd, (struct sockaddr*)&addr, addrlen) == SOCKET_ERROR)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "iocp StartListen bind failed, error:%d", iErr);

		socket_close(fd);
		return -1;
	}

	if (listen(fd, backlog) == SOCKET_ERROR)
	{
		int iErr = WSAGetLastError();
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