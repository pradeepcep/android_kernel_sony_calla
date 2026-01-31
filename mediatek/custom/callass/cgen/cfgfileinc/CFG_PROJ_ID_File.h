/*
 * Copyright (C) 2014 Sony Mobile Communications AB.
 * All rights, including trade secret rights, reserved.
 */
 
#ifndef _CFG_PROJ_ID_FILE_H
#define _CFG_PROJ_ID_FILE_H

typedef struct
{
   char proj_id[16];
}FILE_PROJ_ID_STRUCT;

#define CFG_FILE_PROJ_ID_REC_SIZE    sizeof(FILE_PROJ_ID_STRUCT)
#define CFG_FILE_PROJ_ID_REC_TOTAL   1

#endif

