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

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#include <vector>
#include <algorithm>

#include "cripts/Lulu.hpp"
#include "cripts/Connections.hpp"

namespace cripts::Certs
{
// These *must* match the values in x509v3.h.
enum class SAN : std::uint8_t {
  OTHER = GEN_OTHERNAME,
  EMAIL = GEN_EMAIL,
  DNS   = GEN_DNS,
  URI   = GEN_URI,
  IPADD = GEN_IPADD,
};
} // namespace cripts::Certs

namespace detail
{
class CertBase
{
  using self_type = CertBase;

  using X509 = struct x509_st;
  using BIO  = struct bio_st;

public:
  class X509Value
  {
    using self_type = X509Value;

  public:
    explicit X509Value(CertBase *owner) : _owner(owner) {}

    virtual ~X509Value() = default;

    X509Value()                  = delete;
    X509Value(const self_type &) = delete;

    self_type &operator=(const self_type &) = delete;
    self_type &operator=(self_type &&)      = delete;

    cripts::string_view
    GetSV() const
    {
      _load();
      return _value;
    }

    operator cripts::string_view() const { return GetSV(); }

  protected:
    void _update_value() const;

    virtual void
    _load() const
    {
      if (!_bio) {
        _bio.reset(BIO_new(BIO_s_mem()));
      }
    }

    void _load_name(X509_NAME *(*getter)(const X509 *)) const;
    void _load_integer(ASN1_INTEGER *(*getter)(X509 *)) const;
    void _load_long(long (*getter)(const X509 *)) const;
    void _load_time(ASN1_TIME *(*getter)(const X509 *)) const;

    CertBase                                         *_owner = nullptr;
    mutable std::unique_ptr<BIO, decltype(&BIO_free)> _bio{nullptr, BIO_free};
    mutable cripts::string_view                       _value;
    mutable bool                                      _ready = false;
  }; //  End class CertBase::X509Value

  // Here comes all the various X509 value fields that we support.
  class Certificate : public X509Value
  {
    using self_type  = Certificate;
    using super_type = X509Value;

  public:
    explicit Certificate(CertBase *owner) : super_type(owner) {}

  protected:
    void _load() const override;

  }; // End class CertBase::Certificate

  class Signature : public X509Value
  {
    using self_type  = Signature;
    using super_type = X509Value;

  public:
    explicit Signature(CertBase *owner) : super_type(owner) {}

  protected:
    void _load() const override;

  }; // End class CertBase::Signature

  class Subject : public X509Value
  {
    using self_type  = Subject;
    using super_type = X509Value;

  public:
    explicit Subject(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_name(X509_get_subject_name);
    }

  }; // End class CertBase::Subject

  class Issuer : public X509Value
  {
    using self_type  = Issuer;
    using super_type = X509Value;

  public:
    explicit Issuer(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_name(X509_get_issuer_name);
    }

  }; // End class CertBase::Issuer

  class SerialNumber : public X509Value
  {
    using self_type  = SerialNumber;
    using super_type = X509Value;

  public:
    explicit SerialNumber(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_integer(X509_get_serialNumber);
    }

  }; // End class CertBase::SerialNumer

  class NotBefore : public X509Value
  {
    using self_type  = NotBefore;
    using super_type = X509Value;

  public:
    explicit NotBefore(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_time(X509_get_notBefore);
    }

  }; // End class CertBase::NotBefore

  class NotAfter : public X509Value
  {
    using self_type  = NotAfter;
    using super_type = X509Value;

  public:
    explicit NotAfter(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_time(X509_get_notAfter);
    }

  }; // End class CertBase::NotAfter

  class Version : public X509Value
  {
    using self_type  = Version;
    using super_type = X509Value;

  public:
    explicit Version(CertBase *owner) : super_type(owner) {}

  protected:
    void
    _load() const override
    {
      _load_long(X509_get_version);
    }

  }; // End class CertBase::SerialNumer

  class SAN
  {
    using self_type = SAN;

    class SANBase
    {
      using self_type = SANBase;

    public:
      using Container = std::vector<cripts::string>;

      explicit SANBase(SAN *owner, cripts::Certs::SAN san_id) : _san_id(san_id), _owner(owner) {}

      virtual ~SANBase() = default;

      SANBase()                = delete;
      SANBase(const SANBase &) = delete;
      SANBase(SANBase &&)      = delete;

      self_type &operator=(const self_type &) = delete;
      self_type &operator=(self_type &&)      = delete;

      [[nodiscard]] cripts::Certs::SAN
      sanType() const
      {
        return _san_id;
      }

      class Iterator
      {
        using self_type = Iterator;

      public:
        using iterator_category = std::forward_iterator_tag;
        using base_iterator     = Container::const_iterator;
        using value_type        = cripts::string_view;
        using reference         = cripts::string_view;

        explicit Iterator(base_iterator iter) : _iter(iter) {}

        ~Iterator() = default;

        Iterator(const Iterator &)            = default;
        Iterator &operator=(const Iterator &) = default;
        Iterator(Iterator &&)                 = default;
        Iterator &operator=(Iterator &&)      = default;

        [[nodiscard]] reference
        operator*() const
        {
          return *_iter;
        }

        self_type &
        operator++()
        {
          ++_iter;
          return *this;
        }

        [[nodiscard]] bool
        operator!=(const self_type &other) const
        {
          return _iter != other._iter;
        }

        [[nodiscard]] base_iterator
        base_iter() const
        {
          return _iter;
        }

      private:
        base_iterator _iter;
      }; // End class SAN::SANBase::Iterator

      void
      ensureLoaded() const
      {
        if (!_ready) {
          _load();
          _ready = true;
        }
      }

      [[nodiscard]] Iterator
      begin() const
      {
        ensureLoaded();
        return Iterator(_data.begin());
      }

      [[nodiscard]] Iterator
      end() const
      {
        return Iterator(_data.end());
      }

      [[nodiscard]] Container &
      Data() const
      {
        ensureLoaded();
        return _data;
      }

      [[nodiscard]] size_t
      size() const
      {
        ensureLoaded();
        return _data.size();
      }

      [[nodiscard]] size_t
      Size() const
      {
        return size();
      }
      [[nodiscard]] cripts::string_view
      operator[](size_t index) const
      {
        ensureLoaded();
        if (index >= _data.size()) {
          return {};
        }
        return _data[index];
      }

    protected:
      void _load() const;

      mutable Container  _data;
      mutable bool       _ready  = false;
      cripts::Certs::SAN _san_id = cripts::Certs::SAN::OTHER;
      SAN               *_owner  = nullptr;

    }; // End class SAN::SANBase

  public:
    template <cripts::Certs::SAN ID> class SANType : public SANBase
    {
      using super_type = SANBase;
      using self_type  = SANType;

    public:
      explicit SANType(SAN *owner) : SANBase(owner, ID) {}

      ~SANType() override = default;

      SANType()                = delete;
      SANType(const SANType &) = delete;
      SANType(SANType &&)      = delete;

      self_type &operator=(const self_type &) = delete;
      self_type &operator=(self_type &&)      = delete;
    }; // End class SAN::SANType

    explicit SAN(CertBase *owner) : email(this), dns(this), uri(this), ipadd(this), _owner(owner) {}

    ~SAN() = default;

    SAN()            = delete;
    SAN(const SAN &) = delete;
    SAN(SAN &&)      = delete;

    self_type &operator=(const self_type &) = delete;
    self_type &operator=(self_type &&)      = delete;

    SAN::SANType<cripts::Certs::SAN::EMAIL> email;
    SAN::SANType<cripts::Certs::SAN::DNS>   dns;
    SAN::SANType<cripts::Certs::SAN::URI>   uri;
    SAN::SANType<cripts::Certs::SAN::IPADD> ipadd;

    class Iterator
    {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type        = std::tuple<cripts::Certs::SAN, cripts::string_view>;
      using reference         = value_type; // Return by value instead of const reference
      using base_iterator     = SANBase::Container::const_iterator;

      explicit Iterator(std::nullptr_t) : _ended(true) {}
      explicit Iterator(const SAN *san) : _san(san) { _advance(); }

      Iterator()                            = default;
      Iterator(const Iterator &)            = default;
      Iterator &operator=(const Iterator &) = default;
      Iterator(Iterator &&)                 = default;
      Iterator &operator=(Iterator &&)      = default;

      ~Iterator() = default;

      [[nodiscard]] reference
      operator*() const
      {
        return {_current};
      }

      Iterator &
      operator++()
      {
        if (_ended) {
          return *this;
        }

        ++_iter;
        if (_iter == _san->_sans[_index - 1]->Data().end()) {
          _advance();
        } else {
          _update_current();
        }

        return *this;
      }

      [[nodiscard]] bool
      operator!=(const Iterator &other) const
      {
        if (_ended && other._ended) {
          return false;
        }

        return _san != other._san || _index != other._index || _iter != other._iter;
      }

      [[nodiscard]] bool
      operator==(const Iterator &other) const
      {
        if (_ended && other._ended) {
          return true;
        }
        return _san == other._san && _index == other._index && _iter == other._iter;
      }

    private:
      void
      _update_current() const
      {
        if (_san && !_ended) {
          _current = std::make_tuple(_san->_sans[_index - 1]->sanType(), cripts::string_view(*_iter));
        }
      }

      void
      _advance()
      {
        if (!_san || _ended) {
          return;
        }

        auto it =
          std::find_if(_san->_sans.begin() + _index, _san->_sans.end(), [](const SANBase *entry) { return entry->Size() > 0; });

        if (it != _san->_sans.end()) {
          _index = std::distance(_san->_sans.begin(), it) + 1;
          _iter  = (*it)->Data().begin();
          _update_current();
        } else {
          _ended = true;
        }
      }

      mutable value_type _current; // The current value of the iterator
      base_iterator      _iter;    // The current iterator within the SAN types
      bool               _ended = false;
      const SAN         *_san   = nullptr;
      size_t             _index = 0;

    }; // End class CertBase::SAN::Iterator

    [[nodiscard]] Iterator
    begin() const
    {
      auto it = Iterator(this);

      return it;
    }

    [[nodiscard]] Iterator
    end() const
    {
      Iterator it{nullptr};

      return it;
    }

    [[nodiscard]] size_t
    size() const
    {
      size_t total = 0;

      for (const auto *san : _sans) {
        total += san->Size();
      }
      return total;
    }

    [[nodiscard]] size_t
    Size() const
    {
      return size();
    }

    [[nodiscard]] Iterator::value_type
    operator[](size_t index) const
    {
      if (index < Size()) {
        size_t cur = 0;

        for (auto *san : _sans) {
          size_t type_size = san->Size();

          if (index < cur + type_size) {
            // Found the SAN type that contains this index
            return std::make_tuple(san->sanType(), cripts::string_view((*san)[index - cur]));
          }
          cur += type_size;
        }
      }

      return std::make_tuple(cripts::Certs::SAN::OTHER, cripts::string_view());
    }

  private:
    const std::array<const SANBase *, 4> _sans = std::to_array<const SANBase *>({&email, &dns, &uri, &ipadd});
    CertBase                            *_owner;

  }; // End class CertBase::SAN

public:
  CertBase(detail::ConnBase &conn)
    : certificate(this),
      signature(this),
      subject(this),
      issuer(this),
      serialNumber(this),
      notBefore(this),
      notAfter(this),
      version(this),
      san(this),
      _conn(&conn)
  {
  }

  CertBase()                  = delete;
  CertBase(const self_type &) = delete;

  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

  Certificate  certificate;
  Signature    signature;
  Subject      subject;
  Issuer       issuer;
  SerialNumber serialNumber;
  NotBefore    notBefore;
  NotAfter     notAfter;
  Version      version;
  SAN          san;

protected:
  X509             *_x509 = nullptr;
  detail::ConnBase *_conn = nullptr;

}; // End class CertBase

template <bool IsMutualTLS> class Cert : public detail::CertBase
{
  using self_type  = Cert<IsMutualTLS>;
  using super_type = detail::CertBase;

public:
  explicit Cert(detail::ConnBase &conn) : super_type(conn) { _x509 = conn.tls.X509(IsMutualTLS); }
}; // End class Cert

} // namespace detail

namespace cripts::Certs
{
using Client = detail::Cert<true>;
using Server = detail::Cert<false>;

} // namespace cripts::Certs

namespace fmt
{

template <> struct formatter<cripts::Certs::SAN> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Certs::SAN san, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", static_cast<int>(san));
  }
};

template <std::derived_from<::detail::CertBase::X509Value> T> struct formatter<T> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const T &val, FormatContext &ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", val.GetSV());
  }
};

} // namespace fmt
