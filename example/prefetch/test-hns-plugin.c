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

/*
This plugin tests the Parent Traffic Server handling of parse/prefetch
rules. It prints information to 'stdout' at various stages to verify the
correctness of the parse/prefetch module. It has the following options:

-p. If 0, the plugin returns TS_PREFETCH_DISCONTINUE when called at the
    TS_PREFETCH_PRE_PARSE_HOOK. If 1, the plugin returns
    TS_PREFETCH_CONTINUE.

-u. If 0, the plugin returns TS_PREFETCH_DISCONTINUE when called at the
    TS_PREFETCH_EMBEDDED_URL_HOOK. If 1, the plugin returns
    TS_PREFETCH_CONTINUE.

-o. If 1, the plugin sets 'object_buf_status' field in the TSPrefetchInfo to
    TS_PREFETCH_OBJ_BUF_NEEDED and expects to be called back with the object.
    If 2, this field is set to TS_PREFETCH_OBJ_BUF_NEEDED_N_TRANSMITTED
    which implies the object is transmitted to the child as well.

-i. If 0, the plugin sets the 'url_response_proto' field in the
    TSPrefetchInfo to TS_PREFETCH_PROTO_UDP. If 1, it sets the
    'url_response_proto' field to TS_PREFETCH_PROTO_TCP.

-d. Specifies the directory where the plugin will store all the prefetched
    objects. All prefetched objects are stored in the PkgPreload format in
    the 'prefetched.objects' file in this directory.

*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ts/ts.h>
#include <ts/experimental.h>

/* We will register the following two hooks */

#define TAG "test-hns-plugin"

TSFile filep1 = NULL;
static int pre_parse_cont = 0;
static int embedded_url_cont = 0;
static int url_proto = 0;
static int embedded_object = 0;

static TSMutex file_write_mutex;


TSPrefetchReturnCode
embedded_object_hook(TSPrefetchHookID hook, TSPrefetchInfo *info)
{
  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail, total_avail;

  TSMutexLock(file_write_mutex);

  printf("(%s) >>> TS_PREFETCH_EMBEDDED_OBJECT_HOOK (%d)\n", TAG, hook);

  printf("(%s) \tobject size for: %s is %lld\n", TAG, info->embedded_url,
         (long long)TSIOBufferReaderAvail(info->object_buf_reader));

  /* Get the embedded objects here */
  total_avail = TSIOBufferReaderAvail(info->object_buf_reader);

  printf("(%s) >>> TSIOBufferReaderAvail returns %lld\n", TAG, (long long)total_avail);

  block = TSIOBufferReaderStart(info->object_buf_reader);
  while (block) {
    block_start = TSIOBufferBlockReadStart(block, info->object_buf_reader, &block_avail);

    if (block_avail == 0) {
      break;
    }

    /* Put the object fragment into the file */
    TSfwrite(filep1, block_start, block_avail);
    TSfflush(filep1);
    TSIOBufferReaderConsume(info->object_buf_reader, block_avail);
    block = TSIOBufferReaderStart(info->object_buf_reader);
  }


  TSIOBufferDestroy(info->object_buf);

  TSMutexUnlock(file_write_mutex);

  return 0;
}

TSPrefetchReturnCode
embedded_url_hook(TSPrefetchHookID hook, TSPrefetchInfo *info)
{
  unsigned char *ip = (unsigned char *)&info->client_ip;

  printf("(%s) >>> TS_PREFETCH_EMBEDDED_URL_HOOK (%d)\n", TAG, hook);

  printf("(%s) \tURL: %s %s Child IP: %u.%u.%u.%u\n", TAG, info->embedded_url, (info->present_in_cache) ? "(present in cache)" : "",
         ip[0], ip[1], ip[2], ip[3]);

  /* We will select UDP for sending url and TCP for sending object */
  if (embedded_object)
    info->object_buf_status = (embedded_object == 1) ? TS_PREFETCH_OBJ_BUF_NEEDED : TS_PREFETCH_OBJ_BUF_NEEDED_N_TRANSMITTED;
  if (url_proto)
    info->url_response_proto = TS_PREFETCH_PROTO_TCP;
  else
    info->url_response_proto = TS_PREFETCH_PROTO_UDP;


  if (!embedded_url_cont) {
    /* This will cause parent TS not to parse the HTML page */
    printf("(%s) \tPlugin returns - TS_PREFETCH_DISCONTINUE\n", TAG);
    return TS_PREFETCH_DISCONTINUE;

  } else {
    /* This will cause TS (MDE plugin) to prefetch the URL */
    printf("(%s) \tURL Response Protocol: %s\n", TAG,
           (info->url_response_proto == TS_PREFETCH_PROTO_TCP) ? "TS_PREFETCH_PROTO_TCP" : "TS_PREFETCH_PROTO_UDP");
    printf("(%s) \tPlugin returns - TS_PREFETCH_CONTINUE\n", TAG);
    return TS_PREFETCH_CONTINUE;
  }
}


TSPrefetchReturnCode
pre_parse_hook(TSPrefetchHookID hook, TSPrefetchInfo *info)
{
  unsigned char *ip = (unsigned char *)&info->client_ip;

  printf("(%s) >>> TS_PREFETCH_PRE_PARSE_HOOK (%d)\n", TAG, hook);

  printf("(%s) \tChild IP : %u.%u.%u.%u\n", TAG, ip[0], ip[1], ip[2], ip[3]);

  if (!pre_parse_cont) {
    /* This will cause parent TS not to parse the HTML page */
    printf("(%s) \tPlugin returns - TS_PREFETCH_DISCONTINUE\n", TAG);
    return TS_PREFETCH_DISCONTINUE;

  } else {
    /* we will let TS parse the page */
    printf("(%s) \tPlugin returns - TS_PREFETCH_CONTINUE\n", TAG);
    return TS_PREFETCH_CONTINUE;
  }
}


void
TSPluginInit(int argc, const char *argv[])
{
  int c, arg;
  extern char *optarg;
  TSPluginRegistrationInfo plugin_info;
  char file_name[512] = {0};
  plugin_info.plugin_name = "test-prefetch";
  plugin_info.vendor_name = "MyCompany";
  plugin_info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &plugin_info) != TS_SUCCESS) {
    TSError("Plugin registration failed.\n");
    return;
  }

  while ((c = getopt(argc, (char *const *)argv, "p:u:i:o:d:")) != EOF) {
    switch (c) {
    case 'p':
    case 'u':
    case 'i':
    case 'o':
      if (!strcmp(optarg, "0"))
        arg = 0;
      else if (!strcmp(optarg, "1"))
        arg = 1;
      else if (!strcmp(optarg, "2"))
        arg = 2;
      else {
        TSError("Invalid argument specified for option: %c\n", c);
        return;
      }
      if (c == 'p')
        pre_parse_cont = arg;
      else if (c == 'u')
        embedded_url_cont = arg;
      else if (c == 'i')
        url_proto = arg;
      else
        embedded_object = arg;
      break;

    case 'd':
      sprintf(file_name, "%s/prefetched.objects", optarg);
      break;
    case '?':
      TSError("Invalid argument specified\n");
      return;
    }
  }

  if (embedded_object) {
    filep1 = TSfopen(file_name, "w");
    if (!filep1) {
      TSError("Cannot open file %s for writing\n", file_name);
      return;
    }
    TSfwrite(filep1, "", 1);
    TSfflush(filep1);

    file_write_mutex = TSMutexCreate();
  }

  /* register our hooks */
  TSPrefetchHookSet(TS_PREFETCH_PRE_PARSE_HOOK, &pre_parse_hook);
  TSPrefetchHookSet(TS_PREFETCH_EMBEDDED_URL_HOOK, &embedded_url_hook);
  TSPrefetchHookSet(TS_PREFETCH_EMBEDDED_OBJECT_HOOK, &embedded_object_hook);
}
