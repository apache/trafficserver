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
 *   (NT): AddHeader.dll "name1: value1" "name2: value2" ...
 *   (Solaris): add-header.so "name1: value1" "name2: value2" ...
 *
 *          namei and valuei are the name and value of the 
 *          ith MIME header to be added to the client request
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>

static INKMBuffer hdr_bufp;
static INKMLoc hdr_loc;

static void
add_header(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer req_bufp;
  INKMLoc req_loc;
  INKMLoc field_loc;
  INKMLoc next_field_loc;
  INKMLoc new_field_loc;
  int retval;

  if (!INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc)) {
    INKError("[add_header] Error while retrieving client request header\n");
    goto done;
  }

  field_loc = INKMimeHdrFieldGet(hdr_bufp, hdr_loc, 0);
  if (field_loc == INK_ERROR_PTR) {
    INKError("[add_header] Error while getting field");
    goto error;
  }

  /* Loop on our header containing fields to add */
  while (field_loc) {

    /* First create a new field in the client request header */
    new_field_loc = INKMimeHdrFieldCreate(req_bufp, req_loc);
    if (new_field_loc == INK_ERROR_PTR) {
      INKError("[add_header] Error while creating new field");
      INKHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* Then copy our new field at this new location */
    retval = INKMimeHdrFieldCopy(req_bufp, req_loc, new_field_loc, hdr_bufp, hdr_loc, field_loc);
    if (retval == INK_ERROR) {
      INKError("[add_header] Error while copying new field");
      INKHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* Add this field to the Http client request header */
    retval = INKMimeHdrFieldAppend(req_bufp, req_loc, new_field_loc);
    if (retval == INK_ERROR) {
      INKError("[add_header] Error while appending new field");
      INKHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      break;
    }

    /* We can now release this handle */
    INKHandleMLocRelease(req_bufp, req_loc, new_field_loc);

    next_field_loc = INKMimeHdrFieldNext(hdr_bufp, hdr_loc, field_loc);
    if (next_field_loc == INK_ERROR_PTR) {
      INKError("[add_header] Error while getting next field to add");
      INKHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
      goto error;
    }

    INKHandleMLocRelease(hdr_bufp, hdr_loc, field_loc);
    field_loc = next_field_loc;
  }


error:
  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
add_header_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    add_header(txnp, contp);
    return 0;
  default:
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKMLoc field_loc;
  const char *p;
  int i, retval;
  INKPluginRegistrationInfo info;

  info.plugin_name = "add-header";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("[PluginInit] Plugin registration failed.\n");
    goto error;
  }

  if (!check_ts_version()) {
    INKError("[PluginInit] Plugin requires Traffic Server 2.0 or later\n");
    goto error;
  }

  if (argc < 2) {
    INKError("[PluginInit] Usage: %s \"name1: value1\" \"name2: value2\" ...>\n", argv[0]);
    goto error;
  }

  hdr_bufp = INKMBufferCreate();
  if (hdr_bufp == INK_ERROR_PTR) {
    INKError("[PluginInit] Can not create mbuffer");
    goto error;
  }

  hdr_loc = INKMimeHdrCreate(hdr_bufp);
  if (hdr_loc == INK_ERROR_PTR) {
    INKError("[PluginInit] Can not create mime header");
    goto error;
  }

  for (i = 1; i < argc; i++) {
    field_loc = INKMimeHdrFieldCreate(hdr_bufp, hdr_loc);
    if (field_loc == INK_ERROR_PTR) {
      INKError("[PluginInit] Error while creating field");
      goto error;
    }

    retval = INKMimeHdrFieldAppend(hdr_bufp, hdr_loc, field_loc);
    if (retval == INK_ERROR) {
      INKError("[PluginInit] Error while adding field");
      goto error;
    }

    p = strchr(argv[i], ':');
    if (p) {
      retval = INKMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, argv[i], p - argv[i]);
      if (retval == INK_ERROR) {
        INKError("[PluginInit] Error while naming field");
        goto error;
      }

      p += 1;
      while (isspace(*p)) {
        p += 1;
      }
      retval = INKMimeHdrFieldValueInsert(hdr_bufp, hdr_loc, field_loc, p, strlen(p), -1);
      if (retval == INK_ERROR) {
        INKError("[PluginInit] Error while inserting field value");
        goto error;
      }
    } else {
      retval = INKMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, argv[i], strlen(argv[i]));
      if (retval == INK_ERROR) {
        INKError("[PluginInit] Error while inserting field value");
        goto error;
      }
    }
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  retval = INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(add_header_plugin, INKMutexCreate()));
  if (retval == INK_ERROR) {
    INKError("[PluginInit] Error while registering to hook");
    goto error;
  }

  goto done;

error:
  INKError("[PluginInit] Plugin not initialized");

done:
  return;
}
