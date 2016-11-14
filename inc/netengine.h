#ifndef netengine_h
#define netengine_h

#include "os.h"
#include "socket_def.h"


#ifdef _WINDOWS

#ifdef NETENGINE_EXPORTS
#define DLL_EXPORT			__declspec(dllexport)
#else
#define DLL_EXPORT			__declspec(dllimport)
#endif

#else
#define DLL_EXPORT 
#endif


#ifdef __cplusplus
extern "C" {
#endif
	
	typedef void * fpmalloc(size_t sz);
	typedef void * fprealloc(void *ptr, size_t size);
	typedef void fpfree(void *ptr);

	// 设置内存申请函数，在调用任何函数之前设置
	DLL_EXPORT void ng_set_malloc(fpmalloc *pmalloc, fprealloc *prealloc, fpfree *pfree);

	struct netengine;

	// 返回错误消息
	typedef void fonmsg(const char *pszMsg);

	// 创建
	DLL_EXPORT struct netengine * ng_create();

	// 用户相关数据
	DLL_EXPORT void ng_set_net_user(struct netengine *net, void *puser);
	DLL_EXPORT void *ng_get_net_user(struct netengine *net);

	// 释放
	DLL_EXPORT void ng_release(struct netengine *pstnetengine);

	// 设置日志函数
	DLL_EXPORT void ng_setmsg(struct netengine *pstnetengine, fonmsg*);

	// 运行pool函数
	DLL_EXPORT void ng_run(struct netengine *pstnetengine);
	// 退出
	DLL_EXPORT void ng_break(struct netengine *pstnetengine);

	// 关闭连接
	DLL_EXPORT void ng_close(struct netengine *net, int id, bool peaceful);

	// 开始侦听，连接，或收发数据
	DLL_EXPORT void ng_start(struct netengine *net, int id);

	// listen suc
	typedef void fonlisten(struct netengine *net, int ilistenid, const char *pszip, int iport, void *puser);
	// return false 将会关闭连接
	typedef bool fonaccept(struct netengine *net, int ilistenid, int inewid, socket_t fd, const char *pszip, int iport, void *puser);

	// 当出现错误，比如发送失败等
	typedef void fonerror(struct netengine *net, int id, int ierror, void *puser);

	// return -1: 关闭连接, >=0 表示已处理数据的长度
	typedef int fonrecv(struct netengine *net, int id, char *pdata, int idatalen, void *puser);

	// 当连接关闭
	typedef void fonclose(struct netengine *net, int id, int ierror, void *puser);

	// 连接返回
	typedef void fonconnect(struct netengine *net, int id, bool bsuc, void *puser);

	// 创建侦听socket
	DLL_EXPORT int ng_listen(struct netengine *pstnetengine, const char *pszaddr, int iPort, int backlog);

	// 设置侦听回调函数
	DLL_EXPORT void ng_set_listen(struct netengine *net, int id, fonlisten *pfonlisten, fonaccept *pfonaccept, fonerror *pfonerror);

	// 设置接收回调函数
	DLL_EXPORT void ng_set_recv(struct netengine *net, int id, fonrecv *pfonrecv, fonclose *pfonclose, fonerror *pfonerror);

	// 建立连接
	DLL_EXPORT int ng_connect(struct netengine *pstnetengine, const char *phost, int port);

	// 设置建立连接回调函数
	DLL_EXPORT void ng_set_connect(struct netengine *net, int id, fonrecv *pfonrecv, fonconnect *pfonconnect, 
		fonclose *pfonclose, fonerror *pfonerror);

	// 设置用户数
	DLL_EXPORT void ng_set_userdata(struct netengine *net, int id, void *puser);

	typedef void (ffreedata)(void *pdata);

	/* 发送数据，不在网络线程时调用此函数，pdata必须会调用pfree来释放，如果pfree为null,则调用free(),
	如果设置了ng_set_malloc，则会调用相应的函数来释放
	发送失败会调用onerror */
	DLL_EXPORT void ng_sendto(struct netengine *net, int id, char *pdata, int idatalen, ffreedata *pfree);

	// 发送数据，当在网络线程时调用此函数，发送失败会调用onerror
	DLL_EXPORT int ng_send(struct netengine *net, int id, void *pdata, int idatalen, bool needfree, ffreedata *pfree);

	// 设置发送和接收是否更新有效时间，超过多少秒关闭连接，会记录有效时间，但不会关闭连接
	DLL_EXPORT void ng_set_valid_time(struct netengine *net, int id, bool send, bool recv, int close_time);

	// 设置缓冲区参数, 默认def_recv,def_send:5K,Max_send:10M
	DLL_EXPORT void ng_set_buf(struct netengine *net, int id, int def_recv, int max_recv, int max_send);

	// 在主线程中运行一个函数，可以用另外一个线程来实现简单的定时器
	typedef void (pfrunfunc)(struct netengine *net, void *user);
	DLL_EXPORT void ng_run_func(struct netengine *net, pfrunfunc *pfrun, void *user);

	// 获取连接的有效时间，只能在网络线程调用
	DLL_EXPORT int64 ng_get_valid_time(struct netengine *net, int id);

	// 获取对方地址，只有connect,accept的连接有效，只能在网络线程调用
	DLL_EXPORT bool ng_get_remote_addr(struct netengine *net, int id, char *host, int hostbuflen, int *port);

	/* 先关闭全部连接，再等上两秒调用ng_break，可保证大都数情况下，内存全部释放，在调试模式下可用，
	如果不关心这个，可直接用ng_break，退出循环*/
	DLL_EXPORT void ng_close_all(struct netengine *net);

	// get time millisec，使用缓存中的时间，加快性能
	DLL_EXPORT int64 ng_get_time(struct netengine *net);

#ifdef __cplusplus
}
#endif

#endif
