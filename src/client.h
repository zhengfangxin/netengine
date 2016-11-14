#ifndef client_h
#define client_h

#include "net_def.h"


bool client_is_data_empty(struct socket *s);

void client_add_send_buffer(struct wb_list *buffer, const void *pdata, int allsize, int size, ffreedata *pfree);

// add for linux
void client_start_recv_send(struct netengine *net, struct socket *s, bool add);
void client_start_send(struct netengine *net, struct socket *s);



void client_free(struct netengine *net, struct socket *s);

int client_send(struct netengine *net, struct socket *s, void *pdata, int len, bool needfree, ffreedata *pfree);

void client_free_write_buffer(struct write_buffer *wb);

void client_update_valid_time(struct netengine *net, struct socket *s);

int64 client_get_valid_time(struct socket *s);

void client_close(struct netengine *net, struct socket *s);

#endif