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

namespace cripts
{

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

  }; // End class cripts::Header::Status

  class Reason
  {
    using self_type = Reason;

  public:
    Reason() = delete;
    Reason(Header *owner) : _owner(owner) {}

    self_type &operator=(cripts::string_view reason);

  private:
    Header *_owner = nullptr;
  }; // End class cripts::Header::Reason

  class Body
  {
    using self_type = Body;

  public:
    Body() = delete;
    Body(Header *owner) : _owner(owner) {}

    self_type &operator=(cripts::string_view body);

  private:
    Header *_owner = nullptr;
  }; // End class cripts::Header::Body

  class Method
  {
    using self_type = Method;

  public:
    Method() = delete;
    Method(cripts::string_view const &method) : _method(method) {}
    Method(const char *const method, int len)
    {
      _method = cripts::string_view(method, static_cast<cripts::string_view::size_type>(len));
    }

    Method(Header *owner) : _owner(owner) {}

    cripts::string_view GetSV();

    operator cripts::string_view() { return GetSV(); }

    // ToDo: This is a bit weird, but seems needed (for now) to allow for the
    // cripts::Header::Method::* constants.
    [[nodiscard]] cripts::string_view::const_pointer
    Data() const
    {
      CAssert(_method.size() > 0);
      return _method.data();
    }

    cripts::string_view::const_pointer
    Data()
    {
      return GetSV().data();
    }

    cripts::string_view::size_type
    Size()
    {
      return GetSV().size();
    }

    cripts::string_view::size_type
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
    Header             *_owner = nullptr;
    cripts::string_view _method;

  }; // End class cripts::Header::Method

  class CacheStatus
  {
    using self_type = CacheStatus;

  public:
    CacheStatus() = delete;
    CacheStatus(Header *owner) : _owner(owner) {}

    cripts::string_view GetSV();

    operator cripts::string_view() { return GetSV(); }

    cripts::string_view::const_pointer
    Data()
    {
      return GetSV().data();
    }

    cripts::string_view::size_type
    Size()
    {
      return GetSV().size();
    }

    cripts::string_view::size_type
    Length()
    {
      return GetSV().size();
    }

  private:
    Header             *_owner = nullptr;
    cripts::string_view _cache;

  }; // Class cripts::Header::CacheStatus

  class String : public cripts::StringViewMixin<String>
  {
    using super_type = cripts::StringViewMixin<String>;
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
    self_type &operator=(const cripts::string_view str) override;
    self_type &operator=(integer val);
    self_type &operator+=(const cripts::string_view str);

    // These specialized assignment operators all use the above
    template <size_t N>
    self_type &
    operator=(const char (&str)[N])
    {
      return operator=(cripts::string_view(str, str[N - 1] ? N : N - 1));
    }

    self_type &
    operator=(char *&str)
    {
      return operator=(cripts::string_view(str, strlen(str)));
    }

    self_type &
    operator=(char const *&str)
    {
      return operator=(cripts::string_view(str, strlen(str)));
    }

    self_type &
    operator=(const std::string &str)
    {
      return operator=(cripts::string_view(str));
    }

  private:
    friend class Header;

    void
    _initialize(cripts::string_view name, cripts::string_view value, Header *owner, TSMLoc field_loc)
    {
      _setSV(value);
      _name      = name;
      _owner     = owner;
      _field_loc = field_loc;
    }

    Header             *_owner     = nullptr;
    TSMLoc              _field_loc = nullptr;
    cripts::string_view _name;

  }; // Class cripts::Header::String

  class Name : public cripts::StringViewMixin<Name>
  {
    using super_type = cripts::StringViewMixin<Name>;
    using self_type  = Name;

  public:
    operator cripts::string_view() const { return GetSV(); }

    self_type &
    operator=(const cripts::string_view str) override
    {
      _setSV(str);

      return *this;
    }

    using super_type::StringViewMixin;

  }; // Class cripts::Header::Name

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
  }; // Class cripts::Header::iterator

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
    _initialized = false;
  }

  [[nodiscard]] TSMBuffer
  BufP()
  {
    _ensure_initialized(this);
    return _bufp;
  }

  [[nodiscard]] TSMLoc
  MLoc()
  {
    _ensure_initialized(this);
    return _hdr_loc;
  }

  String operator[](const cripts::string_view str);

  [[nodiscard]] bool
  Initialized() const
  {
    return _initialized;
  }

  void
  Erase(const cripts::string_view header)
  {
    _ensure_initialized(this);
    operator[](header) = "";
  }

  Iterator            begin();
  cripts::string_view iterate(); // This is a little helper for the iterators

  [[nodiscard]] Iterator
  end() const
  {
    return Iterator::end(); // Static end iterator. ToDo: Does this have any value over making a new one always?
  }

  // This should only be called from the Context initializers!
  void
  set_state(cripts::Transaction *state)
  {
    _state = state;
  }

  Status      status;
  Reason      reason;
  Body        body;
  CacheStatus cache;

protected:
  static void
  _ensure_initialized(self_type *ptr)
  {
    if (!ptr->Initialized()) [[unlikely]] {
      ptr->_initialize();
    }
  }

  void virtual _initialize() { _initialized = true; }

  TSMBuffer            _bufp         = nullptr;
  TSMLoc               _hdr_loc      = nullptr;
  cripts::Transaction *_state        = nullptr; // Pointer into the owning Context's State
  TSMLoc               _iterator_loc = nullptr;
  uint32_t             _iterator_tag = 0; // This is used to assure that we don't have more than one active iterator on a header
  bool                 _initialized  = false;

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
    static self_type &_get(cripts::Context *context);
    void              _initialize() override;

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
    static self_type &_get(cripts::Context *context);
    void              _initialize() override;

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
    static self_type &_get(cripts::Context *context);
    void              _initialize() override;

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
    static self_type &_get(cripts::Context *context);
    void              _initialize() override;

  }; // End class Server::Response

} // namespace Server

// Some static methods for the Method class
namespace Method
{
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro
  extern const cripts::Header::Method GET;
  extern const cripts::Header::Method HEAD;
  extern const cripts::Header::Method POST;
  extern const cripts::Header::Method PUT;
  extern const cripts::Header::Method PUSH;
  extern const cripts::Header::Method DELETE;
  extern const cripts::Header::Method OPTIONS;
  extern const cripts::Header::Method CONNECT;
  extern const cripts::Header::Method TRACE;
  // This is a special feature of ATS
  extern const cripts::Header::Method PURGE;
} // namespace Method

class Context;
} // namespace cripts

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<cripts::Header::Method> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Header::Method &method, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", method.GetSV());
  }
};

template <> struct formatter<cripts::Header::String> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const cripts::Header::String &str, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", str.GetSV());
  }
};

template <> struct formatter<cripts::Header::Name> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const cripts::Header::Name &name, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", name.GetSV());
  }
};

} // namespace fmt
