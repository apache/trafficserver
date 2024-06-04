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

//  Bundle::Headers::activate().rm_headers("Client::Request", ["X-ATS-Request-ID", "X-ATS-Request-Start", "X-ATS-Request-End"])
//                             .set_headers("Client::Response", {{"X-Foo", "bar", {"X-Fie", "fum"}});
#pragma once

#include <vector>

#include "cripts/Bundle.hpp"

namespace detail
{
class HRWBridge
{
  using self_type = HRWBridge;

public:
  HRWBridge(const self_type &)      = delete;
  void operator=(const self_type &) = delete;

  HRWBridge() = delete;
  HRWBridge(const Cript::string_view &str) : _value(str) {}

  virtual ~HRWBridge() = default;

  virtual Cript::string_view
  value(Cript::Context *context)
  {
    return _value;
  }

protected:
  Cript::string _value;

}; // class HRWBridge

} // namespace detail

namespace
{
// We support 4 different type of header operations, so isolate the common code
class HeadersType
{
  using self_type = HeadersType;

public:
  HeadersType()                     = default;
  HeadersType(const self_type &)    = delete;
  void operator=(const self_type &) = delete;

  ~HeadersType()
  {
    for (auto &header : set_headers) {
      delete header.second;
    }
  }

  std::vector<Cript::string>                                 rm_headers;
  std::vector<std::pair<Cript::string, detail::HRWBridge *>> set_headers;
};
} // namespace

namespace Bundle
{
class Headers : public Cript::Bundle::Base
{
  using super_type = Cript::Bundle::Base;
  using self_type  = Headers;

public:
  using super_type::Base;

  bool validate(std::vector<Cript::Bundle::Error> &errors) const override;

  // This is the factory to create an instance of this bundle
  static self_type &
  _activate(Cript::Instance &inst)
  {
    auto *entry = new self_type();

    inst.addBundle(entry);

    return *entry;
  }

  const Cript::string &
  name() const override
  {
    return _name;
  }

  static detail::HRWBridge *bridgeFactory(const Cript::string &source);

  self_type &rm_headers(const Cript::string_view target, const std::vector<Cript::string> &headers);
  self_type &set_headers(const Cript::string_view target, const std::vector<std::pair<Cript::string, Cript::string>> &headers);

  void doRemap(Cript::Context *context) override;
  void doSendResponse(Cript::Context *context) override;
  void doSendRequest(Cript::Context *context) override;
  void doReadResponse(Cript::Context *context) override;

private:
  static const Cript::string _name;

  HeadersType _client_request;
  HeadersType _client_response;
  HeadersType _server_request;
  HeadersType _server_response;
};

} // namespace Bundle
