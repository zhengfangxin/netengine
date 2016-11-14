#include "connect.h"
#include "net_def_win.h"
#include "client.h"

static LPFN_CONNECTEX lpfnConnectEx = NULL;

static int _do_connect(struct netengine *net, struct socket *s) {
	int family;
	int status;

	const char *phost = s->szaddr;
	int port = s->port;

	union sockaddr_all addr;
	int addrlen = sizeof(addr);
	status = make_addr(phost, port, (struct sockaddr *)&addr, &addrlen, IPPROTO_TCP, false, &family);
	if (status < 0)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "ConnectServer GetAddrinfo failed, error:%d", iErr);

		return iErr;
	}	

	socket_t fd = WSASocket(family, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (fd == INVALID_SOCKET)
	{
		int iErr = WSAGetLastError();
		show_msg(net, "ConnectServer WSASocket failed, error:%d", iErr);

		return iErr;
	}

	// 绑定到完成端口上
	if (CreateIoCompletionPort((HANDLE)fd, net->iocp, 0, 0) == NULL)
	{
		uint iErr = GetLastError();
		show_msg(net, "ConnectServer CreateIoCompletionPort failed, error:%u", iErr);

		return iErr;
	}

	s->protocol = family;
	s->fd = fd;

	{
		union sockaddr_all sa_local;
		int iLen = sizeof(sa_local);
		memset(&sa_local, 0, sizeof(sa_local));
		sa_local.s.sa_family = family;
		if (family == AF_INET)
		{
			iLen = sizeof(sa_local.v4);
		}

		// 不解， 为什么一定要先绑定一个本地地址才行
		if (SOCKET_ERROR == bind((SOCKET)fd, (struct sockaddr*)(&sa_local), iLen))
		{
			int iErr = WSAGetLastError();
			show_msg(net, "ConnectServer bind failed, error:%d", iErr);
			return iErr;
		}
	}

	// 
	if (!lpfnConnectEx)
	{
		GUID guidConnectEx = WSAID_CONNECTEX;
		DWORD dwBytes = 0;
		if (WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidConnectEx, sizeof(guidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx),
			&dwBytes, NULL, NULL) != 0)
		{
			int iErr = WSAGetLastError();
			show_msg(net, "ConnectServer WSAIoctl failed, error:%d", iErr);

			return iErr;
		}
	}

	s->iocp_accept.id = s->id;
	s->iocp_accept.type = iocp_connect;

	// 发出异步请求连接
	if (!lpfnConnectEx((SOCKET)fd, (struct sockaddr*)(&addr), addrlen, (LPVOID)NULL, 0, NULL, (LPOVERLAPPED)&s->iocp_accept))
	{
		int iError = WSAGetLastError();
		if (ERROR_IO_PENDING != iError)
		{
			int iErr = WSAGetLastError();
			show_msg(net, "ConnectServer ConnectEx failed, error:%d", iErr);

			return iErr;
		}
	}
	return 0;
}
int connect_start(struct netengine *net, struct socket *s) {
	int ierr = _do_connect(net, s);
	if (ierr != 0) {
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, ierr, s->puser);
		}
		close_fd(net, s);
		socket_free(net, s);
		return -1;
	}
	return 0;
}

void connect_on_connected(struct netengine *net, struct socket *s, bool berror) {
	if (NULL != s->pfon_connect) {
		s->pfon_connect(net, s->id, !berror, s->puser);
	}
	if (berror) {
		close_fd(net, s);
		socket_free(net, s);
		return;
	}
	s->type = SOCKET_TYPE_CONNECTED;

	client_start_recv_send(net, s, false);
}

void connect_close(struct netengine *net, struct socket *s) {

}