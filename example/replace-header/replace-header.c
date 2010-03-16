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
replace_header(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer resp_bufp;
  INKMLoc resp_loc;
  INKMLoc field_loc;

  if (!INKHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc)) {
    INKError("couldn't retrieve server response header.\n");
    goto done;
  }

  field_loc = INKMimeHdrFieldRetrieve(resp_bufp, resp_loc, INK_MIME_FIELD_ACCEPT_RANGES);
  if (field_loc == 0) {
    /* field was not found */

    /* create a new field in the header */
    field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc);
    /* set its name */
    INKMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, INK_MIME_FIELD_ACCEPT_RANGES, INK_MIME_LEN_ACCEPT_RANGES);
    /* set its value */
    INKMimeHdrFieldValueInsert(resp_bufp, resp_loc, field_loc, "none", 4, -1);
    /* insert it into the header */
    INKMimeHdrFieldInsert(resp_bufp, resp_loc, field_loc, -1);
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
  } else {
    /* clear the field */
    INKMimeHdrFieldValuesClear(resp_bufp, resp_loc, field_loc);
    /* set the value to "none" */
    INKMimeHdrFieldValueInsert(resp_bufp, resp_loc, field_loc, "none", 4, -1);
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
  }

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
replace_header_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
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
  INKPluginRegistrationInfo info;

  info.plugin_name = "replace-header";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed. \n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(replace_header_plugin, NULL));
}
