/** @file
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

#include "response.h"

#include <cinttypes>
#include <cstring>
#include <mutex>

#include "ts/ts.h"

// canned body string for a 416, stolen from nginx
std::string const &
bodyString416()
{
  static std::string bodystr;
  static std::mutex mutex;
  std::lock_guard<std::mutex> const guard(mutex);

  if (bodystr.empty()) {
    bodystr.append("<html>\n");
    bodystr.append("<head><title>416 Requested Range Not Satisfiable</title></head>\n");
    bodystr.append("<body bgcolor=\"white\">\n");
    bodystr.append("<center><h1>416 Requested Range Not Satisfiable</h1></center>");
    bodystr.append("<hr><center>ATS/");
    bodystr.append(TS_VERSION_STRING);
    bodystr.append("</center>\n");
    bodystr.append("</body>\n");
    bodystr.append("</html>\n");
  }

  return bodystr;
}

// Form a 502 response, preliminary
std::string const &
string502()
{
  static std::string msg;
  static std::mutex mutex;
  std::lock_guard<std::mutex> const guard(mutex);

  if (msg.empty()) {
    std::string bodystr;
    bodystr.append("<html>\n");
    bodystr.append("<head><title>502 Bad Gateway</title></head>\n");
    bodystr.append("<body bgcolor=\"white\">\n");
    bodystr.append("<center><h1>502 Bad Gateway: Missing/Malformed "
                   "Content-Range</h1></center>");
    bodystr.append("<hr><center>ATS/");
    bodystr.append(TS_VERSION_STRING);
    bodystr.append("</center>\n");
    bodystr.append("</body>\n");
    bodystr.append("</html>\n");

    static int const CLEN = 1024;
    char clenstr[CLEN];
    int const clen = snprintf(clenstr, CLEN, "%lu", bodystr.size());

    msg.append("HTTP/1.1 502 Bad Gateway\r\n");
    msg.append("Content-Length: ");
    msg.append(clenstr, clen);
    msg.append("\r\n");

    msg.append("\r\n");
    msg.append(bodystr);
  }

  return msg;
}

void
form416HeaderAndBody(HttpHeader &header, int64_t const contentlen, std::string const &bodystr)
{
  header.removeKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED);
  header.removeKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG);
  header.removeKey(TS_MIME_FIELD_ACCEPT_RANGES, TS_MIME_LEN_ACCEPT_RANGES);

  header.setStatus(TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE);
  char const *const reason = TSHttpHdrReasonLookup(TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE);
  header.setReason(reason, strlen(reason));

  char bufstr[256];
  int buflen = snprintf
    //    (bufstr, 255, "%" PRId64, bodystr.size());
    (bufstr, 255, "%lu", bodystr.size());
  header.setKeyVal(TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, bufstr, buflen);

  static char const *const ctypestr = "text/html";
  static int const ctypelen         = strlen(ctypestr);
  header.setKeyVal(TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE, ctypestr, ctypelen);

  buflen = snprintf(bufstr, 255, "*/%" PRId64, contentlen);
  header.setKeyVal(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE, bufstr, buflen);
}
