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

/* orderplugin2.c: one of the set of 5 plugins that help determine if  
 *                 the order in which the plugins are invoked at any
 *                 hook is the same as the order in which they appear
 *                 in plugin.config file. The plugin logs an error 
 *                 message in logs/error.log file if the sequence in
 *                 which the plugin is invoked is incorrect.
 *
 *
 *   Usage: 
 *   (NT):orderplugin2.dll valuei
 *   (Solaris):orderplugin2.so valuei
 *  
 *   valuei is the order in which the plugin name appears in 
 *          plugin.config file among the set of the 5 plugins 
 *          (ignore orderstartplugin.so). 
 *          i.e. If the plugin is listed on top of the other
 *           3 plugins then valuei is 1.
 *
 */

#include<stdio.h>
#include<string.h>
#include<assert.h>
#include "ts.h"

#define FIELD_NAME "RANK"
int value;

static int
plugin2(INKCont contp, INKEvent event, void *edata)
{

  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc field_loc;

  int count;

  INKHttpTxn txnp = (INKHttpTxn) edata;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    printf("Couldn't retrieve Client Request Header\n");
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return 0;
  }

  if ((field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, FIELD_NAME, -1)) != 0) {
    count = INKMimeFieldValueGetInt(bufp, field_loc, 0);
    if (value != ++count) {
      INKError("Incorrect sequence of calling...orderplugin2\n");
    }
    INKMimeFieldValueSetInt(bufp, field_loc, 0, value);

  }

  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{

  INKMutex lock1;

  INKCont contp;


  if (argc != 2) {
    printf("Usage: orderplugin2.so <valuei>\n");
    return;
  }

  sscanf(argv[1], "%d", &value);

  lock1 = INKMutexCreate();

  contp = INKContCreate(plugin2, lock1);

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_SEND_REQUEST_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_READ_CACHE_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);


}
