#ifndef _SECTRACE_PROXY_H
#define _SECTRACE_PROXY_H


#include <linux/types.h>


extern int proxy_map(uint32_t driver_id, unsigned long pa, size_t size);
extern int proxy_unmap(uint32_t driver_id, unsigned long pa, size_t size);
extern int proxy_transact(uint32_t driver_id);
extern void proxy_deinit(void);


#endif /* _SECTRACE_PROXY_H */
