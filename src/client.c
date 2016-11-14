#include "client.h"
#include "net_malloc.h"
#include "net.h"


#ifdef _WINDOWS
#include "net_def_win.h"
#else
#include "net_def_linux.h"
#endif

bool client_is_data_empty(struct socket *s) {
	return s->send_list.head==NULL;
}

void client_add_send_buffer(struct wb_list *buffer, const void *pdata, int allsize, int size, ffreedata *pfree) {
	struct write_buffer *wb = net_malloc(sizeof(*wb));
	wb->buffer = (void*)pdata;
	wb->size = allsize;
	wb->cursz = size;
	wb->next = NULL;
	wb->pfree = pfree;

	if (buffer->tail == NULL) {
		buffer->head = buffer->tail = wb;
	}
	else {
		buffer->tail->next = wb;
		buffer->tail = wb;
	}
}

void client_free_write_buffer(struct write_buffer *wb) {
	if (wb->pfree) {
		wb->pfree(wb->buffer);
	}
	else {
		net_free(wb->buffer);
	}	
}

void client_update_valid_time(struct netengine *net, struct socket *s) {
	s->valid_time = ng_get_time(net);
}

int64 client_get_valid_time(struct socket *s) {
	return s->valid_time;
}