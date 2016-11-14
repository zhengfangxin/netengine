#include "net.h"
#include "netengine.h"
#include "net_malloc.h"
#include "thread_api.h"
#include <assert.h>
#include "pubfunc.h"
#include "accept.h"
#include "client.h"
#include "connect.h"
#include "net_def_win.h"
#include "client_win.h"


int WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
		break;
	}

	return 1;
}



struct netengine * ng_create() {
	WSADATA WSAData;
	int i;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		// socket初始化失败
		return NULL;
	}

	struct netengine *net = net_malloc(sizeof(*net));
	memset(net, 0, sizeof(*net));

	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
	if (NULL == iocp)
	{
		net_free(net);
		return NULL;
	}
	net->iocp = iocp;
	for (i = 0; i < MAX_SOCKET; ++i) {
		struct socket *s = &net->slot[i];
		s->type = SOCKET_TYPE_INVALID;
		s->id = -1;
		s->fd = -1;
	}
	net->check_time = DEF_CLOSE_TIME / 3;
	return net;
}

void ng_release(struct netengine *net){
	if (NULL != net->iocp) {
		CloseHandle(net->iocp);
		net->iocp = NULL;		
	}
	net_close_all(net);
	net_free_all(net);
	net_free(net);
	WSACleanup();
}

// 运行pool函数
void ng_run(struct netengine *net){
	HANDLE hIocp = net->iocp;
	OVERLAPPED_ENTRY entry[128];
	ULONG count;

	bool is_exit = false;
	while (!is_exit) {
		count = 0;
		BOOL r = GetQueuedCompletionStatusEx(hIocp, entry, ARRAYSIZE(entry), &count, INFINITE, false);
		assert(r);
		if (!r) {
			assert(false);
			break;
		}
		
		net_update_time(net);

		for (uint i = 0; i < count; ++i) {				
			OVERLAPPED_ENTRY *cur = entry+i;
			ULONG_PTR key = cur->lpCompletionKey;
			OVERLAPPED *pol = cur->lpOverlapped;

			if (NULL != pol) {
				assert(key == 0);
				bool error = false;				

				struct iocpnode *piocp = (struct iocpnode *)pol;
				struct socket *s = GETSOCKET(net, piocp->id);

				DWORD bytes = 0;
				DWORD flags;
				BOOL ret = WSAGetOverlappedResult(s->fd, cur->lpOverlapped, &bytes, false, &flags);
				if (!ret) {
					error = true;
				}
				switch (piocp->type) {
				case iocp_listen:
					accept_on_accecpted(net, s, error);
					break;
				case iocp_recv:
					if (bytes <= 0) {
						error = true;
					}
					client_on_recv(net, s, bytes, error);
					break;
				case iocp_send: {
					if (bytes <= 0) {
						error = true;
					}
					struct send_node *node = (struct send_node *)piocp;
					client_on_send(net, s, bytes, node, error);
				}
				break;
				case iocp_connect:
					connect_on_connected(net, s, error);
					break;
				default:
					assert(false);
				}
			}
			else if (0 != key) {
				assert(pol == NULL);
				// cmd
				struct request_package *cmd = (struct request_package *)key;
				int type = cmd->header[7];
				int id = -1;
				pro_cmd(net, cmd->header[7], cmd, &id);
				net_free(cmd);
				if (type == req_exit) {
					is_exit = true;
				}
			}
		}		
	}
}

void send_request(struct netengine *net, struct request_package *preq, int type, int len) {

	struct request_package *pdata = net_malloc(sizeof(*pdata));
	memcpy(pdata, preq, sizeof(*pdata));
	pdata->header[6] = len;
	pdata->header[7] = type;
	
	bool bSuc = PostQueuedCompletionStatus(net->iocp, 0, (ULONG_PTR)pdata, NULL);
	if (!bSuc) {
		int ierr = GetLastError();
		show_msg(net, "PostQueuedCompletionStatus failed", ierr);
	}
}