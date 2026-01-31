/* touchscreen/ektf2k_kthread_mtk.c - ELAN EKTF2K touchscreen driver
* for MTK65xx serial platform.
*
* Copyright (C) 2012 Elan Microelectronics Corporation.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* 2011/12/06: The first release, version 0x0001
* 2012/2/15:  The second release, version 0x0002 for new bootcode
* 2012/5/8:   Release version 0x0003 for china market
*             Integrated 2 and 5 fingers driver code together and
*             auto-mapping resolution.
* 2012/8/24:  MTK version
* 2013/2/1:   Release for MTK6589/6577/6575/6573/6513 Platform
*             For MTK6575/6573/6513, please disable both of ELAN_MTK6577 and MTK6589DMA.
*                          It will use 8+8+2 received packet protocol
*             For MTK6577, please enable ELAN_MTK6577 and disable MTK6589DMA.
*                          It will use Elan standard protocol (18bytes of protocol).
*             For MTK6589, please enable both of ELAN_MTK6577 and MTK6589DMA.
* 2013/5/15   Fixed MTK6589_DMA issue.
*/

//#define SOFTKEY_AXIS_VER
//#define ELAN_TEN_FINGERS
#define MTK6589_DMA
#define ELAN_MTK6577
//#define TPD_HAVE_BUTTON

#ifdef ELAN_TEN_FINGERS
#define PACKET_SIZE		44		/* support 10 fingers packet */
#else
//<<Mel - 4/10, Modify firmware support 2 fingers. 
//#define PACKET_SIZE		8 		/* support 2 fingers packet  */
#define PACKET_SIZE		18			/* support 5 fingers packet  */
//>>Mel - 4/10, Modify firmware support 2 fingers. 
#endif
//0818
#define DEVICE_NAME "elan_ktf2k"

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/rtpm_prio.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/wakelock.h>

#include <linux/dma-mapping.h>

#include <mach/mt_pm_ldo.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_boot.h>
#ifndef TPD_NO_GPIO
#include "cust_gpio_usage.h"
#endif

// for linux 2.6.36.3
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/ioctl.h>

//dma
#include <linux/dma-mapping.h>

//battery add 20150127 By Ken. Check charger state.
#include <mach/battery_common.h>

//#include "ekth3250.h"
#include "tpd.h"
//#include "mach/eint.h"

//#include "tpd_custom_ekth3250.h"
#include "ektf2k_mtk.h"

#include <cust_eint.h>

#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>

#define ELAN_DEBUG 1

#define PWR_STATE_DEEP_SLEEP	0
#define PWR_STATE_NORMAL		1
#define PWR_STATE_MASK			BIT(3)

#define CMD_S_PKT			0x52
#define CMD_R_PKT			0x53
#define CMD_W_PKT			0x54

#define HELLO_PKT			0x55
#define FIVE_FINGERS_PKT 0x5D
#define MTK_FINGERS_PKT  0x6D    /** 2 Fingers: 5A 5 Fingers 5D, 10 Fingers: 62 **/

#define TWO_FINGERS_PKT  0x5A
#define MTK_FINGERS_PKT  0x6D
#define TEN_FINGERS_PKT	 0x62

#define RESET_PKT			   0x77
#define CALIB_PKT			   0xA8


#define TPD_OK 0
//#define HAVE_TOUCH_KEY

#define LCT_VIRTUAL_KEY

#ifdef MTK6589_DMA
static uint8_t *gpDMABuf_va = NULL;
static uint32_t gpDMABuf_pa = NULL;
#endif

//0721-start
#define TP_REPLACE_ALSPS 
#if defined(TP_REPLACE_ALSPS)
int ckt_tp_replace_ps_mod_on(void);
int ckt_tp_replace_ps_mod_off(void);
int  ckt_tp_replace_ps_enable(int enable);
u16  ckt_get_tp_replace_ps_value(void);
int ckt_tp_replace_ps_state= 0;
int ckt_tp_replace_ps_close = 0;
#endif
//0721-end

//0604 add -start
#define ESD_CHECK
#ifdef ESD_CHECK
static int have_interrupts = 0;
static struct workqueue_struct *esd_wq = NULL;
static struct delayed_work esd_work;
static unsigned long delay = 2*HZ;

//declare function
static void elan_touch_esd_func(struct work_struct *work);
#endif
//0604 add -end

#ifdef TPD_HAVE_BUTTON
#define TPD_KEY_COUNT           3
#define TPD_KEYS                { KEY_MENU, KEY_HOMEPAGE, KEY_BACK}
#define TPD_KEYS_DIM            {{107,1370,109,TPD_BUTTON_HEIGH},{365,1370,109,TPD_BUTTON_HEIGH},{617,1370,102,TPD_BUTTON_HEIGH}}

static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif

//<<Mel - 4/10, Add pin define. 
#define CUST_EINT_POLARITY_LOW          0
#define CUST_EINT_TOUCH_PANEL_SENSITIVE 1
//Mel - 4/10, Add pin define>>. 

// modify
#define SYSTEM_RESET_PIN_SR 	135

//Add these Define

#define IAP_PORTION   1
#define PAGERETRY     30
#define IAPRESTART    5
#define CMD_54001234	0

//IRQ disable/enable
//#define IRQ_NODE_DEBUG_USE

//Dynamic debug log node start
#define NO_DEBUG       0
#define DEBUG_MSG     1
static int debug_flag = DEBUG_MSG;
#define touch_debug(level, ...) \
        do { \
             if (debug_flag >= (level)) \
                 printk("[elan_debug]:" __VA_ARGS__); \
        } while (0)
//Dynamic debug log node end

//20141218 HH FW by Ken -start
static uint8_t file_fw_data_main[] = {
#include "E2-3_Truly_FW55AF_ID049C_20150212.i"
};

static uint8_t file_fw_data_2nd[] = {
#include "E2-3_HH_FW55AF_ID00B7_20150212.i"
};
//20141218 HH FW by Ken -end

static uint8_t *file_fw_data = file_fw_data_main;

// For Firmware Update 
#define ELAN_IOCTLID	0xD0
#define IOCTL_I2C_SLAVE	_IOW(ELAN_IOCTLID,  1, int)
#define IOCTL_MAJOR_FW_VER  _IOR(ELAN_IOCTLID, 2, int)
#define IOCTL_MINOR_FW_VER  _IOR(ELAN_IOCTLID, 3, int)
#define IOCTL_RESET  _IOR(ELAN_IOCTLID, 4, int)
#define IOCTL_IAP_MODE_LOCK  _IOR(ELAN_IOCTLID, 5, int)
#define IOCTL_CHECK_RECOVERY_MODE  _IOR(ELAN_IOCTLID, 6, int)
#define IOCTL_FW_VER  _IOR(ELAN_IOCTLID, 7, int)
#define IOCTL_X_RESOLUTION  _IOR(ELAN_IOCTLID, 8, int)
#define IOCTL_Y_RESOLUTION  _IOR(ELAN_IOCTLID, 9, int)
#define IOCTL_FW_ID  _IOR(ELAN_IOCTLID, 10, int)
#define IOCTL_ROUGH_CALIBRATE  _IOR(ELAN_IOCTLID, 11, int)
#define IOCTL_IAP_MODE_UNLOCK  _IOR(ELAN_IOCTLID, 12, int)
#define IOCTL_I2C_INT  _IOR(ELAN_IOCTLID, 13, int)
#define IOCTL_RESUME  _IOR(ELAN_IOCTLID, 14, int)
#define IOCTL_POWER_LOCK  _IOR(ELAN_IOCTLID, 15, int)
#define IOCTL_POWER_UNLOCK  _IOR(ELAN_IOCTLID, 16, int)
#define IOCTL_FW_UPDATE  _IOR(ELAN_IOCTLID, 17, int)
#define IOCTL_BC_VER  _IOR(ELAN_IOCTLID, 18, int)
#define IOCTL_2WIREICE  _IOR(ELAN_IOCTLID, 19, int)
#define IOCTL_GET_UPDATE_PROGREE	_IOR(CUSTOMER_IOCTLID,  2, int)


#define CUSTOMER_IOCTLID	0xA0
#define IOCTL_CIRCUIT_CHECK  _IOR(CUSTOMER_IOCTLID, 1, int)
#define ELAN_PATH "/system/factory_ektf/rawdata.sh"

extern struct tpd_device *tpd;

uint8_t RECOVERY=0x00;
int FW_VERSION=0x00;
int X_RESOLUTION=0x00; // Please fill the right resolution if resolution mapping error. 
int Y_RESOLUTION=0x00; // Please fill the right resolution if resolution mapping error.
int FW_ID=0x00;
int BC_VERSION = 0x00;
int work_lock=0x00;
int power_lock=0x00;
int circuit_ver=0x01;
int button_state = 0;
/*++++i2c transfer start+++++++*/
//<<Mel - 4/10, Modify I2C slave address to 0x15. 
//int file_fops_addr=0x10;
int file_fops_addr=0x15;
//Mel - 4/10, Modify I2C slave address to 0x15>>. 
/*++++i2c transfer end+++++++*/
int tpd_down_flag=0;
//0828 add ps_enable_flag
// Celia set TP-PS func. default 0=close
int ps_enable_flag=0;
// Celia start. If did not receive 55:55:xx:xx, set RST
int reset_again = 0;
//20141218 - tp id gpio by Ken
int TP_ID = NULL;
//20150127 fix USB charger by Ken
int TP_charger_state = NULL;

struct i2c_client *i2c_client = NULL;
struct task_struct *thread = NULL;

static DECLARE_WAIT_QUEUE_HEAD(waiter);
static inline int elan_ktf2k_ts_parse_xy(uint8_t *data,
uint16_t *x, uint16_t *y);
#if 0
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_hw_debounce(kal_uint8 eintno, kal_uint32 ms);
extern kal_uint32 mt65xx_eint_set_sens(kal_uint8 eintno, kal_bool sens);
extern void mt65xx_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
kal_bool auto_umask);
#endif

static int tpd_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tpd_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
static int tpd_remove(struct i2c_client *client);
static int touch_event_handler(void *unused);


static int tpd_flag = 0;

#if 0
static int key_pressed = -1;

struct osd_offset{
	int left_x;
	int right_x;
	unsigned int key_event;
};

static struct osd_offset OSD_mapping[] = { // Range need define by Case!
	{35, 290,  KEY_MENU},	//menu_left_x, menu_right_x, KEY_MENU
	{303, 467, KEY_HOME},	//home_left_x, home_right_x, KEY_HOME
	{473, 637, KEY_BACK},	//back_left_x, back_right_x, KEY_BACK
	{641, 905, KEY_SEARCH},	//search_left_x, search_right_x, KEY_SEARCH
};
#endif 

#if IAP_PORTION
uint8_t ic_status=0x00;	//0:OK 1:master fail 2:slave fail
int update_progree=0;
//<<Mel - 4/10, Modify I2C slave address to 0x15. 
uint8_t I2C_DATA[3] = {/*0x10*/0x15, 0x20, 0x21};/*I2C devices address*/  
//Mel - 4/10, Modify I2C slave address to 0x15>>. 
int is_OldBootCode = 0; // 0:new 1:old


enum
{
	PageSize		= 132,
	ACK_Fail		= 0x00,
	ACK_OK			= 0xAA,
	ACK_REWRITE	= 0x55,
};

enum
{
	E_FD			= -1,
};
#endif

//Add 0821 start
static const struct i2c_device_id tpd_id[] = 
{
	{ "ektf2k_mtk", 0 },
	{ }
};

#ifdef ELAN_MTK6577
static struct i2c_board_info __initdata i2c_tpd={ I2C_BOARD_INFO("ektf2k_mtk", (/*0x20*/0x2a>>1))};
#else
//<<Mel - 4/10, Modify I2C slave address to 0x15. 
unsigned short force[] = {0, /*0x20*/0x2a, I2C_CLIENT_END, I2C_CLIENT_END};
//Mel - 4/10, Modify I2C slave address to 0x15>>. 
static const unsigned short *const forces[] = { force, NULL };
//static struct i2c_client_address_data addr_data = { .forces = forces, };
#endif

static struct i2c_driver tpd_i2c_driver =
{
	.driver = {
		.name = "ektf2k_mtk",
		.owner = THIS_MODULE,
	},
	.probe = tpd_probe,
	.remove =  tpd_remove,
	.id_table = tpd_id,
	.detect = tpd_detect,
	//.address_data = &addr_data,
};
//Add 0821 end



struct elan_ktf2k_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct workqueue_struct *elan_wq;
	struct work_struct work;
	struct early_suspend early_suspend;
	int intr_gpio;
	// Firmware Information
	int fw_ver;
	int fw_id;
	int bc_ver;
	int x_resolution;
	int y_resolution;
	// For Firmare Update 
	struct miscdevice firmware;
	struct hrtimer timer;
	//0818
	struct attribute_group attrs;
	unsigned long flags;
	unsigned int iap_mode;
	unsigned short i2caddr; 
	struct wake_lock wakelock; 
	//0818
};

static struct elan_ktf2k_ts_data *private_ts;
static int __fw_packet_handler(struct i2c_client *client);
static int elan_ktf2k_ts_rough_calibrate(struct i2c_client *client);
static int tpd_resume(struct i2c_client *client);

#if IAP_PORTION
int Update_FW_One(/*struct file *filp,*/ struct i2c_client *client, int recovery);
static int __hello_packet_handler(struct i2c_client *client);
int IAPReset();
#endif

#ifdef MTK6589_DMA
static int elan_i2c_dma_recv_data(struct i2c_client *client, uint8_t *buf,uint8_t len)
{
	int rc;
	uint8_t *pReadData = 0;
	unsigned short addr = 0;
	pReadData = gpDMABuf_va;
	addr = client->addr ;
	client->addr |= I2C_DMA_FLAG;	
	if(!pReadData){
		printk("[elan] dma_alloc_coherent failed!\n");
		return -1;
	}
	rc = i2c_master_recv(client, gpDMABuf_pa, len);
	printk("[elan] elan_i2c_dma_recv_data rc=%d!\n",rc);
	//	copy_to_user(buf, pReadData, len);
	return rc;
}

static int elan_i2c_dma_send_data(struct i2c_client *client, uint8_t *buf,uint8_t len)
{
	int rc;
	unsigned short addr = 0;
	uint8_t *pWriteData = gpDMABuf_va;
	addr = client->addr ;
	client->addr |= I2C_DMA_FLAG;	
	
	if(!pWriteData){
		printk("[elan] dma_alloc_coherent failed!\n");
		return -1;
	}
	// copy_from_user(pWriteData, ((void*)buf), len);

	rc = i2c_master_send(client, gpDMABuf_pa, len);
	client->addr = addr;
	touch_debug(1,"[elan] elan_i2c_dma_send_data rc=%d!\n",rc);
	return rc;
}
#endif
// For Firmware Update 
int elan_iap_open(struct inode *inode, struct file *filp){ 
	touch_debug(1,"into elan_iap_open\n");
	if (private_ts == NULL)  printk("private_ts is NULL~~~");
	
	return 0;
}

int elan_iap_release(struct inode *inode, struct file *filp){    
	return 0;
}

static ssize_t elan_iap_write(struct file *filp, const char *buff, size_t count, loff_t *offp){  
	int ret;
	char *tmp;

	touch_debug(1,"into elan_iap_write\n");
	#ifdef ESD_CHECK  //0604
	have_interrupts = 1;
	#endif
	
	if (count > 8192)
	count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);
	
	if (tmp == NULL)
	return -ENOMEM;

#ifdef MTK6589_DMA    
	if (copy_from_user(gpDMABuf_va, buff, count)) {
		kfree(tmp);
		return -EFAULT;
	}
	ret = elan_i2c_dma_send_data(private_ts->client, tmp, count);
#else
	if (copy_from_user(tmp, buff, count)) {
		kfree(tmp);
		return -EFAULT;
	}    
	ret = i2c_master_send(private_ts->client, tmp, count);
#endif    
	kfree(tmp);
	return (ret == 1) ? count : ret;

}

ssize_t elan_iap_read(struct file *filp, char *buff, size_t count, loff_t *offp){    
	char *tmp;
	int ret;  
	long rc;

	#ifdef ESD_CHECK  //0604
	have_interrupts = 1;
	#endif

	touch_debug(1, "into elan_iap_read\n");
	if (count > 8192)
	count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);

	if (tmp == NULL)
	return -ENOMEM;
#ifdef MTK6589_DMA
	ret = elan_i2c_dma_recv_data(private_ts->client, tmp, count);
	if (ret >= 0)
	rc = copy_to_user(buff, gpDMABuf_va, count);    
#else    
	ret = i2c_master_recv(private_ts->client, tmp, count);
	if (ret >= 0)
	rc = copy_to_user(buff, tmp, count);    
#endif  

	kfree(tmp);

	//return ret;
	return (ret == 1) ? count : ret;
	
}

static long elan_iap_ioctl(/*struct inode *inode,*/ struct file *filp,    unsigned int cmd, unsigned long arg){

	int __user *ip = (int __user *)arg;
	touch_debug(1, "into elan_iap_ioctl\n");
	touch_debug(1, "cmd value %x\n",cmd);

	switch (cmd) {
	case IOCTL_I2C_SLAVE:
		private_ts->client->addr = (int __user)arg;
		private_ts->client->addr &= I2C_MASK_FLAG;
		private_ts->client->addr |= I2C_ENEXT_FLAG;
		//file_fops_addr = 0x15;
		break;
	case IOCTL_MAJOR_FW_VER:
		break;
	case IOCTL_MINOR_FW_VER:
		break;
	case IOCTL_RESET:

		mt_set_gpio_mode( GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO );
		mt_set_gpio_dir( GPIO_CTP_RST_PIN, GPIO_DIR_OUT );
		mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
		mdelay(10);
		//	#if !defined(EVB)
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
		//	#endif
		mdelay(10);
		mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );

		break;
	case IOCTL_IAP_MODE_LOCK:
		printk("[elan]%s %x=IOCTL_IAP_MODE_LOCK\n", __func__,IOCTL_IAP_MODE_LOCK);
		work_lock=1;
		disable_irq(CUST_EINT_TOUCH_PANEL_NUM);
		mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
		//cancel_work_sync(&private_ts->work);
		#ifdef ESD_CHECK
		cancel_delayed_work_sync(&esd_work);
		#endif
		break;
	case IOCTL_IAP_MODE_UNLOCK:
		work_lock=0;
		enable_irq(CUST_EINT_TOUCH_PANEL_NUM);
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		#ifdef ESD_CHECK
		queue_delayed_work(esd_wq, &esd_work, delay);
		#endif
		break;
	case IOCTL_CHECK_RECOVERY_MODE:
		return RECOVERY;
		break;
	case IOCTL_FW_VER:
		__fw_packet_handler(private_ts->client);
		return FW_VERSION;
		break;
	case IOCTL_X_RESOLUTION:
		__fw_packet_handler(private_ts->client);
		return X_RESOLUTION;
		break;
	case IOCTL_Y_RESOLUTION:
		__fw_packet_handler(private_ts->client);
		return Y_RESOLUTION;
		break;
	case IOCTL_FW_ID:
		__fw_packet_handler(private_ts->client);
		return FW_ID;
		break;
	case IOCTL_ROUGH_CALIBRATE:
		return elan_ktf2k_ts_rough_calibrate(private_ts->client);
	case IOCTL_I2C_INT:
		put_user(mt_get_gpio_in(GPIO_CTP_EINT_PIN),ip);
		printk("[elan]GPIO_CTP_EINT_PIN = %d\n", mt_get_gpio_in(GPIO_CTP_EINT_PIN));

		break;	
	case IOCTL_RESUME:
		tpd_resume(private_ts->client);
		break;	
	case IOCTL_CIRCUIT_CHECK:
		return circuit_ver;
		break;
	case IOCTL_POWER_LOCK:
		power_lock=1;
		break;
	case IOCTL_POWER_UNLOCK:
		power_lock=0;
		break;
#if IAP_PORTION		
	case IOCTL_GET_UPDATE_PROGREE:
		update_progree=(int __user)arg;
		break; 

	case IOCTL_FW_UPDATE:
		//RECOVERY = IAPReset(private_ts->client);
		RECOVERY=0;
		#ifdef ESD_CHECK
		cancel_delayed_work_sync(&esd_work);
		#endif
		Update_FW_One(private_ts->client, RECOVERY);
		#ifdef ESD_CHECK
		queue_delayed_work(esd_wq, &esd_work, delay);
		#endif
		
#endif
	case IOCTL_BC_VER:
		__fw_packet_handler(private_ts->client);
		return BC_VERSION;
		break;
	default:            
		break;   
	}       
	return 0;
}

struct file_operations elan_touch_fops = {    
	.open =         elan_iap_open,    
	.write =        elan_iap_write,    
	.read = 	elan_iap_read,    
	.release =	elan_iap_release,    
	.unlocked_ioctl=elan_iap_ioctl, 
};
#if IAP_PORTION
int EnterISPMode(struct i2c_client *client, uint8_t  *isp_cmd)
{
	char buff[4] = {0};
	int len = 0;
	
	len = i2c_master_send(private_ts->client, isp_cmd,  sizeof(isp_cmd));
	if (len != sizeof(buff)) {
		printk("[ELAN] ERROR: EnterISPMode fail! len=%d\r\n", len);
		return -1;
	}
	else
	printk("[ELAN] IAPMode write data successfully! cmd = [%2x, %2x, %2x, %2x]\n", isp_cmd[0], isp_cmd[1], isp_cmd[2], isp_cmd[3]);
	return 0;
}

int ExtractPage(struct file *filp, uint8_t * szPage, int byte)
{
	int len = 0;

	len = filp->f_op->read(filp, szPage,byte, &filp->f_pos);
	if (len != byte) 
	{
		printk("[ELAN] ExtractPage ERROR: read page error, read error. len=%d\r\n", len);
		return -1;
	}

	return 0;
}

int WritePage(uint8_t * szPage, int byte)
{
	int len = 0;

	len = i2c_master_send(private_ts->client, szPage,  byte);
	if (len != byte) 
	{
		printk("[ELAN] ERROR: write page error, write error. len=%d\r\n", len);
		return -1;
	}

	return 0;
}

int GetAckData(struct i2c_client *client)
{
	int len = 0;

	char buff[2] = {0};
	
	len=i2c_master_recv(private_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff)) {
		printk("[ELAN] ERROR: read data error, write 50 times error. len=%d\r\n", len);
		return -1;
	}

	printk("[ELAN] GetAckData:%x,%x\n",buff[0],buff[1]);
	if (buff[0] == 0xaa/* && buff[1] == 0xaa*/) 
	return ACK_OK;
	else if (buff[0] == 0x55 && buff[1] == 0x55)
	return ACK_REWRITE;
	else
	return ACK_Fail;

	return 0;
}

void print_progress(int page, int ic_num, int j)
{
	int i, percent,page_tatol,percent_tatol;
	char str[256];
	str[0] = '\0';
	for (i=0; i<((page)/10); i++) {
		str[i] = '#';
		str[i+1] = '\0';
	}
	
	page_tatol=page+249*(ic_num-j);
	percent = ((100*page)/(249));
	percent_tatol = ((100*page_tatol)/(249*ic_num));

	if ((page) == (249))
	percent = 100;

	if ((page_tatol) == (249*ic_num))
	percent_tatol = 100;		

	printk("\rprogress %s| %d%%", str, percent);
	
	if (page == (249))
	printk("\n");
}
/* 
* Restet and (Send normal_command ?)
* Get Hello Packet
*/
int  IAPReset()
{
	printk("[ELAN] IAPReset()\n");
	mt_set_gpio_mode( GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO );
	mt_set_gpio_dir( GPIO_CTP_RST_PIN, GPIO_DIR_OUT );
	mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
	mdelay(10);
	//	#if !defined(EVB)
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
	//		#endif
	mdelay(10);
	mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
	return 1;

}

/* Check Master & Slave is "55 aa 33 cc" */
int CheckIapMode(void)
{
	char buff[4] = {0},len = 0;
	//WaitIAPVerify(1000000);
	//len = read(fd, buff, sizeof(buff));
	len=i2c_master_recv(private_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff)) 
	{
		printk("[ELAN] CheckIapMode ERROR: read data error,len=%d\r\n", len);
		return -1;
	}
	else
	{
		
		if (buff[0] == 0x55 && buff[1] == 0xaa && buff[2] == 0x33 && buff[3] == 0xcc)
		{
			printk("[ELAN] CheckIapMode is 55 aa 33 cc\n");
			return 0;
		}
		else// if ( j == 9 )
		{
			printk("[ELAN] Mode= 0x%x 0x%x 0x%x 0x%x\r\n", buff[0], buff[1], buff[2], buff[3]);
			printk("[ELAN] ERROR:  CheckIapMode error\n");
			return -1;
		}
	}
	printk("\n");	
}

int Update_FW_One(struct i2c_client *client, int recovery)
{
	printk("[ELAN] Update_FW_One\n");
	int res = 0,ic_num = 1;
	int iPage = 0, rewriteCnt = 0; //rewriteCnt for PAGE_REWRITE
	int i = 0;
	int PageNum=0;
	uint8_t data;

	int restartCnt = 0, checkCnt = 0; // For IAP_RESTART
	//uint8_t recovery_buffer[4] = {0};
	int byte_count;
	uint8_t *szBuff = NULL;
	int curIndex = 0;
#if CMD_54001234
	uint8_t isp_cmd[] = {0x54, 0x00, 0x12, 0x34};	 //54 00 12 34
#else
	uint8_t isp_cmd[] = {0x45, 0x49, 0x41, 0x50};	 //45 49 41 50
#endif
	uint8_t recovery_buffer[4] = {0};

	IAP_RESTART:	

	data=I2C_DATA[0];//Master
	dev_dbg(&client->dev, "[ELAN] %s: address data=0x%x \r\n", __func__, data);

	//	if(recovery != 0x80)
	//	{
	printk("[ELAN] Firmware upgrade normal mode !\n");

	IAPReset();
	mdelay(20);	

	res = EnterISPMode(private_ts->client, isp_cmd);	 //enter ISP mode

	//res = i2c_master_recv(private_ts->client, recovery_buffer, 4);   //55 aa 33 cc 
	//printk("[ELAN] recovery byte data:%x,%x,%x,%x \n",recovery_buffer[0],recovery_buffer[1],recovery_buffer[2],recovery_buffer[3]);			

	mdelay(10);
#if 0
	//Check IC's status is IAP mode(55 aa 33 cc) or not
	res = CheckIapMode();	 //Step 1 enter ISP mode
	if (res == -1) //CheckIapMode fail
	{	
		checkCnt ++;
		if (checkCnt >= 3)
		{
			printk("[ELAN] ERROR: CheckIapMode %d times fails!\n", IAPRESTART);
			return E_FD;
		}
		else
		{
			printk("[ELAN] CheckIapMode retry %dth times! And restart IAP~~~\n\n", checkCnt);
			goto IAP_RESTART;
		}
	}
	else
	printk("[ELAN]  CheckIapMode ok!\n");
#endif

	// Send Dummy Byte	
	printk("[ELAN] send one byte data:%x,%x",private_ts->client->addr,data);
	res = i2c_master_send(private_ts->client, &data,  sizeof(data));
	if(res!=sizeof(data))
	{
		printk("[ELAN] dummy error code = %d\n",res);
	}	
	mdelay(20);

	//20141218 check Tulry or HH FW by Ken -start
	if(TP_ID)
		PageNum = (sizeof(file_fw_data_2nd)/sizeof(uint8_t)/PageSize);
	else
		PageNum = (sizeof(file_fw_data_main)/sizeof(uint8_t)/PageSize);
	//20141218 check Tulry or HH FW by Ken -end

	printk("[ELAN] Update FW PageNum =%d\n",PageNum);
	// Start IAP
	for( iPage = 1; iPage <= PageNum; iPage++ ) 
	{
		PAGE_REWRITE:
#if 1
		//#if 0
		// 8byte mode
		//szBuff = fw_data + ((iPage-1) * PageSize); 
		for(byte_count=1;byte_count<=17;byte_count++)
		{
			if(byte_count!=17)
			{		
				//printk("[ELAN] byte %d\n",byte_count);
				//printk("curIndex =%d\n",curIndex);
				szBuff = file_fw_data + curIndex;
				curIndex =  curIndex + 8;

				//ioctl(fd, IOCTL_IAP_MODE_LOCK, data);
				res = WritePage(szBuff, 8);
			}
			else
			{
				//printk("byte %d\n",byte_count);
				//printk("curIndex =%d\n",curIndex);
				szBuff = file_fw_data + curIndex;
				curIndex =  curIndex + 4;
				//ioctl(fd, IOCTL_IAP_MODE_LOCK, data);
				res = WritePage(szBuff, 4); 
			}
		} // end of for(byte_count=1;byte_count<=17;byte_count++)
#endif 
#if 0 // 132byte mode		
		szBuff = file_fw_data + curIndex;
		curIndex =  curIndex + PageSize;
		res = WritePage(szBuff, PageSize);
#endif
#if 1
		if(iPage==PageNum || iPage==1)
		{
			mdelay(70); 			 
		}
		else
		{
			mdelay(70); 			 
		}
#endif	
		res = GetAckData(private_ts->client);

		if (ACK_OK != res) 
		{
			mdelay(50); 
			printk("[ELAN] ERROR: GetAckData fail! res=%d\r\n", res);
			if ( res == ACK_REWRITE ) 
			{
				rewriteCnt = rewriteCnt + 1;
				if (rewriteCnt == PAGERETRY)
				{
					printk("[ELAN] ID 0x%02x %dth page ReWrite %d times fails!\n", data, iPage, PAGERETRY);
					return E_FD;
				}
				else
				{
					printk("[ELAN] ---%d--- page ReWrite %d times!\n",  iPage, rewriteCnt);
					curIndex = curIndex - PageSize;
					goto PAGE_REWRITE;
				}
			}
			else
			{
				restartCnt = restartCnt + 1;
				if (restartCnt >= 5)
				{
					printk("[ELAN] ID 0x%02x ReStart %d times fails!\n", data, IAPRESTART);
					return E_FD;
				}
				else
				{
					printk("[ELAN] ===%d=== page ReStart %d times!\n",  iPage, restartCnt);
					goto IAP_RESTART;
				}
			}
		}
		else {
			printk("  data : 0x%02x ",  data);
			rewriteCnt=0;
			print_progress(iPage,ic_num,i);
		}

		//mdelay(10);
	} // end of for(iPage = 1; iPage <= PageNum; iPage++)

	if (IAPReset() > 0) {
		printk("[ELAN] Update ALL Firmware successfully!\n");
		mdelay(200);
		__fw_packet_handler(private_ts->client);
	}

	return 0;
}

#endif
// End Firmware Update

#if 0
static void elan_ktf2k_ts_early_suspend(struct early_suspend *h);
static void elan_ktf2k_ts_late_resume(struct early_suspend *h);
#endif

static ssize_t elan_ktf2k_gpio_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct elan_ktf2k_ts_data *ts = private_ts;

	//ret = gpio_get_value(ts->intr_gpio);
	ret = mt_get_gpio_in(GPIO_CTP_EINT_PIN);
	printk(KERN_DEBUG "GPIO_TP_INT_N=%d\n", ts->intr_gpio);
	sprintf(buf, "GPIO_TP_INT_N=%d\n", ret);
	ret = strlen(buf) + 1;
	return ret;
}

static DEVICE_ATTR(gpio, S_IRUGO, elan_ktf2k_gpio_show, NULL);

static ssize_t elan_ktf2k_vendor_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct elan_ktf2k_ts_data *ts = private_ts;

	sprintf(buf, "%s_x%04.4x\n", "ELAN_KTF2K", ts->fw_ver);
	ret = strlen(buf) + 1;
	return ret;
}
#if 0
static DEVICE_ATTR(vendor, S_IRUGO, elan_ktf2k_vendor_show, NULL);

static struct kobject *android_touch_kobj;

static int elan_ktf2k_touch_sysfs_init(void)
{
	int ret ;

	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		printk(KERN_ERR "[elan]%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_gpio.attr);
	if (ret) {
		printk(KERN_ERR "[elan]%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret) {
		printk(KERN_ERR "[elan]%s: sysfs_create_group failed\n", __func__);
		return ret;
	}
	return 0 ;
}

static void elan_touch_sysfs_deinit(void)
{
	sysfs_remove_file(android_touch_kobj, &dev_attr_vendor.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_gpio.attr);
	kobject_del(android_touch_kobj);
}	
#endif


static int __elan_ktf2k_ts_poll(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	// Elan for FW55AB. fix charger noise
	int status = 0, retry = 20;

	do {
		//status = gpio_get_value(ts->intr_gpio);
		status = mt_get_gpio_in(GPIO_CTP_EINT_PIN);
		printk("[elan]: %s: status = %d\n", __func__, status);
		retry--;
		//mdelay(200); //0403 modify
		mdelay(10);
	} while (status == 1 && retry > 0);

	printk( "[elan]%s: poll interrupt status %s\n",
	__func__, status == 1 ? "high" : "low");
	return (status == 0 ? 0 : -ETIMEDOUT);
}

static int elan_ktf2k_ts_poll(struct i2c_client *client)
{
	return __elan_ktf2k_ts_poll(client);
}

static int elan_ktf2k_ts_get_data(struct i2c_client *client, uint8_t *cmd,
uint8_t *buf, size_t size)
{
	int rc;

	dev_dbg(&client->dev, "[elan]%s: enter\n", __func__);

	if (buf == NULL)
	return -EINVAL;


	if ((i2c_master_send(client, cmd, 4)) != 4) {
		dev_err(&client->dev,
		"[elan]%s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}


	rc = elan_ktf2k_ts_poll(client);
	if (rc < 0)
	return -EINVAL;
	else {

		if (i2c_master_recv(client, buf, size) != size ||
				buf[0] != CMD_S_PKT)
		return -EINVAL;
	}

	return 0;
}

static int __hello_packet_handler(struct i2c_client *client)
{
	int rc;
	uint8_t buf_recv[8] = { 0 };

	rc = elan_ktf2k_ts_poll(client);
	if (rc < 0) {
		printk( "[elan] %s: Int poll failed!\n", __func__);
		RECOVERY=0x80;
		return RECOVERY;	
	}

	rc = i2c_master_recv(client, buf_recv, 8);

	printk("[elan] %s: hello packet %2x:%2X:%2x:%2x:%2x:%2X:%2x:%2x\n", __func__, buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3] , buf_recv[4], buf_recv[5], buf_recv[6], buf_recv[7]);

	if(buf_recv[0]==0x55 && buf_recv[1]==0x55 && buf_recv[2]==0x80 && buf_recv[3]==0x80) {
		printk("[elan] into RECOVERY MODE!!\n");
		RECOVERY=0x80;
		return RECOVERY; 
	}else{
		if(buf_recv[0]==0x55 && buf_recv[1]==0x55) {
			// Elan start. modify for FW55AB, fix charger noise
			mdelay(300);
			rc = elan_ktf2k_ts_poll(client);
			if (rc<0) {
				printk("[elan] Can't polling INT low before receving reK packet!\n");
			} else {
				rc = i2c_master_recv(client, buf_recv, 4);
				printk("[elan] %s: Calibration Packet %2x:%2X:%2x:%2x\n", __func__, buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3]);
			}
			// Elan end. modify for FW55AB, fix charger noise
		} else {
			// Celia start. If did not receive 55:55:xx:xx, set RST
			reset_again = 1;
			printk("[elan] Did not receive 55:55:xx:xx, need reset, reset_again=%d\n",reset_again);
			// Celia end. If did not receive 55:55:xx:xx, set RST
		}
	}
	
	return 0;
}

static int __fw_packet_handler(struct i2c_client *client)
{
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int rc;
	int major, minor;
	uint8_t cmd[] = {CMD_R_PKT, 0x00, 0x00, 0x01};/* Get Firmware Version*/
	uint8_t cmd_x[] = {0x53, 0x60, 0x00, 0x00}; /*Get x resolution*/
	uint8_t cmd_y[] = {0x53, 0x63, 0x00, 0x00}; /*Get y resolution*/
	uint8_t cmd_id[] = {0x53, 0xf0, 0x00, 0x01}; /*Get firmware ID*/
	uint8_t cmd_bc[] = {CMD_R_PKT, 0x10, 0x00, 0x01};/* Get BootCode Version*/ //0403 modify
	uint8_t buf_recv[4] = {0};

	printk( "[elan] %s: n", __func__);
	// Firmware version
	rc = elan_ktf2k_ts_get_data(client, cmd, buf_recv, 4);
	if (rc < 0)
	return rc;
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	private_ts->fw_ver = major << 8 | minor;
	FW_VERSION = major << 8 | minor;
	// Firmware ID
	rc = elan_ktf2k_ts_get_data(client, cmd_id, buf_recv, 4);
	if (rc < 0)
	return rc;
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	private_ts->fw_id = major << 8 | minor;
	FW_ID = major << 8 | minor;
	// X Resolution
	rc = elan_ktf2k_ts_get_data(client, cmd_x, buf_recv, 4);
	if (rc < 0)
	return rc;
	minor = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	private_ts->x_resolution =minor;
	X_RESOLUTION = minor;
	
	// Y Resolution	
	rc = elan_ktf2k_ts_get_data(client, cmd_y, buf_recv, 4);
	if (rc < 0)
	return rc;
	minor = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	private_ts->y_resolution =minor;
	Y_RESOLUTION = minor;

	// Bootcode version
	rc = elan_ktf2k_ts_get_data(client, cmd_bc, buf_recv, 4);
	if (rc < 0)
	return rc;
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	private_ts->bc_ver = major << 8 | minor;
	BC_VERSION = major << 8 | minor;
	
	printk( "[elan] %s: firmware version: 0x%4.4x\n",
	__func__, FW_VERSION);
	printk( "[elan] %s: firmware ID: 0x%4.4x\n",
	__func__, FW_ID);
	printk( "[elan] %s: x resolution: %d, y resolution: %d, LCM x resolution: %d, LCM y resolution: %d\n",
	__func__, X_RESOLUTION, Y_RESOLUTION,LCM_X_MAX,LCM_Y_MAX);
	printk( "[elan] %s: bootcode version: 0x%4.4x\n",
	__func__, BC_VERSION);
	return 0;
}

static inline int elan_ktf2k_ts_parse_xy(uint8_t *data,
uint16_t *x, uint16_t *y)
{
	*x = *y = 0;

	*x = (data[0] & 0xf0);
	*x <<= 4;
	*x |= data[1];

	*y = (data[0] & 0x0f);
	*y <<= 8;
	*y |= data[2];

	return 0;
}

static int elan_ktf2k_ts_setup(struct i2c_client *client)
{
	int rc;

	rc = __hello_packet_handler(client);
	printk("[elan] hellopacket's rc = %d\n",rc);

	mdelay(10);
	if (rc != 0x80){
		rc = __fw_packet_handler(client);
		if (rc < 0)
		printk("[elan] %s, fw_packet_handler fail, rc = %d\n", __func__, rc);
		else
		printk("[elan] %s: firmware checking done.\n", __func__);
		/* Check for FW_VERSION, if 0x0000 means FW update fail! */
		if ( FW_VERSION == 0x00)
		{
			rc = 0x80;
			printk("[elan] FW_VERSION = %d, last FW update fail\n", FW_VERSION);
		}
	}
	touch_debug(1, "return hellopacket's rc = %d\n",rc);
	return rc; /* Firmware need to be update if rc equal to 0x80(Recovery mode)   */
}

static int elan_ktf2k_ts_rough_calibrate(struct i2c_client *client){
	uint8_t cmd[] = {CMD_W_PKT, 0x29, 0x00, 0x01};

	//dev_info(&client->dev, "[elan] %s: enter\n", __func__);
	printk("[elan] %s: enter\n", __func__);
	dev_info(&client->dev,
	"[elan] dump cmd: %02x, %02x, %02x, %02x\n",
	cmd[0], cmd[1], cmd[2], cmd[3]);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd)) {
		dev_err(&client->dev,
		"[elan] %s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int elan_ktf2k_ts_set_power_state(struct i2c_client *client, int state)
{
	uint8_t cmd[] = {CMD_W_PKT, 0x50, 0x00, 0x01};

	dev_dbg(&client->dev, "[elan] %s: enter\n", __func__);

	cmd[1] |= (state << 3);

	dev_dbg(&client->dev,
	"[elan] dump cmd: %02x, %02x, %02x, %02x\n",
	cmd[0], cmd[1], cmd[2], cmd[3]);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd)) {
		dev_err(&client->dev,
		"[elan] %s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int elan_ktf2k_ts_get_power_state(struct i2c_client *client)
{
	int rc = 0;
	uint8_t cmd[] = {CMD_R_PKT, 0x50, 0x00, 0x01};
	uint8_t buf[4], power_state;

	rc = elan_ktf2k_ts_get_data(client, cmd, buf, 4);
	if (rc)
	return rc;

	power_state = buf[1];
	dev_dbg(&client->dev, "[elan] dump repsponse: %0x\n", power_state);
	power_state = (power_state & PWR_STATE_MASK) >> 3;
	dev_dbg(&client->dev, "[elan] power state = %s\n",power_state == PWR_STATE_DEEP_SLEEP ? "Deep Sleep" : "Normal/Idle");

	return power_state;
}
//20150127 add by Ken. fix USB charger noise start
static void elan_ktf2k_set_AC_mode(void){
	int res = 0;
	char buf_AC_mode[4] = {0x54, 0x5C, 0x01, 0x01}; // 54 5C 01 01 //set AC_mode

	res = i2c_master_send(private_ts->client, buf_AC_mode, sizeof(buf_AC_mode));
	if (res != sizeof(buf_AC_mode))
		printk("[elan] AC_mode set fail.\n");
	else
		printk("[elan] AC_mode set success.\n");
}

static void elan_ktf2k_set_Normal_mode(void){
	int res = 0;
	char buf_Normal_mode[4] = {0x54, 0x5C, 0x00, 0x01}; //54 5C 00 01 //set Normal_mode

	res = i2c_master_send(private_ts->client, buf_Normal_mode, sizeof(buf_Normal_mode));
	if (res != sizeof(buf_Normal_mode))
		printk("[elan] Normal_mode set fail.\n");
	else
		printk("[elan] Normal_mode set success.\n");
}

static void elan_ktf2k_read_charger_mode(void){
	char buf_read_charger_state[4] = {0x53, 0x5C, 0x00, 0x01}; //53 5C 00 01, read power state
	int res = 0;
	char recv_buff[4];
	int retry = 5;
	memset(recv_buff,0x00,sizeof(recv_buff));

	// Read status of ELAN is AC or normal mode
	res = i2c_master_send(private_ts->client, buf_read_charger_state, sizeof(buf_read_charger_state));
	if (res != sizeof(buf_read_charger_state))
		printk("[elan] read charger state fail.\n");
	else
		printk("[elan] read charger state success.\n");

	do {
		res = i2c_master_recv(private_ts->client, recv_buff, 4);
		touch_debug(1,"retry:%d, receive:0x%x 0x%x 0x%x 0x%x, res:%d\n",retry, recv_buff[0], recv_buff[1], recv_buff[2], recv_buff[3], res);
		retry--;
	} while ((res != sizeof(recv_buff)) && (retry>0));

	// Just check the state of ELAN, do not need to re-set state.
	if (recv_buff[0] == 0x52 && recv_buff[1] == 0x5C && recv_buff[2] == 0x01 && recv_buff[3] == 0x01) {
		touch_debug(1, "charger_state in AC_mode\n");
		TP_charger_state = 1; // AC Mode
	} else if (recv_buff[0] == 0x52 && recv_buff[1] == 0x5C && recv_buff[2] == 0x00 && recv_buff[3] == 0x01) {
		touch_debug(1,"charger_state in Normal_mode\n");
		TP_charger_state = 0; // Normal Mode
	} else {
		touch_debug(1,"%s Can not get charger state.\n",__func__);
	}
}

static int elan_ktf2k_auto_change_charger(void){
    int charger_state ;

    charger_state = bat_is_charger_exist();
    printk("[elan] charger_state = %d\n",charger_state);

    if(TP_charger_state != charger_state){
        if(charger_state == 1 ){ //set AC mode
            elan_ktf2k_set_AC_mode();
        }else{  //set Normal mode
            elan_ktf2k_set_Normal_mode();
        }
        elan_ktf2k_read_charger_mode();
    }
}
//20150127 add by Ken. fix USB charger noise End

static int elan_ktf2k_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
	int err;
	u8 beg = addr; 
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,    .flags = 0,
			.len = 1,                .buf= &beg
		},
		{
			.addr = client->addr,    .flags = I2C_M_RD,
			.len = len,             .buf = data,
			.ext_flag = I2C_DMA_FLAG,
		}
	};

	if (!client)
	return -EINVAL;

	err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
	if (err != len) {
		printk("[elan] elan_ktf2k_read_block err=%d\n", err);
		err = -EIO;
	} else {
		printk("[elan] elan_ktf2k_read_block ok\n");
		err = 0;    /*no error*/
	}
	return err;


}


static int elan_ktf2k_ts_recv_data(struct i2c_client *client, uint8_t *buf)
{
	int rc, bytes_to_recv=PACKET_SIZE;
	uint8_t *pReadData = 0;
	unsigned short addr = 0;

	if (buf == NULL)
	return -EINVAL;
	memset(buf, 0, bytes_to_recv);

	//#ifdef ELAN_MTK6577
#ifdef MTK6589_DMA
	addr = client->addr ;
	client->addr |= I2C_DMA_FLAG;
	pReadData = gpDMABuf_va;
	if(!pReadData){
		printk("[elan] dma_alloc_coherent failed!\n");
	}
	rc = i2c_master_recv(client, gpDMABuf_pa, bytes_to_recv);
	copy_to_user(buf, pReadData, bytes_to_recv);
	client->addr = addr;
	touch_debug(1,"%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15],buf[16], buf[17]);
	
#else	
	rc = i2c_master_recv(client, buf, 8);
	if (rc != 8)
	    touch_debug(1,"The first package error.\n");
	printk("[elan_recv] %x %x %x %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
	mdelay(1);

	if (buf[0] == FIVE_FINGERS_PKT){    //for five finger
		rc = i2c_master_recv(client, buf+ 8, 8);	
		if (rc != 8)
			touch_debug(1,"The second package error.\n");
		printk("[elan_recv] %x %x %x %x %x %x %x %x\n", buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
		rc = i2c_master_recv(client, buf+ 16, 2);
		if (rc != 2)
			touch_debug(1,"The third package error.\n");
		mdelay(1);
		printk("[elan_recv] %x %x \n", buf[16], buf[17]);
	}
#endif	
	
	return rc;
}

#ifdef SOFTKEY_AXIS_VER //SOFTKEY is reported via AXI
static void elan_ktf2k_ts_report_data(struct i2c_client *client, uint8_t *buf)
{
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *idev = tpd->dev;
	uint16_t x, y;
	uint16_t fbits=0;
	uint8_t i, num, reported = 0;
	uint8_t idx, btn_idx;
	int finger_num;
	int limitY = ELAN_Y_MAX -100; // limitY need define by Case!

	/* for 10 fingers	*/
	if (buf[0] == TEN_FINGERS_PKT){
		finger_num = 10;
		num = buf[2] & 0x0f; 
		fbits = buf[2] & 0x30;	
		fbits = (fbits << 4) | buf[1]; 
		idx=3;
		btn_idx=33;
	}
	// for 5 fingers	
	else if ((buf[0] == MTK_FINGERS_PKT) || (buf[0] == FIVE_FINGERS_PKT)){
		finger_num = 5;
		num = buf[1] & 0x07; 
		fbits = buf[1] >>3;
		idx=2;
		btn_idx=17;
	}else{
		// for 2 fingers      
		finger_num = 2;
		num = buf[7] & 0x03; 
		fbits = buf[7] & 0x03;
		idx=1;
		btn_idx=7;
	}

	switch (buf[0]) {
		//<<Mel - 4/10, Add 0x78 packet. 
	case 0x78 :  // chip may reset due to watch dog
		//printk(KERN_EMERG "!!!!!!!tp chip check event\n");
		break;
		//Mel - 4/10, Add 0x78 packet>>. 
	case MTK_FINGERS_PKT:
	case TWO_FINGERS_PKT:
	case FIVE_FINGERS_PKT:	
	case TEN_FINGERS_PKT:
		//input_report_key(idev, BTN_TOUCH, 1);
		if (num == 0)
		{
			//dev_dbg(&client->dev, "no press\n");
			if(key_pressed < 0){
				input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 0);
				input_mt_sync(idev);
				
				if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
				{   
					tpd_button(x, y, 0);  
				}
				TPD_EM_PRINT(x, y, x, y, 0, 0);
			}
			else{
				//dev_err(&client->dev, "[elan] KEY_RELEASE: key_code:%d\n",OSD_mapping[key_pressed].key_event);
				input_report_key(idev, OSD_mapping[key_pressed].key_event, 0);
				key_pressed = -1;
			}
		}
		else 
		{			
			//dev_dbg(&client->dev, "[elan] %d fingers\n", num);                        
			//input_report_key(idev, BTN_TOUCH, 1);
			for (i = 0; i < finger_num; i++) 
			{	
				if ((fbits & 0x01)) 
				{
					elan_ktf2k_ts_parse_xy(&buf[idx], &x, &y);  
					//elan_ktf2k_ts_parse_xy(&buf[idx], &y, &x);
					//x = X_RESOLUTION-x;	 
					//y = Y_RESOLUTION-y; 
#if 1
					if(X_RESOLUTION > 0 && Y_RESOLUTION > 0)
					{
						x = ( x * LCM_X_MAX )/X_RESOLUTION;
						y = ( y * LCM_Y_MAX )/Y_RESOLUTION;
					}
					else
					{
						x = ( x * LCM_X_MAX )/ELAN_X_MAX;
						y = ( y * LCM_Y_MAX )/ELAN_Y_MAX;
					}
#endif 		 
					touch_debug(1,"SOFTKEY_AXIS_VER %s, x=%d, y=%d\n",__func__, x , y);
					
					if (!((x<=0) || (y<=0) || (x>=X_RESOLUTION) || (y>=Y_RESOLUTION))) 
					{   
						if ( y < limitY )
						{
							input_report_abs(idev, ABS_MT_TRACKING_ID, i);
							input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 8);
							input_report_abs(idev, ABS_MT_POSITION_X, x);
							input_report_abs(idev, ABS_MT_POSITION_Y, y);
							input_mt_sync(idev);
							if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
							{   
								tpd_button(x, y, 1);  
							}
							TPD_EM_PRINT(x, y, x, y, i-1, 1);
						}
						else
						{
							int i=0;
							for(i=0;i<4;i++)
							{
								if((x > OSD_mapping[i].left_x) && (x < OSD_mapping[i].right_x))
								{
									//dev_err(&client->dev, "[elan] KEY_PRESS: key_code:%d\n",OSD_mapping[i].key_event);
									//printk("[elan] %d KEY_PRESS: key_code:%d\n", i, OSD_mapping[i].key_event);
									input_report_key(idev, OSD_mapping[i].key_event, 1);
									key_pressed = i;
								}
							}
						}
						reported++;
						
					} // end if border
				} // end if finger status
				fbits = fbits >> 1;
				idx += 3;
			} // end for
		}

		if (reported)
		input_sync(idev);
		else 
		{
			input_mt_sync(idev);
			input_sync(idev);
		}

		break;
	default:
		dev_err(&client->dev,
		"[elan] %s: unknown packet type: %0x\n", __func__, buf[0]);
		break;
	} // end switch
	return;
}
#else //SOFTKEY is reported via BTN bit
static void elan_ktf2k_ts_report_data(struct i2c_client *client, uint8_t *buf)
{
	/*struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);*/
	struct input_dev *idev = tpd->dev;
	uint16_t x, y;
	uint16_t fbits=0;
	uint8_t i, num, reported = 0;
	uint8_t idx, btn_idx;
	int finger_num;

	hwm_sensor_data *sensor_data;
	touch_debug(1,"%s\n",__func__);

	#if  1//defined(TP_REPLACE_ALSPS)
	//face close turn off lcd led
	if(buf[0] ==0xFA && buf[1] ==0xCE && buf[2] ==0xAA && buf[3] ==0xAA){
		ckt_tp_replace_ps_close=0;
		printk("[elan-ps] elan_ktf2k_ts_report_data - face away\n");
	}

	//face away turn on lcd led
	if(buf[0] ==0xFA && buf[1] ==0xCE && buf[2] ==0x55 && buf[3] ==0x55){
		ckt_tp_replace_ps_close=1;
		printk("[elan-ps] elan_ktf2k_ts_report_data - face close \n");
	}
	#endif
	//0721-end

	/* for 10 fingers	*/
	if (buf[0] == TEN_FINGERS_PKT){
		finger_num = 10;
		num = buf[2] & 0x0f; 
		fbits = buf[2] & 0x30;	
		fbits = (fbits << 4) | buf[1]; 
		idx=3;
		btn_idx=33;
	}
	// for 5 fingers	
	else if ((buf[0] == MTK_FINGERS_PKT) || (buf[0] == FIVE_FINGERS_PKT)){
		finger_num = 5;
		num = buf[1] & 0x07; 
		fbits = buf[1] >>3;
		idx=2;
		btn_idx=17;
	}else{
		// for 2 fingers      
		finger_num = 2;
		num = buf[7] & 0x03; 
		fbits = buf[7] & 0x03;
		idx=1;
		btn_idx=7;
	}

	// Celia start, ELAN add processor for package 0x98
	uint8_t cmd_test_buff[] = {0x53, 0x90, 0x00, 0x01};
	uint8_t exit_test_buff[] = {0xa5, 0xa5, 0xa5, 0xa5};
	uint8_t exit_diamond_test_buff[] = {0x54, 0x8d, 0x00, 0x01};
	uint8_t recv_buff[4]={0};
	// Celia end, ELAN add processor for package 0x98

	switch (buf[0]) {
		//<<Mel - 4/10, Add 0x78 packet.
		case 0x78 :  // chip may reset due to watch dog
			//printk(KERN_EMERG "!!!!!!!tp chip check event\n");
			break;
		//Mel - 4/10, Add 0x78 packet>>.

		// Celia start, ELAN add processor for package 0x98
		case 0x98:
			if ((elan_i2c_dma_send_data(client, cmd_test_buff, sizeof(cmd_test_buff))) != sizeof(cmd_test_buff)) {
				printk("cmd_test_buff send failed.\n");
			}
			if ((elan_i2c_dma_recv_data(client, recv_buff, sizeof(recv_buff))) != sizeof(recv_buff)) {
				printk("recv_buff send failed.\n");
			}
			if ((elan_i2c_dma_send_data(client, exit_test_buff, sizeof(exit_test_buff))) != sizeof(exit_test_buff)) {
				printk("exit_test_buff send failed.\n");
			}
			mdelay(100);
			if ((elan_i2c_dma_send_data(client, exit_diamond_test_buff, sizeof(exit_diamond_test_buff))) != sizeof(exit_diamond_test_buff)) {
				printk("exit_diamond_test_buff send failed.\n");
			}
			break;
		// Celia end, ELAN add processor for package 0x98

	case MTK_FINGERS_PKT:
	case TWO_FINGERS_PKT:
	case FIVE_FINGERS_PKT:	
	case TEN_FINGERS_PKT:
		//input_report_key(idev, BTN_TOUCH, 1);
		if (num == 0)
		{
			dev_dbg(&client->dev, "[elan] no press\n");
			touch_debug(1,"tp button_state0= %x\n",button_state);
			touch_debug(1,"tp buf[btn_idx] = %x KEY_MENU=%x KEY_HOME=%x KEY_BACK=%x KEY_SEARCH =%x\n",buf[btn_idx], KEY_MENU, KEY_HOME, KEY_BACK, KEY_SEARCH);

			touch_debug(1,"touch up\n");
			input_report_key(idev, BTN_TOUCH, 0);
			input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(idev);
			tpd_down_flag = 0;

			if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
			{   
				tpd_button(x, y, 0);  
			}
			TPD_EM_PRINT(x, y, x, y, 0, 0);
			
		}
		else 
		{			
			//dev_dbg(&client->dev, "[elan] %d fingers\n", num);                        
			input_report_key(idev, BTN_TOUCH, 1);
			for (i = 0; i < finger_num; i++) 
			{	
				if ((fbits & 0x01)) 
				{
					elan_ktf2k_ts_parse_xy(&buf[idx], &x, &y);  
					//elan_ktf2k_ts_parse_xy(&buf[idx], &y, &x); 
					#if 1
					if(X_RESOLUTION > 0 && Y_RESOLUTION > 0)
					{
						x = ( x * LCM_X_MAX )/X_RESOLUTION;
						y = ( y * LCM_Y_MAX )/Y_RESOLUTION;
					}
					else
					{
						x = ( x * LCM_X_MAX )/ELAN_X_MAX;
						y = ( y * LCM_Y_MAX )/ELAN_Y_MAX;
					}
					#endif 		 

					//x = ( x * LCM_X_MAX )/ELAN_X_MAX;
					//y = ( y * LCM_Y_MAX )/ELAN_Y_MAX;
					touch_debug(1,"%s, x=%d, y=%d\n",__func__, x , y);
					//x = LCM_X_MAX-x;	 
					//y = Y_RESOLUTION-y;			     
					if (!((x<=0) || (y<=0) || (x>=LCM_X_MAX) || (y>=LCM_Y_MAX))) 
					{   
						input_report_key(idev, BTN_TOUCH, 1);
						input_report_abs(idev, ABS_MT_TRACKING_ID, i);
						input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 8);
						input_report_abs(idev, ABS_MT_POSITION_X, x);
						input_report_abs(idev, ABS_MT_POSITION_Y, y);
						input_mt_sync(idev);
						reported++;
						tpd_down_flag=1;
						if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
						{   
							tpd_button(x, y, 1);  
						}
						TPD_EM_PRINT(x, y, x, y, i-1, 1);
					} // end if border
				} // end if finger status
				fbits = fbits >> 1;
				idx += 3;
			} // end for
		}
		if (reported) {
			input_sync(idev);
		} else {
			input_mt_sync(idev);
			input_sync(idev);
		}
		break;
	default:
		touch_debug(1,"%s: unknown packet type: %0x\n", __func__, buf[0]);
		break;
	} // end switch
	return;
}
#endif
static void elan_ktf2k_ts_work_func(struct work_struct *work)
{
	int rc;
	struct elan_ktf2k_ts_data *ts =
	container_of(work, struct elan_ktf2k_ts_data, work);
	uint8_t buf[PACKET_SIZE] = { 0 };

	//		if (gpio_get_value(ts->intr_gpio))
	if (mt_get_gpio_in(GPIO_CTP_EINT_PIN))
	{
		//enable_irq(ts->client->irq);
		printk("[elan]: Detected Jitter at INT pin. \n");
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		return;
	}
	
	rc = elan_ktf2k_ts_recv_data(ts->client, buf);

	if (rc < 0)
	{
		//enable_irq(ts->client->irq);
		printk("[elan] elan_ktf2k_ts_recv_data Error, Error code %d \n", rc);
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		return;
	}

	//printk("[elan] %2x,%2x,%2x,%2x,%2x,%2x\n",buf[0],buf[1],buf[2],buf[3],buf[5],buf[6]);
	elan_ktf2k_ts_report_data(ts->client, buf);

	//enable_irq(ts->client->irq);
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

	return;
}

static irqreturn_t elan_ktf2k_ts_irq_handler(int irq, void *dev_id)
{
	struct elan_ktf2k_ts_data *ts = dev_id;
	struct i2c_client *client = ts->client;

	dev_dbg(&client->dev, "[elan] %s\n", __func__);
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	tpd_flag = 1;
	wake_up_interruptible(&waiter);
	return IRQ_HANDLED;
}

static int elan_ktf2k_ts_register_interrupt(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;

	err = request_irq(client->irq, elan_ktf2k_ts_irq_handler,
	IRQF_TRIGGER_LOW, client->name, ts);
	if (err)
	dev_err(&client->dev, "[elan] %s: request_irq %d failed\n",
	__func__, client->irq);

	return err;
}

static int touch_event_handler(void *unused)
{
	int rc;
	uint8_t buf[PACKET_SIZE] = { 0 };

	int touch_state = 3;
	//	int button_state = 0;
	unsigned long time_eclapse;
	struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };
	sched_setscheduler(current, SCHED_RR, &param);
	int last_key = 0;
	int key;
	int index = 0;
	int i =0;

	do
	{
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		enable_irq(CUST_EINT_TOUCH_PANEL_NUM);
		set_current_state(TASK_INTERRUPTIBLE);
		wait_event_interruptible(waiter, tpd_flag != 0);
		tpd_flag = 0;
		set_current_state(TASK_RUNNING);
		disable_irq(CUST_EINT_TOUCH_PANEL_NUM);
		mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
		rc = elan_ktf2k_ts_recv_data(private_ts->client, buf);

		if (rc < 0)
		{
			printk("[elan] rc<0\n");
			
			continue;
		}

		elan_ktf2k_ts_report_data(/*ts*/private_ts->client, buf);

	}while(!kthread_should_stop());

	return 0;
}

static int tpd_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
	strcpy(info->type, TPD_DEVICE);
	return 0;
}

static void tpd_eint_interrupt_handler(void)
{
	//    printk("[elan]TPD int\n");
	#ifdef ESD_CHECK	 //0604
	have_interrupts = 1;
	#endif


	tpd_flag = 1;
	wake_up_interruptible(&waiter);
}

s32 tpd_ps_operate(void *self, u32 command, void *buff_in, s32 size_in,
void *buff_out, s32 size_out, s32 *actualout)
{
	s32 err = 0;
	s32 value;
	hwm_sensor_data *sensor_data;

	switch (command)
	{
	case SENSOR_DELAY:
		if ((buff_in == NULL) || (size_in < sizeof(int)))
		{
			//TPD_DEBUG("Set delay parameter error!");
			err = -EINVAL;
		}
		// Do nothing
		break;
	case SENSOR_ENABLE:
		printk("[elan-ps]tracy:enter SENSOR_ENABLE!!!!\n");
		if ((buff_in == NULL) || (size_in < sizeof(int)))
		{
			//TPD_DEBUG("Enable sensor parameter error!");
			err = -EINVAL;
		}
		else
		{
			printk("[elan-ps]ps_enable_flag=%d!!!!\n", ps_enable_flag);
			if (ps_enable_flag){  //0828 ps_enable_flag
				value = *(int *)buff_in;
				printk("[elan-ps]tracy:the value of enable is %d \n", value);
				ckt_tp_replace_ps_enable(value);
				err=0;
			}
			//printk("[elan]tracy:the value of err is %d !!!!\n", err);
		}
		break;
	case SENSOR_GET_DATA:
		//printk("[elan]tracy:enter SENSOR_GET_DATA!!!!\n");
		if ((buff_out == NULL) || (size_out < sizeof(hwm_sensor_data)))
		{
			printk("Get sensor data parameter error!");
			err = -EINVAL;
		}
		else
		{
			
			sensor_data = (hwm_sensor_data *)buff_out;
			//mdelay(2000);
			#if 1
			if( ckt_tp_replace_ps_close==1)
			{
				sensor_data->values[0] = 0;
				printk("[elan-ps]tracy:enter SENSOR_GET_DATA sensor_data=0\n");
			}
			else
			{
				sensor_data->values[0] = 1;
				printk("[elan-ps]tracy:enter SENSOR_GET_DATA sensor_data=1\n");
			}
			sensor_data->value_divide = 1;
			sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
			
			#endif
		}
		break;
	default:
		//TPD_DEBUG("proxmy sensor operate function no this parameter %d!\n", command);
		err = -1;
		break;
	}
	return err;
}

//0814 add start
static ssize_t show_reset(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client); 

	IAPReset();

	return sprintf(buf, "Reset Touch Screen \n");
}

#ifdef IRQ_NODE_DEBUG_USE
static ssize_t show_enable_irq(struct device *dev,
struct device_attribute *attr, char *buf)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

	work_lock=0;
	//enable_irq(private_ts->client->irq);
	printk("mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM).....");
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	wake_unlock(&private_ts->wakelock);

	return sprintf(buf, "Enable IRQ \n");
}

static ssize_t show_disable_irq(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

	work_lock=1;
	printk("mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM).....");
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	//disable_irq(private_ts->client->irq);
	wake_lock(&private_ts->wakelock);
	
	return sprintf(buf, "Disable IRQ \n");
}
#endif

static ssize_t show_calibrate(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret = 0;

	ret = elan_ktf2k_ts_rough_calibrate(client);
	return sprintf(buf, "%s\n",
	(ret == 0) ? " Testing the node of calibrate finish" : "Testing the node of calibrate fail");
}

static ssize_t show_fw_update(struct device *dev,
struct device_attribute *attr, char *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);

	printk("[elan] show_fw_update mt_eint_mask\n");
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	#ifdef ESD_CHECK
	cancel_delayed_work_sync(&esd_work);
	#endif
	power_lock = 1;
	wake_lock(&private_ts->wakelock);
	work_lock=1;
	
	ret = Update_FW_One(client, 0);

	// Start check esd after FW updated
	work_lock=0;
	printk("[elan] show_fw_update mt_eint_unmask\n");
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	wake_unlock(&private_ts->wakelock);
	#ifdef ESD_CHECK
	queue_delayed_work(esd_wq, &esd_work, delay);
	#endif
	return sprintf(buf, "Update Firmware\n");
}
static ssize_t show_fw_version_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	return sprintf(buf, "0x%04x\n", private_ts->fw_ver);
	
}

static ssize_t show_fw_id_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "0x%04x\n", private_ts->fw_id);
}

static ssize_t show_bc_version_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "0x%04x\n", private_ts->bc_ver);
}

static ssize_t show_drv_version_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "driver version 5.0");
}

static ssize_t show_iap_mode(struct device *dev,
struct device_attribute *attr, char *buf)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

	return sprintf(buf, "%s\n", 
	(private_ts->fw_ver == 0) ? "Recovery" : "Normal" );
}
//0828 - ps status
static ssize_t ps_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	printk("ps_enable_flag=%d\n",ps_enable_flag);
	if (ps_enable_flag)
		return sprintf(buf, "Enable PS \n");
	else
		return sprintf(buf, "Disable PS \n");
}

//20141218 - tp id gpio by Ken -start
static ssize_t show_tp_id_gpio_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	if(TP_ID)
		return sprintf(buf, "HH\n");
	else
		return sprintf(buf, "Truly\n");
}
//20141218 - tp id gpio by Ken -end

// Dynamic debug log node start
static ssize_t get_debug(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	printk("[elan] debug_flag=%d\n",debug_flag);
	if (debug_flag)
		return sprintf(buf, "Enable Debug \n");
	else
		return sprintf(buf, "Disable Debug \n");
}

static ssize_t set_debug(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf,"%d",&debug_flag);
	if (debug_flag)
		printk("[elan] debug_flag Set Enable\n");
	else
		printk("[elan] debug_flag Set Disable\n");
	return count;
}
// Dynamic debug log node end

static DEVICE_ATTR(reset, S_IRUGO, show_reset, NULL);
#ifdef IRQ_NODE_DEBUG_USE
static DEVICE_ATTR(enable_irq, S_IRUGO, show_enable_irq, NULL);
static DEVICE_ATTR(disable_irq, S_IRUGO, show_disable_irq, NULL);
#endif
static DEVICE_ATTR(calibrate, S_IRUGO, show_calibrate, NULL);
static DEVICE_ATTR(fw_version, S_IRUGO, show_fw_version_value, NULL);
static DEVICE_ATTR(fw_id, S_IRUGO, show_fw_id_value, NULL);
static DEVICE_ATTR(bc_version, S_IRUGO, show_bc_version_value, NULL);
static DEVICE_ATTR(drv_version, S_IRUGO, show_drv_version_value, NULL);
static DEVICE_ATTR(fw_update, S_IRUGO, show_fw_update, NULL);
static DEVICE_ATTR(iap_mode, S_IRUGO, show_iap_mode, NULL);
static DEVICE_ATTR(ps_status, S_IRUGO, ps_show, NULL);
static DEVICE_ATTR(debug, S_IRUGO |S_IWUSR, get_debug, set_debug);
//20141218 add tp_id by Ken
static DEVICE_ATTR(tp_id, S_IRUGO, show_tp_id_gpio_value, NULL);

#ifndef IRQ_NODE_DEBUG_USE
static struct attribute *elan_attributes[] = {
	&dev_attr_reset.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_fw_id.attr,
	&dev_attr_bc_version.attr,
	&dev_attr_drv_version.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_iap_mode.attr,
	&dev_attr_ps_status.attr,
	&dev_attr_debug.attr,
	&dev_attr_tp_id.attr,
	NULL
};
#else
static struct attribute *elan_attributes[] = {
	&dev_attr_reset.attr,
	&dev_attr_enable_irq.attr,
	&dev_attr_disable_irq.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_fw_id.attr,
	&dev_attr_bc_version.attr,
	&dev_attr_drv_version.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_iap_mode.attr,
	&dev_attr_ps_status.attr,
	&dev_attr_debug.attr,
	&dev_attr_tp_id.attr,
	NULL
};
#endif

static struct attribute_group elan_attribute_group = {
	.name = DEVICE_NAME,
	.attrs = elan_attributes,
};

//0814 add end
static int tpd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int fw_err = 0;
	int New_FW_ID;	
	int New_FW_VER;	
	int retval = TPD_OK;
	int node_err = -1;
	int node_check_cnt = 0;
	// Celia add for improve FW update flow
	int update_flag = 0;

	static struct elan_ktf2k_ts_data ts;
	#if  1 //defined(TP_REPLACE_ALSPS)
	struct hwmsen_object obj_ps;
	int tp_err;
	#endif
	client->addr |= I2C_ENEXT_FLAG;

	touch_debug(1, "%s:client addr is %x, TPD_DEVICE = %s\n",__func__,client->addr,TPD_DEVICE);
	touch_debug(1, "%s:I2C_WR_FLAG=%x,I2C_MASK_FLAG=%x,I2C_ENEXT_FLAG =%x\n",__func__,I2C_WR_FLAG,I2C_MASK_FLAG,I2C_ENEXT_FLAG);
	client->timing =  100;

	touch_debug(1, "%x=IOCTL_I2C_INT %x=IOCTL_IAP_MODE_LOCK %x=IOCTL_IAP_MODE_UNLOCK\n",IOCTL_I2C_INT, IOCTL_IAP_MODE_LOCK, IOCTL_IAP_MODE_UNLOCK);

#if 1
	//client->timing = 400;   
	i2c_client = client;
	private_ts = &ts;
	private_ts->client = client;
	//private_ts->addr = 0x2a;
#endif

	//Celia start
	//<<20140519 Tracy Modify Power configuration.
	hwPowerOn(MT6325_POWER_LDO_VGP1, VOL_2800, "TP");
	//hwPowerOn(MT6325_POWER_LDO_VGP1, VOL_1800, "TP_ENT");
	printk("[elan] MT6325_POWER_LDO_VGP1,VOL_2800\n");
	//hwPowerOn(MT65XX_POWER_LDO_VGP5, VOL_2800, "TP");
	//hwPowerOn(MT65XX_POWER_LDO_VGP5, VOL_1800, "TP_ENT");
	//20140519 Tracy Modify Power configuration>>. 
	msleep(10);
	//Celia end

#if 0
	/*LDO enable*/
	mt_set_gpio_mode(GPIO_CTP_EN_PIN, GPIO_CTP_EN_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_EN_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_EN_PIN, GPIO_OUT_ZERO);
	msleep(50);
	mt_set_gpio_out(GPIO_CTP_EN_PIN, GPIO_OUT_ONE);
#endif
	printk("[elan] ELAN enter tpd_probe_ ,the i2c addr=0x%x\n", client->addr);
	//printk("GPIO43 =%d,GPIO_CTP_EINT_PIN =%d,GPIO_DIR_IN=%d,CUST_EINT_TOUCH_PANEL_NUM=%d\n",GPIO43,GPIO_CTP_EINT_PIN,GPIO_DIR_IN,CUST_EINT_TOUCH_PANEL_NUM);

	// Celia start, error handle
	// Reset Touch Pannel
	if(mt_set_gpio_mode( GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO ) != 0) {
		printk ("[elan] set gpio GPIO_CTP_RST_PIN mode error");
		return -1;
	}
	if(mt_set_gpio_dir( GPIO_CTP_RST_PIN, GPIO_DIR_OUT ) != 0)
		printk ("[elan] set gpio GPIO_CTP_RST_PIN dir error");
	if(mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE ) != 0)
		printk ("[elan] set gpio GPIO_CTP_RST_PIN out one error");
	mdelay(10);
	//#if !defined(EVB)
	if(mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO) != 0)
		printk ("[elan] set gpio GPIO_CTP_RST_PIN out zero error");
	//#endif
	mdelay(10);
	if(mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE ) != 0)
		printk ("[elan] set gpio GPIO_CTP_RST_PIN out one error");
	// End Reset Touch Pannel

	//20141218 set tp_id GPIO by Ken -start
	if(mt_set_gpio_mode( GPIO_MSDC2_DAT1, GPIO_MSDC2_DAT1_M_GPIO ) != 0) {
		printk ("[elan] set gpio GPIO_MSDC2_DAT1 mode error");
		return -1;
	}
	if(mt_set_gpio_dir( GPIO_MSDC2_DAT1, GPIO_DIR_IN ) != 0)
		printk ("[elan] set gpio GPIO_MSDC2_DAT1 dir error");
	mdelay(10);
	//20141218 set tp_id GPIO by Ken -end

	if(mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT) != 0) {
		printk ("[elan] set gpio GPIO_CTP_EINT_PIN mode error");
		return -1;
	}
	if(mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN) != 0)
		printk ("[elan] set gpio GPIO_CTP_EINT_PIN dir error");
	if(mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE) != 0)
		printk ("[elan] set gpio GPIO_CTP_EINT_PIN pull enable error");
	if(mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP) != 0)
		printk ("[elan] set gpio GPIO_CTP_EINT_PIN pull select error");
	// Celia end, error handle
	msleep( 200 );

#ifdef HAVE_TOUCH_KEY
	int retry;
	for(retry = 0; retry <3; retry++)
	{
		input_set_capability(tpd->dev,EV_KEY,tpd_keys_local[retry]);
	}
#endif

	// Setup Interrupt Pin
	/* //0403 mark
	mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
	mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP);
*/
	
	mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_SENSITIVE);
	mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_CN);
	//<<Mel - 4/10, Modify Interrupt triger to falling. 
	mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, /*EINTF_TRIGGER_LOW*/EINTF_TRIGGER_FALLING, tpd_eint_interrupt_handler, 1);
	//mt65xx_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_EN, CUST_EINT_POLARITY_LOW, tpd_eint_interrupt_handler, 1);
	//Mel - 4/10, Modify Interrupt triger to falling>>. 
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	//msleep(100); //0403 mark
	// End Setup Interrupt Pin	
	tpd_load_status = 1;

#ifdef MTK6589_DMA    
	gpDMABuf_va = (u8 *)dma_alloc_coherent(NULL, 4096, &gpDMABuf_pa, GFP_KERNEL);
	if(!gpDMABuf_va){
		printk(KERN_INFO "[elan] Allocate DMA I2C Buffer failed\n");
	}
#endif

	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	fw_err = elan_ktf2k_ts_setup(client);
	printk("[elan]%s, fw_err:%d\n",__func__,fw_err);
	if (fw_err < 0) {
		// Can not detect Elan chip, maybe i2c can not use, or no Elan chip inside, or...
		printk(KERN_INFO "[elan] Can not detect Elan chip\n");
		return -1;
	}
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

	wake_lock_init(&private_ts->wakelock, WAKE_LOCK_SUSPEND, "elan_touch");

#if 1/*RESET RESOLUTION: tmp use ELAN_X_MAX & ELAN_Y_MAX*/ 
	touch_debug(1, "RESET RESOLUTION, tmp use ELAN_X_MAX & ELAN_Y_MAX\n");
	input_set_abs_params(tpd->dev, ABS_X, 0,  ELAN_X_MAX, 0, 0);
	input_set_abs_params(tpd->dev, ABS_Y, 0,  ELAN_Y_MAX, 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_POSITION_X, 0, ELAN_X_MAX, 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_POSITION_Y, 0, ELAN_Y_MAX, 0, 0);
#endif 

	#ifndef LCT_VIRTUAL_KEY
	set_bit( KEY_BACK,  tpd->dev->keybit );
	set_bit( KEY_HOMEPAGE,  tpd->dev->keybit );
	set_bit( KEY_MENU,  tpd->dev->keybit );
	#endif
	

	thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);

	if(IS_ERR(thread))
	{
		retval = PTR_ERR(thread);
		printk(TPD_DEVICE "[elan] failed to create kernel thread: %d\n", retval);
		return -1;
	}

	printk("[elan]  ELAN Touch Panel Device Probe %s\n", (retval < TPD_OK) ? "FAIL" : "PASS");
	
	//0814add
	if (sysfs_create_group(&client->dev.kobj, &elan_attribute_group))
	dev_err(&client->dev, "sysfs create group error\n");
	//0814
	
	// Firmware Update
	// MISC
	ts.firmware.minor = MISC_DYNAMIC_MINOR;
	ts.firmware.name = "elan-iap";
	ts.firmware.fops = &elan_touch_fops;
	ts.firmware.mode = S_IRWXUGO; 

	// Celia start, error handle
	printk("[elan] misc_register starting...\n");
	do {
		node_check_cnt++;
		node_err = misc_register(&ts.firmware);
		printk("[elan] misc_register result: %d\n", node_err);
	} while (node_err < 0 && node_check_cnt < 5);

	if (node_err< 0){
		printk("[elan] misc_register failed!! retry %d times.", node_check_cnt);
	} else {
		printk("[elan] misc_register finished!!");
	}
	// Celia end, error handle

	// End Firmware Update

#ifdef ESD_CHECK //0604
	INIT_DELAYED_WORK(&esd_work, elan_touch_esd_func);
	esd_wq = create_singlethread_workqueue("esd_wq");	
	if (!esd_wq) {
		retval = -ENOMEM;
		return -1;
	}
	
	queue_delayed_work(esd_wq, &esd_work, delay);
#endif

#if 1
	obj_ps.polling = 1; 
	//0--interrupt mode;1--polling mode;
	touch_debug(1,"touch_proxi_register\n");
	obj_ps.sensor_operate = tpd_ps_operate;
	//   printk("[elan]hwmsen_attach\n");
	if ((tp_err = hwmsen_attach(ID_PROXIMITY, &obj_ps)))
	{
		TPD_DEBUG("hwmsen attach fail, return:%d.", tp_err);
	}

#endif
#if IAP_PORTION
	if(1)
	{
		/* FW ID & FW VER*/
		/* For ektf21xx and ektf20xx  */
		//20141218 select load file by Ken -start
		TP_ID = mt_get_gpio_in(GPIO_MSDC2_DAT1);
		if(TP_ID) //(TP_ID == 0x01)
			file_fw_data = file_fw_data_2nd;
		else //(TP_ID == 0x00)
			file_fw_data = file_fw_data_main;
		//20141218 select load file by Ken -end
		New_FW_ID = file_fw_data[0x7D67]<<8  | file_fw_data[0x7D66] ;
		New_FW_VER = file_fw_data[0x7D65]<<8  | file_fw_data[0x7D64] ;

		printk(" FW_ID=0x%x,   New_FW_ID=0x%x \n",  FW_ID, New_FW_ID);   	       
		printk(" FW_VERSION=0x%x,   New_FW_VER=0x%x \n",  FW_VERSION  , New_FW_VER);  
		
		/* for firmware auto-upgrade  */
		// Elan start, update fw when device is recovery mode
		if (RECOVERY == 0X80) {
			printk("[elan] update FW when device is recovery mode");
			update_flag = 1;
		} else {
		// Elan end, update fw when device is recovery mode
			//20141218 new check update by Ken -start
			// Celia modified for improve FW update flow 20141224
			if (New_FW_VER > FW_VERSION) {
				printk("[elan] Update FW (New_FW_Ver > FW_VERSION)\n");
				update_flag = 1;
			} else if (New_FW_VER == FW_VERSION) {
				if (New_FW_ID != FW_ID) {
					printk("[elan] the sensor is different with existing FW_ID\n");
					update_flag = 1;
				} else {
					printk("[elan] FW vesion is the newest. Do not update.\n");
				}
			} else {
				printk("New_FW_Ver < FW_VERSION. Do no update FW\n");
			}
			//20141218 new check update by Ken -end
		}
		// Celia start. Modified for improve FW update flow
		if (update_flag) {
			printk("%s, start to process fw update\n", __func__);
			printk("[elan] FW Update work_lock=1, cancel_delayed_work\n");
			// Stop check esd when updating FW
			mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
			#ifdef ESD_CHECK
			cancel_delayed_work_sync(&esd_work);
			#endif
			power_lock = 1;
			wake_lock(&private_ts->wakelock);
			work_lock=1;

			Update_FW_One(client, RECOVERY);

			// Start check esd after FW updated
			work_lock=0;
			printk("[elan] FW Update work_lock=0, queue_delayed_work\n");
			mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
			wake_unlock(&private_ts->wakelock);
			#ifdef ESD_CHECK
			queue_delayed_work(esd_wq, &esd_work, delay);
			#endif
		} else {
			touch_debug(1, "%s, do not need update FW\n", __func__);
		}
		// Celia end. Modified for improve FW update flow

	}
#endif

	// Celia start. ELAN add reset TP
	// If did not receive 55:55:xx:xx, set RST
	if (reset_again) {
		touch_debug(1,"Reset in the end of probe function");
		// Reset Touch Pannel
		mt_set_gpio_mode( GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO );
		mt_set_gpio_dir( GPIO_CTP_RST_PIN, GPIO_DIR_OUT );
		mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
		mdelay(10);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
		mdelay(10);
		mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
	} else {
		touch_debug(1,"reset_again=%d, probe finished\n",reset_again);
	}
	// Celia end. ELAN add reset

	touch_debug(1,"%s: check charger state\n", __func__);
	elan_ktf2k_read_charger_mode(); //20150127 add by Ken. fix USB charger noise

	return 0;
}

//0721-start
#if defined(TP_REPLACE_ALSPS)
int ckt_tp_replace_ps_mod_on(void)
{
	int res = 0;
	char buf_on[] = {0x54, 0xC1, 0x00, 0x01};
	// ELAN start. Turn off ESD machnism when TP-PS on
	char buf_esd_off[] = {0x54, 0x2f, 0x00, 0x01};

	#ifdef ESD_CHECK
	cancel_delayed_work_sync(&esd_work);
	#endif
	res = i2c_master_send(i2c_client, buf_esd_off, sizeof(buf_esd_off));
	if (res != sizeof(buf_esd_off)) {
		printk("[elan-ps] close esd fail\n");
	} else {
		printk("[elan-ps] close esd success\n");
	}
	// ELAN end. Turn off ESD machnism when TP-PS on

	ckt_tp_replace_ps_close = 0;
	res = i2c_master_send(i2c_client, buf_on, sizeof(buf_on));
	if (res != sizeof(buf_on)) {
		printk("[elan-ps] turn on face mod faild\n");
		ckt_tp_replace_ps_state=0;
		return 0;
	} else {
		ckt_tp_replace_ps_state=1;
		printk("[elan-ps] turn on face mod ok\n");
		return 1;
	}
}

int ckt_tp_replace_ps_mod_off(void)
{
#if 1
	char buf_off[] = {0x54, 0xC0, 0x00, 0x01};
	// ELAN add. Turn on ESD machnism when TP-PS off
	char buf_esd_on[] = {0x54, 0x2f, 0x01, 0x01};
	ssize_t ret = 0;

	// ELAN start. Turn on ESD machnism when TP-PS off
	ret = i2c_master_send(i2c_client, buf_esd_on, sizeof(buf_esd_on));
	if (ret != sizeof(buf_esd_on)) {
		printk("[elan-ps] open esd fail\n");
	} else {
		printk("[elan-ps] open esd success\n");
	}
	#ifdef ESD_CHECK
	queue_delayed_work(esd_wq, &esd_work, delay);
	#endif
	// ELAN end. Turn on ESD machnism when TP-PS off

	ckt_tp_replace_ps_close = 0;
	ret = i2c_master_send(i2c_client, buf_off, sizeof(buf_off));
	if (ret != sizeof(buf_off)) {	
		ckt_tp_replace_ps_state=1;
		printk("[elan-ps] turn off face mod faild\n");
		return 0;
	} else {
		ckt_tp_replace_ps_state=0;
		printk("[elan-ps] turn off face mod ok\n");
		return 1;
	}
#else
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, 0);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, 1);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, 0);
	msleep(30);

	// for enable/reset pin
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, 0);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, 1);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, 1);
	msleep(300);
	ckt_tp_replace_ps_close = 0;
	ckt_tp_replace_ps_state=0;
	printk("[elan-ps] turn off face mod ok\n");
	return 1;
#endif
}
int  ckt_tp_replace_ps_enable(int enable)
{
	if (enable)
	{
		if(1==ckt_tp_replace_ps_mod_on())
		{
			printk("[elan-ps]open the ps mod successful\n");
			return 1;
		}
		else
		{
			printk("[elan-ps]open the ps mod fail\n");
			return 0;
		}
		
	}
	else
	{
		if(1==ckt_tp_replace_ps_mod_off())
		{
			printk("[elan-ps]close the ps mod successful\n");
			return 1;
		}
		else
		{
			printk("[elan-ps]close the ps mod fail\n");
			return 0;
		}
	}
}

u16  ckt_get_tp_replace_ps_value(void)
{
	if(1==ckt_tp_replace_ps_close)
	{
		printk("[elan-ps]ckt_get_tp_replace_ps_value 500\n");
		return 500;
	}
	else
	{
		printk("[elan-ps]ckt_get_tp_replace_ps_value 100\n");
		return 100;
	}
}
#endif
//0721-end

#ifdef ESD_CHECK  //0604
static void elan_touch_esd_func(struct work_struct *work)
{	
	touch_debug(1, "%s: enter.......\n", __FUNCTION__);
	elan_ktf2k_auto_change_charger();
	
	if(have_interrupts == 1){
		touch_debug(1, "%s: had interrup not need check\n", __func__);
	}
	else{

		mt_set_gpio_mode(GPIO_CTP_RST_PIN, 0);
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, 1);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, 0);
		msleep(10);

		// for enable/reset pin
		mt_set_gpio_mode(GPIO_CTP_RST_PIN, 0); 
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, 1);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, 1);
		msleep(100);
	}
	
	have_interrupts = 0;	
	queue_delayed_work(esd_wq, &esd_work, delay);
	touch_debug(1, "%s: exit.......\n", __FUNCTION__);
}
#endif

static int tpd_remove(struct i2c_client *client)

{
	printk("[elan] TPD removed\n");
	
	#ifdef MTK6589_DMA    
	if(gpDMABuf_va){
		dma_free_coherent(NULL, 4096, gpDMABuf_va, gpDMABuf_pa);
		gpDMABuf_va = NULL;
		gpDMABuf_pa = NULL;
	}
	#endif    

	return 0;
}


static int tpd_suspend(struct i2c_client *client, pm_message_t message)
{
	// Celia start. Touch release before suspend
	int rc;
	int syncNull = 0;
	uint8_t buf[PACKET_SIZE] = { 0 };
	uint8_t num;
	// Celia end. Touch release before suspend
	int retval = TPD_OK;
	uint8_t cmd[] = {CMD_W_PKT, 0x50, 0x00, 0x01};
	// ELAN solve TP report event when PS on (LCM dark)
	int res = 0;
	#if 1 //defined(TP_REPLACE_ALSPS)
	if(ckt_tp_replace_ps_state == 1)
	{
		printk("[elan-ps] %s: can't enter sleep mode when tp replace ps\n", __func__);
		return retval;
	}
	#endif

	printk("[elan] TP enter into sleep mode\n");
	// ELAN solve TP report event when PS on (LCM dark) start
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	re_suspend:
		if ((i2c_master_send(private_ts->client, cmd, sizeof(cmd))) != sizeof(cmd)) {
		printk("[elan] %s: i2c_master_send failed\n", __func__);
		return -retval;
		}

	res = elan_ktf2k_ts_get_power_state(private_ts->client);
	if (res!=0) goto re_suspend;
	// ELAN solve TP report event when PS on (LCM dark) end

	// Celia start. Touch release before suspend
	rc = elan_ktf2k_ts_recv_data(private_ts->client, buf);
	if (rc < 0) {
		printk("[elan] rc<0\n");
	}

	if ((buf[0] == MTK_FINGERS_PKT) || (buf[0] == FIVE_FINGERS_PKT)){
		syncNull = 1;
		touch_debug(1,"%s, suspend when touch\n",__func__);
	} else {
		syncNull = 0;
		touch_debug(1,"%s, suspend without touch on screen\n",__func__);
	}

	if (syncNull) {
		printk("[elan] release touch\n");
		msleep(50);
		input_mt_sync(tpd->dev);
		input_sync(tpd->dev);
	} else {
		touch_debug(1,"%s, suspend...\n",__func__);
	}
	// Celia end. Touch release before suspend

#ifdef ESD_CHECK
	cancel_delayed_work_sync(&esd_work);
#endif

	return retval;
}


static int tpd_resume(struct i2c_client *client)
{
	int retval = TPD_OK;
	uint8_t cmd[] = {CMD_W_PKT, 0x58, 0x00, 0x01};
	printk("[elan] %s wake up\n", __func__);

	//0721-start
#if defined(TP_REPLACE_ALSPS)
	if(ckt_tp_replace_ps_state == 1)
	{
		//ckt_tp_replace_ps_state = 0;
		printk(TPD_DEVICE "[elan-ps] %s: can't enter sleep mode when tp replace ps\n", __func__);
		return retval;
	}
#endif
	//0721-end
#ifdef ESD_CHECK
	queue_delayed_work(esd_wq, &esd_work, delay);
#endif

#if 1
	// Reset Touch Pannel
	mt_set_gpio_mode( GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO );
	mt_set_gpio_dir( GPIO_CTP_RST_PIN, GPIO_DIR_OUT );
	mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
	mdelay(10);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
	mdelay(10);
	mt_set_gpio_out( GPIO_CTP_RST_PIN, GPIO_OUT_ONE );
#else 
	if ((i2c_master_send(private_ts->client, cmd, sizeof(cmd))) != sizeof(cmd)) 
	{
		printk("[elan] %s: i2c_master_send failed\n", __func__);
		return -retval;
	}
#endif
	// ELAN modify for i2c transfer time and auto calibration time
	msleep(200);

	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	return retval;
}

static int tpd_local_init(void)
{
	printk("[elan]: I2C Touchscreen Driver init\n");
	if(i2c_add_driver(&tpd_i2c_driver) != 0)
	{
		printk("[elan]: unable to add i2c driver.\n");
		return -1;
	}
	
	if(tpd_load_status == 0) 
	{
		printk("ektf3248 add error touch panel driver.\n");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}    

#ifdef TPD_HAVE_BUTTON
#ifdef LCT_VIRTUAL_KEY
	tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
#endif
#endif
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
	TPD_DO_WARP = 1;
	memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
	memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
	memcpy(tpd_calmat, tpd_def_calmat_local, 8*4);
	memcpy(tpd_def_calmat, tpd_def_calmat_local, 8*4);         
#endif 
	printk("end %s, %d\n", __FUNCTION__, __LINE__);
	tpd_type_cap = 1;
	return 0;
}


static struct tpd_driver_t tpd_device_driver =
{
	.tpd_device_name = "ektf2k_mtk",       
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif
};

// Celia start (cci_hw_ic.c is later than TP driver, can not get velue through cci_hw_ic.c)
// Using two TP driver due to PDP1 (EVT1) use Synaptics, PDP2 (EVT2) use ELAN.
int cci_det_hw_id_forTP(void)
{
	int tmp_id = 0;
	int board_type;
	char* phase_id;

	//Detect HW ID
	mt_set_gpio_mode(GPIO_HW_ID_1, GPIO_HW_ID_1_M_GPIO); // GPIO_LCM_ID_M_GPIO or GPIO_LCM_ID_M_CLK
	mt_set_gpio_dir(GPIO_HW_ID_1, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_1, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_1) == 1) tmp_id|= SET_MODEL_TYPE_1;

	mt_set_gpio_mode(GPIO_HW_ID_2, GPIO_HW_ID_2_M_GPIO); // GPIO_LCM_ID_M_GPIO or GPIO_LCM_ID_M_CLK
	mt_set_gpio_dir(GPIO_HW_ID_2, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_2, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_2) == 1) tmp_id|= SET_MODEL_TYPE_2;

	mt_set_gpio_mode(GPIO_HW_ID_3, GPIO_HW_ID_3_M_GPIO); // GPIO_LCM_ID_M_GPIO or GPIO_LCM_ID_M_CLK
	mt_set_gpio_dir(GPIO_HW_ID_3, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_3, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_3) == 1) tmp_id|= SET_MODEL_TYPE_3;

	printk("[elan-cci] board_get_hw_id [%x]\n", tmp_id);

	return tmp_id;
}
// Celia end

static int __init tpd_driver_init(void)
{
	printk("[elan]: Driver Verison MTK0005 for MTK65xx serial\n");

	// Celia start. Using two TP driver due to PDP1 (EVT1) use Synaptics, PDP2 (EVT2) use ELAN
	int cci_hw_id = 9; // No phase is num 9.
	cci_hw_id = cci_det_hw_id_forTP();
	printk("[elan] cci_hw_id:%d, ",cci_hw_id);
	if(cci_hw_id == 1) {
		printk("Phase EVT1, TP do not use ELAN\n");
		return 0;
	} else {
		printk("After Phase EVT1, TP use ELAN\n");
		// Celia start. Using two TP driver due to PDP1 (EVT1) use Synaptics, PDP2 (EVT2) use ELAN
		#ifdef ELAN_MTK6577
			printk("[elan] Enable ELAN_MTK6577\n");
			i2c_register_board_info(0, &i2c_tpd, 1);
		#endif
		if(tpd_driver_add(&tpd_device_driver) < 0) {
			printk("[elan]: %s driver failed\n", __func__);
		}
	}
	return 0;
}

static void __exit tpd_driver_exit(void)
{
	printk("[elan]: %s elan touch panel driver exit\n", __func__);
	tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

