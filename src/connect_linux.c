#include "connect.h"
#include "net_def_linux.h"
#include "client.h"
#include "socket_poll.h"


static int _do_connect(struct netengine *net, struct socket *s) {
	int status;
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	struct addrinfo *ai_ptr = NULL;
	char port[16];
	sprintf(port, "%d", s->port);
	memset(&ai_hints, 0, sizeof( ai_hints ) );
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	status = getaddrinfo( s->szaddr, port, &ai_hints, &ai_list );
	if ( status != 0 ) {
		show_msg(net, "getaddrinfo failed %s", gai_strerror(status));
		goto _failed;
	}
	int sock= -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next ) {
		sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol );
		if ( sock < 0 ) {
			continue;
		}
		sp_nonblocking(sock);
		status = connect( sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		if ( status != 0 && errno != EINPROGRESS) {
			close(sock);
			sock = -1;
			continue;
		}
		break;
	}

	if (sock < 0) {
		show_msg(net, "connect failed %s", gai_strerror(status));
		goto _failed;
	}
	if (sp_add(net->event_fd, sock, s)) {
		close(sock);
		int err = errno;
		show_msg(net, "sp_add failed %d %s", strerror(err));
		goto _failed;
	}
	sp_write(net->event_fd, sock, s, true);
	
	s->protocol =  ai_ptr->ai_family;
	s->fd = sock;
	if(status == 0) {
		freeaddrinfo( ai_list );
		return 1;
	} else {
		
	}

	freeaddrinfo( ai_list );
	return 0;
_failed:
	freeaddrinfo( ai_list );
	return -1;
}
int connect_start(struct netengine *net, struct socket *s) {
	int ret = _do_connect(net, s);
	if (ret == 0) {
	}
	else if (ret < 0) {
		int err = errno;
		if (NULL != s->pfon_error) {
			s->pfon_error(net, s->id, err, s->puser);
		}
		close_fd(net, s);
		socket_free(net, s);
		return -1;
	}
	else {
		// connect suc
		connect_on_connected(net, s, false);
	}
	return 0;
}

void connect_on_connected(struct netengine *net, struct socket *s, bool error) {
	bool connect_failed = false;
	if (error) {
		connect_failed = true;
	}
	int sock_error;
	socklen_t len = sizeof(sock_error);  
	int code = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &sock_error, &len);
	if (code < 0 || sock_error) {
		connect_failed = true;
	}

	if (NULL != s->pfon_connect) {
		s->pfon_connect(net, s->id, !connect_failed, s->puser);
	}
	if (connect_failed) {		
		close_fd(net, s);
		socket_free(net, s);
		return;
	}
	s->type = SOCKET_TYPE_CONNECTED;

	client_start_recv_send(net, s, false);
}

void connect_close(struct netengine *net, struct socket *s) {
	sp_del(net->event_fd, s->fd);
}