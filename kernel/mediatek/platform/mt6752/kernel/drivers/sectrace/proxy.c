
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/printk.h>

#include <mobicore_driver_api.h>

#include <tlProxy_Api.h>

#include "proxy.h"


static const int proxy_log_on = 1;
#define PROXY_LOG(fmt, arg...) \
	do { \
		if (proxy_log_on) pr_debug("SECTRACE[PROXY]:%s(): "fmt"\n", __func__, ##arg); \
	} while (0)

#define PROXY_MSG(fmt, arg...) \
	pr_info("SECTRACE[PROXY]:%s(): "fmt"\n", __func__, ##arg)

#define PROXY_WARNING(fmt, arg...) \
	pr_warning("SECTRACE[PROXY]:%s(): "fmt"\n", __func__, ##arg)

#define PROXY_ERR(fmt, arg...) \
	pr_err("SECTRACE[PROXY]:%s(): "fmt"\n", __func__, ##arg)


static const struct mc_uuid_t uuid = TL_PROXY_UUID;

static struct mc_session_handle tlSessionHandle = {
	.session_id = 0,
};

static const uint32_t mc_deviceId = MC_DEVICE_ID_DEFAULT;

static tciMessage_t *pTci = NULL;

static DEFINE_MUTEX(tciMutex);

static int device_opened = 0;



static enum mc_result open_mobicore_device(void)
{
	enum mc_result mcRet = MC_DRV_OK;

	if (!device_opened) {
		PROXY_LOG("=============== open mobicore device ===============\n");
		/* Open MobiCore device */
		mcRet = mc_open_device(mc_deviceId);
		if (MC_DRV_ERR_INVALID_OPERATION == mcRet) {
			// skip false alarm when the mc_open_device(mc_deviceId) is called more than once
			PROXY_LOG("mc_open_device already done\n");
		} else if (MC_DRV_OK != mcRet) {
			PROXY_ERR("mc_open_device failed: %d @%s line %d\n", mcRet, __func__, __LINE__);
			return mcRet;
		}
		device_opened = 1;
	}

	return MC_DRV_OK;
}

static void close_mobicore_device(void)
{
	enum mc_result mcRet = MC_DRV_OK;

	if (device_opened) {
		PROXY_LOG("=============== close mobicore device ===============\n");
		/* Close MobiCore device */
		mcRet = mc_close_device(mc_deviceId);
		if (MC_DRV_OK != mcRet) {
			PROXY_ERR("mc_close_device failed: %d @%s, line %d\n", mcRet, __func__, __LINE__);
			return;
		}
		device_opened = 0;
	}
}

static enum mc_result init_session_tl(void)
{
	enum mc_result mcRet = MC_DRV_OK;

	mcRet = open_mobicore_device();
	if (MC_DRV_OK != mcRet) {
		PROXY_ERR("open mobicore device failed");
		return mcRet;
	}

	if (0 != tlSessionHandle.session_id) {
		PROXY_LOG("proxy trustlet session already created\n");
		return MC_DRV_OK;
	}

	PROXY_LOG("=============== init ssession ===============\n");
	do {
		/* Allocating WSM for TCI */
		mcRet = mc_malloc_wsm(mc_deviceId, 0, sizeof(tciMessage_t), (uint8_t **)(&pTci), 0);
		if (MC_DRV_OK != mcRet) {
			PROXY_LOG("mc_malloc_wsm failed: %d @%s line %d\n", mcRet, __func__, __LINE__);
			break;
		}

		/* Open session the trustlet */
		memset(&tlSessionHandle, 0, sizeof(tlSessionHandle));
		tlSessionHandle.device_id = mc_deviceId;
		mcRet = mc_open_session(&tlSessionHandle,
								&uuid,
								(uint8_t *)pTci,
								(uint32_t)sizeof(tciMessage_t));
		if (MC_DRV_OK != mcRet) {
			PROXY_ERR("mc_open_session failed: %d @%s line %d\n", mcRet, __func__, __LINE__);
			/* clear session handle if failed */
			memset(&tlSessionHandle, 0, sizeof(tlSessionHandle));
			/* free TCI */
			if (MC_DRV_OK == mc_free_wsm(mc_deviceId, (uint8_t *)pTci)) {
				pTci = NULL;
			}			
			break;
		}
	} while (0);

	return mcRet;
}

static void deinit_session_tl(void)
{
	enum mc_result mcRet = MC_DRV_OK;

	PROXY_LOG("=============== close trustlet session ===============\n");
	/* Close session */
	if (0 != tlSessionHandle.session_id) {
		mcRet = mc_close_session(&tlSessionHandle);
		if (MC_DRV_OK != mcRet) {
			PROXY_ERR("mc_close_session failed: %d @%s line %d\n", mcRet, __func__, __LINE__);
			return;
		}
		/* clear session handle */
		memset(&tlSessionHandle, 0, sizeof(tlSessionHandle));
	}

	if (NULL != pTci) {
		mcRet = mc_free_wsm(mc_deviceId, (uint8_t *)pTci);
		if (MC_DRV_OK != mcRet) {
			PROXY_ERR("mc_free_wsm failed: %d @%s line %d\n", mcRet, __func__, __LINE__);
			return;
		}
		pTci = NULL;
	}
}

static enum mc_result init_session(void)
{
	enum mc_result mcRet;

	mcRet = init_session_tl();
	if (MC_DRV_OK != mcRet) {
		PROXY_ERR("init tl session failed");
		return mcRet;
	}

	return MC_DRV_OK;
}

static void deinit_session(void)
{
    deinit_session_tl();
    close_mobicore_device();
}

static int notifyTrustletCommandValue(uint32_t command, unsigned long *value1, unsigned long *value2, unsigned long *value3)
{
	enum mc_result mc_ret = MC_DRV_OK;
	int ret = 0;

	mutex_lock(&tciMutex);

	init_session();
	if (0 == tlSessionHandle.session_id) {
		PROXY_ERR("invalid session handle of Trustlet @%s line %d\n", __func__, __LINE__);
		mutex_unlock(&tciMutex);
		return -1;
	}

	/* prepare data */
	memset(pTci, 0, sizeof(tciMessage_t));
	pTci->cmd.header.commandId = command;
	pTci->value1 = *value1;
	pTci->value2 = *value2;
	pTci->value3 = *value3;

	PROXY_LOG("notify Trustlet CMD: %d \n", command);
	/* Notify the trustlet */
	mc_ret = mc_notify(&tlSessionHandle);
	if (MC_DRV_OK != mc_ret) {
		PROXY_ERR("mc_notify failed: %d @%s line %d\n", mc_ret, __func__, __LINE__);
		mutex_unlock(&tciMutex);
		return -1;
	}

	PROXY_LOG("Trustlet CMD: %d wait notification \n", command);
	/* Wait for response from the trustlet */
	mc_ret = mc_wait_notification(&tlSessionHandle, MC_INFINITE_TIMEOUT);
	if (MC_DRV_OK != mc_ret) {
		PROXY_ERR("mc_wait_notification failed: %d @%s line %d\n", mc_ret, __func__, __LINE__);
		mutex_unlock(&tciMutex);
		return -1;
	}

	*value1 = pTci->value1;
	*value2 = pTci->value2;
	*value3 = pTci->value3;

	if (0 == pTci->ResultData) {
		PROXY_LOG("Trustlet CMD: %d done \n", command);
	} else {
		PROXY_ERR("Trustlet CMD: %d return error(%u) \n", command, pTci->ResultData);
		ret = (int)pTci->ResultData;
	}

	mutex_unlock(&tciMutex);
	return ret;
}

int proxy_map(uint32_t driver_id, unsigned long pa, size_t size)
{
	unsigned long value1 = (unsigned long)driver_id;
	unsigned long value2 = (unsigned long)pa;
	unsigned long value3 = (unsigned long)size;
	return notifyTrustletCommandValue(CMD_PROXY_MAP, &value1, &value2, &value3);
}

int proxy_unmap(uint32_t driver_id, unsigned long pa, size_t size)
{
	unsigned long value1 = (unsigned long)driver_id;
	unsigned long value2 = (unsigned long)pa;
	unsigned long value3 = (unsigned long)size;
	return notifyTrustletCommandValue(CMD_PROXY_UNMAP, &value1, &value2, &value3);
}

int proxy_transact(uint32_t driver_id)
{
	unsigned long value1 = (unsigned long)driver_id;
	unsigned long value2 = 0;
	unsigned long value3 = 0;
	return notifyTrustletCommandValue(CMD_PROXY_TRANSACT, &value1, &value2, &value3);
}

void proxy_deinit(void)
{
	mutex_lock(&tciMutex);
	deinit_session();
	mutex_unlock(&tciMutex);
}

