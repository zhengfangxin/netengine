#pragma once
#include "client.h"

void client_on_recv(struct netengine *net, struct socket *s, int size, bool error);
void client_on_send(struct netengine *net, struct socket *s, int size, struct send_node *node, bool error);