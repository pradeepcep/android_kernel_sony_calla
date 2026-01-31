#include <linux/kernel.h>
#include <linux/string.h>

#include <mach/mtk_rtc.h>
#include <mach/wd_api.h>
//[LY28] ==> CCI KLog, added by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
#include <linux/cciklog.h>
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, added by Jimmy@CCI

extern void wdt_arch_reset(char);

#ifdef CONFIG_SONY_S1_SUPPORT
#define S1_WARMBOOT_MAGIC_VAL (0xBEEF)
#define S1_WARMBOOT_NORMAL    (0x7651)
#define S1_WARMBOOT_S1        (0x6F53)
#define S1_WARMBOOT_FB        (0x7700)
#define S1_WARMBOOT_NONE      (0x0000)
#define S1_WARMBOOT_CLEAR     (0xABAD)
#define S1_WARMBOOT_TOOL      (0x7001)
#define S1_WARMBOOT_RECOVERY  (0x7711)
#define S1_WARMBOOT_FOTA      (0x6F46)

extern void write_magic(volatile unsigned long magic_write, int log_option);
#endif

void arch_reset(char mode, const char *cmd)
{
    char reboot = 0;
    int res=0;
    struct wd_api*wd_api = NULL;
    
    res = get_wd_api(&wd_api);
    printk("arch_reset: cmd = %s\n", cmd ? : "NULL");

    if (cmd && !strcmp(cmd, "charger")) {
        /* do nothing */
//[LY28] ==> CCI KLog, modified by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
		cklc_save_magic(KLOG_MAGIC_POWER_OFF, KLOG_STATE_NONE);
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, modified by Jimmy@CCI
    } else if (cmd && !strcmp(cmd, "recovery")) {
//[LY28] ==> CCI KLog, modified by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
		cklc_save_magic(KLOG_MAGIC_RECOVERY, KLOG_STATE_NONE);
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, modified by Jimmy@CCI
        rtc_mark_recovery();
    } else if (cmd && !strcmp(cmd, "bootloader")){
//[LY28] ==> CCI KLog, modified by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
		cklc_save_magic(KLOG_MAGIC_BOOTLOADER, KLOG_STATE_NONE);
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, modified by Jimmy@CCI
    		rtc_mark_fast();	
    } 
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
	else if (cmd && !strcmp(cmd, "kpoc")){
//[LY28] ==> CCI KLog, modified by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
		cklc_save_magic(KLOG_MAGIC_POWER_OFF, KLOG_STATE_NONE);
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, modified by Jimmy@CCI
		rtc_mark_kpoc();
	}
#endif
//CEI comments start//
//Add for handle oemS and oemF by adb reboot command
#ifdef CONFIG_SONY_S1_SUPPORT
	else if( (cmd && !strcmp(cmd, "oemS")) || (cmd && !strcmp(cmd, "oem-53")) )
	{
	    	reboot = 1;
		//rtc_mark_s1_service();
		write_magic(S1_WARMBOOT_MAGIC_VAL | (S1_WARMBOOT_S1 << 16), 0);
	}
	else if(cmd && !strcmp(cmd, "oemF"))
	{
	    	reboot = 1;
		//rtc_mark_fota();
		write_magic(S1_WARMBOOT_MAGIC_VAL | (S1_WARMBOOT_FOTA << 16), 0);
	}
#endif // #ifdef CONFIG_SONY_S1_SUPPORT
//CEI comments end//
    else {
    	reboot = 1;
//[LY28] ==> CCI KLog, modified by Jimmy@CCI
#ifdef CONFIG_CCI_KLOG
		cklc_save_magic(KLOG_MAGIC_REBOOT, KLOG_STATE_NONE);
#endif // #ifdef CONFIG_CCI_KLOG
//[LY28] <== CCI KLog, modified by Jimmy@CCI

#ifdef CONFIG_SONY_S1_SUPPORT
		write_magic(S1_WARMBOOT_MAGIC_VAL | (S1_WARMBOOT_NORMAL << 16), 0);
#endif
    }

    if(res){
        printk("arch_reset, get wd api error %d\n",res);
    } else {
        wd_api->wd_sw_reset(reboot);
    }
}

