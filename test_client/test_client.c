#include "netengine.h"
#include "pubfunc.h"
#include "debug_malloc.h"
#include "pro_def.h"

// 当出现错误，比如发送失败等
static void on_error(struct netengine *net, int id, int ierror, void *puser) {
	char szip[64];
	int port = 0;
	szip[0] = 0;
	if (ng_get_remote_addr(net, id, szip, sizeof(szip), &port)) {
	}
	else {
		printf("get remote addr failed\n");
	}
	printf("on error id:%d %s:%d error:%d user:%p\n", id, szip, port, ierror, puser);
}

// return -1: 关闭连接, >=0 表示已处理数据的长度
static int on_recv(struct netengine *net, int id, char *pdata, int idatalen, void *puser) {
	static bool show = false;
	if (!show) {
		show = true;
		char szip[64];
		int port;
		if (ng_get_remote_addr(net, id, szip, sizeof(szip), &port)) {
			//printf("on recv get remote addr %s:%d", szip, port);
		}
		else {
			printf("on recv get remote addr failed\n");
		}

		printf("recv id:%d %s:%d len:%d user:%p\n", id, szip, port, idatalen, puser);
	}

	static int c = 0;
	static int len = 0;
	static int64 time = 0;

	int prolen = 0;
	while (prolen < idatalen) {
		int remainlen = idatalen - prolen;
		char *cur = pdata + prolen;
		struct pro_head *head = (struct pro_head *)cur;
		const int head_len = sizeof(*head);
		if (remainlen < head_len) {
			break;
		}
		int packlen = head->len + head_len;
		if (remainlen < packlen) {
			break;
		}
		assert(packlen == remainlen);
		prolen += packlen;

		if (head->check != CHECK) {
			printf("head check failed %d %d", head->check, CHECK);
			return -1;
		}
		++c;
		len += packlen;

		int64 curtime = ng_get_time(net);
		int sub = (int)(curtime - time);
		if (sub > 3000) {
			time = curtime;
			printf("recv time:%dms pack:%d alllen:%d\n", sub, c, len);
			c = 0;
			len = 0;
		}
		ng_send(net, id, cur, packlen, false, NULL);
	}
	return prolen;
}

// 当连接关闭
static void on_close(struct netengine *net, int id, int ierror, void *puser) {
	char szip[64];
	int port = 0;
	szip[0] = 0;
	if (ng_get_remote_addr(net, id, szip, sizeof(szip), &port)) {
	}
	else {
		printf("get remote addr failed\n");
	}
	printf("on error id:%d %s:%d error:%d user:%p\n", id, szip, port, ierror, puser);
}

// 连接返回
static void on_connect(struct netengine *net, int id, bool bsuc, void *puser) {
	char szip[64];
	int port = 0;
	szip[0] = 0;
	if (ng_get_remote_addr(net, id, szip, sizeof(szip), &port)) {
	}
	else {
		printf("on connect get remote addr failed\n");
	}
	printf("on connect id:%d %s:%d suc:%d user:%p\n", id, szip, port, bsuc, puser);
}

static void onmsg(const char *pszMsg) {
	printf("[net]%s\n", pszMsg);
}

struct netengine *net = NULL;
int ipv4id;
int ipv6id;
int iconnect_count = 10;
int wait_time = 30;
static int runTest(void *param) {
	{
		printf("connect 127.0.0.1:10000 and close\n");
		int id = 0;
		id = ng_connect(net, "127.0.0.1", 10000);
		ng_set_connect(net, id, on_recv, on_connect, on_close, on_error);
		ng_start(net, id);
		thread_sleep(2000);
		ng_close(net, id, true);

		const char *ip = "::1";
		printf("connect %s:10000 and close\n", ip);
		id = ng_connect(net, ip, 10000);

		ng_set_connect(net, id, on_recv, on_connect, on_close, on_error);
		ng_start(net, id);
		thread_sleep(2000);
		ng_close(net, id, true);
	}
	
	const int connect = iconnect_count;
	printf("test %d connection\n", connect);
	int *id = malloc(connect*sizeof(int));
	int i = 0;
	for (; i < connect/2; ++i) {
		id[i] = ng_connect(net, "127.0.0.1", 10000);
		if (id[i] < 0) {
			printf("connect failed %d\n", i);
		}
	}
	for (; i < connect; ++i) {
		id[i] = ng_connect(net, "::1", 10000);
		if (id[i] < 0) {
			printf("connect failed %d\n", i);
		}
	}
	for (i = 0; i < connect; ++i) {
		ng_set_connect(net, id[i], on_recv, on_connect, on_close, on_error);
		ng_start(net, id[i]);

		uint len = get_rand(0, 200 * 1024);
		struct pro_head head;
		head.len = len;
		head.check = CHECK;
		int alllen = sizeof(head) + len;
		char *psend = malloc(alllen);
		memcpy(psend, &head, sizeof(head));
		ng_sendto(net, id[i], psend, alllen, NULL);
	}
	int c = 0;
	while (c < wait_time) {
		thread_sleep(1000);
		++c;
	}

	for (i = 0; i < connect/2; ++i) {
		ng_close(net, id[i], true);
	}

	c = 0;
	while (c < 2) {
		thread_sleep(1000);
		++c;
	}

	ng_close_all(net);
#ifdef _WINDOWS
	thread_sleep(2000);
#endif
	ng_break(net);

	free(id);

	return 0;
}
int main(int argc, char *argv[]) {

	_RegDebugNew();

	if (argc > 1) {
		iconnect_count = atoi(argv[1]);
	}
	if (argc > 2) {
		wait_time = atoi(argv[2]);
	}
	net = ng_create();
	if (NULL == net) {
		printf("create failed\n");
		return -1;
	}
	ng_setmsg(net, onmsg);

	thread_create((thread_func)runTest, NULL);
	ng_run(net);

	printf("net free\n");

	ng_release(net);

	return 0;
}