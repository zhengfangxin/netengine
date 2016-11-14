#ifndef net_buf_h
#define net_buf_h

#include "os.h"


struct netbuf;

struct netbuf *netbuf_create();

// 设置初始和最大数量，默认分配min, 达到最大后，如果数据清空，则会释放全部内存
void netbuf_set_min_max(struct netbuf *buf, int min, int max);

void netbuf_release(struct netbuf *buf);

// 返回一个可写的缓冲区，buflen为可写长度，如果没有空闲区域则会分配新空间
void *netbuf_get_write(struct netbuf *buf, int *buflen);

// 通过上面写入数据后，手动加上数据有效长度
void netbuf_add_len(struct netbuf *buf, int len);

// 也可直接写入
void netbuf_write(struct netbuf *buf, const void *data, int len);

// 获取有效的数据
void *netbuf_get_data(struct netbuf *buf, int *len);

// 消费掉多少数据
void netbuf_skip_data(struct netbuf *buf, int len);

#endif