#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <mach/mt_gpio.h>
#include <mach/mt_gpio_core.h>
#include <cust_gpio_usage.h>
#include "cci_hw_id.h"

/*
  bit 0 -- HW ID 1 (GPIO93)
  bit 1 -- HW ID 2 (GPIO92)
  bit 2 -- HW ID 3 (GPIO91)

  bit 3 -- Project ID 1 (GPIO88)
  bit 4 -- Project ID 2 (GPIO89)
  bit 5 -- Project ID 3 (GPIO90)
  to be con.
*/
int cci_hw_id = 0;
char* cci_phase_name;

const char cci_board_type_str[][20] = 
{
"EVT1 board", //0 0 1
"EVT2 board", //0 0 0
"DVT1 board",  //1 0 0
"DVT2 board",  //0 1 0
"PVT board",   //1 1 0
""
};

const char cci_project_str[][20] = 
{
"LY28", //0 0 0
"LY29", //0 0 1	
"LY30",  //0 1 0
"LY31",//0 1 1
"LY32", //1 0 0
"LY33", //1 0 1
"LY34",//1 1 0
"LY35",//1 1 1
""
};

int get_cci_hw_id(void)
{
	return cci_hw_id;
}
EXPORT_SYMBOL(get_cci_hw_id);

char* get_cci_phase_name(void)
{
	return cci_phase_name;
}
EXPORT_SYMBOL(get_cci_phase_name);

int board_type_with_hw_id(void)
{
    //return ( ( (cci_hw_id>>4) & 0x07 ) + 1);
    return (cci_hw_id & 0x07);
}
EXPORT_SYMBOL(board_type_with_hw_id);

int project_id_with_hw_id(void)
{
    return ( ((cci_hw_id>>3) & 0x07));
}
EXPORT_SYMBOL(project_id_with_hw_id);

const char* get_cci_hw_id_name(int value)
{
	char* board_name;
	switch(value)
	{
		case 1:
			board_name = cci_board_type_str[0];
			return board_name;
			break;
		case 0:
			board_name = cci_board_type_str[1];
			return board_name;
			break;
		case 4:
			board_name = cci_board_type_str[2];
			return board_name;
			break;
		case 2:
			board_name = cci_board_type_str[3];
			return board_name;
			break;
		case 6:
			board_name = cci_board_type_str[4];
			return board_name;
			break;
		default:
			board_name = cci_board_type_str[5];
			printk("EROOR: Not support HW ID numebr\n");
			return board_name;
			break;
	}
}

void cci_det_hw_id(void)
{
	int tmp_id = 0;
	int board_type =0;
	int project_id = 0;
	//char* project_name;

	//Detect HW ID
	
	//Phase ID GPIO91/92/93-->Begin
	mt_set_gpio_mode(GPIO_HW_ID_1, GPIO_HW_ID_1_M_GPIO); 
	mt_set_gpio_dir(GPIO_HW_ID_1, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_1, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_1) == 1) tmp_id|= SET_MODEL_TYPE_1;

	mt_set_gpio_mode(GPIO_HW_ID_2, GPIO_HW_ID_2_M_GPIO); 
	mt_set_gpio_dir(GPIO_HW_ID_2, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_2, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_2) == 1) tmp_id|= SET_MODEL_TYPE_2;

	mt_set_gpio_mode(GPIO_HW_ID_3, GPIO_HW_ID_3_M_GPIO); 
	mt_set_gpio_dir(GPIO_HW_ID_3, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
	mt_set_gpio_pull_enable(GPIO_HW_ID_3, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
	if (mt_get_gpio_in(GPIO_HW_ID_3) == 1) tmp_id|= SET_MODEL_TYPE_3;
	//Phase ID GPIO91/92/93-->End

	

	cci_hw_id = tmp_id;

	board_type = board_type_with_hw_id();
	cci_phase_name = get_cci_hw_id_name(board_type);

	//Project ID--B
	if (board_type == 1) //EVT1 board GPIO85 & GPIO84 for project ID
	{
		mt_set_gpio_mode(GPIO_HW_ID_10, GPIO_HW_ID_10_M_GPIO); 
		mt_set_gpio_dir(GPIO_HW_ID_10, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
		mt_set_gpio_pull_enable(GPIO_HW_ID_10, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
		if (mt_get_gpio_in(GPIO_HW_ID_10) == 1) tmp_id|= SET_MODEL_TYPE_4;

		mt_set_gpio_mode(GPIO_HW_ID_9, GPIO_HW_ID_9_M_GPIO); 
		mt_set_gpio_dir(GPIO_HW_ID_9, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
		mt_set_gpio_pull_enable(GPIO_HW_ID_9, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
		if (mt_get_gpio_in(GPIO_HW_ID_9) == 1) tmp_id|= SET_MODEL_TYPE_5;
	}
	else	//After EVT1 board, using GPIO 88~90 for project ID
	{

		//Project ID GPIO88/89/90-->Begin
		mt_set_gpio_mode(GPIO_HW_ID_6, GPIO_HW_ID_6_M_GPIO); 
		mt_set_gpio_dir(GPIO_HW_ID_6, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
		mt_set_gpio_pull_enable(GPIO_HW_ID_6, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
		if (mt_get_gpio_in(GPIO_HW_ID_6) == 1) tmp_id|= SET_MODEL_TYPE_4;

		mt_set_gpio_mode(GPIO_HW_ID_5, GPIO_HW_ID_5_M_GPIO); 
		mt_set_gpio_dir(GPIO_HW_ID_5, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
		mt_set_gpio_pull_enable(GPIO_HW_ID_5, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
		if (mt_get_gpio_in(GPIO_HW_ID_5) == 1) tmp_id|= SET_MODEL_TYPE_5;

		mt_set_gpio_mode(GPIO_HW_ID_4, GPIO_HW_ID_4_M_GPIO); // 
		mt_set_gpio_dir(GPIO_HW_ID_4, GPIO_DIR_IN); // GPIO_DIR_IN or GPIO_DIR_OUT
		mt_set_gpio_pull_enable(GPIO_HW_ID_4, GPIO_PULL_DISABLE); // GPIO_PULL_DISABLE or GPIO_PULL_ENABLE
		if (mt_get_gpio_in(GPIO_HW_ID_4) == 1) tmp_id|= SET_MODEL_TYPE_6;
		//Project ID GPIO88/89/90-->end
	}

	cci_hw_id = tmp_id;
	project_id = project_id_with_hw_id();

	//Project ID--E

	//printk("//board_get_hw_id [%x], %s\n", cci_hw_id, 
   	//cci_board_type_str[board_type - 1]);
	printk("//board_get_hw_id [%x], %s, %s\n", cci_hw_id, get_cci_phase_name(), cci_project_str[project_id]);

	return ;
	
	//Detect HW ID
}


static int board_type_proc_show(struct seq_file *m, void *v)
{

	int board_type;
	char* name;

	board_type = board_type_with_hw_id();
	name = get_cci_hw_id_name(board_type);
	//seq_printf(m, "%s\n",
		//cci_board_type_str[board_type - 1]);
	seq_printf(m, "%s\n",name);

	return 0;
}

static int cci_hwid_info_proc_show(struct seq_file *m, void *v)
{
	int hwid,projid;
	hwid = get_cci_hw_id();
	projid = project_id_with_hw_id();

	seq_printf(m, "%s=%d %s=%d %s=%d\n", "hwid", hwid, "phaseid", board_type_with_hw_id(), "projectid", projid);

	return 0;
}

static int board_type_open_proc(struct inode *inode, struct file *file)
{
	return single_open(file, board_type_proc_show, NULL);
}

static int cci_hwid_info_open_proc(struct inode *inode, struct file *file)
{
	return single_open(file, cci_hwid_info_proc_show, NULL);
}

static const struct file_operations board_type_proc_fops = {
	.open		= board_type_open_proc,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations cci_hwid_info_proc_fops = {
	.open		= cci_hwid_info_open_proc,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init cci_hw_id_init(void)
{
	

	int err = 0;
	printk("cci_hwid_init enter\n");
	cci_det_hw_id();

	proc_create("cci_hw_board_type",0,NULL,&board_type_proc_fops);
	proc_create("cci_hwid_info",0,NULL,&cci_hwid_info_proc_fops);
	
	return err;
}

static void __exit cci_hw_id_exit(void)
{
	printk("cci_hwid_exit enter\n");
}

module_init(cci_hw_id_init);
module_exit(cci_hw_id_exit);

MODULE_DESCRIPTION("cci hardware ID driver");
MODULE_LICENSE("GPL");
