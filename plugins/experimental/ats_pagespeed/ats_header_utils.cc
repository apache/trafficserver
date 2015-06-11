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

#include "ats_header_utils.h"

GoogleString
get_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header_name)
{
  const char *val = NULL;
  int val_len;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header_name, -1);

  if (field_loc) {
    val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &val_len);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    return GoogleString(val, val_len);
  }

  return GoogleString("");
}

void
unset_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header_name)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header_name, -1);

  if (field_loc) {
    TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

void
hide_accept_encoding(TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, hidden_header_name, -1);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}

void
restore_accept_encoding(TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, hidden_header_name, -1);

  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}

void
set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header_name, const char *header_value)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header_name, -1);

  if (field_loc) {
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, header_value, -1);
  } else {
    if (TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc) == TS_SUCCESS) {
      TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, header_name, -1);
      TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, header_value, -1);
    } else {
      TSError("[ats_header_utils] Field creation error for field [%s]", header_name);
      return;
    }
  }

  if (field_loc) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}
