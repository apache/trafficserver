/** @file

  SpdyCommon.cc

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

#include "SpdyCommon.h"
#include "SpdyCallbacks.h"

// SPDYlay callbacks
spdylay_session_callbacks spdy_callbacks;

// statistic names
RecRawStatBlock *spdy_rsb; ///< Container for statistics.

static char const *const SPDY_STAT_CURRENT_CLIENT_SESSION_NAME  = "proxy.process.spdy.current_client_sessions";
static char const *const SPDY_STAT_CURRENT_CLIENT_STREAM_NAME   = "proxy.process.spdy.current_client_streams";
static char const *const SPDY_STAT_TOTAL_CLIENT_STREAM_NAME     = "proxy.process.spdy.total_client_streams";
static char const *const SPDY_STAT_TOTAL_TRANSACTIONS_TIME_NAME = "proxy.process.spdy.total_transactions_time";
static char const *const SPDY_STAT_TOTAL_CLIENT_CONNECTION_NAME = "proxy.process.spdy.total_client_connections";

// Configurations
uint32_t spdy_max_concurrent_streams    = 100;
uint32_t spdy_initial_window_size       = 1048576;
int32_t spdy_accept_no_activity_timeout = 120;
int32_t spdy_no_activity_timeout_in     = 115;

string
http_date(time_t t)
{
  char buf[32];
  tm *tms  = gmtime(&t); // returned struct is statically allocated.
  size_t r = strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tms);
  return std::string(&buf[0], &buf[r]);
}

int
spdy_config_load()
{
  REC_EstablishStaticConfigInt32U(spdy_max_concurrent_streams, "proxy.config.spdy.max_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(spdy_initial_window_size, "proxy.config.spdy.initial_window_size_in");
  REC_EstablishStaticConfigInt32(spdy_no_activity_timeout_in, "proxy.config.spdy.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32(spdy_accept_no_activity_timeout, "proxy.config.spdy.accept_no_activity_timeout");

  spdy_callbacks_init(&spdy_callbacks);

  // Get our statistics up
  spdy_rsb = RecAllocateRawStatBlock(static_cast<int>(SPDY_N_STATS));
  RecRegisterRawStat(spdy_rsb, RECT_PROCESS, SPDY_STAT_CURRENT_CLIENT_SESSION_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(SPDY_STAT_CURRENT_CLIENT_SESSION_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(spdy_rsb, RECT_PROCESS, SPDY_STAT_CURRENT_CLIENT_STREAM_NAME, RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(SPDY_STAT_CURRENT_CLIENT_STREAM_COUNT), RecRawStatSyncSum);
  RecRegisterRawStat(spdy_rsb, RECT_PROCESS, SPDY_STAT_TOTAL_CLIENT_STREAM_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(SPDY_STAT_TOTAL_TRANSACTIONS_TIME), RecRawStatSyncCount);
  RecRegisterRawStat(spdy_rsb, RECT_PROCESS, SPDY_STAT_TOTAL_TRANSACTIONS_TIME_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(SPDY_STAT_TOTAL_TRANSACTIONS_TIME), RecRawStatSyncSum);
  RecRegisterRawStat(spdy_rsb, RECT_PROCESS, SPDY_STAT_TOTAL_CLIENT_CONNECTION_NAME, RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(SPDY_STAT_TOTAL_CLIENT_CONNECTION_COUNT), RecRawStatSyncSum);

  return 0;
}

SpdyNV::SpdyNV(TSFetchSM fetch_sm)
{
  int i, len;
  char *p;
  const char *name, *value;
  int name_len, value_len, hdr_len, nr_fields;
  TSMLoc loc, field_loc, next_loc;
  TSMBuffer bufp;

  bufp = TSFetchRespHdrMBufGet(fetch_sm);
  loc  = TSFetchRespHdrMLocGet(fetch_sm);

  hdr_len  = TSMimeHdrLengthGet(bufp, loc);
  mime_hdr = malloc(hdr_len);
  TSReleaseAssert(mime_hdr);

  nr_fields = TSMimeHdrFieldsCount(bufp, loc);

  valid_response = true;

  if (nr_fields <= 0) {
    Debug("spdy_error", "invalid fetchsm %p, nr_fields %d, hdr_len %d", fetch_sm, nr_fields, hdr_len);
    valid_response = false;
  }

  nv = (const char **)malloc((2 * nr_fields + 5) * sizeof(char *));
  TSReleaseAssert(nv);

  //
  // Process Status and Version
  //
  i = TSHttpHdrVersionGet(bufp, loc);
  snprintf(version, sizeof(version), "HTTP/%d.%d", TS_HTTP_MAJOR(i), TS_HTTP_MINOR(i));

  i     = TSHttpHdrStatusGet(bufp, loc);
  value = (char *)TSHttpHdrReasonGet(bufp, loc, &value_len);
  snprintf(status, sizeof(version), "%d ", i);
  i   = strlen(status);
  len = sizeof(status) - i;
  len = value_len > len ? len : value_len;
  strncpy(&status[i], value, len);
  status[len + i] = '\0';
  ;

  i       = 0;
  nv[i++] = ":version";
  nv[i++] = version;
  nv[i++] = ":status";
  nv[i++] = status;

  //
  // Process HTTP headers
  //
  p         = (char *)mime_hdr;
  field_loc = TSMimeHdrFieldGet(bufp, loc, 0);
  while (field_loc) {
    name = TSMimeHdrFieldNameGet(bufp, loc, field_loc, &name_len);
    TSReleaseAssert(name && name_len);

    //
    // According SPDY v3 spec, in RESPONSE:
    // The Connection, Keep-Alive, Proxy-Connection, and
    // Transfer-Encoding headers are not valid and MUST not be sent.
    //
    if (!strncasecmp(name, "Connection", name_len))
      goto next;

    if (!strncasecmp(name, "Keep-Alive", name_len))
      goto next;

    if (!strncasecmp(name, "Proxy-Connection", name_len))
      goto next;

    if (!strncasecmp(name, "Transfer-Encoding", name_len))
      goto next;

    value = TSMimeHdrFieldValueStringGet(bufp, loc, field_loc, -1, &value_len);

    //
    // Any HTTP headers with empty value are invalid,
    // we should ignore them.
    //
    if (!value || !value_len)
      goto next;

    strncpy(p, name, name_len);
    nv[i++] = p;
    p += name_len;
    *p++ = '\0';

    strncpy(p, value, value_len);
    nv[i++] = p;
    p += value_len;
    *p++ = '\0';

  next:
    next_loc = TSMimeHdrFieldNext(bufp, loc, field_loc);
    TSHandleMLocRelease(bufp, loc, field_loc);
    field_loc = next_loc;
  }
  nv[i] = NULL;

  if (field_loc)
    TSHandleMLocRelease(bufp, loc, field_loc);
}

SpdyNV::~SpdyNV()
{
  if (nv)
    free(nv);

  if (mime_hdr)
    free(mime_hdr);
}
