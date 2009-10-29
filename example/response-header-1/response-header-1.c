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
 * response-header-1.c:  
 *		an example program which illustrates adding and manipulating
 *		an HTTP response MIME header:
 *
 *   Authorized possession and use of this software pursuant only
 *   to the terms of a written license agreement.
 *
 *	Usage:	response-header-1.so
 *
 *	add read_resp_header hook
 *	get http response header
 *	if 200, then 
 *		add mime extension header with count of zero
 *		add mime extension header with date response was received
 *		add "Cache-Control: public" header
 *	else if 304, then 
 *		retrieve cached header
 *		get old value of mime header count
 *		increment mime header count
 *		store mime header with new count
 *	
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "InkAPI.h"

static int init_buffer_status;

static char *mimehdr1_name;
static char *mimehdr2_name;
static char *mimehdr1_value;

static INKMBuffer hdr_bufp;
static INKMLoc hdr_loc;

static INKMLoc field_loc;
static INKMLoc value_loc;

static void
modify_header(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer resp_bufp;
  INKMBuffer cached_bufp;
  INKMLoc resp_loc;
  INKMLoc cached_loc;
  INKHttpStatus resp_status;
  INKMLoc new_field_loc;
  INKMLoc new_value_loc;
  INKMLoc cached_field_loc;
  time_t recvd_time;

  const char *chkptr;
  int chklength;

  int num_refreshes;

  if (!init_buffer_status)
    return;                     /* caller reenables */

  if (!INKHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc)) {
    INKError("couldn't retrieve server response header\n");
    return;                     /* caller reenables */
  }

  /* INKqa06246/INKqa06144 */
  resp_status = INKHttpHdrStatusGet(resp_bufp, resp_loc);

  if (INK_HTTP_STATUS_OK == resp_status) {

    INKDebug("resphdr", "Processing 200 OK");
    new_field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc);
    INKDebug("resphdr", "Created new resp field with loc %d", new_field_loc);

    /* copy name/values created at init 
     * ( "x-num-served-from-cache" ) : ( "0"  )  
     */
    INKMimeHdrFieldCopy(resp_bufp, resp_loc, new_field_loc, hdr_bufp, hdr_loc, field_loc);

        /*********** Unclear why this is needed **************/
    INKMimeHdrFieldInsert(resp_bufp, resp_loc, new_field_loc, -1);


    /* Cache-Control: Public */
    new_field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc);
    INKDebug("resphdr", "Created new resp field with loc %d", new_field_loc);
    INKMimeHdrFieldInsert(resp_bufp, resp_loc, new_field_loc, -1);
    INKMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc,
                           INK_MIME_FIELD_CACHE_CONTROL, INK_MIME_LEN_CACHE_CONTROL);
    INKMimeHdrFieldValueInsert(resp_bufp, resp_loc, new_field_loc,
                               INK_HTTP_VALUE_PUBLIC, strlen(INK_HTTP_VALUE_PUBLIC), -1);

    /* 
     * mimehdr2_name  = INKstrdup( "x-date-200-recvd" ) : CurrentDateTime
     */
    new_field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc);
    INKDebug("resphdr", "Created new resp field with loc %d", new_field_loc);
    INKMimeHdrFieldInsert(resp_bufp, resp_loc, new_field_loc, -1);
    INKMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc, mimehdr2_name, strlen(mimehdr2_name));
    recvd_time = time(NULL);
    INKMimeHdrFieldValueInsertDate(resp_bufp, resp_loc, new_field_loc, recvd_time, -1);

    INKHandleMLocRelease(resp_bufp, resp_loc, new_field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);

  } else if (INK_HTTP_STATUS_NOT_MODIFIED == resp_status) {

    INKDebug("resphdr", "Processing 304 Not Modified");

    /* N.B.: Protect writes to data (hash on URL + mutex: (ies)) */

    /* Get the cached HTTP header */
    if (!INKHttpTxnCachedRespGet(txnp, &cached_bufp, &cached_loc)) {
      INKError("STATUS 304, INKHttpTxnCachedRespGet():");
      INKError("couldn't retrieve cached response header\n");
      INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
      return;                   /* Caller reenables */
    }

    /* Get the cached MIME field name for this HTTP header */
    cached_field_loc = INKMimeHdrFieldFind(cached_bufp, cached_loc,
                                           (const char *) mimehdr1_name, strlen(mimehdr1_name));
    if (0 == cached_field_loc) {
      INKError("Can't find header %s in cached document", mimehdr1_name);
      INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
      INKHandleMLocRelease(cached_bufp, INK_NULL_MLOC, cached_loc);
      return;                   /* Caller reenables */
    }

    /* Get the cached MIME value for this name in this HTTP header */
    chkptr = INKMimeHdrFieldValueGet(cached_bufp, cached_loc, cached_field_loc, 0, &chklength);

    if (NULL == chkptr || !chklength) {
      INKError("Could not find value for cached MIME field name %s", mimehdr1_name);
      INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
      INKHandleMLocRelease(cached_bufp, INK_NULL_MLOC, cached_loc);
      INKHandleMLocRelease(cached_bufp, cached_loc, cached_field_loc);
      return;                   /* Caller reenables */
    }
    INKDebug("resphdr", "Header field value is %s, with length %d", chkptr, chklength);


    /* TODO check these comments for correctness */
    /*
     * Since INKMimeHdrFieldValueGet returned with valid values
     * are we also guaranteed that INKMimeHdrFieldValueGetUint returns
     * valid values? There is no erro code for INKMimeHdrFieldValueGetUint
     * and 0 is a valid value. 
     */
    /* Get the cached MIME value for this name in this HTTP header */
    /*
       num_refreshes = 
       INKMimeHdrFieldValueGetUint(cached_bufp, cached_loc, 
       cached_field_loc, 0);
       INKDebug("resphdr", 
       "Cached header shows %d refreshes so far", num_refreshes );

       num_refreshes++ ;
     */

       /* txn origin server response for this transaction stored
       * in resp_bufp, resp_loc
       *
       * Create a new MIME field/value. Cached value has been incremented.
       * Insert new MIME field/value into the server response buffer, 
       * allow HTTP processing to continue. This will update
       * (indirectly invalidates) the cached HTTP headers MIME field. 
       * It is apparently not necessary to update all of the MIME fields
       * in the in-process response in order to have the cached response 
       * become invalid.
     */
    new_field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc);

    /* mimehdr1_name : INKstrdup( "x-num-served-from-cache" ) ; */

    INKMimeHdrFieldInsert(resp_bufp, resp_loc, new_field_loc, -1);
    INKMimeHdrFieldNameSet(resp_bufp, resp_loc, new_field_loc, mimehdr1_name, strlen(mimehdr1_name));

    INKMimeHdrFieldValueInsertUint(resp_bufp, resp_loc, new_field_loc, num_refreshes, -1);

    INKHandleStringRelease(cached_bufp, cached_loc, chkptr);
    INKHandleMLocRelease(resp_bufp, resp_loc, new_field_loc);
    INKHandleMLocRelease(cached_bufp, cached_loc, cached_field_loc);
    INKHandleMLocRelease(cached_bufp, INK_NULL_MLOC, cached_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);

  } else {
    INKDebug("resphdr", "other response code %d", resp_status);
  }

  /*
   *  Additional 200/304 processing can go here, if so desired.
   */

  /* Caller reneables */
}


static int
modify_response_header_plugin(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    INKDebug("resphdr", "Called back with INK_EVENT_HTTP_READ_RESPONSE_HDR");
    modify_header(txnp, contp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    /*  fall through  */

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

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }

  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKMLoc chk_field_loc;

  const char *p;
  int i;
  INKPluginRegistrationInfo info;

  info.plugin_name = "response-header-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 5.2.0 or later\n");
    return;
  }

  init_buffer_status = 0;
  if (argc > 1) {
    INKError("usage: %s \n", argv[0]);
    INKError("warning: too many args %d\n", argc);
    INKError("warning: ignoring unused arguments beginning with %s\n", argv[1]);
  }

  /*
   *  The following code sets up an "init buffer" containing an extension header
   *  and its initial value.  This will be the same for all requests, so we try
   *  to be efficient and do all of the work here rather than on a per-transaction
   *  basis.
   */


  hdr_bufp = INKMBufferCreate();
  hdr_loc = INKMimeHdrCreate(hdr_bufp);

  mimehdr1_name = INKstrdup("x-num-served-from-cache");
  mimehdr1_value = INKstrdup("0");

  /* Create name here and set DateTime value when o.s. 
   * response 200 is recieved 
   */
  mimehdr2_name = INKstrdup("x-date-200-recvd");

  INKDebug("resphdr", "Inserting header %s with value %s into init buffer", mimehdr1_name, mimehdr1_value);

  field_loc = INKMimeHdrFieldCreate(hdr_bufp, hdr_loc);
  INKMimeHdrFieldInsert(hdr_bufp, hdr_loc, field_loc, -1);
  INKMimeHdrFieldNameSet(hdr_bufp, hdr_loc, field_loc, mimehdr1_name, strlen(mimehdr1_name));
  value_loc = INKMimeHdrFieldValueInsert(hdr_bufp, hdr_loc, field_loc, mimehdr1_value, strlen(mimehdr1_value), -1);
  INKDebug("resphdr", "init buffer hdr, field and value locs are %d, %d and %d", hdr_loc, field_loc, value_loc);
  init_buffer_status = 1;


  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(modify_response_header_plugin, NULL));

  /*
   *  The following code demonstrates how to extract the field_loc from the header.
   *  In this plugin, the init buffer and thus field_loc never changes.  Code 
   *  similar to this may be used to extract header fields from any buffer.
   */

  if (0 == (chk_field_loc = INKMimeHdrFieldGet(hdr_bufp, hdr_loc, 0))) {
    INKError("couldn't retrieve header field from init buffer");
    INKError("marking init buffer as corrupt; no more plugin processing");
    init_buffer_status = 0;
    /* bail out here and reenable transaction */
  } else {
    if (field_loc != chk_field_loc)
      INKError("retrieved buffer field loc is %d when it should be %d", chk_field_loc, field_loc);
  }
}
