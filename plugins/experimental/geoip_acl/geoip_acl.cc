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

//////////////////////////////////////////////////////////////////////////////////////////////
//
// Main entry points for the plugin hooks etc.
//
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/remap_version.h>
#include <cstdio>
#include <cstring>

#include "lulu.h"
#include "acl.h"

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  if (Acl::init()) {
    Dbg(dbg_ctl, "remap plugin is successfully initialized");
    return TS_SUCCESS; /* success */
  } else {
    return TS_ERROR;
  }
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, need more parameters", PLUGIN_NAME);
    return TS_ERROR;
  } else {
    Acl *a = nullptr;

    // ToDo: Should do better processing here, to make it easier to deal with
    // rules other then country codes.
    if (!strncmp(argv[2], "country", 11)) {
      Dbg(dbg_ctl, "creating an ACL rule with ISO country codes");
      a = new CountryAcl();
    }

    if (a) {
      if (a->process_args(argc, argv) > 0) {
        *ih = static_cast<void *>(a);
      } else {
        TSError("[%s] Unable to create remap instance, no geo-identifying tokens provided", PLUGIN_NAME);
        return TS_ERROR;
      }
    } else {
      TSError("[%s] Unable to create remap instance, no supported ACL specified as first parameter", PLUGIN_NAME);
      return TS_ERROR;
    }
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  Acl *a = static_cast<Acl *>(ih);

  delete a;
}

///////////////////////////////////////////////////////////////////////////////
// Main entry point when used as a remap plugin.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  if (nullptr == ih) {
    Dbg(dbg_ctl, "No ACLs configured, this is probably a plugin bug");
  } else {
    Acl *a = static_cast<Acl *>(ih);

    if (!a->eval(rri, rh)) {
      Dbg(dbg_ctl, "denying request");
      TSHttpTxnStatusSet(rh, static_cast<TSHttpStatus>(403));
      a->send_html(rh);
    }
  }

  return TSREMAP_NO_REMAP;
}
