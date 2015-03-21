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

/*
  prefetch-plugin-eg1.c : an example plugin which interacts with
                          Traffic Server's prefetch feature

*/

#include <stdio.h>
#include <string.h>
#include <ts/ts.h>
#include <ts/experimental.h>

TSPrefetchReturnCode
my_preparse_hook(TSPrefetchHookID hook, TSPrefetchInfo *info)
{
  unsigned char *ip = (unsigned char *)&info->client_ip;

  printf("preparse hook (%d): request from child %u.%u.%u.%u\n", hook, ip[0], ip[1], ip[2], ip[3]);


  /* we will let TS parse the page */
  return TS_PREFETCH_CONTINUE;
}

TSPrefetchReturnCode
my_embedded_url_hook(TSPrefetchHookID hook, TSPrefetchInfo *info)
{
  unsigned char *ip = (unsigned char *)&info->client_ip;

  printf("url hook (%d): url: %s %s child: %u.%u.%u.%u\n", hook, info->embedded_url,
         (info->present_in_cache) ? "(present in cache)" : "", ip[0], ip[1], ip[2], ip[3]);

  /*
     We will select UDP for sending url and TCP for sending object
   */

  info->url_proto = TS_PREFETCH_PROTO_UDP;
  info->url_response_proto = TS_PREFETCH_PROTO_TCP;

  /* we can return TS_PREFETCH_DISCONTINUE if we dont want TS to prefetch
     this url */

  return TS_PREFETCH_CONTINUE;
}


void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = "prefetch_plugin_eg1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed.\n");
  }

  /* register our hooks */
  TSPrefetchHookSet(TS_PREFETCH_PRE_PARSE_HOOK, &my_preparse_hook);
  TSPrefetchHookSet(TS_PREFETCH_EMBEDDED_URL_HOOK, &my_embedded_url_hook);
}
