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
#pragma once

// Bundle for common header operations
//  Bundle::Headers::activate().rm_headers("Client::Request", ["X-ATS-Request-ID", "X-ATS-Request-Start", "X-ATS-Request-End"])
//                             .set_headers("Client::Response", {{"X-Foo", "bar", {"X-Fie", "fum"}});

#include "cripts/Lulu.hpp"
#include "cripts/Instance.hpp"
#include "cripts/Bundle.hpp"

namespace detail
{
class HRWBridge
{
  using self_type = HRWBridge;

public:
  HRWBridge()                       = delete;
  HRWBridge(const self_type &)      = delete;
  void operator=(const self_type &) = delete;

  HRWBridge(const cripts::string_view &str) : _value(str) {}

  virtual ~HRWBridge() = default;

  virtual cripts::string_view
  value(cripts::Context * /* context ATS_UNUSED */)
  {
    return _value;
  }

protected:
  cripts::string _value;

}; // class HRWBridge

// We support 4 different type of header operations, so isolate the common code
class HeadersType
{
  using self_type = HeadersType;

public:
  using HeaderList      = std::vector<cripts::string>;
  using HeaderValueList = std::vector<std::pair<const cripts::string, detail::HRWBridge *>>;

  HeadersType()                     = default;
  HeadersType(const self_type &)    = delete;
  void operator=(const self_type &) = delete;

  ~HeadersType()
  {
    for (auto &header : set_headers) {
      delete header.second;
    }
  }

  HeaderList      rm_headers;
  HeaderValueList set_headers;
};
} // namespace detail

namespace cripts::Bundle
{
class Headers : public cripts::Bundle::Base
{
  using super_type = cripts::Bundle::Base;
  using self_type  = Headers;

public:
  using HeaderList      = std::vector<cripts::string>;
  using HeaderValueList = std::vector<std::pair<const cripts::string, const cripts::string>>;

  using super_type::Base;

  // This is the factory to create an instance of this bundle
  static self_type &
  _activate(cripts::Instance &inst)
  {
    auto *entry = new self_type();

    inst.AddBundle(entry);

    return *entry;
  }

  [[nodiscard]] const cripts::string &
  Name() const override
  {
    return _name;
  }

  static detail::HRWBridge *BridgeFactory(const cripts::string &source);

  self_type &rm_headers(const cripts::string_view target, const HeaderList &headers);
  self_type &set_headers(const cripts::string_view target, const HeaderValueList &headers);

  void doRemap(cripts::Context *context) override;
  void doSendResponse(cripts::Context *context) override;
  void doSendRequest(cripts::Context *context) override;
  void doReadResponse(cripts::Context *context) override;

private:
  static const cripts::string _name;

  detail::HeadersType _client_request;
  detail::HeadersType _client_response;
  detail::HeadersType _server_request;
  detail::HeadersType _server_response;
};

} // namespace cripts::Bundle
