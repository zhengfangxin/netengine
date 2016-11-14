#include "net.h"
#include "netengine.h"
#include "net_malloc.h"
#include "thread_api.h"
#include <assert.h>
#include "pubfunc.h"
#include "accept.h"
#include "client.h"
#include "connect.h"
#include "net_def_linux.h"
#include "socket_poll.h"
#include "client_linux.h"


struct netengine * ng_create() {
	int i;	
	int fd[2];
	poll_fd efd = sp_create();
	if (sp_invalid(efd)) {
		fprintf(stderr, "socket-server: create event pool failed.\n");
		return NULL;
	}
	if (pipe(fd)) {
		sp_release(efd);
		fprintf(stderr, "socket-server: create socket pair failed.\n");
		return NULL;
	}
	if (sp_add(efd, fd[0], NULL)) {
		// add recvctrl_fd to event poll
		fprintf(stderr, "socket-server: can't add server fd to event pool.\n");
		close(fd[0]);
		close(fd[1]);
		sp_release(efd);
		return NULL;
	}
	
	struct netengine *net = net_malloc(sizeof(*net));
	memset(net, 0, sizeof(*net));
	
	net->event_fd = efd;
	net->recvctrl_fd = fd[0];
	net->sendctrl_fd = fd[1];
	net->checkctrl = 1;
	
	net->alloc_id = 0;
	net->event_n = 0;
	net->event_index = 0;
	FD_ZERO(&net->rfds);
	assert(net->recvctrl_fd < FD_SETSIZE);
	
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
	net_close_all(net);
	net_free_all(net);
	
	close(net->sendctrl_fd);
	close(net->recvctrl_fd);
	sp_release(net->event_fd);
	
	net_free(net);
}

static int
has_cmd(struct netengine *net) {
	struct timeval tv = {0,0};
	int retval;

	FD_SET(net->recvctrl_fd, &net->rfds);

	retval = select(net->recvctrl_fd+1, &net->rfds, NULL, NULL, &tv);
	if (retval == 1) {
		return 1;
	}
	return 0;
}

static void
block_readpipe(struct netengine *net, int pipefd, void *buffer, int sz) {
	for (;;) {
		int n = read(pipefd, buffer, sz);
		if (n<0) {
			if (errno == EINTR)
				continue;
			show_msg(net, "socket-server : read pipe error %s.",strerror(errno));
			return;
		}
		// must atomic read from a pipe
		assert(n == sz);
		return;
	}
}

// return -1:error, 0:suc, 1:exit
static int
ctrl_cmd(struct netengine *net, int *id) {
	int fd = net->recvctrl_fd;
	// the length of message is one byte, so 256+8 buffer size is enough.
	struct request_package cmd;
	uchar header[2];
	block_readpipe(net, fd, header, sizeof(header));	
	int len = header[0];
	int type = header[1];
	block_readpipe(net, fd, cmd.u.buffer, len);
	int ret = pro_cmd(net, type, &cmd, id);	

	if (type == req_exit) {
		return 1;
	}
	return ret;
}

static inline void 
clear_closed_event(struct netengine *net, int id) {
	int i;
	for (i=net->event_index; i<net->event_n; i++) {
		struct event *e = &net->ev[i];
		struct socket *s = e->s;
		if (s) {
			if (s->type == SOCKET_TYPE_INVALID && s->id == id) {
				e->s = NULL;
				break;
			}
		}
	}
}

// 运行pool函数
void ng_run(struct netengine *net){
	int id = -1;
	for (;;) {
		net_update_time(net);
		if (net->checkctrl) {
			if (has_cmd(net)) {
				int ret = ctrl_cmd(net, &id);
				if (ret == 1) {
					// exit
					break;
				}
				if (ret == -1) {
					clear_closed_event(net, id);
				}
				continue;
			} else {
				net->checkctrl = 0;
			}
		}
		if (net->event_index == net->event_n) {
			net->event_n = sp_wait(net->event_fd, net->ev, MAX_EVENT);
			net->checkctrl = 1;
			net->event_index = 0;
			if (net->event_n <= 0) {
				net->event_n = 0;
				continue;
			}
		}

		struct event *e = &net->ev[net->event_index++];
		struct socket *s = e->s;
		if (s == NULL) {
			// dispatch pipe message at beginning
			continue;
		}
		switch (s->type) {
		case SOCKET_TYPE_CONNECTING:
			connect_on_connected(net, s, false);
			break;
		case SOCKET_TYPE_LISTEN: {
			accept_on_accecpted(net, s, false);			
			break;
		}
		case SOCKET_TYPE_INVALID:
			show_msg(net, "socket-server: invalid socket");
			break;
		default:
			if (e->read) {
				int ret = client_on_recv(net, s);
				if (e->write && ret!=0) {
					e->write = false;
				}
			}
			if (e->write) {
				client_on_send(net, s);
			}
			break;
		}
	}
}

void send_request(struct netengine *net, struct request_package *preq, int type, int len) {
	preq->header[6] = len;
	preq->header[7] = type;
	int send_len = len+2;
	assert(send_len<255);
	
	for (;;) {
		int n = write(net->sendctrl_fd, &preq->header[6], send_len);
		if (n<0) {
			if (errno != EINTR) {
				show_msg(net, "socket-server : send ctrl command error %s.", strerror(errno));
			}
			continue;
		}
		assert(n == send_len);
		return;
	}
}

