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


#include "ts.h"


/* Run prototype code in this small plug-in. Then place this
 * code into it's own section.
*/
static int
TSProto(TSCont contp, TSEvent event, void *eData)
{

  TSHttpTxn txnp = (TSHttpTxn) eData;
  TSHttpSsn ssnp = (TSHttpSsn) eData;

  switch (event) {

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    TSDebug("tag", "event %d received\n", event);
    /* TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE); */
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_RESPONSE_TRANSFORM:
    TSDebug("tag", "event %d received\n", event);
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSDebug("tag", "Undefined event %d received\n");
    break;
  }

}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp = TSContCreate(TSProto, NULL);

  /* Context: TSHttpTxnTransformRespGet():
   * Q: are both of these received and if so, in what order?
   */
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
}
