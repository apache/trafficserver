/** @file

  Implements the Traffic Server Internal C API support functions.

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

#include "iocore/net/SSLAPIHooks.h"
#include "api/LifecycleAPIHooks.h"
#include "api/InkAPIInternal.h"

char traffic_server_version[128] = "";
int  ts_major_version            = 0;
int  ts_minor_version            = 0;
int  ts_patch_version            = 0;

/* URL schemes */
const char *TS_URL_SCHEME_FILE;
const char *TS_URL_SCHEME_FTP;
const char *TS_URL_SCHEME_GOPHER;
const char *TS_URL_SCHEME_HTTP;
const char *TS_URL_SCHEME_HTTPS;
const char *TS_URL_SCHEME_MAILTO;
const char *TS_URL_SCHEME_NEWS;
const char *TS_URL_SCHEME_NNTP;
const char *TS_URL_SCHEME_PROSPERO;
const char *TS_URL_SCHEME_TELNET;
const char *TS_URL_SCHEME_TUNNEL;
const char *TS_URL_SCHEME_WAIS;
const char *TS_URL_SCHEME_PNM;
const char *TS_URL_SCHEME_RTSP;
const char *TS_URL_SCHEME_RTSPU;
const char *TS_URL_SCHEME_MMS;
const char *TS_URL_SCHEME_MMSU;
const char *TS_URL_SCHEME_MMST;
const char *TS_URL_SCHEME_WS;
const char *TS_URL_SCHEME_WSS;

/* URL schemes string lengths */
int TS_URL_LEN_FILE;
int TS_URL_LEN_FTP;
int TS_URL_LEN_GOPHER;
int TS_URL_LEN_HTTP;
int TS_URL_LEN_HTTPS;
int TS_URL_LEN_MAILTO;
int TS_URL_LEN_NEWS;
int TS_URL_LEN_NNTP;
int TS_URL_LEN_PROSPERO;
int TS_URL_LEN_TELNET;
int TS_URL_LEN_TUNNEL;
int TS_URL_LEN_WAIS;
int TS_URL_LEN_PNM;
int TS_URL_LEN_RTSP;
int TS_URL_LEN_RTSPU;
int TS_URL_LEN_MMS;
int TS_URL_LEN_MMSU;
int TS_URL_LEN_MMST;
int TS_URL_LEN_WS;
int TS_URL_LEN_WSS;

/* MIME fields */
const char *TS_MIME_FIELD_ACCEPT;
const char *TS_MIME_FIELD_ACCEPT_CHARSET;
const char *TS_MIME_FIELD_ACCEPT_ENCODING;
const char *TS_MIME_FIELD_ACCEPT_LANGUAGE;
const char *TS_MIME_FIELD_ACCEPT_RANGES;
const char *TS_MIME_FIELD_AGE;
const char *TS_MIME_FIELD_ALLOW;
const char *TS_MIME_FIELD_APPROVED;
const char *TS_MIME_FIELD_AUTHORIZATION;
const char *TS_MIME_FIELD_BYTES;
const char *TS_MIME_FIELD_CACHE_CONTROL;
const char *TS_MIME_FIELD_CLIENT_IP;
const char *TS_MIME_FIELD_CONNECTION;
const char *TS_MIME_FIELD_CONTENT_BASE;
const char *TS_MIME_FIELD_CONTENT_ENCODING;
const char *TS_MIME_FIELD_CONTENT_LANGUAGE;
const char *TS_MIME_FIELD_CONTENT_LENGTH;
const char *TS_MIME_FIELD_CONTENT_LOCATION;
const char *TS_MIME_FIELD_CONTENT_MD5;
const char *TS_MIME_FIELD_CONTENT_RANGE;
const char *TS_MIME_FIELD_CONTENT_TYPE;
const char *TS_MIME_FIELD_CONTROL;
const char *TS_MIME_FIELD_COOKIE;
const char *TS_MIME_FIELD_DATE;
const char *TS_MIME_FIELD_DISTRIBUTION;
const char *TS_MIME_FIELD_ETAG;
const char *TS_MIME_FIELD_EXPECT;
const char *TS_MIME_FIELD_EXPIRES;
const char *TS_MIME_FIELD_FOLLOWUP_TO;
const char *TS_MIME_FIELD_FROM;
const char *TS_MIME_FIELD_HOST;
const char *TS_MIME_FIELD_IF_MATCH;
const char *TS_MIME_FIELD_IF_MODIFIED_SINCE;
const char *TS_MIME_FIELD_IF_NONE_MATCH;
const char *TS_MIME_FIELD_IF_RANGE;
const char *TS_MIME_FIELD_IF_UNMODIFIED_SINCE;
const char *TS_MIME_FIELD_KEEP_ALIVE;
const char *TS_MIME_FIELD_KEYWORDS;
const char *TS_MIME_FIELD_LAST_MODIFIED;
const char *TS_MIME_FIELD_LINES;
const char *TS_MIME_FIELD_LOCATION;
const char *TS_MIME_FIELD_MAX_FORWARDS;
const char *TS_MIME_FIELD_MESSAGE_ID;
const char *TS_MIME_FIELD_NEWSGROUPS;
const char *TS_MIME_FIELD_ORGANIZATION;
const char *TS_MIME_FIELD_PATH;
const char *TS_MIME_FIELD_PRAGMA;
const char *TS_MIME_FIELD_PROXY_AUTHENTICATE;
const char *TS_MIME_FIELD_PROXY_AUTHORIZATION;
const char *TS_MIME_FIELD_PROXY_CONNECTION;
const char *TS_MIME_FIELD_PUBLIC;
const char *TS_MIME_FIELD_RANGE;
const char *TS_MIME_FIELD_REFERENCES;
const char *TS_MIME_FIELD_REFERER;
const char *TS_MIME_FIELD_REPLY_TO;
const char *TS_MIME_FIELD_RETRY_AFTER;
const char *TS_MIME_FIELD_SENDER;
const char *TS_MIME_FIELD_SERVER;
const char *TS_MIME_FIELD_SET_COOKIE;
const char *TS_MIME_FIELD_STRICT_TRANSPORT_SECURITY;
const char *TS_MIME_FIELD_SUBJECT;
const char *TS_MIME_FIELD_SUMMARY;
const char *TS_MIME_FIELD_TE;
const char *TS_MIME_FIELD_TRANSFER_ENCODING;
const char *TS_MIME_FIELD_UPGRADE;
const char *TS_MIME_FIELD_USER_AGENT;
const char *TS_MIME_FIELD_VARY;
const char *TS_MIME_FIELD_VIA;
const char *TS_MIME_FIELD_WARNING;
const char *TS_MIME_FIELD_WWW_AUTHENTICATE;
const char *TS_MIME_FIELD_XREF;
const char *TS_MIME_FIELD_X_FORWARDED_FOR;
const char *TS_MIME_FIELD_FORWARDED;

/* MIME fields string lengths */
int TS_MIME_LEN_ACCEPT;
int TS_MIME_LEN_ACCEPT_CHARSET;
int TS_MIME_LEN_ACCEPT_ENCODING;
int TS_MIME_LEN_ACCEPT_LANGUAGE;
int TS_MIME_LEN_ACCEPT_RANGES;
int TS_MIME_LEN_AGE;
int TS_MIME_LEN_ALLOW;
int TS_MIME_LEN_APPROVED;
int TS_MIME_LEN_AUTHORIZATION;
int TS_MIME_LEN_BYTES;
int TS_MIME_LEN_CACHE_CONTROL;
int TS_MIME_LEN_CLIENT_IP;
int TS_MIME_LEN_CONNECTION;
int TS_MIME_LEN_CONTENT_BASE;
int TS_MIME_LEN_CONTENT_ENCODING;
int TS_MIME_LEN_CONTENT_LANGUAGE;
int TS_MIME_LEN_CONTENT_LENGTH;
int TS_MIME_LEN_CONTENT_LOCATION;
int TS_MIME_LEN_CONTENT_MD5;
int TS_MIME_LEN_CONTENT_RANGE;
int TS_MIME_LEN_CONTENT_TYPE;
int TS_MIME_LEN_CONTROL;
int TS_MIME_LEN_COOKIE;
int TS_MIME_LEN_DATE;
int TS_MIME_LEN_DISTRIBUTION;
int TS_MIME_LEN_ETAG;
int TS_MIME_LEN_EXPECT;
int TS_MIME_LEN_EXPIRES;
int TS_MIME_LEN_FOLLOWUP_TO;
int TS_MIME_LEN_FROM;
int TS_MIME_LEN_HOST;
int TS_MIME_LEN_IF_MATCH;
int TS_MIME_LEN_IF_MODIFIED_SINCE;
int TS_MIME_LEN_IF_NONE_MATCH;
int TS_MIME_LEN_IF_RANGE;
int TS_MIME_LEN_IF_UNMODIFIED_SINCE;
int TS_MIME_LEN_KEEP_ALIVE;
int TS_MIME_LEN_KEYWORDS;
int TS_MIME_LEN_LAST_MODIFIED;
int TS_MIME_LEN_LINES;
int TS_MIME_LEN_LOCATION;
int TS_MIME_LEN_MAX_FORWARDS;
int TS_MIME_LEN_MESSAGE_ID;
int TS_MIME_LEN_NEWSGROUPS;
int TS_MIME_LEN_ORGANIZATION;
int TS_MIME_LEN_PATH;
int TS_MIME_LEN_PRAGMA;
int TS_MIME_LEN_PROXY_AUTHENTICATE;
int TS_MIME_LEN_PROXY_AUTHORIZATION;
int TS_MIME_LEN_PROXY_CONNECTION;
int TS_MIME_LEN_PUBLIC;
int TS_MIME_LEN_RANGE;
int TS_MIME_LEN_REFERENCES;
int TS_MIME_LEN_REFERER;
int TS_MIME_LEN_REPLY_TO;
int TS_MIME_LEN_RETRY_AFTER;
int TS_MIME_LEN_SENDER;
int TS_MIME_LEN_SERVER;
int TS_MIME_LEN_SET_COOKIE;
int TS_MIME_LEN_STRICT_TRANSPORT_SECURITY;
int TS_MIME_LEN_SUBJECT;
int TS_MIME_LEN_SUMMARY;
int TS_MIME_LEN_TE;
int TS_MIME_LEN_TRANSFER_ENCODING;
int TS_MIME_LEN_UPGRADE;
int TS_MIME_LEN_USER_AGENT;
int TS_MIME_LEN_VARY;
int TS_MIME_LEN_VIA;
int TS_MIME_LEN_WARNING;
int TS_MIME_LEN_WWW_AUTHENTICATE;
int TS_MIME_LEN_XREF;
int TS_MIME_LEN_X_FORWARDED_FOR;
int TS_MIME_LEN_FORWARDED;

/* HTTP miscellaneous values */
const char *TS_HTTP_VALUE_BYTES;
const char *TS_HTTP_VALUE_CHUNKED;
const char *TS_HTTP_VALUE_CLOSE;
const char *TS_HTTP_VALUE_COMPRESS;
const char *TS_HTTP_VALUE_DEFLATE;
const char *TS_HTTP_VALUE_GZIP;
const char *TS_HTTP_VALUE_BROTLI;
const char *TS_HTTP_VALUE_IDENTITY;
const char *TS_HTTP_VALUE_KEEP_ALIVE;
const char *TS_HTTP_VALUE_MAX_AGE;
const char *TS_HTTP_VALUE_MAX_STALE;
const char *TS_HTTP_VALUE_MIN_FRESH;
const char *TS_HTTP_VALUE_MUST_REVALIDATE;
const char *TS_HTTP_VALUE_NONE;
const char *TS_HTTP_VALUE_NO_CACHE;
const char *TS_HTTP_VALUE_NO_STORE;
const char *TS_HTTP_VALUE_NO_TRANSFORM;
const char *TS_HTTP_VALUE_ONLY_IF_CACHED;
const char *TS_HTTP_VALUE_PRIVATE;
const char *TS_HTTP_VALUE_PROXY_REVALIDATE;
const char *TS_HTTP_VALUE_PUBLIC;
const char *TS_HTTP_VALUE_S_MAXAGE;

/* HTTP miscellaneous values string lengths */
int TS_HTTP_LEN_BYTES;
int TS_HTTP_LEN_CHUNKED;
int TS_HTTP_LEN_CLOSE;
int TS_HTTP_LEN_COMPRESS;
int TS_HTTP_LEN_DEFLATE;
int TS_HTTP_LEN_GZIP;
int TS_HTTP_LEN_BROTLI;
int TS_HTTP_LEN_IDENTITY;
int TS_HTTP_LEN_KEEP_ALIVE;
int TS_HTTP_LEN_MAX_AGE;
int TS_HTTP_LEN_MAX_STALE;
int TS_HTTP_LEN_MIN_FRESH;
int TS_HTTP_LEN_MUST_REVALIDATE;
int TS_HTTP_LEN_NONE;
int TS_HTTP_LEN_NO_CACHE;
int TS_HTTP_LEN_NO_STORE;
int TS_HTTP_LEN_NO_TRANSFORM;
int TS_HTTP_LEN_ONLY_IF_CACHED;
int TS_HTTP_LEN_PRIVATE;
int TS_HTTP_LEN_PROXY_REVALIDATE;
int TS_HTTP_LEN_PUBLIC;
int TS_HTTP_LEN_S_MAXAGE;

/* HTTP methods */
const char *TS_HTTP_METHOD_CONNECT;
const char *TS_HTTP_METHOD_DELETE;
const char *TS_HTTP_METHOD_GET;
const char *TS_HTTP_METHOD_HEAD;
const char *TS_HTTP_METHOD_OPTIONS;
const char *TS_HTTP_METHOD_POST;
const char *TS_HTTP_METHOD_PURGE;
const char *TS_HTTP_METHOD_PUT;
const char *TS_HTTP_METHOD_TRACE;
const char *TS_HTTP_METHOD_PUSH;

/* HTTP methods string lengths */
int TS_HTTP_LEN_CONNECT;
int TS_HTTP_LEN_DELETE;
int TS_HTTP_LEN_GET;
int TS_HTTP_LEN_HEAD;
int TS_HTTP_LEN_OPTIONS;
int TS_HTTP_LEN_POST;
int TS_HTTP_LEN_PURGE;
int TS_HTTP_LEN_PUT;
int TS_HTTP_LEN_TRACE;
int TS_HTTP_LEN_PUSH;

////////////////////////////////////////////////////////////////////
//
// FileImpl
//
////////////////////////////////////////////////////////////////////
FileImpl::FileImpl() : m_fd(-1), m_mode(CLOSED), m_buf(nullptr), m_bufsize(0), m_bufpos(0) {}

FileImpl::~FileImpl()
{
  fclose();
}

int
FileImpl::fopen(const char *filename, const char *mode)
{
  if (mode[0] == '\0') {
    return 0;
  } else if (mode[0] == 'r') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = READ;
    m_fd   = open(filename, O_RDONLY);
  } else if (mode[0] == 'w') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd   = open(filename, O_WRONLY | O_CREAT, 0644);
  } else if (mode[0] == 'a') {
    if (mode[1] != '\0') {
      return 0;
    }
    m_mode = WRITE;
    m_fd   = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
  }

  if (m_fd < 0) {
    m_mode = CLOSED;
    return 0;
  } else {
    return 1;
  }
}

void
FileImpl::fclose()
{
  if (m_fd != -1) {
    fflush();

    close(m_fd);
    m_fd   = -1;
    m_mode = CLOSED;
  }

  ats_free(m_buf);
  m_buf     = nullptr;
  m_bufsize = 0;
  m_bufpos  = 0;
}

ssize_t
FileImpl::fread(void *buf, size_t length)
{
  size_t  amount;
  ssize_t err;

  if ((m_mode != READ) || (m_fd == -1)) {
    return -1;
  }

  if (!m_buf) {
    m_bufpos  = 0;
    m_bufsize = 1024;
    m_buf     = (char *)ats_malloc(m_bufsize);
  }

  if (m_bufpos < length) {
    amount = length;
    if (amount < 1024) {
      amount = 1024;
    }
    if (amount > (m_bufsize - m_bufpos)) {
      while (amount > (m_bufsize - m_bufpos)) {
        m_bufsize *= 2;
      }
      m_buf = (char *)ats_realloc(m_buf, m_bufsize);
    }

    do {
      err = read(m_fd, &m_buf[m_bufpos], amount);
    } while ((err < 0) && (errno == EINTR));

    if (err < 0) {
      return -1;
    }

    m_bufpos += err;
  }

  if (buf) {
    amount = length;
    if (amount > m_bufpos) {
      amount = m_bufpos;
    }
    memcpy(buf, m_buf, amount);
    memmove(m_buf, &m_buf[amount], m_bufpos - amount);
    m_bufpos -= amount;
    return amount;
  } else {
    return m_bufpos;
  }
}

ssize_t
FileImpl::fwrite(const void *buf, size_t length)
{
  const char *p, *e;
  size_t      avail;

  if ((m_mode != WRITE) || (m_fd == -1)) {
    return -1;
  }

  if (!m_buf) {
    m_bufpos  = 0;
    m_bufsize = 1024;
    m_buf     = (char *)ats_malloc(m_bufsize);
  }

  p = (const char *)buf;
  e = p + length;

  while (p != e) {
    avail = m_bufsize - m_bufpos;
    if (avail > length) {
      avail = length;
    }
    memcpy(&m_buf[m_bufpos], p, avail);

    m_bufpos += avail;
    p        += avail;
    length   -= avail;

    if ((length > 0) && (m_bufpos > 0)) {
      if (fflush() <= 0) {
        break;
      }
    }
  }

  return (p - (const char *)buf);
}

ssize_t
FileImpl::fflush()
{
  char   *p, *e;
  ssize_t err = 0;

  if ((m_mode != WRITE) || (m_fd == -1)) {
    return -1;
  }

  if (m_buf) {
    p = m_buf;
    e = &m_buf[m_bufpos];

    while (p != e) {
      do {
        err = write(m_fd, p, e - p);
      } while ((err < 0) && (errno == EINTR));

      if (err < 0) {
        break;
      }

      p += err;
    }

    err = p - m_buf;
    memmove(m_buf, &m_buf[err], m_bufpos - err);
    m_bufpos -= err;
  }

  return err;
}

char *
FileImpl::fgets(char *buf, size_t length)
{
  char  *e;
  size_t pos;

  if (length == 0) {
    return nullptr;
  }

  if (!m_buf || (m_bufpos < (length - 1))) {
    pos = m_bufpos;

    if (fread(nullptr, length - 1) < 0) {
      return nullptr;
    }

    if (!m_bufpos && (pos == m_bufpos)) {
      return nullptr;
    }
  }

  e = (char *)memchr(m_buf, '\n', m_bufpos);
  if (e) {
    e += 1;
    if (length > (size_t)(e - m_buf + 1)) {
      length = e - m_buf + 1;
    }
  }

  ssize_t rlen = fread(buf, length - 1);
  if (rlen >= 0) {
    buf[rlen] = '\0';
  }

  return buf;
}

////////////////////////////////////////////////////////////////////
//
// api_init
//
////////////////////////////////////////////////////////////////////

void
api_init()
{
  // HDR FIX ME

  static int init = 1;

  if (init) {
    init = 0;

    /* URL schemes */
    TS_URL_SCHEME_FILE     = URL_SCHEME_FILE;
    TS_URL_SCHEME_FTP      = URL_SCHEME_FTP;
    TS_URL_SCHEME_GOPHER   = URL_SCHEME_GOPHER;
    TS_URL_SCHEME_HTTP     = URL_SCHEME_HTTP;
    TS_URL_SCHEME_HTTPS    = URL_SCHEME_HTTPS;
    TS_URL_SCHEME_MAILTO   = URL_SCHEME_MAILTO;
    TS_URL_SCHEME_NEWS     = URL_SCHEME_NEWS;
    TS_URL_SCHEME_NNTP     = URL_SCHEME_NNTP;
    TS_URL_SCHEME_PROSPERO = URL_SCHEME_PROSPERO;
    TS_URL_SCHEME_TELNET   = URL_SCHEME_TELNET;
    TS_URL_SCHEME_WAIS     = URL_SCHEME_WAIS;
    TS_URL_SCHEME_WS       = URL_SCHEME_WS;
    TS_URL_SCHEME_WSS      = URL_SCHEME_WSS;

    TS_URL_LEN_FILE     = URL_LEN_FILE;
    TS_URL_LEN_FTP      = URL_LEN_FTP;
    TS_URL_LEN_GOPHER   = URL_LEN_GOPHER;
    TS_URL_LEN_HTTP     = URL_LEN_HTTP;
    TS_URL_LEN_HTTPS    = URL_LEN_HTTPS;
    TS_URL_LEN_MAILTO   = URL_LEN_MAILTO;
    TS_URL_LEN_NEWS     = URL_LEN_NEWS;
    TS_URL_LEN_NNTP     = URL_LEN_NNTP;
    TS_URL_LEN_PROSPERO = URL_LEN_PROSPERO;
    TS_URL_LEN_TELNET   = URL_LEN_TELNET;
    TS_URL_LEN_WAIS     = URL_LEN_WAIS;
    TS_URL_LEN_WS       = URL_LEN_WS;
    TS_URL_LEN_WSS      = URL_LEN_WSS;

    /* MIME fields */
    TS_MIME_FIELD_ACCEPT                    = MIME_FIELD_ACCEPT;
    TS_MIME_FIELD_ACCEPT_CHARSET            = MIME_FIELD_ACCEPT_CHARSET;
    TS_MIME_FIELD_ACCEPT_ENCODING           = MIME_FIELD_ACCEPT_ENCODING;
    TS_MIME_FIELD_ACCEPT_LANGUAGE           = MIME_FIELD_ACCEPT_LANGUAGE;
    TS_MIME_FIELD_ACCEPT_RANGES             = MIME_FIELD_ACCEPT_RANGES;
    TS_MIME_FIELD_AGE                       = MIME_FIELD_AGE;
    TS_MIME_FIELD_ALLOW                     = MIME_FIELD_ALLOW;
    TS_MIME_FIELD_APPROVED                  = MIME_FIELD_APPROVED;
    TS_MIME_FIELD_AUTHORIZATION             = MIME_FIELD_AUTHORIZATION;
    TS_MIME_FIELD_BYTES                     = MIME_FIELD_BYTES;
    TS_MIME_FIELD_CACHE_CONTROL             = MIME_FIELD_CACHE_CONTROL;
    TS_MIME_FIELD_CLIENT_IP                 = MIME_FIELD_CLIENT_IP;
    TS_MIME_FIELD_CONNECTION                = MIME_FIELD_CONNECTION;
    TS_MIME_FIELD_CONTENT_BASE              = MIME_FIELD_CONTENT_BASE;
    TS_MIME_FIELD_CONTENT_ENCODING          = MIME_FIELD_CONTENT_ENCODING;
    TS_MIME_FIELD_CONTENT_LANGUAGE          = MIME_FIELD_CONTENT_LANGUAGE;
    TS_MIME_FIELD_CONTENT_LENGTH            = MIME_FIELD_CONTENT_LENGTH;
    TS_MIME_FIELD_CONTENT_LOCATION          = MIME_FIELD_CONTENT_LOCATION;
    TS_MIME_FIELD_CONTENT_MD5               = MIME_FIELD_CONTENT_MD5;
    TS_MIME_FIELD_CONTENT_RANGE             = MIME_FIELD_CONTENT_RANGE;
    TS_MIME_FIELD_CONTENT_TYPE              = MIME_FIELD_CONTENT_TYPE;
    TS_MIME_FIELD_CONTROL                   = MIME_FIELD_CONTROL;
    TS_MIME_FIELD_COOKIE                    = MIME_FIELD_COOKIE;
    TS_MIME_FIELD_DATE                      = MIME_FIELD_DATE;
    TS_MIME_FIELD_DISTRIBUTION              = MIME_FIELD_DISTRIBUTION;
    TS_MIME_FIELD_ETAG                      = MIME_FIELD_ETAG;
    TS_MIME_FIELD_EXPECT                    = MIME_FIELD_EXPECT;
    TS_MIME_FIELD_EXPIRES                   = MIME_FIELD_EXPIRES;
    TS_MIME_FIELD_FOLLOWUP_TO               = MIME_FIELD_FOLLOWUP_TO;
    TS_MIME_FIELD_FROM                      = MIME_FIELD_FROM;
    TS_MIME_FIELD_HOST                      = MIME_FIELD_HOST;
    TS_MIME_FIELD_IF_MATCH                  = MIME_FIELD_IF_MATCH;
    TS_MIME_FIELD_IF_MODIFIED_SINCE         = MIME_FIELD_IF_MODIFIED_SINCE;
    TS_MIME_FIELD_IF_NONE_MATCH             = MIME_FIELD_IF_NONE_MATCH;
    TS_MIME_FIELD_IF_RANGE                  = MIME_FIELD_IF_RANGE;
    TS_MIME_FIELD_IF_UNMODIFIED_SINCE       = MIME_FIELD_IF_UNMODIFIED_SINCE;
    TS_MIME_FIELD_KEEP_ALIVE                = MIME_FIELD_KEEP_ALIVE;
    TS_MIME_FIELD_KEYWORDS                  = MIME_FIELD_KEYWORDS;
    TS_MIME_FIELD_LAST_MODIFIED             = MIME_FIELD_LAST_MODIFIED;
    TS_MIME_FIELD_LINES                     = MIME_FIELD_LINES;
    TS_MIME_FIELD_LOCATION                  = MIME_FIELD_LOCATION;
    TS_MIME_FIELD_MAX_FORWARDS              = MIME_FIELD_MAX_FORWARDS;
    TS_MIME_FIELD_MESSAGE_ID                = MIME_FIELD_MESSAGE_ID;
    TS_MIME_FIELD_NEWSGROUPS                = MIME_FIELD_NEWSGROUPS;
    TS_MIME_FIELD_ORGANIZATION              = MIME_FIELD_ORGANIZATION;
    TS_MIME_FIELD_PATH                      = MIME_FIELD_PATH;
    TS_MIME_FIELD_PRAGMA                    = MIME_FIELD_PRAGMA;
    TS_MIME_FIELD_PROXY_AUTHENTICATE        = MIME_FIELD_PROXY_AUTHENTICATE;
    TS_MIME_FIELD_PROXY_AUTHORIZATION       = MIME_FIELD_PROXY_AUTHORIZATION;
    TS_MIME_FIELD_PROXY_CONNECTION          = MIME_FIELD_PROXY_CONNECTION;
    TS_MIME_FIELD_PUBLIC                    = MIME_FIELD_PUBLIC;
    TS_MIME_FIELD_RANGE                     = MIME_FIELD_RANGE;
    TS_MIME_FIELD_REFERENCES                = MIME_FIELD_REFERENCES;
    TS_MIME_FIELD_REFERER                   = MIME_FIELD_REFERER;
    TS_MIME_FIELD_REPLY_TO                  = MIME_FIELD_REPLY_TO;
    TS_MIME_FIELD_RETRY_AFTER               = MIME_FIELD_RETRY_AFTER;
    TS_MIME_FIELD_SENDER                    = MIME_FIELD_SENDER;
    TS_MIME_FIELD_SERVER                    = MIME_FIELD_SERVER;
    TS_MIME_FIELD_SET_COOKIE                = MIME_FIELD_SET_COOKIE;
    TS_MIME_FIELD_STRICT_TRANSPORT_SECURITY = MIME_FIELD_STRICT_TRANSPORT_SECURITY;
    TS_MIME_FIELD_SUBJECT                   = MIME_FIELD_SUBJECT;
    TS_MIME_FIELD_SUMMARY                   = MIME_FIELD_SUMMARY;
    TS_MIME_FIELD_TE                        = MIME_FIELD_TE;
    TS_MIME_FIELD_TRANSFER_ENCODING         = MIME_FIELD_TRANSFER_ENCODING;
    TS_MIME_FIELD_UPGRADE                   = MIME_FIELD_UPGRADE;
    TS_MIME_FIELD_USER_AGENT                = MIME_FIELD_USER_AGENT;
    TS_MIME_FIELD_VARY                      = MIME_FIELD_VARY;
    TS_MIME_FIELD_VIA                       = MIME_FIELD_VIA;
    TS_MIME_FIELD_WARNING                   = MIME_FIELD_WARNING;
    TS_MIME_FIELD_WWW_AUTHENTICATE          = MIME_FIELD_WWW_AUTHENTICATE;
    TS_MIME_FIELD_XREF                      = MIME_FIELD_XREF;
    TS_MIME_FIELD_X_FORWARDED_FOR           = MIME_FIELD_X_FORWARDED_FOR;
    TS_MIME_FIELD_FORWARDED                 = MIME_FIELD_FORWARDED;

    TS_MIME_LEN_ACCEPT                    = MIME_LEN_ACCEPT;
    TS_MIME_LEN_ACCEPT_CHARSET            = MIME_LEN_ACCEPT_CHARSET;
    TS_MIME_LEN_ACCEPT_ENCODING           = MIME_LEN_ACCEPT_ENCODING;
    TS_MIME_LEN_ACCEPT_LANGUAGE           = MIME_LEN_ACCEPT_LANGUAGE;
    TS_MIME_LEN_ACCEPT_RANGES             = MIME_LEN_ACCEPT_RANGES;
    TS_MIME_LEN_AGE                       = MIME_LEN_AGE;
    TS_MIME_LEN_ALLOW                     = MIME_LEN_ALLOW;
    TS_MIME_LEN_APPROVED                  = MIME_LEN_APPROVED;
    TS_MIME_LEN_AUTHORIZATION             = MIME_LEN_AUTHORIZATION;
    TS_MIME_LEN_BYTES                     = MIME_LEN_BYTES;
    TS_MIME_LEN_CACHE_CONTROL             = MIME_LEN_CACHE_CONTROL;
    TS_MIME_LEN_CLIENT_IP                 = MIME_LEN_CLIENT_IP;
    TS_MIME_LEN_CONNECTION                = MIME_LEN_CONNECTION;
    TS_MIME_LEN_CONTENT_BASE              = MIME_LEN_CONTENT_BASE;
    TS_MIME_LEN_CONTENT_ENCODING          = MIME_LEN_CONTENT_ENCODING;
    TS_MIME_LEN_CONTENT_LANGUAGE          = MIME_LEN_CONTENT_LANGUAGE;
    TS_MIME_LEN_CONTENT_LENGTH            = MIME_LEN_CONTENT_LENGTH;
    TS_MIME_LEN_CONTENT_LOCATION          = MIME_LEN_CONTENT_LOCATION;
    TS_MIME_LEN_CONTENT_MD5               = MIME_LEN_CONTENT_MD5;
    TS_MIME_LEN_CONTENT_RANGE             = MIME_LEN_CONTENT_RANGE;
    TS_MIME_LEN_CONTENT_TYPE              = MIME_LEN_CONTENT_TYPE;
    TS_MIME_LEN_CONTROL                   = MIME_LEN_CONTROL;
    TS_MIME_LEN_COOKIE                    = MIME_LEN_COOKIE;
    TS_MIME_LEN_DATE                      = MIME_LEN_DATE;
    TS_MIME_LEN_DISTRIBUTION              = MIME_LEN_DISTRIBUTION;
    TS_MIME_LEN_ETAG                      = MIME_LEN_ETAG;
    TS_MIME_LEN_EXPECT                    = MIME_LEN_EXPECT;
    TS_MIME_LEN_EXPIRES                   = MIME_LEN_EXPIRES;
    TS_MIME_LEN_FOLLOWUP_TO               = MIME_LEN_FOLLOWUP_TO;
    TS_MIME_LEN_FROM                      = MIME_LEN_FROM;
    TS_MIME_LEN_HOST                      = MIME_LEN_HOST;
    TS_MIME_LEN_IF_MATCH                  = MIME_LEN_IF_MATCH;
    TS_MIME_LEN_IF_MODIFIED_SINCE         = MIME_LEN_IF_MODIFIED_SINCE;
    TS_MIME_LEN_IF_NONE_MATCH             = MIME_LEN_IF_NONE_MATCH;
    TS_MIME_LEN_IF_RANGE                  = MIME_LEN_IF_RANGE;
    TS_MIME_LEN_IF_UNMODIFIED_SINCE       = MIME_LEN_IF_UNMODIFIED_SINCE;
    TS_MIME_LEN_KEEP_ALIVE                = MIME_LEN_KEEP_ALIVE;
    TS_MIME_LEN_KEYWORDS                  = MIME_LEN_KEYWORDS;
    TS_MIME_LEN_LAST_MODIFIED             = MIME_LEN_LAST_MODIFIED;
    TS_MIME_LEN_LINES                     = MIME_LEN_LINES;
    TS_MIME_LEN_LOCATION                  = MIME_LEN_LOCATION;
    TS_MIME_LEN_MAX_FORWARDS              = MIME_LEN_MAX_FORWARDS;
    TS_MIME_LEN_MESSAGE_ID                = MIME_LEN_MESSAGE_ID;
    TS_MIME_LEN_NEWSGROUPS                = MIME_LEN_NEWSGROUPS;
    TS_MIME_LEN_ORGANIZATION              = MIME_LEN_ORGANIZATION;
    TS_MIME_LEN_PATH                      = MIME_LEN_PATH;
    TS_MIME_LEN_PRAGMA                    = MIME_LEN_PRAGMA;
    TS_MIME_LEN_PROXY_AUTHENTICATE        = MIME_LEN_PROXY_AUTHENTICATE;
    TS_MIME_LEN_PROXY_AUTHORIZATION       = MIME_LEN_PROXY_AUTHORIZATION;
    TS_MIME_LEN_PROXY_CONNECTION          = MIME_LEN_PROXY_CONNECTION;
    TS_MIME_LEN_PUBLIC                    = MIME_LEN_PUBLIC;
    TS_MIME_LEN_RANGE                     = MIME_LEN_RANGE;
    TS_MIME_LEN_REFERENCES                = MIME_LEN_REFERENCES;
    TS_MIME_LEN_REFERER                   = MIME_LEN_REFERER;
    TS_MIME_LEN_REPLY_TO                  = MIME_LEN_REPLY_TO;
    TS_MIME_LEN_RETRY_AFTER               = MIME_LEN_RETRY_AFTER;
    TS_MIME_LEN_SENDER                    = MIME_LEN_SENDER;
    TS_MIME_LEN_SERVER                    = MIME_LEN_SERVER;
    TS_MIME_LEN_SET_COOKIE                = MIME_LEN_SET_COOKIE;
    TS_MIME_LEN_STRICT_TRANSPORT_SECURITY = MIME_LEN_STRICT_TRANSPORT_SECURITY;
    TS_MIME_LEN_SUBJECT                   = MIME_LEN_SUBJECT;
    TS_MIME_LEN_SUMMARY                   = MIME_LEN_SUMMARY;
    TS_MIME_LEN_TE                        = MIME_LEN_TE;
    TS_MIME_LEN_TRANSFER_ENCODING         = MIME_LEN_TRANSFER_ENCODING;
    TS_MIME_LEN_UPGRADE                   = MIME_LEN_UPGRADE;
    TS_MIME_LEN_USER_AGENT                = MIME_LEN_USER_AGENT;
    TS_MIME_LEN_VARY                      = MIME_LEN_VARY;
    TS_MIME_LEN_VIA                       = MIME_LEN_VIA;
    TS_MIME_LEN_WARNING                   = MIME_LEN_WARNING;
    TS_MIME_LEN_WWW_AUTHENTICATE          = MIME_LEN_WWW_AUTHENTICATE;
    TS_MIME_LEN_XREF                      = MIME_LEN_XREF;
    TS_MIME_LEN_X_FORWARDED_FOR           = MIME_LEN_X_FORWARDED_FOR;
    TS_MIME_LEN_FORWARDED                 = MIME_LEN_FORWARDED;

    /* HTTP methods */
    TS_HTTP_METHOD_CONNECT = HTTP_METHOD_CONNECT;
    TS_HTTP_METHOD_DELETE  = HTTP_METHOD_DELETE;
    TS_HTTP_METHOD_GET     = HTTP_METHOD_GET;
    TS_HTTP_METHOD_HEAD    = HTTP_METHOD_HEAD;
    TS_HTTP_METHOD_OPTIONS = HTTP_METHOD_OPTIONS;
    TS_HTTP_METHOD_POST    = HTTP_METHOD_POST;
    TS_HTTP_METHOD_PURGE   = HTTP_METHOD_PURGE;
    TS_HTTP_METHOD_PUT     = HTTP_METHOD_PUT;
    TS_HTTP_METHOD_TRACE   = HTTP_METHOD_TRACE;
    TS_HTTP_METHOD_PUSH    = HTTP_METHOD_PUSH;

    TS_HTTP_LEN_CONNECT = HTTP_LEN_CONNECT;
    TS_HTTP_LEN_DELETE  = HTTP_LEN_DELETE;
    TS_HTTP_LEN_GET     = HTTP_LEN_GET;
    TS_HTTP_LEN_HEAD    = HTTP_LEN_HEAD;
    TS_HTTP_LEN_OPTIONS = HTTP_LEN_OPTIONS;
    TS_HTTP_LEN_POST    = HTTP_LEN_POST;
    TS_HTTP_LEN_PURGE   = HTTP_LEN_PURGE;
    TS_HTTP_LEN_PUT     = HTTP_LEN_PUT;
    TS_HTTP_LEN_TRACE   = HTTP_LEN_TRACE;
    TS_HTTP_LEN_PUSH    = HTTP_LEN_PUSH;

    /* HTTP miscellaneous values */
    TS_HTTP_VALUE_BYTES            = HTTP_VALUE_BYTES;
    TS_HTTP_VALUE_CHUNKED          = HTTP_VALUE_CHUNKED;
    TS_HTTP_VALUE_CLOSE            = HTTP_VALUE_CLOSE;
    TS_HTTP_VALUE_COMPRESS         = HTTP_VALUE_COMPRESS;
    TS_HTTP_VALUE_DEFLATE          = HTTP_VALUE_DEFLATE;
    TS_HTTP_VALUE_GZIP             = HTTP_VALUE_GZIP;
    TS_HTTP_VALUE_BROTLI           = HTTP_VALUE_BROTLI;
    TS_HTTP_VALUE_IDENTITY         = HTTP_VALUE_IDENTITY;
    TS_HTTP_VALUE_KEEP_ALIVE       = HTTP_VALUE_KEEP_ALIVE;
    TS_HTTP_VALUE_MAX_AGE          = HTTP_VALUE_MAX_AGE;
    TS_HTTP_VALUE_MAX_STALE        = HTTP_VALUE_MAX_STALE;
    TS_HTTP_VALUE_MIN_FRESH        = HTTP_VALUE_MIN_FRESH;
    TS_HTTP_VALUE_MUST_REVALIDATE  = HTTP_VALUE_MUST_REVALIDATE;
    TS_HTTP_VALUE_NONE             = HTTP_VALUE_NONE;
    TS_HTTP_VALUE_NO_CACHE         = HTTP_VALUE_NO_CACHE;
    TS_HTTP_VALUE_NO_STORE         = HTTP_VALUE_NO_STORE;
    TS_HTTP_VALUE_NO_TRANSFORM     = HTTP_VALUE_NO_TRANSFORM;
    TS_HTTP_VALUE_ONLY_IF_CACHED   = HTTP_VALUE_ONLY_IF_CACHED;
    TS_HTTP_VALUE_PRIVATE          = HTTP_VALUE_PRIVATE;
    TS_HTTP_VALUE_PROXY_REVALIDATE = HTTP_VALUE_PROXY_REVALIDATE;
    TS_HTTP_VALUE_PUBLIC           = HTTP_VALUE_PUBLIC;
    TS_HTTP_VALUE_S_MAXAGE         = HTTP_VALUE_S_MAXAGE;

    TS_HTTP_LEN_BYTES            = HTTP_LEN_BYTES;
    TS_HTTP_LEN_CHUNKED          = HTTP_LEN_CHUNKED;
    TS_HTTP_LEN_CLOSE            = HTTP_LEN_CLOSE;
    TS_HTTP_LEN_COMPRESS         = HTTP_LEN_COMPRESS;
    TS_HTTP_LEN_DEFLATE          = HTTP_LEN_DEFLATE;
    TS_HTTP_LEN_GZIP             = HTTP_LEN_GZIP;
    TS_HTTP_LEN_BROTLI           = HTTP_LEN_BROTLI;
    TS_HTTP_LEN_IDENTITY         = HTTP_LEN_IDENTITY;
    TS_HTTP_LEN_KEEP_ALIVE       = HTTP_LEN_KEEP_ALIVE;
    TS_HTTP_LEN_MAX_AGE          = HTTP_LEN_MAX_AGE;
    TS_HTTP_LEN_MAX_STALE        = HTTP_LEN_MAX_STALE;
    TS_HTTP_LEN_MIN_FRESH        = HTTP_LEN_MIN_FRESH;
    TS_HTTP_LEN_MUST_REVALIDATE  = HTTP_LEN_MUST_REVALIDATE;
    TS_HTTP_LEN_NONE             = HTTP_LEN_NONE;
    TS_HTTP_LEN_NO_CACHE         = HTTP_LEN_NO_CACHE;
    TS_HTTP_LEN_NO_STORE         = HTTP_LEN_NO_STORE;
    TS_HTTP_LEN_NO_TRANSFORM     = HTTP_LEN_NO_TRANSFORM;
    TS_HTTP_LEN_ONLY_IF_CACHED   = HTTP_LEN_ONLY_IF_CACHED;
    TS_HTTP_LEN_PRIVATE          = HTTP_LEN_PRIVATE;
    TS_HTTP_LEN_PROXY_REVALIDATE = HTTP_LEN_PROXY_REVALIDATE;
    TS_HTTP_LEN_PUBLIC           = HTTP_LEN_PUBLIC;
    TS_HTTP_LEN_S_MAXAGE         = HTTP_LEN_S_MAXAGE;

    init_global_http_hooks();
    init_global_lifecycle_hooks();
    global_config_cbs = new ConfigUpdateCbTable;

    // Setup the version string for returning to plugins
    ink_strlcpy(traffic_server_version, AppVersionInfo::get_version().version(), sizeof(traffic_server_version));
    // Extract the elements.
    // coverity[secure_coding]
    if (sscanf(traffic_server_version, "%d.%d.%d", &ts_major_version, &ts_minor_version, &ts_patch_version) != 3) {
      Warning("Unable to parse traffic server version string '%s'\n", traffic_server_version);
    }
  }
}
