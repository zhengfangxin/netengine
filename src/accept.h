#ifndef accept_h
#define accept_h

#include "net_def.h"

void accept_on_accecpted(struct netengine *net, struct socket *s, bool berror);
int accept_start(struct netengine *net, struct socket *s);

void accept_close(struct netengine *net, struct socket *s);
void accept_free(struct netengine *net, struct socket *s);

#endif