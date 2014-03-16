
#include "P_SpdyCommon.h"
#include "P_SpdyCallbacks.h"

Config SPDY_CFG;

string
http_date(time_t t)
{
  char buf[32];
  tm* tms = gmtime(&t); // returned struct is statically allocated.
  size_t r = strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tms);
  return std::string(&buf[0], &buf[r]);
}


int
spdy_config_load()
{
  SPDY_CFG.nr_accept_threads = 1;
  SPDY_CFG.accept_no_activity_timeout = 30;
  SPDY_CFG.no_activity_timeout_in = 30;
  SPDY_CFG.spdy.verbose = false;
  SPDY_CFG.spdy.enable_tls = false;
  SPDY_CFG.spdy.keep_host_port = false;
  //
  // SPDY plugin will share the same port number with
  // http server, unless '--port' is given.
  //
  SPDY_CFG.spdy.serv_port = -1;
  SPDY_CFG.spdy.max_concurrent_streams = 1000;
  SPDY_CFG.spdy.initial_window_size = (64 << 10);

  spdy_callbacks_init(&SPDY_CFG.spdy.callbacks);

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
  loc = TSFetchRespHdrMLocGet(fetch_sm);

  hdr_len = TSMimeHdrLengthGet(bufp, loc);
  mime_hdr = malloc(hdr_len);
  TSReleaseAssert(mime_hdr);

  nr_fields = TSMimeHdrFieldsCount(bufp, loc);
  nv = (const char **)malloc((2*nr_fields + 5) * sizeof(char *));
  TSReleaseAssert(nv);

  //
  // Process Status and Version
  //
  i = TSHttpHdrVersionGet(bufp, loc);
  snprintf(version, sizeof(version), "HTTP/%d.%d", TS_HTTP_MAJOR(i), TS_HTTP_MINOR(i));

  i = TSHttpHdrStatusGet(bufp, loc);
  value = (char *)TSHttpHdrReasonGet(bufp, loc, &value_len);
  snprintf(status, sizeof(version), "%d ", i);
  i = strlen(status);
  len = sizeof(status) - i;
  len = value_len > len ? len : value_len;
  strncpy(&status[i], value, len);
  status[len + i] = '\0';;

  i = 0;
  nv[i++] = ":version";
  nv[i++] = version;
  nv[i++] = ":status";
  nv[i++] = status;

  //
  // Process HTTP headers
  //
  p = (char *)mime_hdr;
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

    strncpy(p, name, name_len);
    nv[i++] = p;
    p += name_len;
    *p++ = '\0';

    value = TSMimeHdrFieldValueStringGet(bufp, loc, field_loc, -1, &value_len);
    TSReleaseAssert(value && value_len);
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
