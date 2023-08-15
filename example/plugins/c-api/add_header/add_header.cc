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

/* add-header.c: a plugin that adds MIME headers to
 *               client request headers.
 *
 *
 *   Usage:
 *     add-header.so "name1: value1" "name2: value2" ...
 *
 *          namei and valuei are the name and value of the
 *          ith MIME header to be added to the client request
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "add_header"

static TSMBuffer hdr_bufp;
static TSMLoc hdr_loc;

static void
add_header(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
{
  TSMBuffer req_bufp;
  TSMLoc req_loc;
  TSMLoc field_loc;
  TSMLoc next_field_loc;
  TSMLoc new_field_loc;
  int retval;

  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) != TS_SUCCESS) {
    TSError("[%s] Unable to retrieve client request header", PLUGIN_NAME);
    goto done;
  }

  field_loc = TSMimeHdrFieldGet(hdr_bufp, hdr_loc, 0);
  if (field_loc == TS_NULL_MLOC) {
    TSError("[%s] Unable to get field", PLUGIN_NAME);
    goto error;
  }

  /* Loop on our header containing fields to add */
  while (field_loc) {
    /* First create a new field in the client request header */
    if (TSMimeHdrFieldCreate(req_bufp, req_loc, &new_field_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to create new field", PLUGIN_NAME);
      TSHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* Then copy our new field at this new location */
    retval = TSMimeHdrFieldCopy(req_bufp, req_loc, new_field_loc, hdr_bufp, hdr_loc, field_loc);
    if (retval == TS_ERROR) {
      TSError("[%s] Unable to copy new field", PLUGIN_NAME);
      TSHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* Add this field to the Http client request header */
    retval = TSMimeHdrFieldAppend(req_bufp, req_loc, new_field_loc);
    if (retval != TS_SUCCESS) {
      TSError("[%s] Unable to append new field", PLUGIN_NAME);
      TSHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* We can now release this handle */
    TSHandleMLocRelease(req_bufp, req_loc, new_field_loc);

    next_field_loc = TSMimeHdrFieldNext(hdr_bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
    field_loc = next_field_loc;
  }

error:
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
add_header_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    add_header(txnp, contp);
    return 0;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSMLoc field_loc;
  const char *p;
  int i, retval;
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    goto error;
  }

  if (argc < 2) {
    TSError("[%s] Usage: %s \"name1: value1\" \"name2: value2\" ...>", PLUGIN_NAME, argv[0]);
    goto error;
  }

  hdr_bufp = TSMBufferCreate();
  if (TSMimeHdrCreate(hdr_bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Can not create mime header", PLUGIN_NAME);
    goto error;
  }

  for (i = 1; i < argc; i++) {
    if (TSMimeHdrFieldCreate(hdr_bufp, hdr_loc, &field_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to create field", PLUGIN_NAME);
      goto error;
    }

    p = strchr(argv[i], ':');
    if (p) {
      retval = TSMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, argv[i], p - argv[i]);
      if (retval == TS_ERROR) {
        TSError("[%s] Unable to name field", PLUGIN_NAME);
        goto error;
      }

      p += 1;
      while (isspace(*p)) {
        p += 1;
      }
      retval = TSMimeHdrFieldValueStringInsert(hdr_bufp, hdr_loc, field_loc, -1, p, strlen(p));
      if (retval == TS_ERROR) {
        TSError("[%s] Unable to insert field value", PLUGIN_NAME);
        goto error;
      }
    } else {
      retval = TSMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, argv[i], strlen(argv[i]));
      if (retval == TS_ERROR) {
        TSError("[%s] Unable to set field name", PLUGIN_NAME);
        goto error;
      }
    }

    // TSMimeHdrFieldAppend is used only after successfully inserting field names and values
    retval = TSMimeHdrFieldAppend(hdr_bufp, hdr_loc, field_loc);
    if (retval != TS_SUCCESS) {
      TSError("[%s] Unable to add field", PLUGIN_NAME);
      goto error;
    }
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(add_header_plugin, TSMutexCreate()));
  goto done;

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);

done:
  return;
}
