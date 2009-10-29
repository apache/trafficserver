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

/*   test_dealloc.c
 *
 *   This plugin is used to show that destroying your continuation
 *   on a TXN_EVENT (or any event that isn't IMMEDIATE or INTERVAL)
 *   leads to the continuation and it's mutex leaking
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "InkAPI.h"

static int
test_destroy_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  if (event == INK_EVENT_HTTP_TXN_CLOSE) {
    INKContDestroy(contp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  } else {
    INKAssert(0);
  }
  return 0;
}

static int
attach_test(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKCont new_cont;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    new_cont = INKContCreate(test_destroy_plugin, INKMutexCreate());
    INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, new_cont);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;
  default:
    INKAssert(0);
  }
  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(attach_test, NULL));
}
