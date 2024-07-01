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

#include <cstring>
#include <iterator>
#include <string>

#include "ts/ts.h"

#include "cripts/Transaction.hpp"
#include "cripts/Lulu.hpp"

class Header
{
  using self_type = Header;

public:
  class Status
  {
    using self_type = Status;

  public:
    Status() = delete;
    Status(Header *owner) : _owner(owner) {}

    operator integer(); // This should not be explicit, nor const
    self_type &operator=(int status);

  private:
    Header      *_owner  = nullptr;
    TSHttpStatus _status = TS_HTTP_STATUS_NONE;

  }; // End class Header::Status

  class Reason
  {
    using self_type = Reason;

  public:
    Reason() = delete;
    Reason(Header *owner) : _owner(owner) {}

    self_type &operator=(Cript::string_view reason);

  private:
    Header *_owner = nullptr;
  }; // End class Header::Reason

  class Body
  {
    using self_type = Body;

  public:
    Body() = delete;
    Body(Header *owner) : _owner(owner) {}

    self_type &operator=(Cript::string_view body);

  private:
    Header *_owner = nullptr;
  }; // End class Header::Body

  class Method
  {
    using self_type = Method;

  public:
    Method() = delete;
    Method(Cript::string_view const &method) : _method(method) {}
    Method(const char *const method, int len)
    {
      _method = Cript::string_view(method, static_cast<Cript::string_view::size_type>(len));
    }

    Method(Header *owner) : _owner(owner) {}

    Cript::string_view GetSV();

    operator Cript::string_view() { return GetSV(); }

    // ToDo: This is a bit weird, but seems needed (for now) to allow for the
    // Header::Method::* constants.
    [[nodiscard]] Cript::string_view::const_pointer
    Data() const
    {
      CAssert(_method.size() > 0);
      return _method.data();
    }

    Cript::string_view::const_pointer
    Data()
    {
      return GetSV().data();
    }

    Cript::string_view::size_type
    Size()
    {
      return GetSV().size();
    }

    Cript::string_view::size_type
    Length()
    {
      return GetSV().size();
    }

    bool
    operator==(Method const &rhs)
    {
      return GetSV().data() == rhs.Data();
    }

    bool
    operator!=(Method const &rhs)
    {
      return GetSV().data() != rhs.Data();
    }

  private:
    Header            *_owner = nullptr;
    Cript::string_view _method;

  }; // End class Header::Method

  class CacheStatus
  {
    using self_type = CacheStatus;

  public:
    CacheStatus() = delete;
    CacheStatus(Header *owner) : _owner(owner) {}

    Cript::string_view GetSV();

    operator Cript::string_view() { return GetSV(); }

    Cript::string_view::const_pointer
    Data()
    {
      return GetSV().data();
    }

    Cript::string_view::size_type
    Size()
    {
      return GetSV().size();
    }

    Cript::string_view::size_type
    Length()
    {
      return GetSV().size();
    }

  private:
    Header            *_owner = nullptr;
    Cript::string_view _cache;

  }; // Class Header::CacheStatus

  class String : public Cript::StringViewMixin<String>
  {
    using super_type = Cript::StringViewMixin<String>;
    using self_type  = String;

  public:
    ~String()
    {
      if (_field_loc) {
        TSHandleMLocRelease(_owner->_bufp, _owner->_hdr_loc, _field_loc);
        _field_loc = nullptr;
      }
    }

  public:
    // Implemented in Headers.cc, they are pretty large
    self_type &operator=(const Cript::string_view str) override;
    self_type &operator=(integer val);
    self_type &operator+=(const Cript::string_view str);

    // These specialized assignment operators all use the above
    template <size_t N>
    self_type &
    operator=(const char (&str)[N])
    {
      return operator=(Cript::string_view(str, str[N - 1] ? N : N - 1));
    }

    self_type &
    operator=(char *&str)
    {
      return operator=(Cript::string_view(str, strlen(str)));
    }

    self_type &
    operator=(char const *&str)
    {
      return operator=(Cript::string_view(str, strlen(str)));
    }

    self_type &
    operator=(const std::string &str)
    {
      return operator=(Cript::string_view(str));
    }

  private:
    friend class Header;

    void
    _initialize(Cript::string_view name, Cript::string_view value, Header *owner, TSMLoc field_loc)
    {
      _setSV(value);
      _name      = name;
      _owner     = owner;
      _field_loc = field_loc;
    }

    Header            *_owner     = nullptr;
    TSMLoc             _field_loc = nullptr;
    Cript::string_view _name;

  }; // Class Header::String

  class Name : public Cript::StringViewMixin<Name>
  {
    using super_type = Cript::StringViewMixin<Name>;
    using self_type  = Name;

  public:
    operator Cript::string_view() const { return GetSV(); }

    self_type &
    operator=(const Cript::string_view str) override
    {
      _setSV(str);

      return *this;
    }

    using super_type::StringViewMixin;

  }; // Class Header::Name

public:
  class Iterator
  {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = Name;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Name *;
    using reference         = const Name &;

    const static uint32_t END_TAG = UINT32_MAX;

    Iterator(Name view, uint32_t tag) : _view(std::move(view)), _tag(tag) {} // Special for the end iterator
    Iterator(Name view, uint32_t tag, Header *owner) : _view(std::move(view)), _tag(tag), _owner(owner) {}

    reference
    operator*() const
    {
      return _view;
    }

    pointer
    operator->()
    {
      return &_view;
    }

    // Prefix increment
    Iterator &
    operator++()
    {
      if (_tag != END_TAG) {
        CAssert(_tag == _owner->_iterator_tag);

        _view = _owner->iterate();
        if (_view.empty()) {
          _tag = END_TAG;
        }
      }

      return *this;
    }

    friend bool
    operator==(const Iterator &a, const Iterator &b)
    {
      return a._tag == b._tag;
    };

    friend bool
    operator!=(const Iterator &a, const Iterator &b)
    {
      return a._tag != b._tag;
    };

    static const Iterator
    end()
    {
      return _end;
    }

  private:
    Name     _view  = nullptr;
    uint32_t _tag   = 0;
    Header  *_owner = nullptr;

    static const Iterator _end;
  }; // Class Header::iterator

  Header() : status(this), reason(this), body(this), cache(this) {}

  ~Header() { Reset(); }

  // Clear anything "cached" in the Url, this is rather draconian, but it's
  // safe...
  void
  Reset()
  {
    if (_bufp && _hdr_loc) {
      TSHandleMLocRelease(_bufp, TS_NULL_MLOC, _hdr_loc);
      _hdr_loc = nullptr;
      _bufp    = nullptr;
    }
    _state = nullptr;
  }

  [[nodiscard]] TSMBuffer
  BufP() const
  {
    return _bufp;
  }

  [[nodiscard]] TSMLoc
  MLoc() const
  {
    return _hdr_loc;
  }

  String operator[](const Cript::string_view str);

  [[nodiscard]] bool
  Initialized() const
  {
    return (_state != nullptr);
  }

  void
  Erase(const Cript::string_view header)
  {
    operator[](header) = "";
  }

  Iterator           begin();
  Cript::string_view iterate(); // This is a little helper for the iterators

  [[nodiscard]] Iterator
  end() const
  {
    return Iterator::end(); // Static end iterator. ToDo: Does this have any value over making a new one always?
  }

  Status      status;
  Reason      reason;
  Body        body;
  CacheStatus cache;

protected:
  void
  _initialize(Cript::Transaction *state)
  {
    _state = state;
  }

  TSMBuffer           _bufp         = nullptr;
  TSMLoc              _hdr_loc      = nullptr;
  Cript::Transaction *_state        = nullptr; // Pointer into the owning Context's State
  TSMLoc              _iterator_loc = nullptr;
  uint32_t            _iterator_tag = 0; // This is used to assure that we don't have more than one active iterator on a header

}; // End class Header

class RequestHeader : public Header
{
  using super_type = Header;
  using self_type  = RequestHeader;

public:
  RequestHeader() : method(this) {} // Special case since only this header has a method

  Method method;
}; // End class RequestHeader

class ResponseHeader : public Header
{
private:
  using super_type = Header;
  using self_type  = ResponseHeader;

public:
  ResponseHeader() = default;

}; // End class ResponseHeader

namespace Client
{
class URL;
class Request : public RequestHeader
{
  using super_type = RequestHeader;
  using self_type  = Request;

public:
  Request()                         = default;
  Request(const self_type &)        = delete;
  void operator=(const self_type &) = delete;

  // Implemented later, because needs the context.
  static self_type &_get(Cript::Context *context);

}; // End class Client::Request

class Response : public ResponseHeader
{
  using super_type = ResponseHeader;
  using self_type  = Response;

public:
  Response()                        = default;
  Response(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  // Implemented later, because needs the context.
  static self_type &_get(Cript::Context *context);

}; // End class Client::Response

} // namespace Client

namespace Server
{
class Request : public RequestHeader
{
  using super_type = RequestHeader;
  using self_type  = Request;

public:
  Request()                         = default;
  Request(const self_type &)        = delete;
  void operator=(const self_type &) = delete;

  // Implemented later, because needs the context.
  static Request &_get(Cript::Context *context);
}; // End class Server::Request

class Response : public ResponseHeader
{
  using super_type = ResponseHeader;
  using self_type  = Response;

public:
  Response()                        = default;
  Response(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  // Implemented later, because needs the context.
  static self_type &_get(Cript::Context *context);

}; // End class Server::Response

} // namespace Server

// Some static methods for the Method class
namespace Cript
{
namespace Method
{
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro
  extern const Header::Method GET;
  extern const Header::Method HEAD;
  extern const Header::Method POST;
  extern const Header::Method PUT;
  extern const Header::Method PUSH;
  extern const Header::Method DELETE;
  extern const Header::Method OPTIONS;
  extern const Header::Method CONNECT;
  extern const Header::Method TRACE;
  // This is a special feature of ATS
  extern const Header::Method PURGE;
} // namespace Method

class Context;
} // namespace Cript

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Header::Method> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Header::Method &method, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", method.GetSV());
  }
};

template <> struct formatter<Header::String> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Header::String &str, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", str.GetSV());
  }
};

template <> struct formatter<Header::Name> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Header::Name &name, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", name.GetSV());
  }
};

} // namespace fmt
