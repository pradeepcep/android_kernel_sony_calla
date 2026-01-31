#include <linux/types.h>
#include <mach/mt_pm_ldo.h>
#include <cust_mag.h>

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


static struct mag_hw cust_mag_hw = {
    .i2c_num = 1,
    .direction = 7,
    .power_id = MT65XX_POWER_NONE,  /*!< LDO is not used */
    .power_vol= VOL_DEFAULT,        /*!< LDO is not used */
};
struct mag_hw* get_cust_mag_hw(void) 
{
    Printhh("[%s] enter..\n", __FUNCTION__);

    return &cust_mag_hw;
}
