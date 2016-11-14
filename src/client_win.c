#include "client.h"
#include "net_def_win.h"
#include "net.h"
#include "net_malloc.h"

static int 
_do_recv(struct netengine *net, struct socket *s) {
	if (s->is_error) {
		return -1;
	}

	DWORD dwNumberOfByteRecved = 0;
	DWORD dwRecvFlag = 0;

	if (s->recv_stream == NULL) {
		s->recv_stream = netbuf_create();
		netbuf_set_min_max(s->recv_stream, s->def_recv, s->def_recv * 3);
	}
	int iRecvBufLen = 0;
	char *pRecfBuf = netbuf_get_write(s->recv_stream, &iRecvBufLen);

	s->recv_buf.len = iRecvBufLen;
	s->recv_buf.buf = pRecfBuf;

	s->iocp_recv.id = s->id;
	s->iocp_recv.type = iocp_recv;
	
	int iWriteRet = WSARecv(s->fd, &s->recv_buf, 1, &dwNumberOfByteRecved, &dwRecvFlag, (LPOVERLAPPED)&s->iocp_recv, NULL);
	if (iWriteRet != 0)
	{
		int iLastError = WSAGetLastError();
		if (WSA_IO_PENDING != iLastError)
		{
			return iLastError;
		}
	}
	s->recving = true;
	return 0;
}

static int _on_recv_data(struct netengine *net, struct socket *s, int size) {
	if (s->type == SOCKET_TYPE_HALFCLOSE) {
		return 0;
	}

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

	if (!s->recving && s->sending_count == 0) {
		socket_free(net, s);
	}
}

static void _try_recv(struct netengine *net, struct socket *s) {
	if (_do_recv(net, s) != 0) {
		if (NULL != s->pfon_close) {
			s->pfon_close(net, s->id, -1, s->puser);
		}
		s->recving = false;
		_on_recv_send_error(net, s);
	}
}

void client_on_recv(struct netengine *net, struct socket *s, int size, bool error) {
	if (!error && size<=0) {
		error = true;	// 有这种情况
	}
	if (error || s->is_error) {
		if (NULL != s->pfon_close) {
			s->pfon_close(net, s->id, -1, s->puser);
		}
		s->recving = false;
		_on_recv_send_error(net, s);		
		return;
	}

	if (size>0 && _on_recv_data(net, s, size) != 0)
	{
		// error
		s->recving = false;
		_on_recv_send_error(net, s);
		return;
	}
	_try_recv(net, s);
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
	// 这里没有处理len!=iremainlen的情况，因为windows iocp这里不会发生这样的情况
	struct send_node *node = net_malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));

	WSABUF *pwsaBuf = &node->wsaBuf;
	if (needfree) {
		pwsaBuf->buf = pdata;
		pwsaBuf->len = iremainlen;
		node->allsize = len;
		node->pfree = pfree;
	}
	else {
		pwsaBuf->buf = net_malloc(iremainlen);
		memcpy(pwsaBuf->buf, pdata, iremainlen);
		pwsaBuf->len = iremainlen;

		node->allsize = iremainlen;
	}

	DWORD dwNumberOfBytesSent = 0;
	DWORD dwFlags = 0;

	node->iocp_node.id = s->id;
	node->iocp_node.type = iocp_send;

	int isendlen = iremainlen;
	int iSendRet = WSASend(s->fd, pwsaBuf, 1, &dwNumberOfBytesSent, dwFlags, (LPOVERLAPPED)node, NULL);
	if (iSendRet != 0)
	{
		int iLastError = WSAGetLastError();
		if (WSAEWOULDBLOCK == iLastError)
		{
			// 等可写事件
			isendlen = 0;
		}
		else if (WSA_IO_PENDING != iLastError)
		{
			isendlen = -1;
		}
		if (isendlen <= 0)
		{
			if (!needfree) {
				if (node->pfree) {
					node->pfree(pwsaBuf->buf);
				}
				else {
					net_free(pwsaBuf->buf);
				}
			}
			net_free(node);
			return isendlen;
		}
	}
	++s->sending_count;
	
	return isendlen;
}

int client_send(struct netengine *net, struct socket *s, void *pdata, int len, bool needfree, ffreedata *pfree) {
	if (s->is_error || s->type==SOCKET_TYPE_HALFCLOSE || s->type==SOCKET_TYPE_INVALID) {
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

	s->wb_len += len;
	if (!error) {
		if (s->type == SOCKET_TYPE_CONNECTED && client_is_data_empty(s)) {
			int ret = _do_send(net, s, pdata, len, remainlen, needfree, pfree);
			if (ret < 0) {
				// error
				error = true;
			}
			else if (ret == 0) {
			}
			else {
				assert(ret == remainlen);
				remainlen -= ret;
			}
		}
	}

	if (error) {
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
	}
	return 0;
}

static void _try_send(struct netengine *net, struct socket *s) {
	if (client_is_data_empty(s)) {
		if (s->type == SOCKET_TYPE_HALFCLOSE) {
			// close
			close_fd(net, s);
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
				assert(ret == remainlen);
				remainlen -= ret;
			}
		}
		if (remainlen > 0) {
			wb->cursz = remainlen;
			break;
		}
		else {
			struct write_buffer *cur = wb;
			wb = wb->next;
			buffer->head = wb;
			// data is consume, no need to free
			net_free(cur);
		}
	}
	if (buffer->head == NULL) {
		buffer->tail = NULL;
	}
	if (error) {
		_on_recv_send_error(net, s);
	}
}
void client_start_send(struct netengine *net, struct socket *s) {
	_try_send(net, s);
}

void client_on_send(struct netengine *net, struct socket *s, int size, struct send_node *node, bool error) {
	assert(node->allsize >= (int)node->wsaBuf.len);
	
	int len = node->wsaBuf.len;
	if (node->pfree != NULL) {
		node->pfree(node->wsaBuf.buf);
	}
	else {
		net_free(node->wsaBuf.buf);
	}
	net_free(node);

	s->wb_len -= len;
	--s->sending_count;

	if (size != len) {
		error = true;
	}
	if (error) {
		_on_recv_send_error(net, s);
		return;
	}
	_try_send(net, s);
}

void client_start_recv_send(struct netengine *net, struct socket *s, bool add) {
	_try_recv(net, s);
	_try_send(net, s);
}

void client_close(struct netengine *net, struct socket *s) {

}