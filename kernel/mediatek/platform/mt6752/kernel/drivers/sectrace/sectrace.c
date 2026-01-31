#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <linux/irqreturn.h>
#include <mach/mt_cpuxgpt.h>

#include <mobicore_driver_api.h>

#include <linux/sectrace.h>
#include "sectrace_priv.h"
#include "proxy.h"



//#define SECTRACE_CONT_MEM_UNCACHED
//#define SECTRACE_FORCE_MAP_ALL_FOR_TL


static const int sectrace_log_on = 1;
#define SECTRACE_LOG(fmt, arg...) \
	do { \
		if (sectrace_log_on) pr_debug("SECTRACE:%s(): "fmt"\n", __func__, ##arg); \
	} while (0)

#define SECTRACE_MSG(fmt, arg...) \
	pr_info("SECTRACE:%s(): "fmt"\n", __func__, ##arg)

#define SECTRACE_WARNING(fmt, arg...) \
	pr_warning("SECTRACE:%s(): "fmt"\n", __func__, ##arg)

#define SECTRACE_ERR(fmt, arg...) \
	pr_err("SECTRACE:%s(): "fmt"\n", __func__, ##arg)


#define SECTRACE_TYPE_DRV		(0x00)
#define SECTRACE_TYPE_TLC		(0x01)
#define SECTRACE_TYPE_PROXY		(0x02)
#define SECTRACE_MASK_TYPE		(0x03)

#define SECTRACE_IF_TCI			(0x00 << 8)
#define SECTRACE_IF_DCI			(0x01 << 8)
#define SECTRACE_MASK_INTERFACE	(0x01 << 8)

#define SECTRACE_USAGE_TL		(0x01 << 16)
#define SECTRACE_USAGE_DR		(0x02 << 16)


struct sectrace_management {
	struct dentry *root_dir;
	struct dentry *ctl_file;

	struct mutex lock;
	struct list_head nodes;

	int inited;
};

enum sectrace_node_state {
	state_enabled = 0,
	state_disabled,
	state_running,
};

enum sectrace_malloc_type {
	type_cont = 0, /* continuous physical memory */
	type_vmalloc,
};

struct sectrace_node {
	struct list_head list;
	struct mutex lock;
	int active;
	char name[32];
	unsigned int flag;
	size_t size;
	int pid; /* fake process id */
	int tid; /* fake thread id */
	void *va; /* kernel virtual address of log buffer */
	unsigned long pa; /* physical address of log buffer */
	enum sectrace_malloc_type malloc_type; /* free buffer with correct function (kfree/vfree) */
	enum sectrace_node_state state;
	int timestamp_diff_sign; /* negtive: NWd<SWd, zero: NWd=SWd, positive: NWd>SWd */
	uint64_t timestamp_diff_value;
	struct dentry *dir;
	struct dentry *ctl_file; /* control (write only) */
	struct dentry *state_file; /* state (read only [writable for TLC]) */
	struct dentry *dump_file; /* dump */
	struct dentry *size_file; /* size (log buffer size) */
	struct dentry *pid_file; /* pid (echo "pid:tid" > pid) */
};

struct sectrace_drv_node {
	struct sectrace_node base;
	union callback_func callback;
};

struct sectrace_tlc_node {
	struct sectrace_node base;
	wait_queue_head_t wq;
	enum sectrace_tlc_event event;
	unsigned int event_counter;
	struct dentry *buffer_file; /* buffer (alloc/free/map) */
	struct dentry *event_file; /* event (read by TLC) */
};

struct sectrace_proxy_node {
	struct sectrace_node base;
	unsigned int driver_id;
	struct dentry *drvid_file; /* driver id (for secure driver) */
};


static struct sectrace_management sectrace_mgt = {
	.inited = 0,
};


static int sectrace_get_timestamp_diff(uint64_t *diff)
{
	unsigned long long normal_timestamp;
	unsigned long long secure_timestamp;
	u64 secure_counter;
	int sign = 0;

	normal_timestamp = sched_clock(); /* unit: ns */
	secure_counter = localtimer_get_phy_count(); /* uint: counter of 13MHz clock */

	do_div(normal_timestamp, 1000); /* translate from ns to us */
	secure_timestamp = (uint64_t)secure_counter;
	do_div(secure_timestamp, 13); /* translate from counter to us */

	/* secure should bigger than normal */
	if (normal_timestamp > secure_timestamp) {
		*diff = normal_timestamp - secure_timestamp;
		sign = 1;
	} else if (normal_timestamp < secure_timestamp) {
		*diff = secure_timestamp - normal_timestamp;
		sign = -1;
	} else {
		*diff = 0;
		sign = 0;
	}

	return sign;
}

static int sectrace_alloc_cont_mem(struct sectrace_node *node)
{
	void *vaddr;
	size_t npages;
	struct page **pages;
	struct page *page;
	size_t i;
	struct page *p;
	unsigned int order;
	pgprot_t pgprot;

	npages = PAGE_ALIGN(node->size) / PAGE_SIZE;

	pages = (struct page **)vmalloc(sizeof(struct page *) * npages);

	order = get_order(node->size);

	page = alloc_pages(GFP_KERNEL, order);
	if (NULL == page) {
		vfree(pages);
		node->va = NULL;
		return -ENOMEM;
	}

	for (i = 0, p = page; i < npages; i++, p++)
		pages[i] = p;

#ifdef SECTRACE_CONT_MEM_UNCACHED
	pgprot = pgprot_writecombine(PAGE_KERNEL);
#else
	pgprot = PAGE_KERNEL;
#endif

	vaddr = vmap(pages, npages, VM_MAP, pgprot);
	if (vaddr == NULL) {
		__free_pages(page, order);
		vfree(pages);
		node->va = NULL;
		return -ENOMEM;
	}

	vfree(pages);

	split_page(page, order);

	node->va = vaddr;
	node->pa = page_to_phys(page);

	/* clear memory */
	memset(node->va, 0, node->size);

	return 0;
}

static int sectrace_free_cont_mem(struct sectrace_node *node)
{
	size_t npages;
	struct page *page;

	if (NULL == node->va)
		return -EINVAL;

	vunmap(node->va);
	node->va = NULL;

	npages = 1 << get_order(node->size);
	page = phys_to_page(node->pa);

	while (npages > 0) {
		__free_page(page);
		page++;
		npages--;
	}

	node->pa = 0;

	return 0;
}

static void sectrace_make_tl_transaction(struct sectrace_node *node, uint32_t cmd, unsigned long param0, unsigned long param1)
{
	volatile struct sectrace_transaction *transaction = (struct sectrace_transaction *)node->va;

	transaction->cmd = cmd;
	transaction->ret = 0;
	transaction->param0 = param0;
	transaction->param1 = param1;
}

static void sectrace_make_dr_transaction(struct sectrace_node *node, uint32_t cmd, unsigned long param0, unsigned long param1)
{
	volatile struct sectrace_transaction *transaction = (struct sectrace_transaction *)(node->va + SIZE_LOG_ITEM);

	transaction->cmd = cmd;
	transaction->ret = 0;
	transaction->param0 = param0;
	transaction->param1 = param1;
}

static inline int sectrace_drv_transact(struct sectrace_drv_node *drv_node)
{
	int ret;

	if (SECTRACE_IF_TCI == (drv_node->base.flag & SECTRACE_MASK_INTERFACE))
		ret = drv_node->callback.tl.transact();
	else if (SECTRACE_IF_DCI == (drv_node->base.flag & SECTRACE_MASK_INTERFACE))
		ret = drv_node->callback.dr.transact();
	else {
		SECTRACE_MSG("connect interface invalid, exit");
		ret = -EPERM;
	}

	return ret;
}

static inline void sectrace_set_log_curr_item(struct sectrace_node *node, unsigned long curr_item)
{
	volatile struct sectrace_log_header *header = (struct sectrace_log_header *)(node->va + (SIZE_LOG_ITEM * NUM_TRANSACTION_ITEM));
	header->curr_item = curr_item;
}

static inline unsigned long sectrace_get_log_curr_item(struct sectrace_node *node)
{
	volatile struct sectrace_log_header *header = (struct sectrace_log_header *)(node->va + (SIZE_LOG_ITEM * NUM_TRANSACTION_ITEM));
	return header->curr_item;
}

static inline size_t sectrace_get_log_item_num(struct sectrace_node *node) {
	return ((node->size / SIZE_LOG_ITEM) - BEGIN_LOG_ITEM);
}

static struct sectrace_log_item *sectrace_get_log_item(struct sectrace_node *node, unsigned long pos)
{
	size_t num = sectrace_get_log_item_num(node);
	unsigned long curr_item = sectrace_get_log_curr_item(node);
	volatile struct sectrace_log_item *item;

	if (pos >= num)
		return NULL;

	item = (volatile struct sectrace_log_item *)(node->va + ((curr_item + BEGIN_LOG_ITEM) * SIZE_LOG_ITEM));
	if (0 != item->timestamp) {
		pos += curr_item;
		if (pos >= num)
			pos -= num;
	}

	return (struct sectrace_log_item *)(node->va + ((pos + BEGIN_LOG_ITEM) * SIZE_LOG_ITEM));
}

static void sectrace_clear_log(struct sectrace_node *node)
{
	void *p = node->va + (BEGIN_LOG_ITEM * SIZE_LOG_ITEM);
	size_t size = node->size - (BEGIN_LOG_ITEM * SIZE_LOG_ITEM);
	memset(p, 0, size);
}

static int sectrace_drv_enable(struct sectrace_drv_node *drv_node)
{
	struct sectrace_node *node = (struct sectrace_node *)drv_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled != node->state) {
		SECTRACE_MSG("node(%s) is enabled, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	if (node->size < KB2BYTE(SECTRACE_BUF_SIZE_MIN)) {
		SECTRACE_MSG("size invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (node->flag & SECTRACE_USAGE_DR) {
		/* secure driver will use log buffer, continuous physical address needed */
		node->malloc_type = type_cont;
		sectrace_alloc_cont_mem(node);
		if (NULL == node->va) {
			SECTRACE_ERR("no memory for log buffer");
			mutex_unlock(&node->lock);
			return -ENOMEM;
		}
	} else if (node->flag & SECTRACE_USAGE_TL) {
		/* only trustlet use */
		node->malloc_type = type_vmalloc;
		node->pa = 0; /* useless */
		node->va = vmalloc_user(node->size);
		if (NULL == node->va) {
			/* try again with kmalloc */
			node->malloc_type = type_cont;
			sectrace_alloc_cont_mem(node);
			if (NULL == node->va) {
				SECTRACE_ERR("no memory for log buffer");
				mutex_unlock(&node->lock);
				return -ENOMEM;
			}
		}
	} else {
		BUG();
		SECTRACE_MSG("usage invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* map the log buffer to secure world */
	if (SECTRACE_IF_TCI == (node->flag & SECTRACE_MASK_INTERFACE)) {
		size_t size;

		/* map buffer to TL */
		size = node->size;
#ifndef SECTRACE_FORCE_MAP_ALL_FOR_TL
		if (!(node->flag & SECTRACE_USAGE_TL)) {
			/* TL won't use the buffer, only map transaction header (2*32Byte) */
			size = SIZE_LOG_ITEM * 2;
		}
#endif

		ret = drv_node->callback.tl.map(node->va, size);
		if (ret) {
			SECTRACE_ERR("map va for TL failed, exit");
			goto err_free;
		}

		/* map buffer to DR if needed */
		if (node->flag & SECTRACE_USAGE_DR) {
			sectrace_make_tl_transaction(node, CMD_TL_MAP_PA, (unsigned long)(node->pa), (unsigned long)(node->size));
			sectrace_make_dr_transaction(node, CMD_DR_NONE, 0, 0);
			ret = drv_node->callback.tl.transact();
			if (ret) {
				drv_node->callback.tl.unmap(node->va, size);
				SECTRACE_ERR("map pa transaction for DR failed, exit");
				goto err_free;
			}
		}
	} else if (SECTRACE_IF_DCI == (node->flag & SECTRACE_MASK_INTERFACE)) {
		/* map buffer to DR */
		ret = drv_node->callback.dr.map(node->pa, node->size);
		if (ret) {
			SECTRACE_ERR("map pa for DR failed, exit");
			goto err_free;
		}
	} else {
		SECTRACE_MSG("connect interface invalid, exit");
		ret = -EPERM;
		goto err_free;
	}

	sectrace_set_log_curr_item(node, 0);
	node->state = state_enabled;

	mutex_unlock(&node->lock);
	return 0;

err_free:
	if (NULL != node->va) {
		if (type_cont == node->malloc_type)
			sectrace_free_cont_mem(node);
		else if (type_vmalloc == node->malloc_type) {
			vfree(node->va);
			node->va = NULL;
			node->pa = 0;
		}
	}

	mutex_unlock(&node->lock);
	return ret;
}

static int sectrace_drv_disable(struct sectrace_drv_node *drv_node)
{
	struct sectrace_node *node = (struct sectrace_node *)drv_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled == node->state) {
		SECTRACE_MSG("node(%s) is disabled, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	/* check va */
	if (NULL == node->va) {
		BUG();
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* let secure world unmap log buffer */
	if (SECTRACE_IF_TCI == (node->flag & SECTRACE_MASK_INTERFACE)) {
		size_t size;

		/* unmap buffer for DR if needed */
		if (node->flag & SECTRACE_USAGE_DR) {
			sectrace_make_tl_transaction(node, CMD_TL_UNMAP_PA, (unsigned long)(node->pa), (unsigned long)(node->size));
			sectrace_make_dr_transaction(node, CMD_DR_NONE, 0, 0);
			ret = drv_node->callback.tl.transact();
			if (ret) {
				SECTRACE_ERR("unmap pa transaction for DR failed, exit");
				mutex_unlock(&node->lock);
				return ret;
			}
		}

		/* unmap buffer for TL */
		size = node->size;
#ifndef SECTRACE_FORCE_MAP_ALL_FOR_TL
		if (!(node->flag & SECTRACE_USAGE_TL)) {
			/* TL won't use the buffer, only map transaction header (2*32Byte) */
			size = SIZE_LOG_ITEM * 2;
		}
#endif

		ret = drv_node->callback.tl.unmap(node->va, size);
		if (ret) {
			SECTRACE_ERR("unmap va for TL failed, exit");
			mutex_unlock(&node->lock);
			return ret;
		}
	} else if (SECTRACE_IF_DCI == (node->flag & SECTRACE_MASK_INTERFACE)) {
		/* unmap buffer for DR */
		ret = drv_node->callback.dr.unmap(node->pa, node->size);
		if (ret) {
			SECTRACE_ERR("unmap pa for DR failed, exit");
			mutex_unlock(&node->lock);
			return ret;
		}
	} else {
		SECTRACE_MSG("connect interface invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* free log buffer */
	if (type_cont == node->malloc_type)
		sectrace_free_cont_mem(node);
	else if (type_vmalloc == node->malloc_type) {
		vfree(node->va);
		node->va = NULL;
		node->pa = 0;
	}

	node->state = state_disabled;

	mutex_unlock(&node->lock);
	return 0;
}

static int sectrace_drv_start(struct sectrace_drv_node *drv_node)
{
	struct sectrace_node *node = (struct sectrace_node *)drv_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled == node->state) {
		SECTRACE_ERR("node(%s) is disabled, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	} else if (state_running == node->state) {
		SECTRACE_MSG("node(%s) is running, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	/* make TL cmd */
	if (node->flag & SECTRACE_USAGE_TL)
		sectrace_make_tl_transaction(node, CMD_TL_START, 0, 0);
	else
		sectrace_make_tl_transaction(node, CMD_TL_NONE, 0, 0);
	/* make DR cmd */
	if (node->flag & SECTRACE_USAGE_DR)
		sectrace_make_dr_transaction(node, CMD_DR_START, 0, 0);
	else
		sectrace_make_dr_transaction(node, CMD_DR_NONE, 0, 0);

	/* transact */
	ret = sectrace_drv_transact(drv_node);
	if (ret) {
		SECTRACE_ERR("transact failed(%d)", ret);
		mutex_unlock(&node->lock);
		return ret;
	}

	/* modify state */
	node->state = state_running;

	/* reset timestamp difference */
	node->timestamp_diff_sign = 0;
	node->timestamp_diff_value = 0;

	mutex_unlock(&node->lock);
	return 0;
}

static int sectrace_drv_stop(struct sectrace_drv_node *drv_node, int reset_curr_item)
{
	struct sectrace_node *node = (struct sectrace_node *)drv_node;
	enum sectrace_node_state state;
	int ret = 0;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	state = node->state;
	switch (state) {
	case state_running:
		/* make TL cmd */
		if (node->flag & SECTRACE_USAGE_TL)
			sectrace_make_tl_transaction(node, CMD_TL_STOP, 0, 0);
		else
			sectrace_make_tl_transaction(node, CMD_TL_NONE, 0, 0);
		/* make DR cmd */
		if (node->flag & SECTRACE_USAGE_DR)
			sectrace_make_dr_transaction(node, CMD_DR_STOP, 0, 0);
		else
			sectrace_make_dr_transaction(node, CMD_DR_NONE, 0, 0);

		/* transact */
		ret = sectrace_drv_transact(drv_node);
		if (ret) {
			SECTRACE_ERR("transact failed(%d)", ret);
			mutex_unlock(&node->lock);
			return ret;
		}

		/* pause or stop, get timestamp difference */
		node->timestamp_diff_sign = sectrace_get_timestamp_diff(&node->timestamp_diff_value);

		/* modify state */
		node->state = state_enabled;

		/* no break, through */

	case state_enabled:
		/* paused or stoped */
		if (reset_curr_item) {
			/* only reset curr_item in log header, transaction isn't needed */
			sectrace_set_log_curr_item(node, 0);
			sectrace_clear_log(node);
		}
		break;

	case state_disabled:
		SECTRACE_MSG("node(%s) is disabled(%d), exit", node->name, node->state);
		ret = -EPERM;
		break;

	default:
		SECTRACE_ERR("state of node(%s) invalid(%d)", node->name, node->state);
		ret = -EPERM;
		break;
	}

	mutex_unlock(&node->lock);
	return ret;
}

static int sectrace_tlc_alloc(struct sectrace_tlc_node *tlc_node, struct sectrace_tlc_buf_alloc *alloc_ret)
{
	struct sectrace_node *node = (struct sectrace_node *)tlc_node;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (NULL != node->va) {
		SECTRACE_MSG("node(%s) va of buffer isn't NULL, exit", node->name);
		mutex_unlock(&node->lock);
		return -EEXIST;
	}

	if (node->size < KB2BYTE(SECTRACE_BUF_SIZE_MIN)) {
		SECTRACE_MSG("size invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (node->flag & SECTRACE_USAGE_DR) {
		/* secure driver will use log buffer, continuous physical address needed */
		node->malloc_type = type_cont;
		sectrace_alloc_cont_mem(node);
		if (NULL == node->va) {
			SECTRACE_ERR("no memory for log buffer");
			mutex_unlock(&node->lock);
			return -ENOMEM;
		}
	} else if (node->flag & SECTRACE_USAGE_TL) {
		/* only trustlet use */
		node->malloc_type = type_vmalloc;
		node->pa = 0; /* useless */
		node->va = vmalloc_user(node->size);
		if (NULL == node->va) {
			/* try again with kmalloc */
			node->malloc_type = type_cont;
			sectrace_alloc_cont_mem(node);
			if (NULL == node->va) {
				SECTRACE_ERR("no memory for log buffer");
				mutex_unlock(&node->lock);
				return -ENOMEM;
			}
		}
	} else {
		SECTRACE_MSG("usage invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	sectrace_set_log_curr_item(node, 0);

	if (NULL != alloc_ret) {
		alloc_ret->pa = node->pa;
		alloc_ret->size = node->size;
	}

	mutex_unlock(&node->lock);
	return 0;
}

static int sectrace_tlc_free(struct sectrace_tlc_node *tlc_node)
{
	struct sectrace_node *node = (struct sectrace_node *)tlc_node;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (NULL == node->va) {
		SECTRACE_MSG("node(%s) va of buffer is freed, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (type_cont == node->malloc_type)
		sectrace_free_cont_mem(node);
	else if (type_vmalloc == node->malloc_type) {
		vfree(node->va);
		node->va = NULL;
		node->pa = 0;
	}

	mutex_unlock(&node->lock);
	return 0;
}

static inline int sectrace_tlc_event_notify(struct sectrace_tlc_node *tlc_node, enum sectrace_tlc_event event)
{
	struct sectrace_node *node = (struct sectrace_node *)tlc_node;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	tlc_node->event = event;
	tlc_node->event_counter++;

	mutex_unlock(&node->lock);

	wake_up(&tlc_node->wq);

	return 0;
}

static int sectrace_tlc_enable(struct sectrace_tlc_node *tlc_node)
{
	return sectrace_tlc_event_notify(tlc_node, event_enable);
}

static int sectrace_tlc_disable(struct sectrace_tlc_node *tlc_node)
{
	return sectrace_tlc_event_notify(tlc_node, event_disable);
}

static int sectrace_tlc_start(struct sectrace_tlc_node *tlc_node)
{
	return sectrace_tlc_event_notify(tlc_node, event_start);
}

static int sectrace_tlc_pause(struct sectrace_tlc_node *tlc_node)
{
	return sectrace_tlc_event_notify(tlc_node, event_pause);
}

static int sectrace_tlc_stop(struct sectrace_tlc_node *tlc_node)
{
	return sectrace_tlc_event_notify(tlc_node, event_stop);
}

static int sectrace_proxy_enable(struct sectrace_proxy_node *proxy_node)
{
	struct sectrace_node *node = (struct sectrace_node *)proxy_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled != node->state) {
		SECTRACE_MSG("node(%s) is enabled, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	/* check driver id */
	if (0 == proxy_node->driver_id) {
		SECTRACE_MSG("driver id isn't set for node(%s), exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (node->size < KB2BYTE(SECTRACE_BUF_SIZE_MIN)) {
		SECTRACE_MSG("size invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if ((!(node->flag & SECTRACE_USAGE_DR)) || (node->flag & SECTRACE_USAGE_TL)) {
		BUG();
		SECTRACE_MSG("usage invalid, exit");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* secure driver will use log buffer, continuous physical address needed */
	node->malloc_type = type_cont;
	sectrace_alloc_cont_mem(node);
	if (NULL == node->va) {
		SECTRACE_ERR("no memory for log buffer");
		mutex_unlock(&node->lock);
		return -ENOMEM;
	}

	/* map the log buffer to secure driver through proxy tl */
	ret = proxy_map(proxy_node->driver_id, node->pa, node->size);
	if (ret) {
		SECTRACE_ERR("map pa failed(%d)", ret);
		goto err_free;
	}

	sectrace_set_log_curr_item(node, 0);
	node->state = state_enabled;

	mutex_unlock(&node->lock);
	return 0;

err_free:
	if (NULL != node->va)
		sectrace_free_cont_mem(node);

	mutex_unlock(&node->lock);
	return ret;
}

static int sectrace_proxy_disable(struct sectrace_proxy_node *proxy_node)
{
	struct sectrace_node *node = (struct sectrace_node *)proxy_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled == node->state) {
		SECTRACE_MSG("node(%s) is disabled, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	/* check va */
	if (NULL == node->va) {
		BUG();
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* unmap the log buffer in secure driver through proxy tl */
	ret = proxy_unmap(proxy_node->driver_id, node->pa, node->size);
	if (ret) {
		SECTRACE_ERR("unmap pa failed(%d)", ret);
		mutex_unlock(&node->lock);
		return ret;
	}

	/* free log buffer */
	sectrace_free_cont_mem(node);

	node->state = state_disabled;

	mutex_unlock(&node->lock);
	return 0;
}

static int sectrace_proxy_start(struct sectrace_proxy_node *proxy_node)
{
	struct sectrace_node *node = (struct sectrace_node *)proxy_node;
	int ret = -EPERM;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	if (state_disabled == node->state) {
		SECTRACE_ERR("node(%s) is disabled, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	} else if (state_running == node->state) {
		SECTRACE_MSG("node(%s) is running, exit", node->name);
		mutex_unlock(&node->lock);
		return 0;
	}

	/* make DR cmd */
	sectrace_make_dr_transaction(node, CMD_DR_START, 0, 0);

	/* transact */
	ret = proxy_transact(proxy_node->driver_id);
	if (ret) {
		SECTRACE_ERR("transact failed(%d)", ret);
		mutex_unlock(&node->lock);
		return ret;
	}

	/* modify state */
	node->state = state_running;

	/* reset timestamp difference */
	node->timestamp_diff_sign = 0;
	node->timestamp_diff_value = 0;

	mutex_unlock(&node->lock);
	return 0;
}

static int sectrace_proxy_stop(struct sectrace_proxy_node *proxy_node, int reset_curr_item)
{
	struct sectrace_node *node = (struct sectrace_node *)proxy_node;
	enum sectrace_node_state state;
	int ret = 0;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* check currernt state */
	state = node->state;
	switch (state) {
	case state_running:
		/* make DR cmd */
		sectrace_make_dr_transaction(node, CMD_DR_STOP, 0, 0);

		/* transact */
		ret = proxy_transact(proxy_node->driver_id);
		if (ret) {
			SECTRACE_ERR("transact failed(%d)", ret);
			mutex_unlock(&node->lock);
			return ret;
		}

		/* pause or stop, get timestamp difference */
		node->timestamp_diff_sign = sectrace_get_timestamp_diff(&node->timestamp_diff_value);

		/* modify state */
		node->state = state_enabled;

		/* no break, through */

	case state_enabled:
		/* paused or stoped */
		if (reset_curr_item) {
			/* only reset curr_item in log header, transaction isn't needed */
			sectrace_set_log_curr_item(node, 0);
			sectrace_clear_log(node);
		}
		break;

	case state_disabled:
		SECTRACE_MSG("node(%s) is disabled(%d), exit", node->name, node->state);
		ret = -EPERM;
		break;

	default:
		SECTRACE_ERR("state of node(%s) invalid(%d)", node->name, node->state);
		ret = -EPERM;
		break;
	}

	mutex_unlock(&node->lock);
	return ret;
}

static int sectrace_enable(struct sectrace_node *node)
{
	int ret;

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_drv_enable((struct sectrace_drv_node *)node);
		break;
	case SECTRACE_TYPE_TLC:
		ret = sectrace_tlc_enable((struct sectrace_tlc_node *)node);
		break;
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_proxy_enable((struct sectrace_proxy_node *)node);
		break;
	default:
		SECTRACE_ERR("type of node(%s) invalid(%u)", node->name, node->flag);
		ret = -EPERM;
		break;
	}

	return ret;
}

static int sectrace_disable(struct sectrace_node *node)
{
	int ret;

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_drv_disable((struct sectrace_drv_node *)node);
		break;
	case SECTRACE_TYPE_TLC:
		ret = sectrace_tlc_disable((struct sectrace_tlc_node *)node);;
		break;
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_proxy_disable((struct sectrace_proxy_node *)node);
		break;
	default:
		SECTRACE_ERR("type of node(%s) invalid(%u)", node->name, node->flag);
		ret = -EPERM;
		break;
	}

	return ret;
}

static int sectrace_start(struct sectrace_node *node)
{
	int ret;

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_drv_start((struct sectrace_drv_node *)node);
		break;
	case SECTRACE_TYPE_TLC:
		ret = sectrace_tlc_start((struct sectrace_tlc_node *)node);;
		break;
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_proxy_start((struct sectrace_proxy_node *)node);
		break;
	default:
		SECTRACE_ERR("type of node(%s) invalid(%u)", node->name, node->flag);
		ret = -EPERM;
		break;
	}

	return ret;
}

static int sectrace_pause(struct sectrace_node *node)
{
	int ret;

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_drv_stop((struct sectrace_drv_node *)node, 0);
		break;
	case SECTRACE_TYPE_TLC:
		ret = sectrace_tlc_pause((struct sectrace_tlc_node *)node);;
		break;
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_proxy_stop((struct sectrace_proxy_node *)node, 0);
		break;
	default:
		SECTRACE_ERR("type of node(%s) invalid(%u)", node->name, node->flag);
		ret = -EPERM;
		break;
	}

	return ret;
}

static int sectrace_stop(struct sectrace_node *node)
{
	int ret;

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_drv_stop((struct sectrace_drv_node *)node, 1);
		break;
	case SECTRACE_TYPE_TLC:
		ret = sectrace_tlc_stop((struct sectrace_tlc_node *)node);;
		break;
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_proxy_stop((struct sectrace_proxy_node *)node, 1);
		break;
	default:
		SECTRACE_ERR("type of node(%s) invalid(%u)", node->name, node->flag);
		ret = -EPERM;
		break;
	}

	return ret;
}

static ssize_t sectrace_node_ctl_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	char buf[16];
	size_t buf_size;
	int ret;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	if (!strcasecmp(buf, "enable")) {
		ret = sectrace_enable(node);
	} else if (!strcasecmp(buf, "disable")) {
		ret = sectrace_disable(node);
	} else if (!strcasecmp(buf, "start")) {
		ret = sectrace_start(node);
	} else if (!strcasecmp(buf, "pause")) {
		ret = sectrace_pause(node);
	} else if (!strcasecmp(buf, "stop")) {
		ret = sectrace_stop(node);
	} else {
		/* invalid string */
		SECTRACE_ERR("%s: control string invalid (%s)", node->name, buf);
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	return buf_size;
}

static const struct file_operations debugfs_sectrace_node_ctl_operations = {
	.open = simple_open,
	.write = sectrace_node_ctl_write,
	.llseek = no_llseek,
};

static ssize_t sectrace_node_state_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	char buf[16];

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	switch (node->state) {
	case state_enabled:
		snprintf(buf, sizeof(buf), "enabled\n");
		break;
	case state_running:
		snprintf(buf, sizeof(buf), "running\n");
		break;
	case state_disabled:
	default:
		snprintf(buf, sizeof(buf), "disabled\n");
		break;
	}

	mutex_unlock(&node->lock);

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t sectrace_node_state_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	char buf[16];
	size_t buf_size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size)) {
		return -EFAULT;
	}

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	/* only TLC node can write state */
	if (SECTRACE_TYPE_TLC != (node->flag & SECTRACE_MASK_TYPE)) {
		SECTRACE_MSG("only TLC can write state");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (!strcasecmp(buf, "enabled")) {
		if (state_running == node->state) {
			/* pause or stop, get timestamp difference */
			node->timestamp_diff_sign = sectrace_get_timestamp_diff(&node->timestamp_diff_value);
		}
		node->state = state_enabled;
	} else if (!strcasecmp(buf, "disabled")) {
		node->state = state_disabled;
	} else if (!strcasecmp(buf, "running")) {
		node->state = state_running;
		/* reset timestamp difference */
		node->timestamp_diff_sign = 0;
		node->timestamp_diff_value = 0;
	} else {
		/* invalid string */
		SECTRACE_ERR("%s: state string invalid (%s)", node->name, buf);
		mutex_unlock(&node->lock);
		return -EINVAL;
	}

	mutex_unlock(&node->lock);

	return buf_size;
}

static const struct file_operations debugfs_sectrace_node_state_operations = {
	.open = simple_open,
	.read = sectrace_node_state_read,
	.write = sectrace_node_state_write,
	.llseek = default_llseek,
};

static void *sectrace_node_dump_seq_start(struct seq_file *m, loff_t *pos)
{
	struct sectrace_node *node = (struct sectrace_node *)(m->private);
	volatile struct sectrace_log_item *log_item;

	mutex_lock(&node->lock);

	if (!node->active)
		return ERR_PTR(-EPERM);

	if ((state_enabled != node->state) || (NULL == node->va))
		return ERR_PTR(-EPERM);

	log_item = sectrace_get_log_item(node, (unsigned long)(*pos));

	if ((NULL == log_item) || (0 == log_item->timestamp))
		return NULL;

	return pos;
}

static void *sectrace_node_dump_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct sectrace_node *node = (struct sectrace_node *)(m->private);
	volatile struct sectrace_log_item *log_item;

	(*pos)++;
	log_item = sectrace_get_log_item(node, (unsigned long)(*pos));

	if ((NULL == log_item) || (0 == log_item->timestamp))
		return NULL;

	return pos;
}

static void sectrace_node_dump_seq_stop(struct seq_file *m, void *v)
{
	struct sectrace_node *node = (struct sectrace_node *)(m->private);

	mutex_unlock(&node->lock);
}

static int sectrace_node_dump_seq_show(struct seq_file *m, void *v)
{
	struct sectrace_node *node = (struct sectrace_node *)(m->private);
	volatile struct sectrace_log_item *log_item;
	unsigned long pos = (unsigned long)(*(loff_t *)v);
	uint64_t second;
	uint32_t microsecond;

	log_item = sectrace_get_log_item(node, pos);

	if (node->timestamp_diff_sign > 0) /* NWd > SWd */
		second = log_item->timestamp + node->timestamp_diff_value;
	else if (node->timestamp_diff_sign < 0) /* NWd < SWd */
		second = log_item->timestamp - node->timestamp_diff_value;
	else /* NWd = SWd */
		second = log_item->timestamp;
	microsecond = do_div(second, 1000000);

	if (sectrace_begin == log_item->type) {
		seq_printf(m, "%16s-%-6d[000]%7llu.%06u: tracing_mark_write: B|%d|%s\n",
					node->name, node->tid, second, microsecond, node->pid, log_item->name);
	} else if (sectrace_end == log_item->type) {
		seq_printf(m, "%16s-%-6d[000]%7llu.%06u: tracing_mark_write: E\n",
					node->name, node->tid, second, microsecond);
	}

	return 0;
}

static const struct seq_operations sectrace_node_dump_seq_ops = {
	.start = sectrace_node_dump_seq_start,
	.next = sectrace_node_dump_seq_next,
	.stop = sectrace_node_dump_seq_stop,
	.show = sectrace_node_dump_seq_show,
};

static int sectrace_node_dump_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &sectrace_node_dump_seq_ops);
	if (!ret)
		((struct seq_file *)file->private_data)->private = inode->i_private;

	return ret;
}

static const struct file_operations debugfs_sectrace_node_dump_operations = {
	.open = sectrace_node_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static ssize_t sectrace_node_size_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	size_t size;
	char buf[16];

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	size = node->size;

	mutex_unlock(&node->lock);

	snprintf(buf, sizeof(buf), "%zd\n", BYTE2KB(size));

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t sectrace_node_size_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	char buf[16];
	size_t buf_size;
	size_t size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	if (strict_strtoul(buf, 10, (unsigned long*)&size)) {
		SECTRACE_ERR("%s: size string invalid (%s)", node->name, buf);
		return -EINVAL;
	}

	/* check size value */
	if (size < SECTRACE_BUF_SIZE_MIN) {
		SECTRACE_MSG("%s: set size should be more than %d (%s)", node->name, SECTRACE_BUF_SIZE_MIN, buf);
		size = SECTRACE_BUF_SIZE_MIN;
	}

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (state_disabled != node->state) {
		SECTRACE_MSG("%s: size only set when disabled", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}
	node->size = KB2BYTE(size);

	mutex_unlock(&node->lock);

	return buf_size;
}

static const struct file_operations debugfs_sectrace_node_size_operations = {
	.open = simple_open,
	.read = sectrace_node_size_read,
	.write = sectrace_node_size_write,
	.llseek = default_llseek,
};

static ssize_t sectrace_node_pid_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	int pid;
	int tid;
	char buf[32];

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	pid = node->pid;
	tid = node->tid;

	mutex_unlock(&node->lock);

	snprintf(buf, sizeof(buf), "%d:%d\n", pid, tid);

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t sectrace_node_pid_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	char buf[32];
	size_t buf_size;
	int pid;
	int tid;
	char *p_colon;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	p_colon = strchr(buf, ':');
	if (NULL == p_colon) {
		/* only pid, tid will equal to pid */
		if (kstrtoint(buf, 10, &pid)) {
			SECTRACE_ERR("%s: pid string invalid (%s)", node->name, buf);
			return -EINVAL;
		}
		tid = pid;
	} else {
		char *str_tid = p_colon + 1;
		*p_colon = '\0';
		if (kstrtoint(buf, 10, &pid)) {
			SECTRACE_ERR("%s: pid string invalid (%s)", node->name, buf);
			return -EINVAL;
		}
		/* skip colon to find tid */
		if (kstrtoint(str_tid, 10, &tid)) {
			SECTRACE_ERR("%s: tid string invalid (%s)", node->name, str_tid);
			return -EINVAL;
		}
	}

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	node->pid = pid;
	node->tid = tid;

	mutex_unlock(&node->lock);

	return buf_size;
}

static const struct file_operations debugfs_sectrace_node_pid_operations = {
	.open = simple_open,
	.read = sectrace_node_pid_read,
	.write = sectrace_node_pid_write,
	.llseek = default_llseek,
};

static long sectrace_node_buffer_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sectrace_tlc_node *tlc_node = (struct sectrace_tlc_node *)(file->private_data);
	void __user *argp = (void __user *)arg;
	struct sectrace_tlc_buf_alloc alloc_ret;
	int ret = 0;

	switch (cmd) {
	case MTK_SECTRACE_TLC_BUF_ALLOC:
		ret = sectrace_tlc_alloc(tlc_node, &alloc_ret);
		if (ret) {
			SECTRACE_ERR("alloc buffer for TLC node failed");
			return ret;
		}
		ret = copy_to_user(argp, &alloc_ret, sizeof(alloc_ret)) ? -EFAULT : 0;
		break;

	case MTK_SECTRACE_TLC_BUF_FREE:
		ret = sectrace_tlc_free(tlc_node);
		if (ret) {
			SECTRACE_ERR("free buffer for TLC node failed");
		}
		break;

	default:
		SECTRACE_ERR("unknown cmd (%u)", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int sectrace_node_buffer_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	int ret = 0;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (NULL == node->va) {
		SECTRACE_ERR("no buffer allocated, can not mmap");
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (type_cont == node->malloc_type) {
#ifdef SECTRACE_CONT_MEM_UNCACHED
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#endif
		if (0 != node->pa) {
			unsigned long uaddr = vma->vm_start;
			unsigned long usize = vma->vm_end - vma->vm_start;
			unsigned long paddr = node->pa + (vma->vm_pgoff << PAGE_SHIFT);

			do {
				struct page *page = phys_to_page(paddr);

				ret = vm_insert_page(vma, uaddr, page);
				if (ret) {
					SECTRACE_ERR("vm_insert_page failed(%d) [pa=0x%lx, size=%lu]", ret, paddr, usize);
					mutex_unlock(&node->lock);
					return ret;
				}

				uaddr += PAGE_SIZE;
				paddr += PAGE_SIZE;
				usize -= PAGE_SIZE;
			} while (usize > 0);
		} else {
			BUG();
		}
	} else if (type_vmalloc == node->malloc_type) {
		ret = remap_vmalloc_range(vma, node->va, vma->vm_pgoff);
		if (ret) {
			SECTRACE_ERR("remap_vmalloc_range failed(%d)", ret);
		}
	} else {
		BUG();
		ret = -EPERM;
	}

	mutex_unlock(&node->lock);
	return ret;
}

static const struct file_operations debugfs_sectrace_node_buffer_operations = {
	.open = simple_open,
	.unlocked_ioctl = sectrace_node_buffer_ioctl,
	.mmap = sectrace_node_buffer_mmap,
	.llseek = no_llseek,
};

static ssize_t sectrace_node_event_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	struct sectrace_tlc_node *tlc_node = (struct sectrace_tlc_node *)node;
	char buf[16];
	unsigned int curr_counter;
	enum sectrace_tlc_event event;
	loff_t pos = 0; /* not support pos */
	int err = 0;

	curr_counter = tlc_node->event_counter;
	err = wait_event_interruptible(tlc_node->wq, curr_counter != tlc_node->event_counter);
	if (err < 0)
		return err;

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	event = tlc_node->event;

	mutex_unlock(&node->lock);

	SECTRACE_LOG("event=%d", event);

	switch (event) {
	case event_enable:
		snprintf(buf, sizeof(buf), "enable");
		break;
	case event_disable:
		snprintf(buf, sizeof(buf), "disable");
		break;
	case event_start:
		snprintf(buf, sizeof(buf), "start");
		break;
	case event_pause:
		snprintf(buf, sizeof(buf), "pause");
		break;
	case event_stop:
		snprintf(buf, sizeof(buf), "stop");
		break;
	default:
		/* unknown event, try again */
		SECTRACE_WARNING("unknown event (%d)", event);
		return -EAGAIN;
	}

	return simple_read_from_buffer(user_buf, count, &pos, buf, strlen(buf));
}

static const struct file_operations debugfs_sectrace_node_event_operations = {
	.open = simple_open,
	.read = sectrace_node_event_read,
	.llseek = default_llseek,
};

static ssize_t sectrace_node_drvid_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	struct sectrace_proxy_node *proxy_node = (struct sectrace_proxy_node *)node;
	unsigned int driver_id;
	char buf[16];

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	driver_id = proxy_node->driver_id;

	mutex_unlock(&node->lock);

	snprintf(buf, sizeof(buf), "0x%08X\n", driver_id);

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t sectrace_node_drvid_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_node *node = (struct sectrace_node *)(file->private_data);
	struct sectrace_proxy_node *proxy_node = (struct sectrace_proxy_node *)node;
	char buf[16];
	size_t buf_size;
	unsigned int driver_id;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	if (kstrtou32(buf, 16, &driver_id)) {
		SECTRACE_ERR("%s: driver id string invalid (%s)", node->name, buf);
		return -EINVAL;
	}

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (state_disabled != node->state) {
		SECTRACE_MSG("%s: proxy driver id only set when disabled", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}
	proxy_node->driver_id = driver_id;

	mutex_unlock(&node->lock);

	return buf_size;
}

static const struct file_operations debugfs_sectrace_node_drvid_operations = {
	.open = simple_open,
	.read = sectrace_node_drvid_read,
	.write = sectrace_node_drvid_write,
	.llseek = default_llseek,
};


static int sectrace_check_flag(unsigned int flags)
{
	switch (flags & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		break;

	case SECTRACE_TYPE_TLC:
		break;

	case SECTRACE_TYPE_PROXY:
		if (SECTRACE_IF_DCI == (flags & SECTRACE_MASK_INTERFACE)) {
			SECTRACE_MSG("proxy only use TCI (flags=%u)", flags);
			return -EINVAL;
		}
		if (flags & SECTRACE_USAGE_TL) {
			SECTRACE_MSG("proxy only print DR's log (flags=%u)", flags);
			return -EINVAL;
		}
		break;
	default:
		SECTRACE_MSG("type invalid (flags=%u)", flags);
		return -EINVAL;
	}

	if (0 == (flags & (SECTRACE_USAGE_TL | SECTRACE_USAGE_DR))) {
		SECTRACE_MSG("usage invalid (flags=%u)", flags);
		return -EINVAL;
	}

	if ((SECTRACE_IF_DCI == (flags & SECTRACE_MASK_INTERFACE)) && (flags & SECTRACE_USAGE_TL)) {
		SECTRACE_MSG("TL's systrace can't be dump when using DCI (flags=%u)", flags);
		return -EINVAL;
	}

	return 0;
}

static struct sectrace_node *sectrace_find_node(struct sectrace_management *mgt, const char *name)
{
	struct sectrace_node *node;

	list_for_each_entry(node, &mgt->nodes, list) {
		if (!strncmp(node->name, name, sizeof(node->name))) {
			/* find the name in the node list */
			return node;
		}
	}

	return NULL;
}

static void sectrace_deinit_node(struct sectrace_node *node)
{
	if (NULL == node)
		return;

	if (NULL != node->pid_file) {
		debugfs_remove(node->pid_file);
		node->pid_file = NULL;
	}

	if (NULL != node->size_file) {
		debugfs_remove(node->size_file);
		node->size_file = NULL;
	}

	if (NULL != node->dump_file) {
		debugfs_remove(node->dump_file);
		node->dump_file = NULL;
	}

	if (NULL != node->state_file) {
		debugfs_remove(node->state_file);
		node->state_file = NULL;
	}

	if (NULL != node->ctl_file) {
		debugfs_remove(node->ctl_file);
		node->ctl_file = NULL;
	}

	if (NULL != node->dir) {
		debugfs_remove(node->dir);
		node->dir = NULL;
	}
}

static int sectrace_init_node(struct sectrace_management *mgt, struct sectrace_node *node, const char *name)
{
	strncpy(node->name, name, sizeof(node->name)-1);
	node->name[sizeof(node->name)-1] = '\0'; /* force to write terminating null byte at the last byte */

	node->dir = debugfs_create_dir(node->name, mgt->root_dir);
	if (NULL == node->dir) {
		SECTRACE_ERR("create node dir failed for '%s'", name);
		goto failed;
	}

	node->ctl_file = debugfs_create_file("ctl", 0222, node->dir, node, &debugfs_sectrace_node_ctl_operations);
	if (NULL == node->ctl_file) {
		SECTRACE_ERR("create ctl file failed for '%s'", name);
		goto failed;
	}

	node->state_file = debugfs_create_file("state", 0666, node->dir, node, &debugfs_sectrace_node_state_operations);
	if (NULL == node->state_file) {
		SECTRACE_ERR("create state file failed for '%s'", name);
		goto failed;
	}

	node->dump_file = debugfs_create_file("dump", 0444, node->dir, node, &debugfs_sectrace_node_dump_operations);
	if (NULL == node->dump_file) {
		SECTRACE_ERR("create dump file failed for '%s'", name);
		goto failed;
	}

	node->size_file = debugfs_create_file("size", 0666, node->dir, node, &debugfs_sectrace_node_size_operations);
	if (NULL == node->size_file) {
		SECTRACE_ERR("create size file failed for '%s'", name);
		goto failed;
	}

	node->pid_file = debugfs_create_file("pid", 0666, node->dir, node, &debugfs_sectrace_node_pid_operations);
	if (NULL == node->pid_file) {
		SECTRACE_ERR("create pid file failed for '%s'", name);
		goto failed;
	}

	INIT_LIST_HEAD(&node->list);
	node->flag = 0;
	node->size = KB2BYTE(SECTRACE_BUF_SIZE_MIN); /* KB -> Byte */
	node->pid = 0;
	node->tid = 0;
	node->va = NULL;
	node->pa = 0;
	node->state = state_disabled;
	node->timestamp_diff_sign = 0;
	node->timestamp_diff_value = 0;

	return 0;

failed:
	sectrace_deinit_node(node);

	return -EPERM;
}

static struct sectrace_drv_node *sectrace_create_drv_node(struct sectrace_management *mgt, const char *name)
{
	struct sectrace_drv_node *drv_node;

	drv_node = (struct sectrace_drv_node *)vzalloc(sizeof(struct sectrace_drv_node));
	if (NULL == drv_node) {
		SECTRACE_ERR("no memory for drv node");
		return NULL;
	}

	mutex_init(&(drv_node->base.lock));

	if (sectrace_init_node(mgt, (struct sectrace_node *)drv_node, name)) {
		SECTRACE_ERR("init drv node failed");
		vfree(drv_node);
		return NULL;
	}

	mutex_lock(&(drv_node->base.lock));

	/* init function pointer to NULL */
	memset(&drv_node->callback, 0, sizeof(union callback_func));

	drv_node->base.active = 1;

	mutex_unlock(&(drv_node->base.lock));

	return drv_node;
}

static int sectrace_destroy_drv_node(struct sectrace_management *mgt, struct sectrace_drv_node *drv_node)
{
	struct sectrace_node *node = (struct sectrace_node *)drv_node;

	/* force to disable node */
	sectrace_drv_disable(drv_node);

	mutex_lock(&node->lock);
	node->active = 0;
	mutex_unlock(&node->lock);

	/* deinit node */
	sectrace_deinit_node(node);

	/* remove node from list */
	mutex_lock(&node->lock);
	list_del(&node->list);
	mutex_unlock(&node->lock);

	vfree(drv_node);

	return 0;
}

static struct sectrace_tlc_node *sectrace_create_tlc_node(struct sectrace_management *mgt, const char *name)
{
	struct sectrace_tlc_node *tlc_node;

	tlc_node = (struct sectrace_tlc_node *)vzalloc(sizeof(struct sectrace_tlc_node));
	if (NULL == tlc_node) {
		SECTRACE_ERR("no memory for TLC node");
		return NULL;
	}

	mutex_init(&(tlc_node->base.lock));

	if (sectrace_init_node(mgt, (struct sectrace_node *)tlc_node, name)) {
		SECTRACE_ERR("init TLC node failed");
		vfree(tlc_node);
		return NULL;
	}

	tlc_node->buffer_file = debugfs_create_file("buffer", 0666, tlc_node->base.dir, tlc_node, &debugfs_sectrace_node_buffer_operations);
	if (NULL == tlc_node->buffer_file) {
		SECTRACE_ERR("create buffer file failed for '%s'", name);
		goto failed;
	}

	tlc_node->event_file = debugfs_create_file("event", 0444, tlc_node->base.dir, tlc_node, &debugfs_sectrace_node_event_operations);
	if (NULL == tlc_node->event_file) {
		SECTRACE_ERR("create event file failed for '%s'", name);
		goto failed;
	}

	mutex_lock(&(tlc_node->base.lock));

	tlc_node->event = event_disable;
	tlc_node->event_counter = 0;
	init_waitqueue_head(&tlc_node->wq);

	tlc_node->base.active = 1;

	mutex_unlock(&(tlc_node->base.lock));

	return tlc_node;

failed:
	if (NULL != tlc_node->event_file) {
		debugfs_remove(tlc_node->event_file);
		tlc_node->event_file = NULL;
	}

	if (NULL != tlc_node->buffer_file) {
		debugfs_remove(tlc_node->buffer_file);
		tlc_node->buffer_file = NULL;
	}

	sectrace_deinit_node((struct sectrace_node *)tlc_node);

	vfree(tlc_node);

	return NULL;
}

static int sectrace_destroy_tlc_node(struct sectrace_management *mgt, struct sectrace_tlc_node *tlc_node)
{
	struct sectrace_node *node = (struct sectrace_node *)tlc_node;

	mutex_lock(&node->lock);

	if (state_disabled != node->state) {
		/* the TLC lib must disable node before delete it */
		SECTRACE_WARNING("TLC node state is not DISABLED");
	}

	node->active = 0;

	mutex_unlock(&node->lock);

	/* remove file for TLC node */
	if (NULL != tlc_node->event_file) {
		debugfs_remove(tlc_node->event_file);
		tlc_node->event_file = NULL;
	}

	if (NULL != tlc_node->buffer_file) {
		debugfs_remove(tlc_node->buffer_file);
		tlc_node->buffer_file = NULL;
	}

	/* deinit node */
	sectrace_deinit_node(node);

	/* remove node from list */
	mutex_lock(&node->lock);
	list_del(&node->list);
	mutex_unlock(&node->lock);

	vfree(tlc_node);

	return 0;

}

static struct sectrace_proxy_node *sectrace_create_proxy_node(struct sectrace_management *mgt, const char *name)
{
	struct sectrace_proxy_node *proxy_node;

	proxy_node = (struct sectrace_proxy_node *)vzalloc(sizeof(struct sectrace_proxy_node));
	if (NULL == proxy_node) {
		SECTRACE_ERR("no memory for proxy node");
		return NULL;
	}

	mutex_init(&(proxy_node->base.lock));

	if (sectrace_init_node(mgt, (struct sectrace_node *)proxy_node, name)) {
		SECTRACE_ERR("init proxy node failed");
		mutex_unlock(&(proxy_node->base.lock));
		vfree(proxy_node);
		return NULL;
	}

	proxy_node->drvid_file = debugfs_create_file("driverid", 0666, proxy_node->base.dir, proxy_node, &debugfs_sectrace_node_drvid_operations);
	if (NULL == proxy_node->drvid_file) {
		SECTRACE_ERR("create driver id file failed for '%s'", name);
		goto failed;
	}

	mutex_lock(&(proxy_node->base.lock));

	proxy_node->driver_id = 0;

	proxy_node->base.active = 1;

	mutex_unlock(&(proxy_node->base.lock));

	return proxy_node;

failed:
	if (NULL != proxy_node->drvid_file) {
		debugfs_remove(proxy_node->drvid_file);
		proxy_node->drvid_file = NULL;
	}

	sectrace_deinit_node((struct sectrace_node *)proxy_node);

	vfree(proxy_node);

	return NULL;
}

static int sectrace_destroy_proxy_node(struct sectrace_management *mgt, struct sectrace_proxy_node *proxy_node)
{
	struct sectrace_node *node = (struct sectrace_node *)proxy_node;

	/* force to disable node */
	sectrace_proxy_disable(proxy_node);

	mutex_lock(&node->lock);
	node->active = 0;
	mutex_unlock(&node->lock);

	/* remove file for TLC node */
	if (NULL != proxy_node->drvid_file) {
		debugfs_remove(proxy_node->drvid_file);
		proxy_node->drvid_file = NULL;
	}

	/* deinit node */
	sectrace_deinit_node(node);

	/* remove node from list */
	mutex_lock(&node->lock);
	list_del(&node->list);
	mutex_unlock(&node->lock);

	vfree(proxy_node);

	return 0;
}

static int sectrace_add(struct sectrace_management *mgt, const char *name, unsigned int flags)
{
	struct sectrace_node *node;

	if (sectrace_check_flag(flags)) {
		/* flags invalid */
		SECTRACE_ERR("flags is invalid");
		return -EINVAL;
	}

	mutex_lock(&mgt->lock);

	/* check the name if existing in the node list */
	if (NULL != sectrace_find_node(mgt, name)) {
		/* find the node */
		SECTRACE_ERR("The same name node existed(%s), add failed", name);
		mutex_unlock(&mgt->lock);
		return -EEXIST;
	}

	switch (flags & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		node = (struct sectrace_node *)sectrace_create_drv_node(mgt, name);
		break;

	case SECTRACE_TYPE_TLC:
		node = (struct sectrace_node *)sectrace_create_tlc_node(mgt, name);
		break;

	case SECTRACE_TYPE_PROXY:
		node = (struct sectrace_node *)sectrace_create_proxy_node(mgt, name);
		break;

	default:
		mutex_unlock(&mgt->lock);
		return -EINVAL;
	}

	if (NULL != node) {
		node->flag = flags;
		list_add(&node->list, &mgt->nodes);
	}

	mutex_unlock(&mgt->lock);
	return 0;
}

static int sectrace_delete(struct sectrace_management *mgt, const char *name)
{
	struct sectrace_node *node;
	int ret;

	mutex_lock(&mgt->lock);

	node = sectrace_find_node(mgt, name);
	if (NULL == node) {
		/* find the node */
		SECTRACE_WARNING("Can't find node(%s), delete failed", name);
		mutex_unlock(&mgt->lock);
		return -ENOENT;
	}

	switch (node->flag & SECTRACE_MASK_TYPE) {
	case SECTRACE_TYPE_DRV:
		ret = sectrace_destroy_drv_node(mgt, (struct sectrace_drv_node *)node);
		break;
	
	case SECTRACE_TYPE_TLC:
		ret = sectrace_destroy_tlc_node(mgt, (struct sectrace_tlc_node *)node);
		break;
	
	case SECTRACE_TYPE_PROXY:
		ret = sectrace_destroy_proxy_node(mgt, (struct sectrace_proxy_node *)node);
		break;
	
	default:
		BUG(); /* the type must be previous one */
		mutex_unlock(&mgt->lock);
		return -EPERM;
	}

	mutex_unlock(&mgt->lock);

	return ret;
}

static int sectrace_set_size(struct sectrace_management *mgt, const char *name, size_t size)
{
	struct sectrace_node *node;

	mutex_lock(&mgt->lock);
	node = sectrace_find_node(mgt, name);
	if (NULL == node) {
		/* find the node */
		SECTRACE_ERR("Can't find node(%s), set callback failed", name);
		mutex_unlock(&mgt->lock);
		return -ENOENT;
	}
	mutex_unlock(&mgt->lock);

	if (size < SECTRACE_BUF_SIZE_MIN) {
		SECTRACE_MSG("%s: set size should be more than %d (%zd)", node->name, SECTRACE_BUF_SIZE_MIN, size);
		size = SECTRACE_BUF_SIZE_MIN;
	}

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (state_disabled != node->state) {
		SECTRACE_MSG("%s: size only set when disabled", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}
	node->size = KB2BYTE(size);

	mutex_unlock(&node->lock);

	return 0;
}

static int sectrace_set_callback(struct sectrace_management *mgt, const char *name, union callback_func *callback)
{
	struct sectrace_node *node;
	struct sectrace_drv_node *drv_node;

	mutex_lock(&mgt->lock);
	node = sectrace_find_node(mgt, name);
	if (NULL == node) {
		/* find the node */
		SECTRACE_ERR("Can't find node(%s), set callback failed", name);
		mutex_unlock(&mgt->lock);
		return -ENOENT;
	}
	mutex_unlock(&mgt->lock);

	mutex_lock(&node->lock);

	if (!node->active) {
		SECTRACE_ERR("node(%s) is inactive, exit", node->name);
		mutex_unlock(&node->lock);
		return -EPERM;
	}

	if (SECTRACE_TYPE_DRV != (node->flag & SECTRACE_MASK_TYPE)) {
		SECTRACE_ERR("The node (%s) isn't driver node, can't set callback", name);
		mutex_unlock(&node->lock);
		return -ENOENT;
	}

	drv_node = (struct sectrace_drv_node *)node;
	memcpy(&drv_node->callback, callback, sizeof(union callback_func));

	mutex_unlock(&node->lock);

	return 0;
}

static ssize_t sectrace_ctl_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sectrace_management *mgt = (struct sectrace_management *)(file->private_data);
	enum {
		STAGE_OPT = 0, /* stage0: opteration(+/-) */
		STAGE_NAME = 1, /* stage1: name */
		STAGE_TYPE = 2, /* stage2: type(TLC/PROXY) */
		STAGE_IF = 3, /* stage3: interface(TCI/DCI) */
		STAGE_USAGE = 4, /* stage4:usage(TL/DR/TD) */
		STAGE_END = 5,
	} stage = STAGE_OPT;
	char buf[128];
	char str_opt[2] = "";
	char str_name[100] = "";
	char str_type[8] = "";
	char str_interface[6] = "";
	char str_usage[6] = "";
	size_t buf_size;
	size_t pos = 0;
	size_t len;
	size_t len_cpy;
	char *p_end;
	int ret;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf_size > 0) {
		if ('\n' == buf[buf_size-1])
			buf[buf_size-1] = '\0';
		else
			buf[buf_size] = '\0';
	} else {
		return -EINVAL;
	}

	while (pos < buf_size) {
		if ('\0' == buf[pos]) {
			/* end of string */
			break;
		}

		if (STAGE_END == stage) {
			/* scan completed */
			break;
		}

		switch (stage) {
		case STAGE_OPT:
			str_opt[0] = buf[pos];
			pos++;
			break;

		case STAGE_NAME:
			p_end = strchr(&buf[pos], ':');
			if (NULL == p_end)
				p_end = &buf[buf_size];
			len = (size_t)(p_end - &buf[pos]);
			if (len > (sizeof(str_name) - 1))
				len_cpy = sizeof(str_name) - 1;
			else
				len_cpy = len;
			memcpy(str_name, &buf[pos], len_cpy);
			str_name[len_cpy] = '\0';
			pos += len + 1;
			break;

		case STAGE_TYPE:
			p_end = strchr(&buf[pos], ',');
			if (NULL == p_end)
				p_end = &buf[buf_size];
			len = (size_t)(p_end - &buf[pos]);
			if (len > (sizeof(str_type) - 1))
				len_cpy = sizeof(str_type) - 1;
			else
				len_cpy = len;
			memcpy(str_type, &buf[pos], len_cpy);
			str_type[len_cpy] = '\0';
			pos += len + 1;
			break;

		case STAGE_IF:
			p_end = strchr(&buf[pos], ',');
			if (NULL == p_end)
				p_end = &buf[buf_size];
			len = (size_t)(p_end - &buf[pos]);
			if (len > (sizeof(str_interface) - 1))
				len_cpy = sizeof(str_interface) - 1;
			else
				len_cpy = len;
			memcpy(str_interface, &buf[pos], len_cpy);
			str_interface[len_cpy] = '\0';
			pos += len + 1;
			break;

		case STAGE_USAGE:
			p_end = strchr(&buf[pos], ',');
			if (NULL == p_end)
				p_end = &buf[buf_size];
			len = (size_t)(p_end - &buf[pos]);
			if (len > (sizeof(str_usage) - 1))
				len_cpy = sizeof(str_usage) - 1;
			else
				len_cpy = len;
			memcpy(str_usage, &buf[pos], len_cpy);
			str_usage[len_cpy] = '\0';
			pos += len + 1;
			break;

		default:
			BUG();
			return -EFAULT;
		}

		stage++;
	}

	if (!strcmp(str_opt, "+")) {
		/* add a node */
		unsigned int flags = 0;

		/* abstrace type for flags */
		if (!strcasecmp(str_type, "TLC"))
			flags |= SECTRACE_TYPE_TLC;
		else if (!strcasecmp(str_type, "PROXY"))
			flags |= SECTRACE_TYPE_PROXY;
		else {
			SECTRACE_WARNING("type invalid (%s)", str_type);
			return -EINVAL;
		}

		/* abstrace interface for flags */
		if (!strcasecmp(str_interface, "TCI"))
			flags |= SECTRACE_IF_TCI;
		else if (!strcasecmp(str_interface, "DCI"))
			flags |= SECTRACE_IF_DCI;
		else {
			SECTRACE_WARNING("interface invalid (%s)", str_interface);
			return -EINVAL;
		}

		/* abstrace usage for flags */
		if (!strcasecmp(str_usage, "TL"))
			flags |= SECTRACE_USAGE_TL;
		else if (!strcasecmp(str_usage, "DR"))
			flags |= SECTRACE_USAGE_DR;
		else if (!strcasecmp(str_usage, "TD"))
			flags |= SECTRACE_USAGE_TL | SECTRACE_USAGE_DR;
		else {
			SECTRACE_WARNING("type invalid (%s)", str_usage);
			return -EINVAL;
		}

		ret = sectrace_add(mgt, str_name, flags);
		if (ret) {
			SECTRACE_ERR("add node(%s) failed(%d)", str_name, ret);
			return ret;
		}
	} else if (!strcmp(str_opt, "-")) {
		/* delete the node */
		ret = sectrace_delete(mgt, str_name);
		if (ret) {
			SECTRACE_ERR("delete node(%s) failed(%d)", str_name, ret);
			return ret;
		}
	} else {
		/* invalid operation string */
		return -EINVAL;
	}

	return buf_size;
}

static const struct file_operations debugfs_sectrace_ctl_operations = {
	.open = simple_open,
	.write = sectrace_ctl_write,
	.llseek = no_llseek,
};


static int __init sectrace_init(void)
{
	mutex_init(&sectrace_mgt.lock);
	INIT_LIST_HEAD(&sectrace_mgt.nodes);

	sectrace_mgt.root_dir = debugfs_create_dir("sectrace", NULL);
	if (NULL == sectrace_mgt.root_dir) {
		SECTRACE_ERR("create root dir failed");
		goto failed;
	}

	sectrace_mgt.ctl_file = debugfs_create_file("ctl", 0222, sectrace_mgt.root_dir, &sectrace_mgt, &debugfs_sectrace_ctl_operations);
	if (NULL == sectrace_mgt.ctl_file) {
		SECTRACE_ERR("create ctl file failed");
		goto failed;
	}

	sectrace_mgt.inited = 1;

	return 0;

failed:
	return -1;
}

static void __exit sectrace_exit(void)
{
	struct sectrace_node *node, *next;
	int err;

	mutex_lock(&sectrace_mgt.lock);

	/* deinit all nodes in the list */
	list_for_each_entry_safe(node, next, &sectrace_mgt.nodes, list) {
		switch (node->flag & SECTRACE_MASK_TYPE) {
		case SECTRACE_TYPE_DRV:
			err = sectrace_destroy_drv_node(&sectrace_mgt, (struct sectrace_drv_node *)node);
			break;

		case SECTRACE_TYPE_TLC:
			err = sectrace_destroy_tlc_node(&sectrace_mgt, (struct sectrace_tlc_node *)node);
			break;

		case SECTRACE_TYPE_PROXY:
			err = sectrace_destroy_proxy_node(&sectrace_mgt, (struct sectrace_proxy_node *)node);
			break;

		default:
			BUG(); /* the type must be previous one */
			/* force to delete and free */
			list_del(&node->list);
			vfree(node);
			break;
		}
	}

	/* remove debugfs file and directory */
	if (NULL != sectrace_mgt.ctl_file) {
		debugfs_remove(sectrace_mgt.ctl_file);
		sectrace_mgt.ctl_file = NULL;
	}

	if (NULL != sectrace_mgt.root_dir) {
		debugfs_remove(sectrace_mgt.root_dir);
		sectrace_mgt.root_dir = NULL;
	}

	/* clear inited flag */
	sectrace_mgt.inited = 0;

	mutex_unlock(&sectrace_mgt.lock);

	proxy_deinit();
}

/* ========== kernel api ========== */
int init_sectrace(const char *name, enum interface_type ci, enum usage_type usage, size_t size, union callback_func *callback)
{
	unsigned int flags = SECTRACE_TYPE_DRV;
	int ret;

	if (!sectrace_mgt.inited) {
		SECTRACE_ERR("sectrace management isn't inited");
		return -EPERM;
	}

	/* check function pointers */
	if ((NULL == callback) ||
		((if_tci == ci) && ((NULL == callback->tl.map) || (NULL == callback->tl.unmap) || (NULL == callback->tl.transact))) ||
		((if_dci == ci) && ((NULL == callback->dr.map) || (NULL == callback->dr.unmap) || (NULL == callback->dr.transact)))) {
		SECTRACE_ERR("callback function pointer is NULL");
		return -EINVAL;
	}

	if (if_tci == ci)
		flags |= SECTRACE_IF_TCI;
	else if (if_dci == ci)
		flags |= SECTRACE_IF_DCI;
	else {
		SECTRACE_ERR("ci is invalid(%d)", ci);
		return -EINVAL;
	}

	if (usage_tl == usage)
		flags |= SECTRACE_USAGE_TL;
	else if (usage_dr == usage)
		flags |= SECTRACE_USAGE_DR;
	else if (usage_tl_dr == usage)
		flags |= SECTRACE_USAGE_TL | SECTRACE_USAGE_DR;
	else {
		SECTRACE_ERR("usage is invalid(%d)", usage);
		return -EINVAL;
	}

	ret = sectrace_add(&sectrace_mgt, name, flags);
	if (ret) {
		SECTRACE_ERR("add node(%s) failed(%d)", name, ret);
		return ret;
	}

	ret = sectrace_set_callback(&sectrace_mgt, name, callback);
	if (ret) {
		SECTRACE_ERR("set callback for node(%s) failed(%d)", name, ret);
		goto failed;
	}

	ret = sectrace_set_size(&sectrace_mgt, name, size);
	if (ret) {
		SECTRACE_ERR("set size for node(%s) failed(%d)", name, ret);
		goto failed;
	}

	return 0;

failed:
	sectrace_delete(&sectrace_mgt, name);
	return ret;
}
EXPORT_SYMBOL_GPL(init_sectrace);

int deinit_sectrace(const char *name)
{
	int ret;

	if (!sectrace_mgt.inited) {
		SECTRACE_ERR("sectrace management isn't inited");
		return -EPERM;
	}

	ret = sectrace_delete(&sectrace_mgt, name);
	if (ret) {
		SECTRACE_ERR("delete node(%s) failed(%d)", name, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(deinit_sectrace);
/* ============================== */

module_init(sectrace_init);
module_exit(sectrace_exit);
MODULE_AUTHOR("FY Yang <FY.Yang@mediatek.com>");
MODULE_DESCRIPTION("Secure Systrace Driver");
MODULE_LICENSE("GPL");

