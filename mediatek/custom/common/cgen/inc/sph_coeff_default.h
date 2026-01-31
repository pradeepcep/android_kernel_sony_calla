/*******************************************************************************
 *
 * Filename:
 * ---------
 *
 *
 * Project:
 * --------
 *   ALPS
 *
 * Description:
 * ------------
 *
 * Author:
 * -------
 * ChiPeng
 *
 *------------------------------------------------------------------------------
 * $Revision:$ 1.0.0
 * $Modtime:$
 * $Log:$
 *
 * 06 26 2010 chipeng.chang
 * [ALPS00002705][Need Patch] [Volunteer Patch] ALPS.10X.W10.11 Volunteer patch for speech parameter 
 * modify speech parameters.
 *
 * Mar 15 2010 mtk02308
 * [ALPS] Init Custom parameter
 *
 *

 *
 *
 *******************************************************************************/
#ifndef SPEECH_COEFF_DEFAULT_H
#define SPEECH_COEFF_DEFAULT_H

#ifndef FALSE
#define FALSE 0
#endif

//speech parameter depen on BT_CHIP cersion
#if defined(MTK_MT6611)

#define BT_COMP_FILTER (1 << 15)
#define BT_SYNC_DELAY  86

#elif defined(MTK_MT6612)

#define BT_COMP_FILTER (1 << 15)
#define BT_SYNC_DELAY  86

#elif defined(MTK_MT6616) || defined(MTK_MT6620) || defined(MTK_MT6622) || defined(MTK_MT6626) || defined(MTK_MT6628)

#define BT_COMP_FILTER (1 << 15)
#define BT_SYNC_DELAY  86

#else // MTK_MT6620

#define BT_COMP_FILTER (0 << 15)
//#define BT_SYNC_DELAY  86
#define BT_SYNC_DELAY  0

#endif

#ifdef MTK_DUAL_MIC_SUPPORT

  #ifndef MTK_INTERNAL
  //#define SPEECH_MODE_PARA13 (371)
  //#define SPEECH_MODE_PARA14 (23)
  //#define SPEECH_MODE_PARA03 (29)
  //#define SPEECH_MODE_PARA08 (400)
  #define SPEECH_MODE_PARA13 (0)
  #define SPEECH_MODE_PARA14 (0)
  #define SPEECH_MODE_PARA03 (31)
  #define SPEECH_MODE_PARA08 (80)
  #else
  #define SPEECH_MODE_PARA13 (0)
  #define SPEECH_MODE_PARA14 (0)
  #define SPEECH_MODE_PARA03 (31)
  #define SPEECH_MODE_PARA08 (80)
  #endif

#else
#define SPEECH_MODE_PARA13 (0)
#define SPEECH_MODE_PARA14 (0)
#define SPEECH_MODE_PARA03 (31)
#define SPEECH_MODE_PARA08 (80)


#endif

#ifdef NXP_SMARTPA_SUPPORT
	#define MANUAL_CLIPPING (1 << 15)
	#define NXP_DELAY_REF   (1 << 6)
	#define PRE_CLIPPING_LEVEL 32767
#else
	#define MANUAL_CLIPPING (0 << 15)
	#define NXP_DELAY_REF   (0 << 6) 
	#define PRE_CLIPPING_LEVEL 10752
#endif



#define DEFAULT_SPEECH_NORMAL_MODE_PARA \
    96,   253, 16388,    31, 57351,    31,   400,    48,\
    80,  4293,   611,     0, 20488, 49523,    23,  8192

#define DEFAULT_SPEECH_EARPHONE_MODE_PARA \
    64,   253, 16388,    31, 57351,    31,   400,   112,\
    80,  4293,   611,     0, 20488,     0,     0,     0

#define DEFAULT_SPEECH_BT_EARPHONE_MODE_PARA \
     0,   253, 10756,    31, 53255,  31,   400,     0, \
    80,  4325,   611,     0, 20488|BT_COMP_FILTER,     0,     0,BT_SYNC_DELAY

#define DEFAULT_SPEECH_LOUDSPK_MODE_PARA \
    96,   224,  3236,    31, 57351,    31,   400,   192,\
   144,  4293,   610,     0, 20500,     0,     0,     0

#define DEFAULT_SPEECH_CARKIT_MODE_PARA \
    96,   224,  5256,    31, 57351, 24607,   400,   132, \
    84,  4325,    611,     0, 20488,        0,     0,     0

#define DEFAULT_SPEECH_BT_CORDLESS_MODE_PARA \
    0,      0,      0,      0,      0,      0,      0,      0, \
    0,      0,      0,      0,      0,      0,      0,      0

#define DEFAULT_SPEECH_AUX1_MODE_PARA \
    0,      0,      0,      0,      0,      0,      0,      0, \
    0,      0,      0,      0,      0,      0,      0,      0

#define DEFAULT_SPEECH_AUX2_MODE_PARA \
    0,      0,      0,      0,      0,      0,      0,      0, \
    0,      0,      0,      0,      0,      0,      0,      0

#define DEFAULT_SPEECH_VOICE_TRACKING_MODE_PARA \
    96|MANUAL_CLIPPING ,   224,  5256,    31, 57351, 24607,   400,   132, \
    84,  4325,    611,     0, 8200|NXP_DELAY_REF,     883,     23,     0

#define DEFAULT_SPEECH_HAC_MODE_PARA \
   96,   253, 16388,SPEECH_MODE_PARA03, 57351,    31,   400,    48,\
SPEECH_MODE_PARA08, 4293,   611,     0, 20488,   49523|SPEECH_MODE_PARA13,   23|SPEECH_MODE_PARA14,  8192

#define DEFAULT_SPEECH_COMMON_PARA \
     6, 55997, 31000, 10752, 32769,     4,     0,     0, \
    0,      0,      0,      0

#define DEFAULT_SPEECH_VOL_PARA \
    0,      0,      0,      0

#define DEFAULT_AUDIO_DEBUG_INFO \
    0,      0,      0,      0,      0,      0,      0,      0, \
    0,      0,      0,      0,      0,      0,      0,      0

#define DEFAULT_VM_SUPPORT  FALSE

#define DEFAULT_AUTO_VM     FALSE

#define DEFAULT_WB_SPEECH_NORMAL_MODE_PARA \
    96,   253, 16388,    31, 57607,    31,   400,    48,\
    80,  4293,   611,     0, 20488, 50035,   424,  8192

#define DEFAULT_WB_SPEECH_EARPHONE_MODE_PARA \
    64,   253, 16388,    31, 57607,    31,   400,   128,\
   144,  4293,   611,     0, 20488,     0,     0,     0

#define DEFAULT_WB_SPEECH_BT_EARPHONE_MODE_PARA \
     0,   253, 10756,    31, 53511,  31,   400,     0, \
    80,  4325,   611,     0, 16392|BT_COMP_FILTER,     0,     0,BT_SYNC_DELAY

#define DEFAULT_WB_SPEECH_LOUDSPK_MODE_PARA \
    96,   224,  2218,    31, 57607,    31,   400,   192,\
   400,  4293,   610,     0, 20488,     0,     0,     0

#define DEFAULT_WB_SPEECH_CARKIT_MODE_PARA \
 65523, 65441,   151,   210, 65285, 65519, 65470,   664,\
 65205, 65480, 65092,  1028,   705, 64483, 64568,  1349

#define DEFAULT_WB_SPEECH_BT_CORDLESS_MODE_PARA \
   169,   253, 65484,    41,   186,   172, 65510, 65475,\
   177,   101,   199, 65413, 65533,    86,   192, 65496

#define DEFAULT_WB_SPEECH_AUX1_MODE_PARA \
  2451, 63847, 63613,   411,  4846, 63269, 64131, 60768,\
  6970,  1338,  1829, 47988, 32767, 32767, 47988,  1829

#define DEFAULT_WB_SPEECH_AUX2_MODE_PARA \
  1338,  6970, 60768, 64131, 63269,  4846,   411, 63613,\
 63847,  2451,  1349, 64568, 64483,   705,  1028, 65092

#define DEFAULT_WB_SPEECH_VOICE_TRACKING_MODE_PARA \
    96|MANUAL_CLIPPING,   224,  5256,    31, 57607, 24607,   400,   132, \
    84,  4325,   611,     0,  8200|NXP_DELAY_REF,     883,     23,     0  

#define DEFAULT_WB_SPEECH_HAC_MODE_PARA \
   96,   253, 16388,SPEECH_MODE_PARA03, 57607,    31,   400,    48,\
SPEECH_MODE_PARA08,  4293,   611,     0, 20488, 50035|SPEECH_MODE_PARA13,   424|SPEECH_MODE_PARA14,  8192

#define MICBAIS  1900

/* The Bluetooth PCM digital volume */
/* default_bt_pcm_in_vol : uplink, only for enlarge volume,
                           0x100 : 0dB  gain
                           0x200 : 6dB  gain
                           0x300 : 9dB  gain
                           0x400 : 12dB gain
                           0x800 : 18dB gain
                           0xF00 : 24dB gain             */

#define DEFAULT_BT_PCM_IN_VOL        0x100
/* default_bt_pcm_out_vol : downlink gain,
                           0x1000 : 0dB; maximum 0x7FFF  */
#define DEFAULT_BT_PCM_OUT_VOL       0x1000

#endif
