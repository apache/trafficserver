/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

#include "Protocol.h"

typedef int (*TxnSMHandler)(TSCont contp, TSEvent event, void *data);

TSCont TxnSMCreate(TSMutex pmutex, TSVConn client_vc, int server_port);

#define TXN_SM_ALIVE 0xAAAA0123
#define TXN_SM_DEAD 0xFEE1DEAD
#define TXN_SM_ZERO 0x00001111

/* The Txn State Machine */
typedef struct _TxnSM {
  unsigned int q_magic;

  TSMutex q_mutex;
  TSAction q_pending_action;
  TxnSMHandler q_current_handler;

  TSVConn q_client_vc;
  TSVConn q_server_vc;

  char *q_client_request;
  char *q_server_response;

  char *q_file_name;
  TSCacheKey q_key;

  char *q_server_name;
  int q_server_port;

  TSVIO q_client_read_vio;
  TSVIO q_client_write_vio;
  TSIOBuffer q_client_request_buffer;
  TSIOBuffer q_client_response_buffer;
  TSIOBufferReader q_client_request_buffer_reader;
  TSIOBufferReader q_client_response_buffer_reader;

  TSVIO q_server_read_vio;
  TSVIO q_server_write_vio;
  TSIOBuffer q_server_request_buffer;
  TSIOBuffer q_server_response_buffer;
  TSIOBufferReader q_server_request_buffer_reader;
  int q_server_response_length;
  int q_block_bytes_read;
  int q_cache_response_length;

  /* Cache related */
  TSVConn q_cache_vc;
  TSIOBufferReader q_cache_response_buffer_reader;
  TSVIO q_cache_read_vio;
  TSVIO q_cache_write_vio;
  TSIOBuffer q_cache_read_buffer;
  TSIOBufferReader q_cache_read_buffer_reader;

} TxnSM;
