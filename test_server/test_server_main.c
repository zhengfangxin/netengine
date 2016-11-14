#include "netengine.h"
#include "pubfunc.h"
#include "pro_def.h"
#include "debug_malloc.h"

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
	//printf("on error id:%d %s:%d error:%d user:%p\n", id, szip, port, ierror, puser);
}

// return -1: 关闭连接, >=0 表示已处理数据的长度
static int on_recv(struct netengine *net, int id, char *pdata, int idatalen, void *puser) {
	ng_send(net, id, pdata, idatalen, false, NULL);
	/*char *s_data = malloc(idatalen);
	memcpy(s_data, pdata, idatalen);
	ng_sendto(net, id, s_data, idatalen, NULL);*/
	return idatalen;

	static bool show = false;
	if (!show) {
		show = true;
		char szip[64];
		int port;
		if (ng_get_remote_addr(net, id, szip, sizeof(szip), &port)) {
			//printf("on recv get remote addr %s:%d\n", szip, port);
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
			assert(false);
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
	//printf("on close id:%d %s:%d error:%d user:%p\n", id, szip, port, ierror, puser);
}

static void on_listen(struct netengine *net, int ilistenid, const char *pszip, int iport, void *puser) {
	printf("on listen suc id:%d %s:%d user:%p\n", ilistenid, pszip, iport, puser);
}
// return false 将会关闭连接
static bool on_accept(struct netengine *net, int ilistenid, int inewid, socket_t fd, const char *pszip, int iport, void *puser) {
	//printf("on accept listenid:%d newfd:%d fd:%d %s:%d user:%p\n", ilistenid, inewid, (int)fd, pszip, iport, puser);
	ng_set_recv(net, inewid, on_recv, on_close, on_error);
	ng_start(net, inewid);
	return true;
}

static void run_abc(struct netengine *net, void *user) {
	printf("abc func is run, user:%p\n", (char*)user);
}

static void onmsg(const char *pszMsg) {
	printf("[net]%s\n", pszMsg);
}

struct netengine *net = NULL;
int ipv4id = -1;
int ipv6id = -1;
int wait_time = 20;
static int runTest(void *param) {
	int c = 0;
	while (c < wait_time) {
		thread_sleep(1000);
		++c;
	}
	if (ipv4id >= 0) ng_close(net, ipv4id, true);
	if (ipv6id >= 0) ng_close(net, ipv6id, true);

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
	return 0;
}
int main(int argc, char *argv[]) {

	_RegDebugNew();

	if (argc > 1) {
		wait_time = atoi(argv[1]);
	}

	net = ng_create();
	if (NULL == net) {
		printf("create failed\n");
		return -1;
	}

	ng_setmsg(net, onmsg);

	printf("abc func will run with user 0x01\n");
	void *user = (void*)0x01;
	ng_run_func(net, run_abc, user);

	ipv4id = ng_listen(net, NULL, 9123, 5000);
	//ipv6id = ng_listen(net, "::1", 9123, 5000);
	if (ipv4id >= 0) {
		ng_set_listen(net, ipv4id, on_listen, on_accept, on_error);
	}
	if (ipv6id >= 0) {
		ng_set_listen(net, ipv6id, on_listen, on_accept, on_error);
	}

	printf("listen on 0:10000 and [::]:10000, id %d %d, this will close after %ds\n", 
		ipv4id, ipv6id, wait_time);
	if (ipv4id >= 0) {
		ng_start(net, ipv4id);
	}
	if (ipv6id >= 0) {
		ng_start(net, ipv6id);
	}

	//printf("will stop after %ds\n", wait_time+2);
	//thread_create((thread_func)runTest, NULL);
	ng_run(net);

	printf("net free\n");

	ng_release(net);	

	return 0;
}
