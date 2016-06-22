/** @file

  A sample plugin to remap requests based on a query parameter

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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/ink_defs.h"

#define PLUGIN_NAME "query_remap"

/* function prototypes */
uint32_t hash_fnv32(char *buf, size_t len);

typedef struct _query_remap_info {
  char *param_name;
  size_t param_len;
  char **hosts;
  int num_hosts;
} query_remap_info;

int
TSRemapInit(TSRemapInterface *api_info ATS_UNUSED, char *errbuf ATS_UNUSED, int errbuf_size ATS_UNUSED)
{
  /* Called at TS startup. Nothing needed for this plugin */
  TSDebug(PLUGIN_NAME, "remap plugin initialized");
  return 0;
}

int
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf ATS_UNUSED, int errbuf_size ATS_UNUSED)
{
  /* Called for each remap rule using this plugin. The parameters are parsed here */
  int i;
  TSDebug(PLUGIN_NAME, "new instance fromURL: %s toURL: %s", argv[0], argv[1]);

  if (argc < 4) {
    TSError("[query_remap] Missing parameters");
    return -1;
  }

  /* initialize the struct to store info about this remap instance
     the argv parameters are:
       0: fromURL
       1: toURL
       2: query param to hash
       3,4,... : server hostnames
  */
  query_remap_info *qri = (query_remap_info *)TSmalloc(sizeof(query_remap_info));

  qri->param_name = TSstrdup(argv[2]);
  qri->param_len  = strlen(qri->param_name);
  qri->num_hosts  = argc - 3;
  qri->hosts      = (char **)TSmalloc(qri->num_hosts * sizeof(char *));

  TSDebug(PLUGIN_NAME, " - Hash using query parameter [%s] with %d hosts", qri->param_name, qri->num_hosts);

  for (i = 0; i < qri->num_hosts; ++i) {
    qri->hosts[i] = TSstrdup(argv[i + 3]);
    TSDebug(PLUGIN_NAME, " - Host %d: %s", i, qri->hosts[i]);
  }

  *ih = (void *)qri;
  TSDebug(PLUGIN_NAME, "created instance %p", *ih);
  return 0;
}

void
TSRemapDeleteInstance(void *ih)
{
  /* Release instance memory allocated in TSRemapNewInstance */
  int i;
  TSDebug(PLUGIN_NAME, "deleting instance %p", ih);

  if (ih) {
    query_remap_info *qri = (query_remap_info *)ih;
    if (qri->param_name)
      TSfree(qri->param_name);
    if (qri->hosts) {
      for (i = 0; i < qri->num_hosts; ++i) {
        TSfree(qri->hosts[i]);
      }
      TSfree(qri->hosts);
    }
    TSfree(qri);
  }
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh ATS_UNUSED, TSRemapRequestInfo *rri)
{
  int hostidx           = -1;
  query_remap_info *qri = (query_remap_info *)ih;

  if (!qri || !rri) {
    TSError("[query_remap] NULL private data or RRI");
    return TSREMAP_NO_REMAP;
  }

  int req_query_len;
  const char *req_query = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &req_query_len);

  if (req_query && req_query_len > 0) {
    char *q, *key;
    char *s = NULL;

    /* make a copy of the query, as it is read only */
    q = (char *)TSstrndup(req_query, req_query_len + 1);

    /* parse query parameters */
    for (key = strtok_r(q, "&", &s); key != NULL;) {
      char *val = strchr(key, '=');
      if (val && (size_t)(val - key) == qri->param_len && !strncmp(key, qri->param_name, qri->param_len)) {
        ++val;
        /* the param key matched the configured param_name
           hash the param value to pick a host */
        hostidx = hash_fnv32(val, strlen(val)) % (uint32_t)qri->num_hosts;
        TSDebug(PLUGIN_NAME, "modifying host based on %s", key);
        break;
      }
      key = strtok_r(NULL, "&", &s);
    }

    TSfree(q);

    if (hostidx >= 0) {
      int req_host_len;
      /* TODO: Perhaps use TSIsDebugTagSet() before calling TSUrlHostGet()... */
      const char *req_host = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &req_host_len);

      if (TSUrlHostSet(rri->requestBufp, rri->requestUrl, qri->hosts[hostidx], strlen(qri->hosts[hostidx])) != TS_SUCCESS) {
        TSDebug(PLUGIN_NAME, "Failed to modify the Host in request URL");
        return TSREMAP_NO_REMAP;
      }
      TSDebug(PLUGIN_NAME, "host changed from [%.*s] to [%s]", req_host_len, req_host, qri->hosts[hostidx]);
      return TSREMAP_DID_REMAP; /* host has been modified */
    }
  }

  /* the request was not modified, TS will use the toURL from the remap rule */
  TSDebug(PLUGIN_NAME, "request not modified");
  return TSREMAP_NO_REMAP;
}

/* FNV (Fowler/Noll/Vo) hash
   (description: http://www.isthe.com/chongo/tech/comp/fnv/index.html) */
uint32_t
hash_fnv32(char *buf, size_t len)
{
  uint32_t hval = (uint32_t)0x811c9dc5; /* FNV1_32_INIT */

  for (; len > 0; --len) {
    hval *= (uint32_t)0x01000193; /* FNV_32_PRIME */
    hval ^= (uint32_t)*buf++;
  }

  return hval;
}
