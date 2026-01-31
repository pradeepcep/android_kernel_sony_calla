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

#ifndef TLPROXY_H_
#define TLPROXY_H_

#include "tci.h"

/*
 * Command ID's for communication Trustlet Connector -> Trustlet.
 */
#define CMD_PROXY_MAP       1
#define CMD_PROXY_UNMAP     2
#define CMD_PROXY_TRANSACT  3

/*
 * Termination codes
 */
#define EXIT_ERROR                  ((uint32_t)(-1))

/*
 * command message.
 *
 * @param len Lenght of the data to process.
 * @param data Data to processed (cleartext or ciphertext).
 */
typedef struct {
    tciCommandHeader_t  header;     /**< Command header */
    uint32_t            len;        /**< Length of data to process or buffer */
    uint32_t            respLen;    /**< Length of response buffer */
} cmd_t;

/*
 * Response structure Trustlet -> Trustlet Connector.
 */
typedef struct {
    tciResponseHeader_t header;     /**< Response header */
    uint32_t            len;
} rsp_t;

/*
 * TCI message data.
 */
typedef struct {
    union {
      cmd_t     cmd;
      rsp_t     rsp;
    };
    unsigned long value1;
    unsigned long value2;
    unsigned long value3;	
    uint32_t ResultData;
} tciMessage_t;

/*
 * Trustlet UUID.
 */
#define TL_PROXY_UUID { { 8, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }

#endif // TLPROXY_H_