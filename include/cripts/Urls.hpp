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

namespace Cript
{
class Context;
}

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ts/remap.h"
#include "ts/ts.h"

class Url
{
  class Component
  {
    using self_type = Component;

  public:
    Component() = default;

    virtual Cript::string_view getSV() = 0;

    std::vector<Cript::string_view> split(char delim);

    operator Cript::string_view() { return getSV(); } // Should not be explicit

    bool
    operator==(Cript::string_view const &rhs)
    {
      return getSV() == rhs;
    }

    bool
    operator!=(Cript::string_view const &rhs)
    {
      return getSV() != rhs;
    }

    virtual void
    reset()
    {
      _data.clear();
    }

    Cript::string_view::const_pointer
    data()
    {
      return getSV().data();
    }

    Cript::string_view::size_type
    size()
    {
      return getSV().size();
    }

    Cript::string_view::size_type
    length()
    {
      return getSV().size();
    }

    // This is not ideal, but best way I can think of for now to mixin the Cript::string_view mixin class
    // Remember to add things here when added to the Lulu.hpp file for the mixin class... :/
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

  protected:
    mutable Cript::string_view _data;
    Url *_owner  = nullptr;
    bool _loaded = false;

  }; // End class Url::Component

public:
  // Each of the members of a URL has its own class, very convenient. Is this
  // great C++? Dunno.
  class Scheme : public Component
  {
    using super_type = Component;
    using self_type  = Scheme;

    friend class Url;

  public:
    Scheme() = default;
    Cript::string_view getSV() override;
    self_type operator=(Cript::string_view scheme);

  }; // End class Url::Scheme

  class Host : public Component
  {
    using super_type = Component;
    using self_type  = Host;

    friend class Url;

  public:
    Host() = default;
    Cript::string_view getSV() override;
    self_type operator=(Cript::string_view host);

  }; // End class Url::Host

  class Port
  {
    using self_type = Port;

    friend class Url;

  public:
    Port() = default;

    void
    reset()
    {
      _port = -1;
    }

    operator integer(); // This should not be explicit, and should not be const
    self_type operator=(int port);

    self_type
    operator=(Cript::string_view str)
    {
      uint32_t port = 80;
      auto result   = std::from_chars(str.data(), str.data() + str.size(), port);

      if (result.ec != std::errc::invalid_argument) {
        return *this;
      } else {
        return operator=(port);
      }
    }

  private:
    Url *_owner   = nullptr;
    integer _port = -1;
  }; // End class Url::Port

  class Path : public Component
  {
  public:
    using Segments = std::vector<Cript::string_view>;

  private:
    using super_type = Component;
    using self_type  = Path;

    friend class Url;

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

      Path *_owner            = nullptr;
      Segments::size_type _ix = 0;

    }; // End class Url::Path::String

  public:
    friend struct fmt::formatter<Path::String>;

    Path() = default;

    void reset() override;

    Cript::string_view getSV() override;
    Cript::string operator+=(Cript::string_view add);
    self_type operator=(Cript::string_view path);
    String operator[](Segments::size_type ix);

    void
    erase(Segments::size_type ix)
    {
      auto p = operator[](ix);

      _size -= p.size();
      p.operator=("");
    }

    void
    erase()
    {
      operator=("");
    }

    void
    clear()
    {
      erase();
    }

    void push(Cript::string_view val);
    void insert(Segments::size_type ix, Cript::string_view val);

    void
    flush()
    {
      if (_modified) {
        operator=(getSV());
      }
    }

  private:
    void _parser();

    bool _modified = false;
    Segments _segments;                 // Lazy loading on this
    Cript::string _storage;             // Used when recombining the segments into a full path
    Cript::string::size_type _size = 0; // Mostly a guestimate for managing _storage

  }; // End class Url::Path

  class Query : public Component
  {
    using super_type = Component;
    using self_type  = Query;

    friend class Url;

    using OrderedParams = std::vector<Cript::string_view>;                            // Ordered parameter nmes
    using HashParams    = std::unordered_map<Cript::string_view, Cript::string_view>; // Hash lookups

    class Parameter : public Cript::StringViewMixin<Parameter>
    {
      using super_type = Cript::StringViewMixin<Parameter>;
      using self_type  = Parameter;

    public:
      Parameter() = default;

      [[nodiscard]] Cript::string_view
      name() const
      {
        return _name;
      }

      void
      erase()
      {
        _owner->erase(_name);
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

      Query *_owner = nullptr;
      Cript::string_view _name;

    }; // End class Url::Query::Parameter

  public:
    friend struct fmt::formatter<Query::Parameter>;

    Query() = default;

    Query(Cript::string_view load)
    {
      _data   = load;
      _size   = load.size();
      _loaded = true;
    }

    void reset() override;

    Cript::string_view getSV() override;
    self_type operator=(Cript::string_view query);
    Cript::string operator+=(Cript::string_view add);
    Parameter operator[](Cript::string_view param);
    void erase(Cript::string_view param);

    void
    erase(std::initializer_list<Cript::string_view> list)
    {
      for (auto &it : list) {
        erase(it);
      }
    }

    void
    erase()
    {
      operator=("");
      _size = 0;
    }

    void
    clear()
    {
      return erase();
    }

    void
    sort()
    {
      // Make sure the hash and vector are populated
      _parser();

      std::sort(_ordered.begin(), _ordered.end());
      _modified = true;
    }

    void
    flush()
    {
      if (_modified) {
        operator=(getSV());
      }
    }

  private:
    void _parser();

    bool _modified = false;
    OrderedParams _ordered;             // Ordered vector of all parameters, can be sorted etc.
    HashParams _hashed;                 // Unordered map to go from "name" to the query parameter
    Cript::string _storage;             // Used when recombining the query params into a
                                        // full query string
    Cript::string::size_type _size = 0; // Mostly a guesttimate

  }; // End class Url::Query

public:
  Url() = default;

  ~Url() { reset(); }

  // Clear anything "cached" in the Url, this is rather draconian, but it's safe...
  virtual void
  reset()
  {
    if (_bufp && _urlp) {
      TSHandleMLocRelease(_bufp, TS_NULL_MLOC, _urlp);
      _urlp = nullptr;
      _bufp = nullptr;

      query.reset();
      path.reset();
    }
    _state = nullptr;
  }

  bool
  initialized() const
  {
    return (_state != nullptr);
  }

  bool
  modified() const
  {
    return _modified;
  }

  TSMLoc
  urlp() const
  {
    return _urlp;
  }

  // Getters / setters for the full URL
  Cript::string url() const;

  Host host;
  Scheme scheme;
  Path path;
  Query query;
  Port port;

protected:
  void
  _initialize(Cript::Transaction *state)
  {
    _state      = state;
    host._owner = path._owner = scheme._owner = query._owner = port._owner = this;
  }

  TSMBuffer _bufp = nullptr;            // These two gets setup via initializing, pointing
                                        // to appropriate headers
  TSMLoc _hdr_loc            = nullptr; // Do not release any of this within the URL classes!
  TSMLoc _urlp               = nullptr; // This is owned by us.
  Cript::Transaction *_state = nullptr; // Pointer into the owning Context's State
  bool _modified             = false;   // We have pending changes on the path components or
                                        // query parameters?

}; // End class Url

// The Pristine URL is immutable, we should "delete" the operator= methods
namespace Pristine
{
class URL : public Url
{
  using super_type = Url;
  using self_type  = URL;

public:
  URL() = default;

  URL(const URL &)            = delete;
  void operator=(const URL &) = delete;

  static URL &_get(Cript::Context *context);

}; // End class Pristine::URL

} // namespace Pristine

namespace Client
{
class URL : public Url
{
  using super_type = Url;
  using self_type  = URL;

public:
  URL()                       = default;
  URL(const URL &)            = delete;
  void operator=(const URL &) = delete;

  // We must not release the bufp etc. since it comes from the RRI structure
  void
  reset() override
  {
  }

  static URL &_get(Cript::Context *context);
  bool _update(Cript::Context *context);

private:
  void _initialize(Cript::Context *context);

}; // End class Client::URL

} // namespace Client

namespace Cache
{
class URL : public Url // ToDo: This can maybe be a subclass of Client::URL ?
{
  using super_type = Url;
  using self_type  = URL;

public:
  URL()                       = default;
  URL(const URL &)            = delete;
  void operator=(const URL &) = delete;

  static URL &_get(Cript::Context *context);
  bool _update(Cript::Context *context);

private:
  void
  _initialize(Cript::Transaction *state, Client::Request *req)
  {
    Url::_initialize(state);

    _bufp    = req->bufp();
    _hdr_loc = req->mloc();
  }

}; // End class Cache::URL

} // namespace Cache

namespace Parent
{
class URL : public Url
{
  using super_type = Url;
  using self_type  = URL;

public:
  URL()                       = default;
  URL(const URL &)            = delete;
  void operator=(const URL &) = delete;

  static URL &_get(Cript::Context *context);
  bool _update(Cript::Context *context);

private:
  void
  _initialize(Cript::Transaction *state, Client::Request *req)
  {
    Url::_initialize(state);

    _bufp    = req->bufp();
    _hdr_loc = req->mloc();
  }

}; // End class Cache::URL

} // namespace Parent

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Url::Scheme> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Scheme &scheme, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", scheme.getSV());
  }
};

template <> struct formatter<Url::Host> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Host &host, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", host.getSV());
  }
};

template <> struct formatter<Url::Port> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Port &port, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", integer(port));
  }
};

template <> struct formatter<Url::Path::String> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Path::String &path, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", path.getSV());
  }
};

template <> struct formatter<Url::Path> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Path &path, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", path.getSV());
  }
};

template <> struct formatter<Url::Query::Parameter> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Query::Parameter &param, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", param.getSV());
  }
};

template <> struct formatter<Url::Query> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Url::Query &query, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", query.getSV());
  }
};

} // namespace fmt
