/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __MONEY_TRACE_H
#define __MONEY_TRACE_H

#include <random>
#include <ctime>
#include "ts/ts.h"
#include "ts/remap.h"

#define PLUGIN_NAME "money_trace"

#define LOG_DEBUG(fmt, ...)                                                                  \
  do {                                                                                       \
    TSDebug(PLUGIN_NAME, "[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)

#define LOG_ERROR(fmt, ...)                                                     \
  do {                                                                          \
    TSError("[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)

#define MIME_FIELD_MONEY_TRACE "X-MoneyTrace"
#define MIME_LEN_MONEY_TRACE 12

struct MT {
  std::minstd_rand0 generator;

  MT() { generator.seed(time(nullptr)); }
  long
  spanId()
  {
    long v = generator();
    return (v * v);
  }
  const char *moneyTraceHdr(const char *mt_request_hdr);
};

struct txndata {
  char *client_request_mt_header;
  char *new_span_mt_header;
};

static struct txndata *allocTransactionData();
static void freeTransactionData(struct txndata *txn_data);
static void mt_cache_lookup_check(TSCont contp, TSHttpTxn txnp, struct txndata *txn_data);
static void mt_check_request_header(TSHttpTxn txnp);
static void mt_send_client_response(TSHttpTxn txnp, struct txndata *txn_data);
static void mt_send_server_request(TSHttpTxn txnp, struct txndata *txn_data);
static int transaction_handler(TSCont contp, TSEvent event, void *edata);

#endif
