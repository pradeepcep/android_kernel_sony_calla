#ifndef _SECTRACE_H
#define _SECTRACE_H


enum interface_type {
	if_tci = 0,
	if_dci,
};

enum usage_type {
	usage_tl = 0,
	usage_dr,
	usage_tl_dr,
};

typedef int (*callback_tl_map)(void *va, size_t size);
typedef int (*callback_tl_unmap)(void *va, size_t size);
typedef int (*callback_tl_transact)(void);

typedef int (*callback_dr_map)(unsigned long pa, size_t size);
typedef int (*callback_dr_unmap)(unsigned long pa, size_t size);
typedef int (*callback_dr_transact)(void);

struct callback_tl_func {
	callback_tl_map map;
	callback_tl_unmap unmap;
	callback_tl_transact transact;
};

struct callback_dr_func {
	callback_dr_map map;
	callback_dr_unmap unmap;
	callback_dr_transact transact;
};

union callback_func {
	struct callback_tl_func tl;
	struct callback_dr_func dr;
};


#if defined(CONFIG_MTK_SEC_TRACE)

extern int init_sectrace(const char *name, enum interface_type ci, enum usage_type usage, size_t size, union callback_func *callback);
extern int deinit_sectrace(const char *name);

#else

static inline int init_sectrace(const char *name, enum interface_type ci, enum usage_type usage, size_t size, union callback_func *callback)
{
	return -ENODEV;
}

static inline int deinit_sectrace(const char *name)
{
	return -ENODEV;
}

#endif

#endif /* _SECTRACE_H */
