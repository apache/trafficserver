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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "ts.h"

static INKMBuffer hdr_bufp;
static INKMLoc hdr_loc;

static void
add_header(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer req_bufp;
  INKMLoc req_loc;
  INKMLoc field_loc;
  INKMLoc new_field_loc;

  if (!INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  field_loc = INKMimeHdrFieldGet(hdr_bufp, hdr_loc, 0);
  while (field_loc) {
    new_field_loc = INKMimeFieldCreate(req_bufp);
    INKMimeFieldCopy(req_bufp, new_field_loc, hdr_bufp, field_loc);
    INKMimeHdrFieldInsert(req_bufp, req_loc, new_field_loc, -1);
    field_loc = INKMimeFieldNext(hdr_bufp, field_loc);
  }

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

void
INKPluginInit(int argc, const char *argv[])
{
  INKMLoc field_loc;
  const char *p;
  int i;

  if (argc < 2) {
    INKError("usage: %s \"name1: value1\" \"name2: value2\" ...>\n", argv[0]);
    return;
  }

  hdr_bufp = INKMBufferCreate();
  hdr_loc = INKMimeHdrCreate(hdr_bufp);

  for (i = 1; i < argc; i++) {
    field_loc = INKMimeFieldCreate(hdr_bufp);
    INKMimeHdrFieldInsert(hdr_bufp, hdr_loc, field_loc, -1);

    p = strchr(argv[i], ':');
    if (p) {
      INKMimeFieldNameSet(hdr_bufp, field_loc, argv[i], p - argv[i]);
      p += 1;
      while (isspace(*p)) {
        p += 1;
      }
      INKMimeFieldValueInsert(hdr_bufp, field_loc, p, -1, -1);
    } else {
      INKMimeFieldNameSet(hdr_bufp, field_loc, argv[i], -1);
    }
  }

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(add_header_plugin, NULL));
}
