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
 *   replace-header.c:
 *	an example program...
 *
 *   NOTE: If faced with duplicate headers, this will only detect the
 *         first instance.  Operational plugins may need to do more!
 *
 *	Usage:
 *
 *
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <ts/ts.h>

static void
replace_header(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer resp_bufp;
  TSMLoc resp_loc;
  TSMLoc field_loc;

  if (TSHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc) != TS_SUCCESS) {
    TSError("couldn't retrieve server response header.\n");
    goto done;
  }

  field_loc = TSMimeHdrFieldFind(resp_bufp, resp_loc, TS_MIME_FIELD_ACCEPT_RANGES, TS_MIME_LEN_ACCEPT_RANGES);
  if (field_loc == TS_NULL_MLOC) {
    /* field was not found */

    /* create a new field in the header */
    TSMimeHdrFieldCreate(resp_bufp, resp_loc, &field_loc); /* Probably should check for errors. */
    /* set its name */
    TSMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, TS_MIME_FIELD_ACCEPT_RANGES, TS_MIME_LEN_ACCEPT_RANGES);
    /* set its value */
    TSMimeHdrFieldValueAppend(resp_bufp, resp_loc, field_loc, -1, "none", 4);
    /* insert it into the header */
    TSMimeHdrFieldAppend(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
  } else {
    /* clear the field */
    TSMimeHdrFieldValuesClear(resp_bufp, resp_loc, field_loc);
    /* set the value to "none" */
    TSMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, field_loc, -1, "none", 4);
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
  }

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
replace_header_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    replace_header(txnp, contp);
    return 0;
  default:
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
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
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = "replace-header";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed. \n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(replace_header_plugin, NULL));
}
