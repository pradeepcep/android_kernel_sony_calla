/*
 * Copyright (C) 2014 Sony Mobile Communications AB.
 * All rights, including trade secret rights, reserved.
 */

#ifndef _CFG_P_SENSOR_THRESHOLD_FILE_H
#define _CFG_P_SENSOR_THRESHOLD_FILE_H

typedef struct
{
	int data[20];
    unsigned int P_SENSOR_CALIBRATION;
    unsigned int PPCOUNT;
    unsigned int HIGHT_THRESHOLD;
    unsigned int LOW_THRESHOLD;
    unsigned int P_OFFSET;
}FILE_P_SENSOR_THRESHOLD_STRUCT;

#define CFG_FILE_P_SENSOR_THRESHOLD_REC_SIZE    sizeof(FILE_P_SENSOR_THRESHOLD_STRUCT)
#define CFG_FILE_P_SENSOR_THRESHOLD_REC_TOTAL   1

#endif

