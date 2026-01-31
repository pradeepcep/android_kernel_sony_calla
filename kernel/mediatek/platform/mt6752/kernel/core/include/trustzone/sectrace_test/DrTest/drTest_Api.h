/*
 * Copyright (c) 2013 TRUSTONIC LIMITED
 * All rights reserved
 * 
 * The present software is the confidential and proprietary information of
 * TRUSTONIC LIMITED. You shall not disclose the present software and shall
 * use it only in accordance with the terms of the license agreement you
 * entered into with TRUSTONIC LIMITED. This software may be subject to
 * export or import laws in certain countries.
 */

/**
 * @file   drfoo_Api.h
 * @brief  Contains DCI command definitions and data structures
 *
 */

#ifndef __DRTESTAPI_H__
#define __DRTESTAPI_H__

#include "dci.h"

/*
 * Command ID's
 */
#define CMD_ID_MAP          1
#define CMD_ID_UNMAP        2
#define CMD_ID_TRANSACT     3
#define CMD_ID_PRINT        256
/*... add more command ids when needed */

/**
 * command message.
 *
 * @param len Lenght of the data to process.
 * @param data Data to be processed
 */
typedef struct {
    dciCommandHeader_t  header;     /**< Command header */
    uint32_t            len;        /**< Length of data to process */
} dr_cmd_t;

/**
 * Response structure
 */
typedef struct {
    dciResponseHeader_t header;     /**< Response header */
    uint32_t            len;
} dr_rsp_t;

/*
 * map/unmap data structure
 */
typedef struct {
    unsigned long pa;
    unsigned long size;
} test_data_t;

/*
 * DCI message data.
 */
typedef struct {
    union {
        dr_cmd_t     command;
        dr_rsp_t     response;
    };

    union {
        test_data_t  test_data;
    };

} dciMessage_t;

/*
 * Driver UUID. Update accordingly after reserving UUID
 */
#define DRV_TEST_UUID { { 2, 0xd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }

#endif // __DRTESTAPI_H__
