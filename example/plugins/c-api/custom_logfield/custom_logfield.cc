/** @file

  This plugin counts the number of times every header has appeared.
  Maintains separate counts for client and origin headers.

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

#include <ts/ts.h>
#include <ts/remap.h>

DbgCtl dbg_ctl{"custom_logfield"};

char PLUGIN_NAME[]   = "header_freq";
char VENDOR_NAME[]   = "Apache Software Foundation";
char SUPPORT_EMAIL[] = "dev@trafficserver.apache.org";
char USER_ARG_NAME[] = "cstm_field";

int
marshal_function(TSHttpTxn txnp, char *buf)
{
  int len = 0;
  int index;

  Dbg(dbg_ctl, "Marshaling a custom field");

  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_NAME, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    if (char *value = static_cast<char *>(TSUserArgGet(txnp, index)); value) {
      len = strlen(value);
      if (buf) {
        TSstrlcpy(buf, value, len + 1);
      }
    }
  }
  return len + 1;
}

int
unmarshal_function(char **buf, char *dest, int len)
{
  Dbg(dbg_ctl, "Unmarshaling a custom field");

  int l = strlen(*buf);
  Dbg(dbg_ctl, "Dest buf size: %d", len);
  Dbg(dbg_ctl, "Unmarshaled value length: %d", l);
  if (l < len) {
    memcpy(dest, *buf, l);
    Dbg(dbg_ctl, "Unmarshaled value: %.*s", l, dest);
    return l;
  } else {
    return -1;
  }
}

int
lifecycle_event_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void * /* edata ATS_UNUSED */)
{
  Dbg(dbg_ctl, "Registering a custom field");

  TSAssert(event == TS_EVENT_LIFECYCLE_LOG_INITIAZLIED);
  TSLogFieldRegister("custom log field", "cstm", TS_LOG_TYPE_STRING, marshal_function, unmarshal_function);

  return TS_SUCCESS;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  Dbg(dbg_ctl, "Initializing plugin");

  TSPluginRegistrationInfo info = {PLUGIN_NAME, VENDOR_NAME, SUPPORT_EMAIL};
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s](%s) Plugin registration failed. \n", PLUGIN_NAME, __FUNCTION__);
  }

  TSCont cont = TSContCreate(lifecycle_event_handler, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_LOG_INITIAZLIED_HOOK, cont);

  int argIndex;
  TSUserArgIndexReserve(TS_USER_ARGS_TXN, USER_ARG_NAME, "This is for cstm log field", &argIndex);
  Dbg(dbg_ctl, "User Arg Index: %d", argIndex);
}

TSReturnCode
TSRemapInit(TSRemapInterface *, char *, int)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int, char **, void **, char *, int)
{
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *)
{
}

TSRemapStatus
TSRemapDoRemap(void *, TSHttpTxn txn, TSRemapRequestInfo *)
{
  Dbg(dbg_ctl, "Remapping");

  int index;
  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_NAME, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    TSUserArgSet(txn, index, const_cast<char *>("abc"));
  }

  return TSREMAP_NO_REMAP;
}
