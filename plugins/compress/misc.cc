/** @file

  Transforms content using gzip, deflate or brotli

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

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#include "misc.h"
#include <cstring>
#include <cinttypes>
#include "debug_macros.h"

voidpf
gzip_alloc(voidpf /* opaque ATS_UNUSED */, uInt items, uInt size)
{
  return static_cast<voidpf>(TSmalloc(items * size));
}

void
gzip_free(voidpf /* opaque ATS_UNUSED */, voidpf address)
{
  TSfree(address);
}

void
normalize_accept_encoding(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  int deflate  = 0;
  int gzip     = 0;
  int br       = 0;
  // remove the accept encoding field(s),
  // while finding out if gzip or deflate is supported.
  while (field) {
    TSMLoc tmp;

    if (!deflate && !gzip) {
      int value_count = TSMimeHdrFieldValuesCount(reqp, hdr_loc, field);

      while (value_count > 0) {
        int val_len = 0;
        const char *val;

        --value_count;
        val = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field, value_count, &val_len);

        if (val_len == static_cast<int>(strlen("br"))) {
          br = !strncmp(val, "br", val_len);
        }
        if (val_len == static_cast<int>(strlen("gzip"))) {
          gzip = !strncmp(val, "gzip", val_len);
        } else if (val_len == static_cast<int>(strlen("deflate"))) {
          deflate = !strncmp(val, "deflate", val_len);
        }
      }
    }

    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldDestroy(reqp, hdr_loc, field); // catch retval?
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }

  // append a new accept-encoding field in the header
  if (deflate || gzip || br) {
    TSMimeHdrFieldCreate(reqp, hdr_loc, &field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    if (br) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "br", strlen("br"));
      info("normalized accept encoding to br");
    }
    if (gzip) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "gzip", strlen("gzip"));
      info("normalized accept encoding to gzip");
    } else if (deflate) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "deflate", strlen("deflate"));
      info("normalized accept encoding to deflate");
    }

    TSMimeHdrFieldAppend(reqp, hdr_loc, field);
    TSHandleMLocRelease(reqp, hdr_loc, field);
  }
}

void
hide_accept_encoding(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name)
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
restore_accept_encoding(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name)
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

const char *
init_hidden_header_name()
{
  char *hidden_header_name;
  const char *var_name = "proxy.config.proxy_name";
  TSMgmtString result;

  if (TSMgmtStringGet(var_name, &result) != TS_SUCCESS) {
    fatal("failed to get server name");
  } else {
    int hidden_header_name_len                 = strlen("x-accept-encoding-") + strlen(result);
    hidden_header_name                         = static_cast<char *>(TSmalloc(hidden_header_name_len + 1));
    hidden_header_name[hidden_header_name_len] = 0;
    sprintf(hidden_header_name, "x-accept-encoding-%s", result);
  }
  return hidden_header_name;
}

int
register_plugin()
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"compress";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    return 0;
  }
  return 1;
}

void
log_compression_ratio(int64_t in, int64_t out)
{
  if (in) {
    info("Compressed size %" PRId64 " (bytes), Original size %" PRId64 ", ratio: %f", out, in, ((float)(in - out) / in));
  } else {
    debug("Compressed size %" PRId64 " (bytes), Original size %" PRId64 ", ratio: %f", out, in, 0.0F);
  }
}
