#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/wait.h>
#include <linux/kthread.h>

#include <linux/sectrace.h>

#include "mobicore_driver_api.h"

#include <tlTest_Api.h>
#include <drTest_Api.h>


static char *param = "t";
module_param(param, charp, 0444);


#define TEST_LOG(fmt, arg...) \
	pr_debug("SECTRACE_TEST:%s(): "fmt"\n", __func__, ##arg); \

#define TEST_MSG(fmt, arg...) \
	pr_info("SECTRACE_TEST:%s(): "fmt"\n", __func__, ##arg)

#define TEST_WARNING(fmt, arg...) \
	pr_warning("SECTRACE_TEST:%s(): "fmt"\n", __func__, ##arg)

#define TEST_ERR(fmt, arg...) \
	pr_err("SECTRACE_TEST:%s(): "fmt"\n", __func__, ##arg)


static const struct mc_uuid_t uuid_tl = TL_TEST_UUID;
static const struct mc_uuid_t uuid_dr = DRV_TEST_UUID;


static const uint32_t mc_deviceId = MC_DEVICE_ID_DEFAULT;
static tciMessage_t *tci = NULL;
static dciMessage_t *dci = NULL;
static struct mc_session_handle tlSessionHandle;
static struct mc_session_handle drSessionHandle;

static struct task_struct *thread_print = NULL;
static unsigned int option = 1;
char node_name[64];
static int run = 0;

static DECLARE_WAIT_QUEUE_HEAD(thread_wq);




// -------------------------------------------------------------
static enum mc_result executeTciCmd(tciCommandId_t cmd, unsigned long value1, unsigned long value2, uint32_t *result)
{
	enum mc_result ret;

	if (NULL == tci) {
		TEST_ERR("TCI has not been set up properly - exiting");
		return MC_DRV_ERR_NO_FREE_MEMORY;
	}

	tci->cmd.header.commandId = cmd;
	tci->cmd.len = 0;
	tci->cmd.respLen = 0;
	tci->value1 = value1;
	tci->value2 = value2;

	TEST_LOG("Preparing command message in TCI");

	TEST_LOG("Notifying the trustlet");
	ret = mc_notify(&tlSessionHandle);

	if (MC_DRV_OK != ret) {
		TEST_ERR("Notify failed: %d", ret);
	    goto exit;
	}

	TEST_LOG("Waiting for the Trustlet response");
	ret = mc_wait_notification(&tlSessionHandle, -1);

	if (MC_DRV_OK != ret) {
		TEST_ERR("Wait for response notification failed: 0x%x", ret);
		goto exit;
	}

	if (NULL != result)
		*result = tci->ResultData;

	TEST_LOG("Verifying that the Trustlet sent a response.");
	if (RSP_ID(cmd) != tci->rsp.header.responseId) {
	    TEST_ERR("Trustlet did not send a response: %d",
				tci->rsp.header.responseId);
		ret = MC_DRV_ERR_INVALID_RESPONSE;
		goto exit;
	}

	if (RET_OK != tci->rsp.header.returnCode) {
		TEST_ERR("Trustlet did not send a valid return code: %d",
				tci->rsp.header.returnCode);
		ret = tci->rsp.header.returnCode;
	}

exit:
	return ret;
}

static enum mc_result executeDciCmd(dciCommandId_t cmd, unsigned long value1, unsigned long value2)
{
    enum mc_result ret;

	if (NULL == dci) {
		TEST_ERR("DCI has not been set up properly - exiting");
		return MC_DRV_ERR_NO_FREE_MEMORY;
	}

	dci->command.header.commandId = cmd;
	dci->command.len = 0;
	dci->test_data.pa = value1;
	dci->test_data.size = value2;

	TEST_LOG("Preparing command message in DCI");

	TEST_LOG("Notifying the trustlet");
	ret = mc_notify(&drSessionHandle);

	if (MC_DRV_OK != ret) {
		TEST_ERR("Notify failed: %d", ret);
		goto exit;
	}

	TEST_LOG("Waiting for the Trustlet response");
	ret = mc_wait_notification(&drSessionHandle, -1);

	if (MC_DRV_OK != ret) {
		TEST_ERR("Wait for response notification failed: 0x%x", ret);
		goto exit;
	}

	TEST_LOG("Verifying that the Trustlet sent a response.");
	if (RSP_ID(cmd) != dci->response.header.responseId) {
		TEST_ERR("Trustlet did not send a response: %d",
				dci->response.header.responseId);
		ret = MC_DRV_ERR_INVALID_RESPONSE;
		goto exit;
	}
 
	if (RET_OK != dci->response.header.returnCode) {
		TEST_ERR("Trustlet did not send a valid return code: %d",
				dci->response.header.returnCode);
		ret = dci->response.header.returnCode;
	}

exit:
	return ret;
}

// -------------------------------------------------------------
static enum mc_result session_open(void)
{
	enum mc_result ret;

	TEST_LOG("Opening <t-base device");
	ret = mc_open_device(mc_deviceId);
	if (MC_DRV_ERR_INVALID_OPERATION == ret) {
		// skip false alarm when the mc_open_device(mc_deviceId) is called more than once
		TEST_LOG("mc_open_device already done \n");
	} else if (MC_DRV_OK != ret) {
		TEST_ERR("mc_open_device failed: %d @%s line %d\n", ret, __func__, __LINE__);
		return ret;
	}

	ret = mc_malloc_wsm(mc_deviceId, 0, sizeof(tciMessage_t), (uint8_t **)&tci, 0);
	if (MC_DRV_OK != ret) {
		TEST_ERR("mc_malloc_wsm failed: %d @%s line %d\n", ret, __func__, __LINE__);
		return ret;
	}

	ret = mc_malloc_wsm(mc_deviceId, 0, sizeof(dciMessage_t), (uint8_t **)&dci, 0);
	if (MC_DRV_OK != ret) {
		TEST_ERR("mc_malloc_wsm failed: %d @%s line %d\n", ret, __func__, __LINE__);
		return ret;
	}

	memset(tci, 0, sizeof(tciMessage_t));
	memset(dci, 0, sizeof(tciMessage_t));

	TEST_MSG("Opening the TL session");
	memset(&tlSessionHandle, 0, sizeof(tlSessionHandle));
	tlSessionHandle.device_id = mc_deviceId; // The device ID (default device is used)
	ret = mc_open_session(&tlSessionHandle,
							&uuid_tl,
							(uint8_t *)tci,
							(uint32_t)sizeof(tciMessage_t));
	if (MC_DRV_OK != ret) {
		TEST_ERR("mc_open_session failed: %d @%s line %d\n", ret, __func__, __LINE__);
		mc_free_wsm(mc_deviceId, (uint8_t *)tci);
		tci = NULL;
		mc_close_device(mc_deviceId);
		return ret;
	} else {
        TEST_MSG("open(tl) succeeded");
    }

	TEST_MSG("Opening the DR session");
	memset(&drSessionHandle, 0, sizeof(drSessionHandle));
	drSessionHandle.device_id = mc_deviceId; // The device ID (default device is used)
	ret = mc_open_session(&drSessionHandle,
							&uuid_dr,
							(uint8_t *)dci,
							(uint32_t)sizeof(dciMessage_t));
	if (MC_DRV_OK != ret) {
		TEST_ERR("mc_open_session failed: %d @%s line %d\n", ret, __func__, __LINE__);
		mc_free_wsm(mc_deviceId, (uint8_t *)dci);
		dci = NULL;
		mc_close_device(mc_deviceId);
		return ret;
	} else {
		TEST_MSG("open(dr) succeeded");
	}

	return ret;
}

static struct mc_bulk_map tl_mapped_info;
// -------------------------------------------------------------
static int tl_map(void *va, size_t size)
{
	enum mc_result ret;

	ret = mc_map(&tlSessionHandle, va, (uint32_t)size, &tl_mapped_info);
	if (MC_DRV_OK != ret) {
		TEST_ERR("map tl addr error");
		return -1;
	}

	ret = executeTciCmd(CMD_TEST_MAP, (unsigned long)(tl_mapped_info.secure_virt_addr), (unsigned long)(tl_mapped_info.secure_virt_len), NULL);
	if (ret != RET_OK) {
		TEST_ERR("Unable to execute CMD_TEST_MAP command: %d", ret);
		goto exit;
	}   

	return 0;

exit:
	mc_unmap(&tlSessionHandle, va, &tl_mapped_info);
	return -1;
}

// -------------------------------------------------------------
static int tl_unmap(void *va, size_t size)
{
    enum mc_result ret;

    ret = executeTciCmd(CMD_TEST_UNMAP, 0, 0, NULL);
    if (ret != RET_OK)
    {
        TEST_ERR("Unable to execute CMD_TEST_UNMAP command: %d", ret);
        goto exit;
    }

    mc_unmap(&tlSessionHandle, va, &tl_mapped_info);

    return 0;

exit:    
    return -1;
}

// -------------------------------------------------------------
static int tl_transact(void)
{
	enum mc_result ret;

	ret = executeTciCmd(CMD_TEST_TRANSACT, 0, 0, NULL);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_TEST_TRANSACT command: %d", ret);
		return -1;
	}

	return 0;
}

// -------------------------------------------------------------
static int tl_print(void)
{
	enum mc_result ret;

	ret = executeTciCmd(CMD_TEST_PRINT, 0, 0, NULL);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_TEST_PRINT command: %d", ret);
		return -1;
	}

	return 0;
}

// -------------------------------------------------------------
static int dr_map(unsigned long pa, size_t size)
{
	enum mc_result ret;

	ret = executeDciCmd(CMD_ID_MAP, (unsigned long)pa, (unsigned long)size);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_ID_MAP command: %d", ret);
		goto exit;
	}

	return 0;

exit:
	return -1;
}

// -------------------------------------------------------------
static int dr_unmap(unsigned long pa, size_t size)
{
	enum mc_result ret;

	ret = executeDciCmd(CMD_ID_UNMAP, 0, 0);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_ID_UNMAP command: %d", ret);
		goto exit;
	}

	return 0;

exit:    
	return -1;
}

// -------------------------------------------------------------
static int dr_transact(void)
{
	enum mc_result ret;

	ret = executeDciCmd(CMD_ID_TRANSACT, 0, 0);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_ID_TRANSACT command: %d", ret);
		goto exit;
	}

	return 0;

exit:    
	return -1;
}

// -------------------------------------------------------------
static int dr_print(void)
{
	enum mc_result ret;

	ret = executeDciCmd(CMD_ID_PRINT, 0, 0);
	if (RET_OK != ret) {
		TEST_ERR("Unable to execute CMD_ID_PRINT command: %d", ret);
		goto exit;
	}

	return 0;

exit:    
	return -1;
}

// -------------------------------------------------------------
static void session_close(void)
{
	enum mc_result ret;

	TEST_MSG("Closing the session");
	ret = mc_close_session(&drSessionHandle);
	if (MC_DRV_OK != ret) {
		TEST_ERR("Closing session failed: %d", ret);
		/* continue even in case of error */
	}
	ret = mc_close_session(&tlSessionHandle);
	if (MC_DRV_OK != ret) {
		TEST_ERR("Closing session failed: %d", ret);
		/* continue even in case of error */
	}

	mc_free_wsm(mc_deviceId, (uint8_t *)dci);
	dci = NULL;
	mc_free_wsm(mc_deviceId, (uint8_t *)tci);
	tci = NULL;

    TEST_MSG("Closing <t-base device");
	ret = mc_close_device(mc_deviceId);
	if (MC_DRV_OK != ret) {
		TEST_ERR("Closing <t-base device failed: %d", ret);
		/* continue even in case of error */;
	}
}

static int print_thread(void *data)
{
	while (run) {
		if ((option >= 1) && (option <= 3)) {
			tl_print();
		} else if (4 == option) {
			dr_print();
		} else if (5 == option) {
			tl_print();
		}
		wait_event_interruptible_timeout(thread_wq, !run, msecs_to_jiffies(1000));
	}

	return 0;
}

static int __init test_init(void)
{
	enum mc_result ret;
	union callback_func callback;
	int inited = 0;

	TEST_MSG("start to sectrce test");

	switch (param[0]) {
	case 't':
		/* TCI (TL+DR)*/
		option = 1;
		break;
	case 'l':
		/* TCI (TL)*/
		option = 2;
		break;
	case 'r':
		/* TCI (DR)*/
		option = 3;
		break;
	case 'd':
		/* DCI */
		option = 4;
		break;
	case 'p':
		/* Proxy */
		option = 5;
		break;
	default:
		TEST_ERR("Unknown option");
		return -1;
	}

    ret = session_open();
	if (MC_DRV_OK != ret) {
		TEST_ERR("open session failed!");
		return ret;
	}

	if ((option >= 1) && (option <= 3)) {
		// TCI test
		enum usage_type usage;
		if (1 == option)
			usage = usage_tl_dr;
		else if (2 == option)
			usage = usage_tl;
		else if (3 == option)
			usage = usage_dr;
		callback.tl.map = tl_map;
		callback.tl.unmap = tl_unmap;
		callback.tl.transact = tl_transact;
		sprintf(node_name, "%s", "Driver_Test_TCI");
		if (init_sectrace(node_name, if_tci, usage, 64, &callback)) {
			TEST_ERR("init_sectrace failed");
			goto exit;
		}
		inited = 1;
	} else if (4 == option) {
		// DCI test
		callback.dr.map = dr_map;
		callback.dr.unmap = dr_unmap;
		callback.dr.transact = dr_transact;
		sprintf(node_name, "%s", "Driver_Test_DCI");
		if (init_sectrace(node_name, if_dci, usage_dr, 64, &callback)) {
			TEST_ERR("init_sectrace failed");
			goto exit;
		}
		inited = 1;
	} else if (5 == option) {
		// Proxy test
	} else {
		TEST_MSG("invalid option");
		goto exit;
	}

	/* create print thread */
	run = 1;
	thread_print = kthread_create(print_thread, NULL, "sectrace_test");
	if (NULL == thread_print) {
		TEST_ERR("create kthread for printing failed");
		goto exit;
	}

	wake_up_process(thread_print);

	return 0;

exit:
	TEST_MSG("test_init error");
	if (inited)
		deinit_sectrace(node_name);
	session_close();

	return -1;
}

static void __exit test_exit(void)
{
	run = 0;
	wake_up(&thread_wq);

	kthread_stop(thread_print);

	deinit_sectrace(node_name);
	session_close();
}


module_init(test_init);
module_exit(test_exit);
MODULE_AUTHOR("FY Yang <FY.Yang@mediatek.com>");
MODULE_DESCRIPTION("Secure Systrace Test");
MODULE_LICENSE("GPL");

