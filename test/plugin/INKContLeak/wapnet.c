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

#include <stdio.h>
#include <string.h>
#include "ts.h"



static int
handle_log_msisdn(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn) edata;

  printf(" handle_log_msisdn \n");
  INKContDestroy(contp);
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  return 0;
}


static int
handle_request(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKCont continuation;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("Couldn't retrieve client request header !");
    return -1;
  }
  printf("In handle_request \n");
  continuation = INKContCreate(handle_log_msisdn, NULL);
  INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, continuation);
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  printf(" handle_request: transaction conitnuing \n");
  return 0;
}


void
INKPluginInit(int argc, const char *argv[])
{

  printf(" INKPluginInit\n");
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(handle_request, NULL));
}
