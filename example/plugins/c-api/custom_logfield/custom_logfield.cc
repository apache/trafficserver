/** @file

  This plugin demonstrates custom log field registration and usage.
  It populates custom log fields from per-transaction user arguments.

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

#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ts/ts.h>
#include <ts/remap.h>

DbgCtl dbg_ctl{"custom_logfield"};

char PLUGIN_NAME[]    = "custom_logfield";
char VENDOR_NAME[]    = "Apache Software Foundation";
char SUPPORT_EMAIL[]  = "dev@trafficserver.apache.org";
char USER_ARG_CSTM[]  = "cstm_field";
char USER_ARG_CSTMI[] = "cstmi_field";
char USER_ARG_CSSN[]  = "cssn_field";

int
write_text_from_user_arg(TSHttpTxn txnp, char *buf, const char *user_arg_name)
{
  int len = 0;
  int index;

  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, user_arg_name, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    if (char *value = static_cast<char *>(TSUserArgGet(txnp, index)); value) {
      Dbg(dbg_ctl, "Value: %s", value);
      len = strlen(value);
      if (buf) {
        TSstrlcpy(buf, value, len + 1);
      }
    }
  }
  return len + 1;
}

int
marshal_function_cstm(TSHttpTxn txnp, char *buf)
{
  if (buf) {
    Dbg(dbg_ctl, "Marshaling a custom field cstm");
  } else {
    Dbg(dbg_ctl, "Marshaling a custom field cstm for size calculation");
  }
  return write_text_from_user_arg(txnp, buf, USER_ARG_CSTM);
}

int
marshal_function_cssn(TSHttpTxn txnp, char *buf)
{
  if (buf) {
    Dbg(dbg_ctl, "Marshaling a built-in field cssn");
  } else {
    Dbg(dbg_ctl, "Marshaling a built-in field cssn for size calculation");
  }
  return write_text_from_user_arg(txnp, buf, USER_ARG_CSSN);
}

int
marshal_function_cstmi(TSHttpTxn txnp, char *buf)
{
  // This implementation is just to demonstrate marshaling an integer value.
  // Predefined marshal function, TSLogIntMarshal, works for simple integer values

  int index;

  if (buf) {
    Dbg(dbg_ctl, "Marshaling a custom field cstmi");
  } else {
    Dbg(dbg_ctl, "Marshaling a custom field cstmi for size calculation");
  }

  if (buf) {
    if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_CSTMI, &index, nullptr) == TS_SUCCESS) {
      Dbg(dbg_ctl, "User Arg Index: %d", index);
      if (int64_t value = reinterpret_cast<int64_t>(TSUserArgGet(txnp, index)); value) {
        Dbg(dbg_ctl, "Value: %" PRId64, value);
        *(reinterpret_cast<int64_t *>(buf)) = value;
      }
    }
  }
  return sizeof(int64_t);
}

std::tuple<int, int>
unmarshal_function_string(char **buf, char *dest, int len)
{
  Dbg(dbg_ctl, "Unmarshaling a string field");

  // This implementation is just to demonstrate unmarshaling a string value.
  // Predefined unmarshal function, TSLogStringUnmarshal, works for simple string values

  int l = strlen(*buf);
  Dbg(dbg_ctl, "Dest buf size: %d", len);
  Dbg(dbg_ctl, "Unmarshaled value length: %d", l);
  if (l < len) {
    memcpy(dest, *buf, l);
    Dbg(dbg_ctl, "Unmarshaled value: %.*s", l, dest);
    return {
      l, // The length of data read from buf
      l  // The length of data written to dest
    };
  } else {
    return {-1, -1};
  }
}

int
lifecycle_event_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSAssert(event == TS_EVENT_LIFECYCLE_LOG_INITIALIZED);

  // This registers a custom log field "cstm".
  Dbg(dbg_ctl, "Registering cstm log field");
  TSLogFieldRegister("custom log field", "cstm", TS_LOG_TYPE_STRING, marshal_function_cstm, unmarshal_function_string);

  // This replaces marshaling and unmarshaling functions for a built-in log field "cssn".
  Dbg(dbg_ctl, "Overriding cssn log field");
  TSLogFieldRegister("modified cssn", "cssn", TS_LOG_TYPE_STRING, marshal_function_cssn, TSLogStringUnmarshal, true);

  // This registers a custom log field "cstmi"
  Dbg(dbg_ctl, "Registering cstmi log field");
  TSLogFieldRegister("custom integer log field", "cstmi", TS_LOG_TYPE_INT, marshal_function_cstmi, TSLogIntUnmarshal);

  // This replaces marshaling and unmarshaling functions for a built-in log field "chi".
  Dbg(dbg_ctl, "Overriding chi log field");
  TSLogFieldRegister(
    "modified cssn", "chi", TS_LOG_TYPE_ADDR,
    [](TSHttpTxn /* txnp */, char *buf) -> int {
      sockaddr_in addr;
      addr.sin_family      = AF_INET;
      addr.sin_port        = htons(80);
      addr.sin_addr.s_addr = inet_addr("192.168.0.1");
      return TSLogAddrMarshal(buf, reinterpret_cast<sockaddr *>(&addr));
    },
    TSLogAddrUnmarshal, true);

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
  TSLifecycleHookAdd(TS_LIFECYCLE_LOG_INITIALIZED_HOOK, cont);

  int argIndex;
  TSUserArgIndexReserve(TS_USER_ARGS_TXN, USER_ARG_CSTM, "This is for cstm log field", &argIndex);
  Dbg(dbg_ctl, "User Arg Index: %d", argIndex);
  TSUserArgIndexReserve(TS_USER_ARGS_TXN, USER_ARG_CSSN, "This is for cssn log field", &argIndex);
  Dbg(dbg_ctl, "User Arg Index: %d", argIndex);
  TSUserArgIndexReserve(TS_USER_ARGS_TXN, USER_ARG_CSTMI, "This is for cstmi log field", &argIndex);
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

  // Store a string value for cstm field
  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_CSTM, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    TSUserArgSet(txn, index, const_cast<char *>("abc"));
  }

  // Store a string value for cssn field
  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_CSSN, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    TSUserArgSet(txn, index, const_cast<char *>("xyz"));
  }

  // Store an integer value for cstmi field
  if (TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, USER_ARG_CSTMI, &index, nullptr) == TS_SUCCESS) {
    Dbg(dbg_ctl, "User Arg Index: %d", index);
    TSUserArgSet(txn, index, reinterpret_cast<void *>(43));
  }

  return TSREMAP_NO_REMAP;
}
