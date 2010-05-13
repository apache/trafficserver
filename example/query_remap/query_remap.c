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

#include <ts/remap.h>
#include <ts/ts.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PLUGIN_NAME "query_remap"

//function prototypes
uint32_t hash_fnv32(char *buf, size_t len);

typedef struct _query_remap_info {
  char *param_name;
  size_t param_len;
  char **hosts;
  int num_hosts;
} query_remap_info;


int tsremap_init(TSRemapInterface *api_info,char *errbuf,int errbuf_size)
{
// Called at TS startup. Nothing needed for this plugin
  INKDebug(PLUGIN_NAME , "remap plugin initialized");
  return 0;
}


int tsremap_new_instance(int argc,char *argv[],ihandle *ih,char *errbuf,int errbuf_size)
{
// Called for each remap rule using this plugin. The parameters are parsed here
  int i;
  INKDebug(PLUGIN_NAME, "new instance fromURL: %s toURL: %s", argv[0], argv[1]);

  if (argc < 4) {
    INKError("Missing parameters for " PLUGIN_NAME);
    return -1;
  }

  //initialize the struct to store info about this remap instance
  // the argv parameters are:
  // 0: fromURL
  // 1: toURL
  // 2: query param to hash
  // 3,4,... : server hostnames
  query_remap_info *qri = (query_remap_info*) INKmalloc(sizeof(query_remap_info));

  qri->param_name = INKstrdup(argv[2]);
  qri->param_len = strlen(qri->param_name);
  qri->num_hosts = argc - 3;
  qri->hosts = (char**) INKmalloc(qri->num_hosts*sizeof(char*));

  INKDebug(PLUGIN_NAME, " - Hash using query parameter [%s] with %d hosts",
           qri->param_name, qri->num_hosts);

  for (i=0; i < qri->num_hosts; ++i) {
    qri->hosts[i] = INKstrdup(argv[i+3]);
    INKDebug(PLUGIN_NAME, " - Host %d: %s", i, qri->hosts[i]);
  }

  *ih = (ihandle)qri;
  INKDebug(PLUGIN_NAME, "created instance %p", *ih);
  return 0;
}

void tsremap_delete_instance(ihandle ih)
{
// Release instance memory allocated in tsremap_new_instance
  int i;
  INKDebug(PLUGIN_NAME, "deleting instance %p", ih);

  if (ih) {
    query_remap_info *qri = (query_remap_info*)ih;
    if (qri->param_name)
      INKfree(qri->param_name);
    if (qri->hosts) {
      for (i=0; i < qri->num_hosts; ++i) {
        INKfree(qri->hosts[i]);
      }
      INKfree(qri->hosts);
    }
    INKfree(qri);
  }
}


int tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo *rri)
{
  int hostidx = -1;
  query_remap_info *qri = (query_remap_info*)ih;

  if (!qri) {
    INKError(PLUGIN_NAME "NULL ihandle");
    return 0;
  }

  INKDebug(PLUGIN_NAME, "tsremap_remap request: %.*s", rri->orig_url_size, rri->orig_url);

  if (rri && rri->request_query && rri->request_query_size > 0) {
    char *q, *s, *key;

    //make a copy of the query, as it is read only
    q = (char*) INKmalloc(rri->request_query_size+1);
    strncpy(q, rri->request_query, rri->request_query_size);
    q[rri->request_query_size] = '\0';

    s = q;
    //parse query parameters
    for (key = strsep(&s, "&"); key != NULL; key = strsep(&s, "&")) {
      char *val = strchr(key, '=');
      if (val && (size_t)(val-key) == qri->param_len &&
          !strncmp(key, qri->param_name, qri->param_len)) {
        ++val;
        //the param key matched the configured param_name
        //hash the param value to pick a host
        hostidx = hash_fnv32(val, strlen(val)) % (uint32_t)qri->num_hosts;
        INKDebug(PLUGIN_NAME, "modifying host based on %s", key);
        break;
      }
    }

    INKfree(q);

    if (hostidx >= 0) {
      rri->new_host_size = strlen(qri->hosts[hostidx]);
      if (rri->new_host_size <= TSREMAP_RRI_MAX_HOST_SIZE) {
        //copy the chosen host into rri
        memcpy(rri->new_host, qri->hosts[hostidx], rri->new_host_size);

        INKDebug(PLUGIN_NAME, "host changed from [%.*s] to [%.*s]",
                 rri->request_host_size, rri->request_host,
                 rri->new_host_size, rri->new_host);
        return 1; //host has been modified
      }
    }
  }

  //the request was not modified, TS will use the toURL from the remap rule
  INKDebug(PLUGIN_NAME, "request not modified");
  return 0;
}


// FNV (Fowler/Noll/Vo) hash
// (description: http://www.isthe.com/chongo/tech/comp/fnv/index.html)
uint32_t
hash_fnv32(char *buf, size_t len)
{
  uint32_t hval = (uint32_t)0x811c9dc5; //FNV1_32_INIT

  for (; len > 0; --len) {
    hval *= (uint32_t)0x01000193; //FNV_32_PRIME
    hval ^= (uint32_t)*buf++;
  }

  return hval;
}

