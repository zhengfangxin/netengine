#pragma once
#include "client.h"

int client_on_recv(struct netengine *net, struct socket *s);
void client_on_send(struct netengine *net, struct socket *s);