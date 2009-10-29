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

#ifndef TXN_SM_H
#define TXN_SM_H

#include "Protocol.h"

typedef int (*TxnSMHandler) (INKCont contp, INKEvent event, void *data);

INKCont TxnSMCreate(INKMutex pmutex, INKVConn client_vc, int server_port);

#define TXN_SM_ALIVE 0xAAAA0123
#define TXN_SM_DEAD  0xFEE1DEAD
#define TXN_SM_ZERO  0x00001111

/* The Txn State Machine */
typedef struct _TxnSM
{
  unsigned int q_magic;

  INKMutex q_mutex;
  INKAction q_pending_action;
  TxnSMHandler q_current_handler;

  INKVConn q_client_vc;
  INKVConn q_server_vc;

  char *q_client_request;
  char *q_server_response;

  char *q_file_name;
  char *q_key;

  char *q_server_name;
  INKU32 q_server_ip;
  int q_server_port;

  INKVIO q_client_read_vio;
  INKVIO q_client_write_vio;
  INKIOBuffer q_client_request_buffer;
  INKIOBuffer q_client_response_buffer;
  INKIOBufferReader q_client_request_buffer_reader;
  INKIOBufferReader q_client_response_buffer_reader;

  INKVIO q_server_read_vio;
  INKVIO q_server_write_vio;
  INKIOBuffer q_server_request_buffer;
  INKIOBuffer q_server_response_buffer;
  INKIOBufferReader q_server_request_buffer_reader;
  int q_server_response_length;
  int q_block_bytes_read;
  int q_cache_response_length;

  /* Cache related */
  INKVConn q_cache_vc;
  INKIOBufferReader q_cache_response_buffer_reader;
  INKVIO q_cache_read_vio;
  INKVIO q_cache_write_vio;
  INKIOBuffer q_cache_read_buffer;
  INKIOBufferReader q_cache_read_buffer_reader;

} TxnSM;

#endif /* Txn_SM_H */
