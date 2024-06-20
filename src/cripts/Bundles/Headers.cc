/*
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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"
#include "cripts/Bundles/Headers.hpp"

namespace
{
enum HeaderTargets : uint8_t {
  NONE,
  CLIENT_REQUEST,
  CLIENT_RESPONSE,
  SERVER_REQUEST,
  SERVER_RESPONSE,
};

HeaderTargets
header_target(const Cript::string_view &target)
{
  if (target == "Client::Request") {
    return CLIENT_REQUEST;
  } else if (target == "Client::Response") {
    return CLIENT_RESPONSE;
  } else if (target == "Server::Request") {
    return SERVER_REQUEST;
  } else if (target == "Server::Response") {
    return SERVER_RESPONSE;
  }

  return NONE;
}

} // namespace

namespace Bundle
{
const Cript::string Headers::_name = "Bundle::Headers";

Headers::self_type &
Headers::rm_headers(const Cript::string_view target, const HeaderList &headers)
{
  switch (header_target(target)) {
  case CLIENT_REQUEST:
    _client_request.rm_headers.insert(_client_request.rm_headers.end(), headers.begin(), headers.end());
    needCallback(Cript::Callbacks::DO_REMAP);
    break;
  case CLIENT_RESPONSE:
    _client_response.rm_headers.insert(_client_response.rm_headers.end(), headers.begin(), headers.end());
    needCallback(Cript::Callbacks::DO_SEND_RESPONSE);
    break;
  case SERVER_REQUEST:
    _client_response.rm_headers.insert(_client_response.rm_headers.end(), headers.begin(), headers.end());
    needCallback(Cript::Callbacks::DO_SEND_REQUEST);
    break;
  case SERVER_RESPONSE:
    _client_response.rm_headers.insert(_client_response.rm_headers.end(), headers.begin(), headers.end());
    needCallback(Cript::Callbacks::DO_READ_RESPONSE);
    break;
  default:
    TSReleaseAssert(!"Invalid target for rm_headers()");
  }

  return *this;
}

Headers::self_type &
Headers::set_headers(const Cript::string_view target, const HeaderValueList &headers)
{
  detail::HeadersType::HeaderValueList *hdrs = nullptr;

  switch (header_target(target)) {
  case CLIENT_REQUEST:
    hdrs = &_client_request.set_headers;
    needCallback(Cript::Callbacks::DO_REMAP);
    break;
  case CLIENT_RESPONSE:
    hdrs = &_client_response.set_headers;
    needCallback(Cript::Callbacks::DO_SEND_RESPONSE);
    break;
  case SERVER_REQUEST:
    hdrs = &_server_request.set_headers;
    needCallback(Cript::Callbacks::DO_SEND_REQUEST);
    break;
  case SERVER_RESPONSE:
    hdrs = &_server_response.set_headers;
    needCallback(Cript::Callbacks::DO_READ_RESPONSE);
    break;
  default:
    TSReleaseAssert(!"Invalid target for set_headers()");
  }

  hdrs->reserve(headers.size());
  for (const auto &hdr : headers) {
    hdrs->emplace_back(hdr.first, Headers::bridgeFactory(hdr.second));
  }

  return *this;
}

void
Headers::doRemap(Cript::Context *context)
{
  borrow req = Client::Request::get();

  for (auto &header : _client_request.rm_headers) {
    req[header] = "";
  }

  for (auto &header : _client_request.set_headers) {
    req[header.first] = header.second->value(context);
  }
}

void
Headers::doSendResponse(Cript::Context *context)
{
  borrow resp = Client::Response::get();

  for (auto &header : _client_response.rm_headers) {
    resp[header] = "";
  }

  for (auto &header : _client_response.set_headers) {
    resp[header.first] = header.second->value(context);
  }
}

void
Headers::doSendRequest(Cript::Context *context)
{
  borrow req = Server::Request::get();

  for (auto &header : _server_request.rm_headers) {
    req[header] = "";
  }

  for (auto &header : _server_request.set_headers) {
    req[header.first] = header.second->value(context);
  }
}

void
Headers::doReadResponse(Cript::Context *context)
{
  borrow resp = Server::Response::get();

  for (auto &header : _server_response.rm_headers) {
    resp[header] = "";
  }

  for (auto &header : _server_response.set_headers) {
    resp[header.first] = header.second->value(context);
  }
}

} // namespace Bundle
