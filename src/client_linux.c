#include "client.h"
#include "net_def_linux.h"
#include "net.h"
#include "net_malloc.h"
#include "socket_poll.h"

static int _on_recv_data(struct netengine *net, struct socket *s) {
	if (s->recv_stream == NULL) {
		s->recv_stream = netbuf_create();
		netbuf_set_min_max(s->recv_stream, s->def_recv, s->def_recv * 3);
	}
	int iRecvBufLen = 0;
	char *pRecfBuf = netbuf_get_write(s->recv_stream, &iRecvBufLen);
	
	int n = (int)read(s->fd, pRecfBuf, iRecvBufLen);
	if (n<0) {
		switch(errno) {
		case EINTR:
			break;
		case EAGAIN:
			fprintf(stderr, "socket-server: EAGAIN capture.\n");
			break;
		default:
			// close when error
			return -1;
		}
		return 0;
	}
	if (n==0) {
		return -1;
	}

	if (s->type == SOCKET_TYPE_HALFCLOSE) {
		// discard recv data
		return 0;
	}

	int size = n;
	netbuf_add_len(s->recv_stream, size);

	int len = 0;
	char *pdata = netbuf_get_data(s->recv_stream, &len);

	int prolen = 0;
	if (s->pfon_recv != NULL) {
		prolen = s->pfon_recv(net, s->id, pdata, len, s->puser);
	}
	else {
		prolen = len;
	}
	if (prolen < 0) {
		return -1;
	}
	if (s->recv_stream == NULL) {
		// may delete socket on process recv data
		return -1;
	}
	netbuf_skip_data(s->recv_stream, prolen);
	{
		int curlen = len - prolen;
		if (curlen >= s->max_recv) {
			show_msg(net, "%s:%d recv buf limit, len:%d", s->szaddr, s->port, curlen);
			return -1;
		}
	}
	return 0;
}

static void
_on_recv_send_error(struct netengine *net, struct socket *s) {
	s->is_error = true;
	close_fd(net, s);

	socket_free(net, s);
}

int client_on_recv(struct netengine *net, struct socket *s) {
	if (_on_recv_data(net, s) != 0)
	{
		if (NULL != s->pfon_close) {
			s->pfon_close(net, s->id, -1, s->puser);
		}
		_on_recv_send_error(net, s);
		return -1;
	}
	return 0;
}

void client_free(struct netengine *net, struct socket *s) {
	if (s->recv_stream != NULL) {
		netbuf_release(s->recv_stream);
		s->recv_stream = NULL;
	}
	struct write_buffer *wb = s->send_list.head;
	while (wb != NULL) {		
		struct write_buffer *cur = wb;
		wb = wb->next;
		client_free_write_buffer(cur);
		net_free(cur);
	}
	s->send_list.head = s->send_list.tail = NULL;
}

static int 
_do_send(struct netengine *net, struct socket *s, void *pdata, int len, int iremainlen, bool needfree, ffreedata *pfree) {
	int sz;
_retry:
	sz = write(s->fd, pdata, iremainlen);
	if (sz < 0) {
		bool try = false;
		switch(errno) {
		case EINTR:
			try = true;
			break;
		case EAGAIN:
			return 0;
		}
		if (try) {
			goto _retry;
		}
		return -1;
	}
	return sz;
}

int client_send(struct netengine *net, struct socket *s, void *pdata, int len, bool needfree, ffreedata *pfree) {
	if (s->is_error || s->type==SOCKET_TYPE_HALFCLOSE) {
		if (needfree) {
			if (NULL != pfree) {
				pfree(pdata);
			}
			else {
				net_free(pdata);
			}
		}
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, -1, s->puser);
		}
		return 0;
	}
	int remainlen = len;
	bool error = false;

	if (s->wb_len > s->max_send) {
		show_msg(net, "%s:%d limit send buf, len:%d", s->szaddr, s->port, s->wb_len);
		error = true;
	}
	
	if (!error) {
		if (s->type == SOCKET_TYPE_CONNECTED && client_is_data_empty(s)) {
			while (remainlen > 0) {
				int ret = _do_send(net, s, pdata, len, remainlen, needfree, pfree);
				if (ret < 0) {
					// error
					error = true;
					break;
				}
				else if (ret == 0) {
					break;
				}
				else {
					remainlen -= ret;
				}
				if (remainlen > 0) {
					// if not all data send, next try
					break;
				}
			}
		}
	}

	if (error || remainlen<=0) {
		if (needfree) {
			if (NULL != pfree) {
				pfree(pdata);
			}
			else {
				net_free(pdata);
			}
		}
	}
	if (error) {		
		_on_recv_send_error(net, s);
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, -1, s->puser);
		}
		return -1;
	}
	if (remainlen > 0) {
		// add to send buffer
		if (!needfree) {
			void *pnew = net_malloc(remainlen);
			memcpy(pnew, (char*)pdata+len- remainlen, remainlen);
			client_add_send_buffer(&s->send_list, pnew, remainlen, remainlen, NULL);
		}
		else {
			client_add_send_buffer(&s->send_list, pdata, len, remainlen, pfree);
		}
		s->wb_len += remainlen;
	}
	return 0;
}

static void _try_send(struct netengine *net, struct socket *s) {
	if (client_is_data_empty(s)) {
		if (s->type == SOCKET_TYPE_HALFCLOSE) {
			// close
			_on_recv_send_error(net, s);
		}
		else {
			sp_write(net->event_fd, s->fd, s, false);
		}
		return;
	}
	struct wb_list *buffer = &s->send_list;
	struct write_buffer *wb = buffer->head;
	bool error = false;
	while (wb != NULL) {
		int remainlen = wb->cursz;
		while (remainlen > 0) {
			int ret = _do_send(net, s, wb->buffer, wb->size, remainlen, true, wb->pfree);
			if (ret < 0) {
				// error
				error = true;
				break;
			}
			else if (ret == 0) {
				break;
			}
			else {
				remainlen -= ret;
			}
			if (remainlen > 0) {
				// if not all data send, next try
				break;
			}
		}
		s->wb_len -= (wb->cursz-remainlen);
		if (remainlen > 0) {
			wb->cursz = remainlen;
			break;
		}
		else {
			struct write_buffer *cur = wb;
			wb = wb->next;
			buffer->head = wb;
			// free data and node
			if (NULL != cur->pfree) {
				cur->pfree(cur->buffer);
			}
			else {
				net_free(cur->buffer);
			}
			net_free(cur);
		}
	}
	if (buffer->head == NULL) {
		buffer->tail = NULL;
		// no date
		sp_write(net->event_fd, s->fd, s, false);
	}
	if (error) {
		_on_recv_send_error(net, s);
	}
}

void client_on_send(struct netengine *net, struct socket *s) {
	_try_send(net, s);
}

static int _add_poll(struct netengine *net, struct socket *s) {
	if (sp_add(net->event_fd, s->fd, s)) {
			if (NULL != s->pfon_close) {
			s->pfon_close(net, s->id, -1, s->puser);
		}
		_on_recv_send_error(net, s);
		return -1;
	}
	return 0;
}

void client_start_recv_send(struct netengine *net, struct socket *s, bool add) {
	if (add) {
		if (_add_poll(net, s) != 0) {
			return ;
		}
	}
	_try_send(net, s);
}

void client_close(struct netengine *net, struct socket *s) {
	sp_del(net->event_fd, s->fd);
}
