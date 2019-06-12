/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "utils.h"

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const void *
SockaddrGetAddress(const sockaddr *saddr)
{
  union {
    const sockaddr *sa;
    const sockaddr_in *sin;
    const sockaddr_in6 *sin6;
  } addr;

  addr.sa = saddr;
  if (addr.sa->sa_family == PF_INET6) {
    return &addr.sin6->sin6_addr;
  } else {
    TSReleaseAssert(addr.sin->sin_family == PF_INET);
    return &addr.sin->sin_addr;
  }
}

uint16_t
SockaddrGetPort(const sockaddr *saddr)
{
  union {
    const sockaddr *sa;
    const sockaddr_in *sin;
    const sockaddr_in6 *sin6;
  } addr;

  addr.sa = saddr;
  if (addr.sa->sa_family == PF_INET6) {
    return addr.sin6->sin6_port;
  } else {
    TSReleaseAssert(addr.sin->sin_family == PF_INET);
    return addr.sin->sin_port;
  }
}

void
HttpDebugHeader(TSMBuffer mbuf, TSMLoc mhdr)
{
  HttpIoBuffer iobuf;
  int64_t nbytes;
  int64_t avail;
  const char *ptr;
  TSIOBufferBlock blk;

  TSHttpHdrPrint(mbuf, mhdr, iobuf.buffer);

  blk   = TSIOBufferReaderStart(iobuf.reader);
  avail = TSIOBufferBlockReadAvail(blk, iobuf.reader);
  ptr   = (const char *)TSIOBufferBlockReadStart(blk, iobuf.reader, &nbytes);

  AuthLogDebug("http request (%u of %u bytes):\n%*.*s", (unsigned)nbytes, (unsigned)avail, (int)nbytes, (int)nbytes, ptr);
}

void
HttpSetMimeHeader(TSMBuffer mbuf, TSMLoc mhdr, const char *name, unsigned value)
{
  TSMLoc mloc;

  mloc = TSMimeHdrFieldFind(mbuf, mhdr, name, -1);
  if (mloc == TS_NULL_MLOC) {
    TSReleaseAssert(TSMimeHdrFieldCreateNamed(mbuf, mhdr, name, -1, &mloc) == TS_SUCCESS);
  } else {
    TSReleaseAssert(TSMimeHdrFieldValuesClear(mbuf, mhdr, mloc) == TS_SUCCESS);
  }

  TSReleaseAssert(TSMimeHdrFieldValueUintInsert(mbuf, mhdr, mloc, 0 /* index */, value) == TS_SUCCESS);
  TSReleaseAssert(TSMimeHdrFieldAppend(mbuf, mhdr, mloc) == TS_SUCCESS);

  TSHandleMLocRelease(mbuf, mhdr, mloc);
}

void
HttpSetMimeHeader(TSMBuffer mbuf, TSMLoc mhdr, const char *name, const char *value)
{
  TSMLoc mloc;

  mloc = TSMimeHdrFieldFind(mbuf, mhdr, name, -1);
  if (mloc == TS_NULL_MLOC) {
    TSReleaseAssert(TSMimeHdrFieldCreateNamed(mbuf, mhdr, name, -1, &mloc) == TS_SUCCESS);
  } else {
    TSReleaseAssert(TSMimeHdrFieldValuesClear(mbuf, mhdr, mloc) == TS_SUCCESS);
  }

  TSReleaseAssert(TSMimeHdrFieldValueStringInsert(mbuf, mhdr, mloc, 0 /* index */, value, -1) == TS_SUCCESS);
  TSReleaseAssert(TSMimeHdrFieldAppend(mbuf, mhdr, mloc) == TS_SUCCESS);

  TSHandleMLocRelease(mbuf, mhdr, mloc);
}

unsigned
HttpGetContentLength(TSMBuffer mbuf, TSMLoc mhdr)
{
  TSMLoc mloc;
  unsigned value = 0;

  mloc = TSMimeHdrFieldFind(mbuf, mhdr, TS_MIME_FIELD_CONTENT_LENGTH, -1);
  if (mloc != TS_NULL_MLOC) {
    value = TSMimeHdrFieldValueUintGet(mbuf, mhdr, mloc, 0 /* index */);
  }

  TSHandleMLocRelease(mbuf, mhdr, mloc);
  return value;
}

bool
HttpIsChunkedEncoding(TSMBuffer mbuf, TSMLoc mhdr)
{
  TSMLoc mloc;
  bool ischunked = false;

  mloc = TSMimeHdrFieldFind(mbuf, mhdr, TS_MIME_FIELD_TRANSFER_ENCODING, -1);
  if (mloc != TS_NULL_MLOC) {
    const char *str;
    int len;

    str = TSMimeHdrFieldValueStringGet(mbuf, mhdr, mloc, -1 /* index */, &len);
    if (str && len) {
      ischunked = (strncmp("chunked", str, len) == 0);
    }
  }

  TSHandleMLocRelease(mbuf, mhdr, mloc);
  return ischunked;
}

bool
HttpGetOriginHost(TSMBuffer mbuf, TSMLoc mhdr, char *name, size_t namelen)
{
  const char *host;
  int len;
  TSMLoc mloc;

  // First, try to get it from the Host header. The Host header this returns
  // depends on whether pristine_host_hdr is set.
  mloc = TSMimeHdrFieldFind(mbuf, mhdr, TS_MIME_FIELD_HOST, -1);
  if (mloc != TS_NULL_MLOC) {
    host = TSMimeHdrFieldValueStringGet(mbuf, mhdr, mloc, -1 /* index */, &len);
    TSHandleMLocRelease(mbuf, mhdr, mloc);

    if (host) {
      AuthLogDebug("using origin %.*s from host header", len, host);
      len = std::min(len, (int)namelen - 1);
      memcpy(name, host, len);
      name[len] = '\0';
      return true;
    }
  }

  // If that didn't work, try to get the origin host from the request URL.
  if (TSHttpHdrUrlGet(mbuf, mhdr, &mloc) == TS_SUCCESS) {
    host = TSUrlHostGet(mbuf, mloc, &len);
    TSHandleMLocRelease(mbuf, mhdr, mloc);

    if (host) {
      AuthLogDebug("using origin %.*s from request URL", len, host);
      len = std::min(len, (int)namelen - 1);
      memcpy(name, host, len);
      name[len] = '\0';
      return true;
    }
  }

  return false;
}

// vim: set ts=4 sw=4 et :
