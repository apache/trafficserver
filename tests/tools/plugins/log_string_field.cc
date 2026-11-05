/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Registers a string log field, %<tstr>, decoded by TSLogStringUnmarshal, whose value is the X-Field request
 * header. A value of EMPTY logs the empty string and a missing header logs "-".
 */

#include <string>
#include <string_view>

#include <ts/ts.h>

namespace
{
constexpr char PLUGIN_NAME[] = "log_string_field";

std::string
field_value(TSHttpTxn txnp)
{
  std::string value{"-"};
  TSMBuffer   bufp;
  TSMLoc      hdr_loc;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    return value;
  }
  if (TSMLoc field = TSMimeHdrFieldFind(bufp, hdr_loc, "X-Field", 7); field != TS_NULL_MLOC) {
    int         len = 0;
    char const *str = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field, -1, &len);
    value.assign(str ? str : "", len);
    if (value == "EMPTY") {
      value.clear();
    }
    TSHandleMLocRelease(bufp, hdr_loc, field);
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return value;
}

int
marshal_tstr(TSHttpTxn txnp, char *buf)
{
  return TSLogStringMarshal(buf, field_value(txnp));
}

int
on_log_initialized(TSCont, TSEvent, void *)
{
  TSLogFieldRegister("test string field", "tstr", TS_LOG_TYPE_STRING, marshal_tstr, TSLogStringUnmarshal);
  return TS_SUCCESS;
}
} // namespace

void
TSPluginInit(int, char const **)
{
  TSPluginRegistrationInfo info = {PLUGIN_NAME, "Apache Software Foundation", "dev@trafficserver.apache.org"};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }
  TSLifecycleHookAdd(TS_LIFECYCLE_LOG_INITIALIZED_HOOK, TSContCreate(on_log_initialized, nullptr));
}
