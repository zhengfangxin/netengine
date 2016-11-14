#include "net_malloc.h"
#include "debug_malloc.h"

static fpmalloc *g_pmalloc = malloc;
static fprealloc *g_prealloc = realloc;
static fpfree *g_pfree = free;

void ng_set_malloc(fpmalloc *pmalloc, fprealloc *prealloc, fpfree *pfree)
{
	if (NULL==pmalloc || NULL == prealloc || NULL == pfree) {
		printf("set malloc error, some function is null");
		return ;
	}
	g_pmalloc = pmalloc;
	g_prealloc = prealloc;
	g_pfree = pfree;
}

void * net_malloc(size_t sz) {
	return g_pmalloc(sz);
}
void * net_realloc(void *ptr, size_t size) {
	return g_prealloc(ptr, size);
}
void net_free(void *ptr) {
	g_pfree(ptr);
}
char * net_strdup(const char *str){
	size_t sz = strlen(str);
	char * ret = net_malloc(sz + 1);
	memcpy(ret, str, sz + 1);
	return ret;
}