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

#pragma once

#include <string>

std::string
simClientRequestHeader()
{
  std::string get_request;
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" /voidlinux.iso HTTP/1.1\r\n");
  get_request.append(TS_MIME_FIELD_HOST);
  get_request.append(": localhost:6010\r\n");
  get_request.append(TS_MIME_FIELD_USER_AGENT);
  get_request.append(": ATS/slicer\r\n");
  get_request.append(TS_MIME_FIELD_ACCEPT);
  get_request.append(": */*\r\n");
  get_request.append("\r\n");
  return get_request;
}

std::string
simClientResponseHeader()
{
  std::string const response("HTTP/1.1 200 Partial Content\r\n"
                             "Date: Wed, 13 Jun 2018 22:50:48 GMT\r\n"
                             "Server: biteme/6.6.6\r\n"
                             "Last-Modified: Mon, 07 May 2018 16:07:31 GMT\r\n"
                             "ETag: \"12500000-56b9fddf5ac99\"\r\n"
                             "Accept-Ranges: bytes\r\n"
                             "Content-Length: 10485760000\r\n"
                             "Cache-Control: public, max-age=900, s-maxage=900\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Age: 0\r\n"
                             "Connection: keep-alive\r\n"
                             "\r\n");
  return response;
}

std::string
rangeRequestStringFor(std::string const &bytesstr)
{
  std::string get_request;
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" /voidlinux.iso HTTP/1.1\r\n");
  get_request.append(TS_MIME_FIELD_HOST);
  get_request.append(": localhost:6010\r\n");
  get_request.append(TS_MIME_FIELD_USER_AGENT);
  get_request.append(": ATS/whatever\r\n");
  get_request.append(TS_MIME_FIELD_ACCEPT);
  get_request.append(": */*\r\n");
  get_request.append(TS_MIME_FIELD_RANGE);
  get_request.append(": ");
  get_request.append(bytesstr);
  get_request.append("\r\n");
  get_request.append("X-Skip-Me");
  get_request.append(": absolutely\r\n");
  get_request.append("\r\n");
  return get_request;
}
