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

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Headers.hpp"

namespace Cript
{
class Context;

class Url
{
  class Component
  {
    using self_type = Component;

  public:
    Component() = default;
    Component(Url *owner) : _owner(owner) {}

    virtual Cript::string_view GetSV() = 0;

    std::vector<Cript::string_view> Split(char delim);

    operator Cript::string_view() { return GetSV(); } // Should not be explicit

    bool
    operator==(Cript::string_view const &rhs)
    {
      return GetSV() == rhs;
    }

    bool
    operator!=(Cript::string_view const &rhs)
    {
      return GetSV() != rhs;
    }

    virtual void
    Reset()
    {
      _data.clear();
    }

    Cript::string_view::const_pointer
    data()
    {
      return GetSV().data();
    }

    Cript::string_view::size_type
    size()
    {
      return GetSV().size();
    }

    Cript::string_view::size_type
    length()
    {
      return GetSV().size();
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

    // This is not ideal, but best way I can think of for now to mixin the Cript::string_view mixin class
    // Remember to add things here when added to the Lulu.hpp file for the mixin class... :/
    [[nodiscard]] constexpr Cript::string_view
    substr(Cript::string_view::size_type pos = 0, Cript::string_view::size_type count = Cript::string_view::npos) const
    {
      return _data.substr(pos, count);
    }

    void
    remove_prefix(Cript::string_view::size_type n)
    {
      _data.remove_prefix(n);
    }

    void
    remove_suffix(Cript::string_view::size_type n)
    {
      _data.remove_suffix(n);
    }

    Cript::string_view &
    ltrim(char c)
    {
      return _data.ltrim(c);
    }

    Cript::string_view &
    rtrim(char c)
    {
      return _data.rtrim(c);
    }

    Cript::string_view &
    trim(char c)
    {
      return _data.trim(c);
    }

    Cript::string_view &
    ltrim(const char *chars = " \t\r\n")
    {
      return _data.ltrim(chars);
    }

    Cript::string_view &
    rtrim(const char *chars = " \t\r\n")
    {
      return _data.rtrim(chars);
    }

    Cript::string_view &
    trim(const char *chars = " \t")
    {
      return _data.trim(chars);
    }

    [[nodiscard]] constexpr char const *
    data_end() const noexcept
    {
      return _data.data_end();
    }

    [[nodiscard]] bool
    ends_with(Cript::string_view suffix) const
    {
      return _data.ends_with(suffix);
    }

    [[nodiscard]] bool
    starts_with(Cript::string_view const prefix) const
    {
      return _data.starts_with(prefix);
    }

    [[nodiscard]] constexpr Cript::string_view::size_type
    find(Cript::string_view const substr, Cript::string_view::size_type pos = 0) const
    {
      return _data.find(substr, pos);
    }

    [[nodiscard]] constexpr Cript::string_view::size_type
    rfind(Cript::string_view const substr, Cript::string_view::size_type pos = 0) const
    {
      return _data.rfind(substr, pos);
    }

    [[nodiscard]] constexpr bool
    contains(Cript::string_view const substr) const
    {
      return (_data.find(substr) != _data.npos);
    }

  protected:
    mutable Cript::string_view _data;
    Url                       *_owner  = nullptr;
    bool                       _loaded = false;

  }; // End class Url::Component

public:
  // Each of the members of a URL has its own class, very convenient. Is this
  // great C++? Dunno.
  class Scheme : public Component
  {
    using super_type = Component;
    using self_type  = Scheme;

  public:
    using Component::Component;

    Cript::string_view GetSV() override;
    self_type          operator=(Cript::string_view scheme);

  }; // End class Url::Scheme

  class Host : public Component
  {
    using super_type = Component;
    using self_type  = Host;

  public:
    using Component::Component;

    Cript::string_view GetSV() override;
    self_type          operator=(Cript::string_view host);

  }; // End class Url::Host

  class Port
  {
    using self_type = Port;

  public:
    Port(Url *owner) : _owner(owner){};

    void
    Reset()
    {
      _port = -1;
    }

    operator integer(); // This should not be explicit, and should not be const
    self_type operator=(int port);

    self_type
    operator=(Cript::string_view str)
    {
      uint32_t port   = 80;
      auto     result = std::from_chars(str.data(), str.data() + str.size(), port);

      if (result.ec != std::errc::invalid_argument) {
        return *this;
      } else {
        return operator=(port);
      }
    }

  private:
    Url    *_owner = nullptr;
    integer _port  = -1;
  }; // End class Url::Port

  class Path : public Component
  {
  public:
    using Segments = std::vector<Cript::string_view>;

  private:
    using super_type = Component;
    using self_type  = Path;

    class String : public Cript::StringViewMixin<String>
    {
      using super_type = Cript::StringViewMixin<String>;
      using self_type  = String;

    public:
      String() = default;

      // Implemented in the Urls.cc file, bigger function
      self_type &operator=(const Cript::string_view str) override;

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
      friend class Path;

      void
      _initialize(Cript::string_view source, Path *owner, Segments::size_type ix)
      {
        _setSV(source);
        _owner = owner;
        _ix    = ix;
      }

      Path               *_owner = nullptr;
      Segments::size_type _ix    = 0;

    }; // End class Url::Path::String

  public:
    friend struct fmt::formatter<Path::String>;

    using Component::Component;

    void Reset() override;

    Cript::string_view GetSV() override;
    Cript::string      operator+=(Cript::string_view add);
    self_type          operator=(Cript::string_view path);
    String             operator[](Segments::size_type ix);

    void
    Erase(Segments::size_type ix)
    {
      auto p = operator[](ix);

      _size -= p.size();
      p.operator=("");
    }

    void
    Erase()
    {
      operator=("");
    }

    void
    Clear()
    {
      Erase();
    }

    void Push(Cript::string_view val);
    void Insert(Segments::size_type ix, Cript::string_view val);

    void
    Flush()
    {
      if (_modified) {
        operator=(GetSV());
      }
    }

  private:
    void _parser();

    bool                     _modified = false;
    Segments                 _segments; // Lazy loading on this
    Cript::string            _storage;  // Used when recombining the segments into a full path
    Cript::string::size_type _size = 0; // Mostly a guestimate for managing _storage

  }; // End class Url::Path

  class Query : public Component
  {
    using super_type = Component;
    using self_type  = Query;

    using OrderedParams = std::vector<Cript::string_view>;                            // Ordered parameter nmes
    using HashParams    = std::unordered_map<Cript::string_view, Cript::string_view>; // Hash lookups

    class Parameter : public Cript::StringViewMixin<Parameter>
    {
      using super_type = Cript::StringViewMixin<Parameter>;
      using self_type  = Parameter;

    public:
      Parameter() = default;

      [[nodiscard]] Cript::string_view
      Name() const
      {
        return _name;
      }

      void
      Erase()
      {
        _owner->Erase(_name);
      }

      // Implemented in the Urls.cc file, bigger function
      self_type &operator=(const Cript::string_view str) override;

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
      friend class Query;

      void
      _initialize(Cript::string_view name, Cript::string_view source, Query *owner)
      {
        _setSV(source);
        _name  = name;
        _owner = owner;
      }

      Query             *_owner = nullptr;
      Cript::string_view _name;

    }; // End class Url::Query::Parameter

  public:
    friend struct fmt::formatter<Query::Parameter>;

    using Component::Component;

    Query(Cript::string_view load)
    {
      _data   = load;
      _size   = load.size();
      _loaded = true;
    }

    void Reset() override;

    Cript::string_view GetSV() override;
    self_type          operator=(Cript::string_view query);
    Cript::string      operator+=(Cript::string_view add);
    Parameter          operator[](Cript::string_view param);
    void               Erase(Cript::string_view param);
    void               Erase(std::initializer_list<Cript::string_view> list, bool keep = false);

    void
    Erase()
    {
      operator=("");
      _size = 0;
    }

    void
    Keep(std::initializer_list<Cript::string_view> list)
    {
      Erase(list, true);
    }

    void
    Clear()
    {
      return Erase();
    }

    void
    Sort()
    {
      // Make sure the hash and vector are populated
      _parser();

      std::sort(_ordered.begin(), _ordered.end());
      _modified = true;
    }

    void
    Flush()
    {
      if (_modified) {
        operator=(GetSV());
      }
    }

  private:
    void _parser();

    bool          _modified = false;
    OrderedParams _ordered;             // Ordered vector of all parameters, can be sorted etc.
    HashParams    _hashed;              // Unordered map to go from "name" to the query parameter
    Cript::string _storage;             // Used when recombining the query params into a
                                        // full query string
    Cript::string::size_type _size = 0; // Mostly a guesttimate

  }; // End class Url::Query

public:
  Url() : scheme(this), host(this), port(this), path(this), query(this) {}

  // Clear anything "cached" in the Url, this is rather draconian, but it's safe...
  virtual void
  Reset()
  {
    if (_bufp && _urlp) {
      TSHandleMLocRelease(_bufp, TS_NULL_MLOC, _urlp);
      _urlp = nullptr;
      _bufp = nullptr;

      query.Reset();
      path.Reset();
    }
    _state = nullptr;
  }

  [[nodiscard]] bool
  Initialized() const
  {
    return (_state != nullptr);
  }

  [[nodiscard]] bool
  Modified() const
  {
    return _modified;
  }

  [[nodiscard]] TSMLoc
  UrlP() const
  {
    return _urlp;
  }

  [[nodiscard]] virtual bool
  ReadOnly() const
  {
    return false;
  }

  // This is the full string for a URL, which needs allocations.
  [[nodiscard]] Cript::string String() const;

  Scheme scheme;
  Host   host;
  Port   port;
  Path   path;
  Query  query;

protected:
  void
  _initialize(Cript::Transaction *state)
  {
    _state = state;
  }

  TSMBuffer           _bufp     = nullptr; // These two gets setup via initializing, to appropriate headers
  TSMLoc              _hdr_loc  = nullptr; // Do not release any of this within the URL classes!
  TSMLoc              _urlp     = nullptr; // This is owned by us.
  Cript::Transaction *_state    = nullptr; // Pointer into the owning Context's State
  bool                _modified = false;   // We have pending changes on the path/query components

}; // End class Url

} // namespace Cript

// The Pristine URL is immutable, we should "delete" the operator= methods
namespace Pristine
{
class URL : public Cript::Url
{
  using super_type = Cript::Url;
  using self_type  = URL;

public:
  URL()                             = default;
  URL(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  static self_type &_get(Cript::Context *context);

  [[nodiscard]] bool
  ReadOnly() const override
  {
    return true;
  }

}; // End class Pristine::URL

} // namespace Pristine

namespace Client
{
class URL : public Cript::Url
{
  using super_type = Cript::Url;
  using self_type  = URL;

public:
  URL()                             = default;
  URL(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  // We must not release the bufp etc. since it comes from the RRI structure
  void
  Reset() override
  {
  }

  static self_type &_get(Cript::Context *context);
  bool              _update(Cript::Context *context);

private:
  void _initialize(Cript::Context *context);

}; // End class Client::URL

} // namespace Client

namespace Remap
{
namespace From
{
  class URL : public Cript::Url
  {
    using super_type = Cript::Url;
    using self_type  = URL;

  public:
    URL()                             = default;
    URL(const self_type &)            = delete;
    void operator=(const self_type &) = delete;

    // We must not release the bufp etc. since it comes from the RRI structure
    void
    Reset() override
    {
    }

    [[nodiscard]] bool
    ReadOnly() const override
    {
      return true;
    }

    static self_type &_get(Cript::Context *context);
    bool              _update(Cript::Context *context);

  private:
    void _initialize(Cript::Context *context);

  }; // End class Client::URL

} // namespace From

namespace To
{
  class URL : public Cript::Url
  {
    using super_type = Cript::Url;
    using self_type  = URL;

  public:
    URL()                             = default;
    URL(const self_type &)            = delete;
    void operator=(const self_type &) = delete;

    // We must not release the bufp etc. since it comes from the RRI structure
    void
    Reset() override
    {
    }

    [[nodiscard]] bool
    ReadOnly() const override
    {
      return true;
    }

    static self_type &_get(Cript::Context *context);
    bool              _update(Cript::Context *context);

  private:
    void _initialize(Cript::Context *context);

  }; // End class Client::URL

} // namespace To

} // namespace Remap

namespace Cache
{
class URL : public Cript::Url // ToDo: This can maybe be a subclass of Client::URL ?
{
  using super_type = Cript::Url;
  using self_type  = URL;

public:
  URL()                             = default;
  URL(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  static self_type &_get(Cript::Context *context);
  bool              _update(Cript::Context *context);

private:
  void
  _initialize(Cript::Transaction *state, Client::Request *req)
  {
    Url::_initialize(state);

    _bufp    = req->BufP();
    _hdr_loc = req->MLoc();
  }

}; // End class Cache::URL

} // namespace Cache

namespace Parent
{
class URL : public Cript::Url
{
  using super_type = Cript::Url;
  using self_type  = URL;

public:
  URL()                             = default;
  URL(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  static self_type &_get(Cript::Context *context);
  bool              _update(Cript::Context *context);

private:
  void
  _initialize(Cript::Transaction *state, Client::Request *req)
  {
    Url::_initialize(state);

    _bufp    = req->BufP();
    _hdr_loc = req->MLoc();
  }

}; // End class Cache::URL

} // namespace Parent

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Cript::Url::Scheme> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Scheme &scheme, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", scheme.GetSV());
  }
};

template <> struct formatter<Cript::Url::Host> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Host &host, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", host.GetSV());
  }
};

template <> struct formatter<Cript::Url::Port> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Port &port, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", integer(port));
  }
};

template <> struct formatter<Cript::Url::Path::String> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Path::String &path, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", path.GetSV());
  }
};

template <> struct formatter<Cript::Url::Path> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Path &path, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", path.GetSV());
  }
};

template <> struct formatter<Cript::Url::Query::Parameter> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Query::Parameter &param, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", param.GetSV());
  }
};

template <> struct formatter<Cript::Url::Query> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::Url::Query &query, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", query.GetSV());
  }
};

} // namespace fmt
