#ifndef connnect_h
#define connect_h

#include "net.h"
#include "net_def.h"


int connect_start(struct netengine *net, struct socket *s);

void connect_on_connected(struct netengine *net, struct socket *s, bool berror);

void connect_close(struct netengine *net, struct socket *s);

#endif