/** @file

  A brief file description

  @section license License

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

/*************************** -*- Mod: C++ -*- ******************************
  P_SSLConfig.h
   Created On      : 07/20/2000

   Description:
   SSL Configurations
 ****************************************************************************/
#pragma once

#include <cstdint>
#include <new>
#include <type_traits>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include <openssl/rand.h>

#include "tscore/ink_inet.h"
#include "tscore/IpMap.h"
#include <tscore/HashFNV.h>

#include "ProxyConfig.h"

#include "SSLSessionCache.h"
#include "YamlSNIConfig.h"

#include "P_SSLUtils.h"

namespace SSLUtilsImpl
{
// An array of const instances of T.  T must be copyable.  The length of the array is fixed at the time of instance
// creation.  The array associated with an instance will have the same value as the array passed to the make()
// static member function.
template <typename T, bool THasEq = false> class DurableConstArray
{
public:
  // Lifetime of array associated with an instance of this class.
  enum Duration {
    PERMANENT,         // Longer than this instance or instance this instance is copied or moved to.
    UNOWNED_TRANSIENT, // Only guaranteed to be as long as this instance.
    OWNED_IN_HEAP      // Dynamically allocated and deleted when this instance is destroyed.
  };

  DurableConstArray() {}

  ~DurableConstArray()
  {
    if (OWNED_IN_HEAP == _duration) {
      for (std::size_t i = 0; i < _size; ++i) {
        _ptr[i].~T();
      }
      delete[] reinterpret_cast<const char *>(_ptr);
    }
  }

  // Use this member function for construction.
  static DurableConstArray
  make(Duration d, T const *ptr, std::size_t size)
  {
    DurableConstArray dc;
    dc._ptr      = size ? ptr : nullptr;
    dc._size     = ptr ? size : 0;
    dc._duration = dc._size ? d : PERMANENT;
    return dc;
  }

  DurableConstArray(const DurableConstArray &that)
  {
    _size = that._size;
    if (PERMANENT == that._duration) {
      _duration = PERMANENT;
      _ptr      = that._ptr;
    } else {
      _duration     = OWNED_IN_HEAP;
      char *p       = new char[sizeof(T) * _size];
      _ptr          = reinterpret_cast<T *>(p);
      std::size_t i = 0;
      try {
        for (; i < _size; ++i, p += sizeof(T)) {
          ::new (p) T(that._ptr[i]);
        }
      } catch (...) {
        while (i) {
          _ptr[--i].~T();
        }
        throw;
      }
    }
  }

  DurableConstArray(DurableConstArray &&that)
  {
    _duration = that._duration;
    _ptr      = that._ptr;
    _size     = that._size;

    ::new (&that) DurableConstArray();
  }

  DurableConstArray &
  operator=(const DurableConstArray &that)
  {
    if (this != &that) {
      this->~DurableConstArray();

      ::new (this) DurableConstArray(that);
    }
    return *this;
  }

  DurableConstArray &
  operator=(DurableConstArray &&that)
  {
    this->~DurableConstArray();

    _duration = that._duration;
    _ptr      = that._ptr;
    _size     = that._size;

    ::new (&that) DurableConstArray();

    return *this;
  }

  Duration
  duration() const
  {
    return _duration;
  }
  T const *
  data() const
  {
    return _ptr;
  }
  std::size_t
  size() const
  {
    return _size;
  }

  friend typename std::enable_if<THasEq, bool>::type
  operator==(DurableConstArray const &a, DurableConstArray const &b)
  {
    if (a._size != b._size) {
      return false;
    }
    if (a._ptr == b._ptr) {
      return true;
    }
    if (!(a._ptr && b._ptr)) {
      return false;
    }
    for (std::size_t i = 0; i < a._size; ++i) {
      if (a._ptr[i] != b._ptr[i]) {
        return false;
      }
    }
    return true;
  }

  friend typename std::enable_if<THasEq, bool>::type
  operator!=(DurableConstArray const &a, DurableConstArray const &b)
  {
    return !(a == b);
  }

private:
  Duration _duration{PERMANENT};
  T const *_ptr{nullptr}; // Array associated with this instance.
  std::size_t _size{0};   // Dimenstion of array.
};

// A Key class for the unordered_map instances below that avoids unnecessary heap use.
class TwoCStrKey
{
public:
  TwoCStrKey() {}

  TwoCStrKey(char const *major, char const *minor) : _major(_make(major)), _minor(_make(minor)) {}

  friend bool
  operator==(TwoCStrKey const &a, TwoCStrKey const &b)
  {
    if (a._major != b._major) {
      return false;
    }
    return a._minor == b._minor;
  }

  std::string_view
  major() const
  {
    return std::string_view(_major.data(), _major.size());
  }
  std::string_view
  minor() const
  {
    return std::string_view(_minor.data(), _minor.size());
  }

  friend struct TwoCStrKeyHash;

private:
  using Comp = DurableConstArray<char, true>;

  Comp _major, _minor;

  Comp
  _make(char const *cStr)
  {
    if (!cStr || !*cStr) {
      return Comp();
    }
    return Comp::make(Comp::UNOWNED_TRANSIENT, cStr, std::strlen(cStr));
  }
};

struct TwoCStrKeyHash {
  std::size_t
  operator()(TwoCStrKey const &k) const
  {
    ATSHash32FNV1a h;

    h.update(k._major.data(), k._major.size());
    h.update(k._minor.data(), k._minor.size());

    // ATSHash32FNV1a::final() does nothing, no need to call.

    return h.get();
  }
};

} // end namespace SSLUtilsImpl

using SSLUtilsTwoCStrKey = SSLUtilsImpl::TwoCStrKey;

struct SSLCertLookup;
struct ssl_ticket_key_block;

/////////////////////////////////////////////////////////////
//
// struct SSLConfigParams
//
// configuration parameters as they appear in the global
// configuration file.
/////////////////////////////////////////////////////////////

typedef void (*init_ssl_ctx_func)(void *, bool);
typedef void (*load_ssl_file_func)(const char *);

struct SSLConfigParams : public ConfigInfo {
  enum SSL_SESSION_CACHE_MODE {
    SSL_SESSION_CACHE_MODE_OFF                 = 0,
    SSL_SESSION_CACHE_MODE_SERVER_OPENSSL_IMPL = 1,
    SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL     = 2
  };

  SSLConfigParams();
  ~SSLConfigParams() override;

  char *serverCertPathOnly;
  char *serverCertChainFilename;
  char *serverKeyPathOnly;
  char *serverCACertFilename;
  char *serverCACertPath;
  char *configFilePath;
  char *dhparamsFile;
  char *cipherSuite;
  char *client_cipherSuite;
  int configExitOnLoadError;
  int clientCertLevel;
  int verify_depth;
  int ssl_session_cache; // SSL_SESSION_CACHE_MODE
  int ssl_session_cache_size;
  int ssl_session_cache_num_buckets;
  int ssl_session_cache_skip_on_contention;
  int ssl_session_cache_timeout;
  int ssl_session_cache_auto_clear;

  char *clientCertPath;
  char *clientCertPathOnly;
  char *clientKeyPath;
  char *clientKeyPathOnly;
  char *clientCACertFilename;
  char *clientCACertPath;
  YamlSNIConfig::Policy verifyServerPolicy;
  YamlSNIConfig::Property verifyServerProperties;
  int client_verify_depth;
  long ssl_ctx_options;
  long ssl_client_ctx_options;

  char *server_tls13_cipher_suites;
  char *client_tls13_cipher_suites;
  char *server_groups_list;
  char *client_groups_list;

  static uint32_t server_max_early_data;
  static uint32_t server_recv_max_early_data;
  static bool server_allow_early_data_params;

  static int ssl_maxrecord;
  static bool ssl_allow_client_renegotiation;

  static bool ssl_ocsp_enabled;
  static int ssl_ocsp_cache_timeout;
  static int ssl_ocsp_request_timeout;
  static int ssl_ocsp_update_period;
  static int ssl_handshake_timeout_in;
  char *ssl_ocsp_response_path_only;

  static size_t session_cache_number_buckets;
  static size_t session_cache_max_bucket_size;
  static bool session_cache_skip_on_lock_contention;

  static IpMap *proxy_protocol_ipmap;

  static init_ssl_ctx_func init_ssl_ctx_cb;
  static load_ssl_file_func load_ssl_file_cb;

  static int async_handshake_enabled;
  static char *engine_conf_file;

  shared_SSL_CTX client_ctx;

  // Client contexts are held by 2-level map:
  // The first level maps from CA bundle file&path to next level map;
  // The second level maps from cert&key to actual SSL_CTX;
  // The second level map owns the client SSL_CTX objects and is responsible for cleaning them up
  using CTX_MAP = std::unordered_map<SSLUtilsTwoCStrKey, shared_SSL_CTX, SSLUtilsImpl::TwoCStrKeyHash>;
  mutable std::unordered_map<SSLUtilsTwoCStrKey, CTX_MAP, SSLUtilsImpl::TwoCStrKeyHash> top_level_ctx_map;
  mutable ink_mutex ctxMapLock;

  shared_SSL_CTX getClientSSL_CTX() const;
  shared_SSL_CTX getCTX(const char *client_cert, const char *key_file, const char *ca_bundle_file,
                        const char *ca_bundle_path) const;

  void cleanupCTXTable();

  void initialize();
  void cleanup();
  void reset();
  void SSLConfigInit(IpMap *global);
};

/////////////////////////////////////////////////////////////
//
// class SSLConfig
//
/////////////////////////////////////////////////////////////

struct SSLConfig {
  static void startup();
  static void reconfigure();
  static SSLConfigParams *acquire();
  static void release(SSLConfigParams *params);
  typedef ConfigProcessor::scoped_config<SSLConfig, SSLConfigParams> scoped_config;

private:
  static int configid;
};

struct SSLCertificateConfig {
  static bool startup();
  static bool reconfigure();
  static SSLCertLookup *acquire();
  static void release(SSLCertLookup *params);

  typedef ConfigProcessor::scoped_config<SSLCertificateConfig, SSLCertLookup> scoped_config;

private:
  static int configid;
};

struct SSLTicketParams : public ConfigInfo {
  ssl_ticket_key_block *default_global_keyblock = nullptr;
  time_t load_time                              = 0;
  char *ticket_key_filename;
  bool LoadTicket(bool &nochange);
  void LoadTicketData(char *ticket_data, int ticket_data_len);
  void cleanup();

  ~SSLTicketParams() override { cleanup(); }
};

struct SSLTicketKeyConfig {
  static void startup();
  static bool reconfigure();
  static bool reconfigure_data(char *ticket_data, int ticket_data_len);

  static SSLTicketParams *
  acquire()
  {
    return static_cast<SSLTicketParams *>(configProcessor.get(configid));
  }

  static void
  release(SSLTicketParams *params)
  {
    if (configid > 0) {
      configProcessor.release(configid, params);
    }
  }

  typedef ConfigProcessor::scoped_config<SSLTicketKeyConfig, SSLTicketParams> scoped_config;

private:
  static int configid;
};

extern SSLSessionCache *session_cache;
