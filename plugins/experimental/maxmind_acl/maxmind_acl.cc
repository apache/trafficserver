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

#include "mmdb.h"
#include "ts/remap_version.h"

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  Dbg(dbg_ctl, "remap plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, missing configuration file", PLUGIN_NAME);
    return TS_ERROR;
  }

  Acl *a = new Acl();
  *ih    = static_cast<void *>(a);
  if (!a->init(argv[2])) {
    TSError("[%s] Failed to initialize maxmind with %s", PLUGIN_NAME, argv[2]);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "created remap instance with configuration %s", argv[2]);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  if (nullptr != ih) {
    Acl *const a = static_cast<Acl *>(ih);
    delete a;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Main entry point when used as a remap plugin.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  if (nullptr == ih) {
    Dbg(dbg_ctl, "No ACLs configured");
  } else {
    Acl *a = static_cast<Acl *>(ih);
    if (!a->eval(rri, rh)) {
      Dbg(dbg_ctl, "denying request");
      TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_FORBIDDEN);
      a->send_html(rh);
    }
  }
  return TSREMAP_NO_REMAP;
}
