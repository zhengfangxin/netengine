#ifndef net_malloc_h
#define net_malloc_h

#include "netengine.h"


void * net_malloc(size_t sz);
void * net_realloc(void *ptr, size_t size);
void net_free(void *ptr);
char * net_strdup(const char *str);

#endif