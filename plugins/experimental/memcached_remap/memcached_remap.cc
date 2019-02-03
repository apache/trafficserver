/*
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
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <libmemcached/memcached.h>

// global settings
static const char *PLUGIN_NAME = "memcached_remap";

// memcached related global variables
memcached_server_st *servers;
memcached_st *memc;

bool
do_memcached_remap(TSCont contp, TSHttpTxn txnp)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc, url_loc, field_loc;
  bool ret_val = false;

  const char *request_host;
  int request_host_length = 0;
  const char *request_scheme;
  int request_scheme_length = 0;
  int request_port          = 80;
  char ikey[1024];
  char *m_result = nullptr;
  size_t oval_length;
  uint32_t flags;
  memcached_return_t lrc;

  if (TSHttpTxnClientReqGet((TSHttpTxn)txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "could not get request data");
    return false;
  }

  if (TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request url");
    goto release_hdr;
  }

  field_loc = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);

  if (!field_loc) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request HOST header");
    goto release_url;
  }

  request_host = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field_loc, -1, &request_host_length);
  if (request_host == nullptr || strlen(request_host) < 1) {
    TSDebug(PLUGIN_NAME, "couldn't find request HOST header");
    goto release_field;
  }

  request_scheme = TSUrlSchemeGet(reqp, url_loc, &request_scheme_length);
  request_port   = TSUrlPortGet(reqp, url_loc);

  TSDebug(PLUGIN_NAME, "      +++++MEMCACHED REMAP+++++      ");

  TSDebug(PLUGIN_NAME, "\nINCOMING REQUEST ->\n ::: from_scheme_desc: %.*s\n ::: from_hostname: %.*s\n ::: from_port: %d",
          request_scheme_length, request_scheme, request_host_length, request_host, request_port);

  snprintf(ikey, 1024, "%.*s://%.*s:%d/", request_scheme_length, request_scheme, request_host_length, request_host, request_port);

  TSDebug(PLUGIN_NAME, "querying for the key %s", ikey);
  m_result = memcached_get(memc, ikey, strlen(ikey), &oval_length, &flags, &lrc);

  char oscheme[1024], ohost[1024];
  int oport;

  if (lrc == MEMCACHED_SUCCESS) {
    TSDebug(PLUGIN_NAME, "got the response from server : %s", m_result);
    TSDebug(PLUGIN_NAME, "scanf result : %d", sscanf(m_result, "%[a-zA-Z]://%[^:]:%d", oscheme, ohost, &oport));
    if (sscanf(m_result, "%[a-zA-Z]://%[^:]:%d", oscheme, ohost, &oport) == 3) {
      if (m_result)
        free(m_result);
      TSDebug(PLUGIN_NAME, "\nOUTGOING REQUEST ->\n ::: to_scheme_desc: %s\n ::: to_hostname: %s\n ::: to_port: %d", oscheme, ohost,
              oport);
      TSMimeHdrFieldValueStringSet(reqp, hdr_loc, field_loc, 0, ohost, -1);
      TSUrlHostSet(reqp, url_loc, ohost, -1);
      TSUrlSchemeSet(reqp, url_loc, oscheme, -1);
      TSUrlPortSet(reqp, url_loc, oport);
      ret_val = true;
    } else {
      if (m_result)
        free(m_result);
      goto not_found;
    }
  } else {
    TSDebug(PLUGIN_NAME, "didn't get any response from the server %d, %d, %d", lrc, flags, (int)oval_length);
    goto not_found;
  }

  goto release_field; // free the result set after processed

not_found:
  // lets build up a nice 404 message for someone
  if (!ret_val) {
    TSHttpHdrStatusSet(reqp, hdr_loc, TS_HTTP_STATUS_NOT_FOUND);
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_NOT_FOUND);
  }
release_field:
  if (field_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, field_loc);
  }
release_url:
  if (url_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
release_hdr:
  if (hdr_loc) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  return ret_val;
}

static int
memcached_remap(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp   = (TSHttpTxn)edata;
  TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSDebug(PLUGIN_NAME, "Reading Request");
    TSSkipRemappingSet(txnp, 1);
    if (!do_memcached_remap(contp, txnp)) {
      reenable = TS_EVENT_HTTP_ERROR;
    }
  }

  TSHttpTxnReenable(txnp, reenable);
  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  memcached_return_t rc;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  TSDebug(PLUGIN_NAME, "about to init memcached");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[memcached_remap] Plugin registration failed");
    return;
  }

  memc = memcached_create(NULL);

  servers = memcached_server_list_append(NULL, "localhost", 11211, &rc);
  if (rc != MEMCACHED_SUCCESS) {
    TSError("[memcached_remap] Plugin registration failed while adding servers.\n");
    return;
  }

  rc = memcached_server_push(memc, servers);
  if (rc != MEMCACHED_SUCCESS) {
    TSError("[memcached_remap] Plugin registration failed while adding to pool.\n");
    return;
  }

  TSCont cont = TSContCreate(memcached_remap, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized [plugin mode]");
  return;
}
