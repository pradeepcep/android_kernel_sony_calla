#include <linux/types.h>
#include <cust_acc.h>
#include <mach/mt_pm_ldo.h>

/*---------------------  Static Definitions -------------------------*/
#define HH_DEBUG 0   //0:disable, 1:enable
#if(HH_DEBUG)
    #define Printhh(string, args...)    printk("HH(K)=> "string, ##args);
#else
    #define Printhh(string, args...)
#endif

#define HH_TIP 0 //give RD information. Set 1 if develop,and set 0 when release.
#if(HH_TIP)
    #define PrintTip(string, args...)    printk("HH(K)=> "string, ##args);
#else
    #define PrintTip(string, args...)
#endif
/*---------------------  Static Classes  ----------------------------*/

/*---------------------------------------------------------------------------*/
int cust_acc_power(struct acc_hw *hw, unsigned int on, char* devname)
{
	Printhh("[%s] enter..\n", __FUNCTION__);

#ifndef FPGA_EARLY_PORTING
    if (hw->power_id == MT65XX_POWER_NONE)
        return 0;
    if (on)
        return hwPowerOn(hw->power_id, hw->power_vol, devname);
    else
        return hwPowerDown(hw->power_id, devname); 
#else
    return 0;
#endif
}
/*---------------------------------------------------------------------------*/
//CEI comments start// 
//Fix g-sensor posiztion not correct.
//CEI comments end//
static struct acc_hw cust_acc_hw = {
    .i2c_num = 1,
//    .direction = 6,
    .direction = 3, // for PDP1, it's ok.
    //.direction = 1,//testing bad, why?? need recali?
    .power_id = MT65XX_POWER_NONE,  /*!< LDO is not used */
    .power_vol= VOL_DEFAULT,        /*!< LDO is not used */
    .firlen = 16,                   /*!< don't enable low pass fileter */
    .power = cust_acc_power,        
    //.is_batch_supported = false,
    .is_batch_supported = true,
};

static struct acc_hw cust_acc_hw_prePDP2 = {
    .i2c_num = 1,
    .direction = 4,
    .power_id = MT65XX_POWER_NONE,  /*!< LDO is not used */
    .power_vol= VOL_DEFAULT,        /*!< LDO is not used */
    .firlen = 16,                   /*!< don't enable low pass fileter */
    .power = cust_acc_power,        
    //.is_batch_supported = false,
    .is_batch_supported = true,
};

static struct acc_hw cust_acc_hw_mainPDP2 = {
    .i2c_num = 1,
    .direction = 5,
    .power_id = MT65XX_POWER_NONE,  /*!< LDO is not used */
    .power_vol= VOL_DEFAULT,        /*!< LDO is not used */
    .firlen = 16,                   /*!< don't enable low pass fileter */
    .power = cust_acc_power,        
    //.is_batch_supported = false,
    .is_batch_supported = true,
};

/*---------------------------------------------------------------------------*/
//CEI comments start//
extern int get_cci_hw_id(void);
int g_iAllId = 0;
extern int board_type_with_hw_id(void);
int g_iHwPhaseId = -1;
//CEI comments end//


struct acc_hw* get_cust_acc_hw(void) 
{
    Printhh("[%s] enter..\n", __FUNCTION__);


//CEI comments start//
    //g_iAllId = get_cci_hw_id();
    g_iHwPhaseId = board_type_with_hw_id();

    Printhh("[%s] g_iAllId = %#x\n", __FUNCTION__, g_iAllId);
    Printhh("[%s] g_iHwPhaseId = %#x\n", __FUNCTION__, g_iHwPhaseId);


    //g_iHwPhaseId = 9;   // for pre-PDP2 test
    if(g_iHwPhaseId  == 1)
    {
        //PDP1
        return &cust_acc_hw;
    }
    else if(g_iHwPhaseId  == 9)
    {
        //prePDP2
        return &cust_acc_hw_prePDP2;
    }
    else if(g_iHwPhaseId  == 0)
    {
        //PDP2
        //return &cust_acc_hw_prePDP2;
        return &cust_acc_hw_mainPDP2;
    }
    else
    {
        return &cust_acc_hw_mainPDP2;
    }
//CEI comments end//

#if 0   //MTK org
    return &cust_acc_hw;
#endif
}
