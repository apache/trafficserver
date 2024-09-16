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
#include "cripts/Preamble.hpp"
#include "cripts/Bundles/Headers.hpp"

#include <cctype>

namespace detail
{

/////////////////////////////////////////////////////////////////////////////
// Bridge for the ID class
class ID : public detail::HRWBridge
{
  using self_type  = ID;
  using super_type = detail::HRWBridge;

  enum class Type : uint8_t { none, REQUEST, PROCESS, UNIQUE };

public:
  ID(const self_type &)             = delete;
  void operator=(const self_type &) = delete;

  ID(const cripts::string_view &id);
  ~ID() override = default;

  cripts::string_view value(cripts::Context *context) override;

private:
  Type _type = Type::none;
};

ID::ID(const cripts::string_view &id) : super_type(id)
{
  if (id == "REQUEST") {
    _type = Type::REQUEST;
  } else if (id == "PROCESS") {
    _type = Type::PROCESS;
  } else if (id == "UNIQUE") {
    _type = Type::UNIQUE;
  } else {
    CFatal("[Cripts::Headers] Unknown HRWBridge ID type: %s.", id.data());
  }
}

cripts::string_view
ID::value(cripts::Context *context)
{
  switch (_type) {
  case Type::REQUEST:
    _value = cripts::UUID::Request::_get(context);
    break;
  case Type::PROCESS:
    _value = cripts::UUID::Process::_get(context);
    break;
  case Type::UNIQUE:
    _value = cripts::UUID::Unique::_get(context);
    break;
  default:
    _value = "";
    break;
  }

  return _value;
}

/////////////////////////////////////////////////////////////////////////////
// Bridge for the IP class
class IP : public detail::HRWBridge
{
  using self_type  = IP;
  using super_type = detail::HRWBridge;

  enum class Type : uint8_t { none, CLIENT, INBOUND, SERVER, OUTBOUND };

public:
  IP(const self_type &)             = delete;
  void operator=(const self_type &) = delete;

  IP(const cripts::string_view &ip);
  ~IP() override = default;

  cripts::string_view value(cripts::Context *context) override;

private:
  Type _type = Type::none;
};

IP::IP(const cripts::string_view &type) : super_type(type)
{
  if (type == "CLIENT") {
    _type = Type::CLIENT;
  } else if (type == "INBOUND") {
    _type = Type::INBOUND;
  } else if (type == "SERVER") {
    _type = Type::SERVER;
  } else if (type == "OUTBOUND") {
    _type = Type::INBOUND;
  } else {
    CFatal("[Cripts::Headers] Unknown HRWBridge IP type: %s.", type.data());
  }
}

cripts::string_view
IP::value(cripts::Context *context)
{
  switch (_type) {
  case Type::CLIENT: {
    auto ip = cripts::Client::Connection::Get().IP();
    _value  = ip.string();
  } break;
  case Type::INBOUND: {
    auto ip = cripts::Client::Connection::Get().LocalIP();
    _value  = ip.string();
  } break;
  case Type::SERVER: {
    auto ip = cripts::Server::Connection::Get().IP();
    _value  = ip.string();
  } break;
  case Type::OUTBOUND: {
    auto ip = cripts::Server::Connection::Get().LocalIP();
    _value  = ip.string();
  } break;
  default:
    _value = "";
    break;
  }

  return _value;
}

/////////////////////////////////////////////////////////////////////////////
// Bridge for the CIDR class, this only deals with the client IP
class CIDR : public detail::HRWBridge
{
  using self_type  = CIDR;
  using super_type = detail::HRWBridge;

public:
  CIDR(const self_type &)           = delete;
  void operator=(const self_type &) = delete;

  CIDR(cripts::string_view &cidr);
  ~CIDR() override = default;

  cripts::string_view value(cripts::Context *context) override;

private:
  unsigned int _ipv4_cidr = 32;
  unsigned int _ipv6_cidr = 128;
};

CIDR::CIDR(cripts::string_view &cidr) : super_type(cidr)
{
  auto ipv4 = cidr.split_prefix_at(',');

  CAssert(ipv4 != cidr); // No ' found

  auto result = std::from_chars(ipv4.data(), ipv4.data() + ipv4.size(), _ipv4_cidr);

  if (result.ec != std::errc()) {
    CFatal("[Cripts::Headers] Invalid IPv4 CIDR parameters: %s.", cidr.data());
  }

  result = std::from_chars(cidr.data(), cidr.data() + cidr.size(), _ipv6_cidr);
  if (result.ec != std::errc()) {
    CFatal("[Cripts::Headers] Invalid IPv6 CIDR parameters: %s.", cidr.data());
  }
}

cripts::string_view
CIDR::value(cripts::Context *context)
{
  auto ip = cripts::Client::Connection::Get().IP();

  _value = ip.string(_ipv4_cidr, _ipv6_cidr);

  return _value;
}

/////////////////////////////////////////////////////////////////////////////
// Bridge for all URLs
class URL : public detail::HRWBridge
{
  using self_type  = URL;
  using super_type = detail::HRWBridge;

  enum class Component : uint8_t { none, HOST, PATH, PORT, QUERY, SCHEME, URL };

public:
  enum class Type : uint8_t { none, CLIENT, REMAP_FROM, REMAP_TO, PRISTINE, CACHE, PARENT };

  URL(const self_type &)           = delete;
  URL operator=(const self_type &) = delete;

  URL(const cripts::string_view &) = delete;
  URL(Type utype, const cripts::string_view &comp);
  ~URL() override = default;

  cripts::string_view value(cripts::Context *context) override;

private:
  cripts::string_view _getComponent(cripts::Url &url);

  Type      _type = Type::none;
  Component _comp = Component::none;
};

cripts::string_view
URL::_getComponent(cripts::Url &url)
{
  switch (_comp) {
  case Component::HOST:
    return url.host.GetSV();
    break;

  case Component::PATH:
    return url.path;
    break;

  case Component::PORT:
    _value = cripts::string(std::to_string(url.port));
    break;

  case Component::QUERY:
    return url.query;
    break;

  case Component::SCHEME:
    return url.scheme;
    break;

  case Component::URL:
    return "";
    // return url.url;
    break;

  default:
    CFatal("[Cripts::Headers] Invalid URL component in HRWBridge.");
    break;
  }

  return ""; // Should never happen
}

URL::URL(Type utype, const cripts::string_view &comp) : super_type("")
{
  _type = utype;

  if (comp == "HOST") {
    _comp = Component::HOST;
  } else if (comp == "PATH") {
    _comp = Component::PATH;
  } else if (comp == "PORT") {
    _comp = Component::PORT;
  } else if (comp == "QUERY") {
    _comp = Component::QUERY;
  } else if (comp == "SCHEME") {
    _comp = Component::SCHEME;
  } else if (comp == "URL") {
    _comp = Component::URL;
  } else {
    CFatal("[Cripts::Headers] Invalid URL component in HRWBridge.");
  }
}

cripts::string_view
URL::value(cripts::Context *context)
{
  switch (_type) {
  case Type::CLIENT: {
    borrow url = cripts::Client::URL::Get();

    return _getComponent(url);
  } break;

  case Type::REMAP_FROM: {
    borrow url = cripts::Remap::From::URL::Get();

    return _getComponent(url);
  } break;

  case Type::REMAP_TO: {
    borrow url = cripts::Remap::To::URL::Get();

    return _getComponent(url);
  } break;

  case Type::PRISTINE: {
    borrow url = cripts::Pristine::URL::Get();

    return _getComponent(url);
  } break;

  case Type::CACHE: {
    borrow url = cripts::Cache::URL::Get();

    return _getComponent(url);
  } break;

  case Type::PARENT: {
    borrow url = cripts::Parent::URL::Get();

    return _getComponent(url);
  } break;

  default:
    CFatal("[Cripts::Headers] Invalid URL type in HRWBridge.");
    break;
  }

  return _value;
}

} // namespace detail

detail::HRWBridge *
cripts::Bundle::Headers::BridgeFactory(const cripts::string &source)
{
  cripts::string_view str = source;

  str.trim_if([](char c) { return std::isspace(c) || c == '"' || c == '\''; });

  if (str.starts_with("%{") && str.ends_with("}")) {
    str.remove_prefix_at('{');
    str.remove_suffix_at('}');

    auto key = str.take_prefix_at(':');

    if (key == "ID") {
      return new detail::ID(str);
    } else if (key == "IP") {
      return new detail::IP(str);
    } else if (key == "CIDR") {
      return new detail::CIDR(str);
    } else if (key == "FROM-URL") {
      return new detail::URL(detail::URL::Type::REMAP_FROM, str);
    } else if (key == "TO-URL") {
      return new detail::URL(detail::URL::Type::REMAP_TO, str);
    } else if (key == "CLIENT-URL") {
      return new detail::URL(detail::URL::Type::CLIENT, str);
    } else if (key == "CACHE-URL") {
      return new detail::URL(detail::URL::Type::CACHE, str);
    } else if (key == "PRISTINE-URL") {
      return new detail::URL(detail::URL::Type::PRISTINE, str);
    } else if (key == "NEXT-HOP") {
      return new detail::URL(detail::URL::Type::PARENT, str);
      // ToDo: Need proper support for URL: type here, which is a bit more complex (context sensitive)
    } else {
      TSError("[Cripts::Headers] Unknown HRWBridge key: %s.", key.data());
    }
  }
  // Always return the "raw" string if we don't have something special to do
  return new detail::HRWBridge(source);
}
