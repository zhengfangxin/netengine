#include "net_buf.h"
#include "net_malloc.h"

struct netbuf {
	char *pbuf;
	int buflen;

	int datapos;
	int datalen;

	int min;
	int max;
};

struct netbuf *netbuf_create() {
	struct netbuf *buf = net_malloc(sizeof(*buf));
	
	memset(buf, 0, sizeof(*buf));

	buf->min = 1024;
	buf->max = 5 * 1024;

	return buf;
}

// 设置初始和最大数量，默认分配min, 达到最大后，如果数据清空，则会释放全部内存
void netbuf_set_min_max(struct netbuf *buf, int min, int max) {
	assert(min > 0 && min < max);
	buf->min = min;
	buf->max = max;
}

void netbuf_release(struct netbuf *buf) {
	net_free(buf->pbuf);
	net_free(buf);
}

static void _resize(struct netbuf *buf, int inewsize) {
	void *pnew = NULL;
	if (NULL != buf->pbuf) {
		pnew = net_realloc(buf->pbuf, inewsize);
		if (NULL == pnew) {
			pnew = net_malloc(inewsize);
			memcpy(pnew, buf->pbuf + buf->datapos, buf->datalen);
			net_free(buf->pbuf);
			buf->datapos = 0;
		}
	}
	else {
		pnew = net_malloc(inewsize);		
	}
	buf->pbuf = pnew;
	buf->buflen = inewsize;
}
// 返回一个可写的缓冲区，buflen为可写长度，如果没有空闲区域则会分配新空间
void *netbuf_get_write(struct netbuf *buf, int *buflen) {
	assert(buf->datapos + buf->datalen <= buf->buflen);

	if (buf->datapos + buf->datalen >= buf->buflen) {
		if (buf->datapos > 0) {
			// copy to begin
			memcpy(buf->pbuf, buf->pbuf + buf->datapos, buf->datalen);
			buf->datapos = 0;
		}
		else {
			int len = buf->min;
			if (buf->buflen > 0) {
				len = buf->buflen*2;
			}
			_resize(buf, len);
		}
	}

	*buflen = buf->buflen - (buf->datapos + buf->datalen);
	return buf->pbuf + (buf->datapos + buf->datalen);
}

// 通过上面写入数据后，手动加上数据有效长度
void netbuf_add_len(struct netbuf *buf, int len) {
	assert( (buf->datapos + buf->datalen + len) <= buf->buflen);
	buf->datalen += len;
}

// 也可直接写入
void netbuf_write(struct netbuf *buf, const void *data, int len) {
	int inewdatalen = buf->datalen + len;

	if ((buf->datapos + buf->datalen + len) > buf->buflen)
	{
		if (inewdatalen < buf->buflen)	{
			// copy to begin
			memcpy(buf->pbuf, buf->pbuf + buf->datapos, buf->datalen);
			buf->datapos = 0;
		}
		else {
			int min = buf->min;
			if (buf->buflen > 0) {
				min = buf->buflen;
			}
			int inewsz = min * 2;
			while (inewsz < inewdatalen) {
				inewsz *= 2;
			}
			_resize(buf, inewsz);
		}
	}
	memcpy(buf->pbuf + buf->datapos + buf->datalen, data, len);
	buf->datalen += len;
}

// 获取有效的数据
void *netbuf_get_data(struct netbuf *buf, int *len) {
	*len = buf->datalen;
	return buf->pbuf + buf->datapos;
}

// 消费掉多少数据
void netbuf_skip_data(struct netbuf *buf, int len) {
	assert(buf->datalen >= len);
	buf->datalen -= len;
	buf->datapos += len;
	if (buf->datalen == 0) {
		buf->datapos = 0;
		if (buf->buflen >= buf->max) {
			net_free(buf->pbuf);
			buf->pbuf = NULL;
			buf->buflen = 0;
		}
	}
}