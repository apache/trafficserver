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
#include <arpa/inet.h>

#include "ts/ts.h"

#include "cripts/Certs.hpp"

namespace
{
} // namespace

namespace detail
{
// The first two are essentially taken from our sslheaders plugin, for compatiblity.
void
CertBase::Certificate::_load() const
{
  if (!_ready && _owner->_x509) {
    long  remain;
    char *ptr;

    super_type::_load();
    PEM_write_bio_X509(_bio.get(), _owner->_x509);

    // The PEM format has newlines in it. mod_ssl replaces those with spaces.
    remain = BIO_get_mem_data(_bio.get(), &ptr);
    for (char *nl; (nl = static_cast<char *>(memchr(ptr, '\n', remain))); ptr = nl) {
      *nl     = ' ';
      remain -= nl - ptr;
    }
    _update_value();
  }
}

void
CertBase::Signature::_load() const
{
  if (!_ready && _owner->_x509) {
    const ASN1_BIT_STRING *sig;
    X509_get0_signature(&sig, nullptr, _owner->_x509);
    const char *ptr = reinterpret_cast<const char *>(sig->data);
    const char *end = ptr + sig->length;

    super_type::_load();
    for (; ptr < end; ++ptr) {
      BIO_printf(_bio.get(), "%02X", static_cast<unsigned char>(*ptr));
    }
    _update_value();
  }
}

void
CertBase::X509Value::_update_value() const
{
  if (BIO_pending(_bio.get())) {
    char *data = nullptr;
    long  len  = BIO_get_mem_data(_bio.get(), &data);

    _value = cripts::Certs::String(data, static_cast<size_t>(len));
    _ready = true;
  }
}

void
CertBase::X509Value::_load_name(X509_NAME *(*getter)(const X509 *)) const
{
  if (!_ready && _owner->_x509) {
    auto *name = getter(_owner->_x509);

    if (name) {
      X509Value::_load();
      X509_NAME_print_ex(_bio.get(), name, 0, XN_FLAG_ONELINE);
      _update_value();
    } else [[unlikely]] {
      _value = cripts::Certs::String();
      _ready = true;
    }
  }
}

void
CertBase::X509Value::_load_integer(ASN1_INTEGER *(*getter)(X509 *)) const
{
  if (!_ready && _owner->_x509) {
    auto *value = getter(_owner->_x509);

    X509Value::_load();
    i2a_ASN1_INTEGER(_bio.get(), value);
    X509Value::_update_value();
  }
}

void
CertBase::X509Value::_load_long(long (*getter)(const X509 *)) const
{
  if (!_ready && _owner->_x509) {
    auto value = getter(_owner->_x509);

    X509Value::_load();
    BIO_printf(_bio.get(), "%ld", value);
    X509Value::_update_value();
  }
}

void
CertBase::X509Value::_load_time(ASN1_TIME *(*getter)(const X509 *)) const
{
  if (!_ready && _owner->_x509) {
    auto *time = getter(_owner->_x509);

    if (time) {
      X509Value::_load();
      ASN1_TIME_print(_bio.get(), time);
      X509Value::_update_value();
    } else [[unlikely]] {
      _value = cripts::Certs::String();
      _ready = true;
    }
  }
}

namespace
{

  using GenWriterFunc = void (*)(const GENERAL_NAME *, BIO *);

  void
  _write_asn1_string(const ASN1_IA5STRING *str, BIO *_bio)
  {
    const char *data = reinterpret_cast<const char *>(ASN1_STRING_get0_data(str));
    int         len  = ASN1_STRING_length(str);

    BIO_write(_bio, data, len);
  }

  void
  _write_ip_address(const ASN1_OCTET_STRING *ip, BIO *_bio)
  {
    char                 buffer[INET6_ADDRSTRLEN];
    const unsigned char *raw = ip->data;
    int                  len = ip->length;

    if (inet_ntop(len == 4 ? AF_INET : AF_INET6, raw, buffer, sizeof(buffer))) {
      BIO_printf(_bio, "%s", buffer);
    }
  }

  static constexpr GenWriterFunc _gen_writers[] = {
    /* 0 */ nullptr,
    /* 1 GEN_EMAIL */ [](const GENERAL_NAME *n, BIO *b) { _write_asn1_string(n->d.rfc822Name, b); },
    /* 2 GEN_DNS   */ [](const GENERAL_NAME *n, BIO *b) { _write_asn1_string(n->d.dNSName, b); },
    /* 3 GEN_X400  */ nullptr,
    /* 4 GEN_DIRNAME */ nullptr,
    /* 5 GEN_EDIPARTY */ nullptr,
    /* 6 GEN_URI   */ [](const GENERAL_NAME *n, BIO *b) { _write_asn1_string(n->d.uniformResourceIdentifier, b); },
    /* 7 GEN_IPADD */ [](const GENERAL_NAME *n, BIO *b) { _write_ip_address(n->d.iPAddress, b); },
    /* 8 GEN_RID   */ nullptr};
} // end anonymous namespace

void
CertBase::SAN::SANBase::_load() const
{
  if (_ready || !_owner->_owner->_x509) {
    return;
  }

  auto *san_names =
    static_cast<STACK_OF(GENERAL_NAME) *>(X509_get_ext_d2i(_owner->_owner->_x509, NID_subject_alt_name, nullptr, nullptr));

  if (!san_names) {
    return;
  }

  auto bio = BIO_new(BIO_s_mem());

  if (bio) {
    for (int i = 0; i < sk_GENERAL_NAME_num(san_names); ++i) {
      const GENERAL_NAME *name = sk_GENERAL_NAME_value(san_names, i);

      if (static_cast<cripts::Certs::SAN>(name->type) == _san_id) {
        GenWriterFunc fn = (name->type < static_cast<int>(std::size(_gen_writers))) ? _gen_writers[name->type] : nullptr;
        CAssert(fn != nullptr);

        BIO_reset(bio);
        fn(name, bio);

        char *ptr = nullptr;
        long  len = BIO_get_mem_data(bio, &ptr);

        if (ptr && len > 0) {
          _data.emplace_back(ptr, static_cast<size_t>(len));
        }
      }
    }
    BIO_free(bio);
  }
  sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
}

cripts::string
CertBase::SAN::SANBase::Join(const char *delim) const
{
  cripts::string str;

  ensureLoaded();
  for (const auto &s : _data) {
    if (!str.empty()) {
      str += delim;
    }
    str += s;
  }

  return str; // RVO
}

void
CertBase::SAN::Iterator::_advance()
{
  if (!_san || _ended) {
    return;
  }

  auto it = std::find_if(_san->_sans.begin() + _index, _san->_sans.end(), [](const SANBase *entry) { return entry->Size() > 0; });

  if (it != _san->_sans.end()) {
    _index = std::distance(_san->_sans.begin(), it) + 1;
    _iter  = (*it)->Data().begin();
    _update_current();
  } else {
    _ended = true;
  }
}

CertBase::SAN::Iterator::value_type
CertBase::SAN::operator[](size_t index) const
{
  if (index < Size()) {
    size_t cur = 0;

    for (auto *san : _sans) {
      size_t type_size = san->Size();

      if (index < cur + type_size) {
        // Found the SAN type that contains this index
        return std::make_tuple(san->sanType(), cripts::Certs::String((*san)[index - cur]));
      }
      cur += type_size;
    }
  }

  return std::make_tuple(cripts::Certs::SAN::OTHER, cripts::Certs::String());
}
} // namespace detail
