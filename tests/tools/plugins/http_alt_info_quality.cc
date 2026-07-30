/** @file

  Select cache alternates by matching request query strings.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.
 */

#include <ts/ts.h>

#include <string>

namespace
{
constexpr char PLUGIN_NAME[] = "http_alt_info_quality";

std::string
get_query(TSMBuffer buffer, TSMLoc header)
{
  TSMLoc url;
  int    length = 0;

  if (TSHttpHdrUrlGet(buffer, header, &url) != TS_SUCCESS) {
    return {};
  }

  const char *query = TSUrlHttpQueryGet(buffer, url, &length);
  std::string result;

  if (query != nullptr) {
    result.assign(query, static_cast<size_t>(length));
  }

  TSHandleMLocRelease(buffer, header, url);
  return result;
}

int
select_alternate(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSAssert(event == TS_EVENT_HTTP_SELECT_ALT);

  auto      alt_info = static_cast<TSHttpAltInfo>(edata);
  TSMBuffer client_buffer;
  TSMBuffer cached_buffer;
  TSMLoc    client_header;
  TSMLoc    cached_header;

  if (TSHttpAltInfoClientReqGet(alt_info, &client_buffer, &client_header) != TS_SUCCESS) {
    TSHttpAltInfoQualitySet(alt_info, 0.0F);
    return 0;
  }
  if (TSHttpAltInfoCachedReqGet(alt_info, &cached_buffer, &cached_header) != TS_SUCCESS) {
    TSHttpAltInfoQualitySet(alt_info, 0.0F);
    TSHandleMLocRelease(client_buffer, TS_NULL_MLOC, client_header);
    return 0;
  }

  std::string client_query = get_query(client_buffer, client_header);
  std::string cached_query = get_query(cached_buffer, cached_header);

  TSHttpAltInfoQualitySet(alt_info, client_query == cached_query ? 1.0F : 0.0F);

  TSHandleMLocRelease(client_buffer, TS_NULL_MLOC, client_header);
  TSHandleMLocRelease(cached_buffer, TS_NULL_MLOC, cached_header);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, TSContCreate(select_alternate, nullptr));
}
