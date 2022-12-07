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

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "ats_context.h"
#include "ats_wasm.h"

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/wasm.h"
#include "src/shared_data.h"
#include "src/shared_queue.h"

namespace ats_wasm
{
// utiltiy function for properties
static void
print_address(struct sockaddr const *ip, std::string *result)
{
  if (ip != nullptr) {
    char cip[128];
    int64_t port = 0;
    if (ip->sa_family == AF_INET) {
      const auto *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(ip);
      inet_ntop(AF_INET, &s_sockaddr_in->sin_addr, cip, sizeof(cip));
      port = s_sockaddr_in->sin_port;
    } else {
      const auto *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(ip);
      inet_ntop(AF_INET6, &s_sockaddr_in6->sin6_addr, cip, sizeof(cip));
      port = s_sockaddr_in6->sin6_port;
    }
    TSDebug(WASM_DEBUG_TAG, "[%s] property retrieval - address: %.*s", __FUNCTION__, static_cast<int>(sizeof(cip)), cip);
    std::string cip_str(cip);
    result->assign(cip_str + ":" + std::to_string(port));
  } else {
    *result = pv_empty;
  }
}

static void
print_port(struct sockaddr const *ip, std::string *result)
{
  if (ip != nullptr) {
    int64_t port = 0;
    if (ip->sa_family == AF_INET) {
      const auto *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(ip);
      port                      = s_sockaddr_in->sin_port;
    } else {
      const auto *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(ip);
      port                       = s_sockaddr_in6->sin6_port;
    }
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for source port: %d", __FUNCTION__, static_cast<int>(port));
    result->assign(reinterpret_cast<const char *>(&port), sizeof(int64_t));
  } else {
    *result = pv_empty;
  }
}

static void
print_certificate(std::string *result, X509_NAME *name)
{
  if (name == nullptr) {
    *result = pv_empty;
    return;
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (bio == nullptr) {
    *result = pv_empty;
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    int64_t len = 0;
    char *ptr   = nullptr;
    len         = BIO_get_mem_data(bio, &ptr);
    result->assign(ptr, len);
    TSDebug(WASM_DEBUG_TAG, "print SSL certificate %.*s", static_cast<int>(len), ptr);
  }

  BIO_free(bio);
}

static void
print_san_certificate(std::string *result, X509 *cert, int type)
{
  int ext_ndx = X509_get_ext_by_NID(cert, NID_subject_alt_name, -1);
  if (ext_ndx >= 0) {
    X509_EXTENSION *ext               = nullptr;
    STACK_OF(GENERAL_NAME) *alt_names = nullptr;
    GENERAL_NAME *gen_name            = nullptr;

    ext       = X509_get_ext(cert, ext_ndx);
    alt_names = static_cast<stack_st_GENERAL_NAME *>(X509V3_EXT_d2i(ext));
    if (alt_names != nullptr) {
      int num    = sk_GENERAL_NAME_num(alt_names);
      bool found = false;
      for (int i = 0; i < num; i++) {
        gen_name = sk_GENERAL_NAME_value(alt_names, i);
        if (gen_name->type == type) {
          char *dnsname   = reinterpret_cast<char *>(ASN1_STRING_data(gen_name->d.dNSName));
          int dnsname_len = ASN1_STRING_length(gen_name->d.dNSName);
          result->assign(dnsname, dnsname_len);
          found = true;
          break;
        }
      }
      if (!found) {
        *result = pv_empty;
      }
      sk_GENERAL_NAME_free(alt_names);
    } else {
      *result = pv_empty;
    }
  } else {
    *result = pv_empty;
  }
}

static bool
get_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view v, std::string *result)
{
  const char *key       = v.data();
  int key_len           = v.size();
  const char *val       = nullptr;
  int val_len           = 0;
  std::string res       = "";
  TSMLoc field_loc      = nullptr;
  TSMLoc next_field_loc = nullptr;
  bool found            = false;

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, key, key_len);
  if (field_loc != TS_NULL_MLOC) {
    while (field_loc != TS_NULL_MLOC) {
      val            = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &val_len);
      next_field_loc = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      res.append(val, val_len);
      if (next_field_loc != TS_NULL_MLOC) {
        res.append(",", 1);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = next_field_loc;
    }
    found = true;
  }
  *result = res;
  return found;
}

static void
set_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view v, std::string_view serialized_value)
{
  bool remove = false;

  if (serialized_value == "") {
    remove = true;
  }

  TSMLoc field_loc = nullptr;
  TSMLoc tmp       = nullptr;
  bool first       = true;

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, v.data(), v.size());

  if (remove) {
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (field_loc != TS_NULL_MLOC) {
    first = true;
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      if (first) {
        first = false;
        TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, serialized_value.data(), serialized_value.size());
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, v.data(), v.size(), &field_loc) != TS_SUCCESS) {
    TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
  } else {
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, serialized_value.data(), serialized_value.size());
    TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
  }

  if (field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

Context::Context() : ContextBase() {}

Context::Context(Wasm *wasm) : ContextBase(wasm) {}

Context::Context(Wasm *wasm, const std::shared_ptr<PluginBase> &plugin) : ContextBase(wasm, plugin) {}

// NB: wasm can be nullptr if it failed to be created successfully.
Context::Context(Wasm *wasm, uint32_t parent_context_id, const std::shared_ptr<PluginBase> &plugin) : ContextBase(wasm)
{
  id_                = (wasm_ != nullptr) ? wasm_->allocContextId() : 0;
  parent_context_id_ = parent_context_id;
  plugin_            = plugin;
  if (wasm_ != nullptr) {
    parent_context_ = wasm_->getContext(parent_context_id_);
  }
}

// utility functions for the extended class
Wasm *
Context::wasm() const
{
  return static_cast<Wasm *>(wasm_);
}

Context *
Context::parent_context() const
{
  return static_cast<Context *>(parent_context_);
}

Context *
Context::root_context() const
{
  const ContextBase *previous = this;
  ContextBase *parent         = parent_context_;
  while (parent != previous) {
    previous = parent;
    parent   = parent->parent_context();
  }
  return static_cast<Context *>(parent);
}

void
Context::initialize(TSHttpTxn txnp)
{
  txnp_ = txnp;
}

void
Context::initialize(TSCont cont)
{
  scheduler_cont_ = cont;
}

TSHttpTxn
Context::txnp()
{
  return txnp_;
}

TSCont
Context::scheduler_cont()
{
  return scheduler_cont_;
}

void
Context::error(std::string_view message)
{
  TSError("%.*s", static_cast<int>(message.size()), message.data());
  abort();
}

// local reply handler
void
Context::onLocalReply()
{
  if (local_reply_) {
    if (txnp_ == nullptr) {
      return;
    }

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientRespGet(txnp_, &bufp, &hdr_loc) != TS_SUCCESS) {
      return;
    }

    // set local reply reason
    if (!local_reply_details_.empty()) {
      TSHttpHdrReasonSet(bufp, hdr_loc, local_reply_details_.data(), local_reply_details_.size());
    }

    // set local reply headers
    for (auto &p : local_reply_headers_) {
      std::string key(p.first);
      std::string value(p.second);

      auto *loc = TSMimeHdrFieldFind(bufp, hdr_loc, key.data(), static_cast<int>(key.size()));
      if (loc != TS_NULL_MLOC) {
        int first = 1;
        while (loc != TS_NULL_MLOC) {
          auto *tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, loc);
          if (first != 0) {
            first = 0;
            TSMimeHdrFieldValueStringSet(bufp, hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
          } else {
            TSMimeHdrFieldDestroy(bufp, hdr_loc, loc);
          }
          TSHandleMLocRelease(bufp, hdr_loc, loc);
          loc = tmp;
        }
      } else if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, key.data(), static_cast<int>(key.size()), &loc) != TS_SUCCESS) {
        TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        return;
      } else {
        TSMimeHdrFieldValueStringSet(bufp, hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        TSMimeHdrFieldAppend(bufp, hdr_loc, loc);
      }

      if (loc != TS_NULL_MLOC) {
        TSHandleMLocRelease(bufp, hdr_loc, loc);
      }
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }
}

//
// General functions
//
WasmResult
Context::log(uint32_t level, std::string_view message)
{
  auto l = static_cast<LogLevel>(level);
  switch (l) {
  case LogLevel::trace:
    TSDebug(WASM_DEBUG_TAG, "wasm trace log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  case LogLevel::debug:
    TSDebug(WASM_DEBUG_TAG, "wasm debug log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  case LogLevel::info:
    TSDebug(WASM_DEBUG_TAG, "wasm info log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  case LogLevel::warn:
    TSDebug(WASM_DEBUG_TAG, "wasm warn log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  case LogLevel::error:
    TSDebug(WASM_DEBUG_TAG, "wasm error log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  case LogLevel::critical:
    TSDebug(WASM_DEBUG_TAG, "wasm critical log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()),
            message.data());
    return WasmResult::Ok;
  default: // e.g. off
    return unimplemented();
  }
  return unimplemented();
}

uint64_t
Context::getCurrentTimeNanoseconds()
{
  return TShrtime();
}

uint64_t
Context::getMonotonicTimeNanoseconds()
{
  return TShrtime();
}

std::string_view
Context::getConfiguration()
{
  return plugin_->plugin_configuration_;
}

WasmResult
Context::setTimerPeriod(std::chrono::milliseconds period, uint32_t *timer_token_ptr)
{
  Wasm *wasm            = this->wasm();
  Context *root_context = this->root_context();
  TSMutexLock(wasm->mutex());
  if (!wasm->existsTimerPeriod(root_context->id())) {
    TSDebug(WASM_DEBUG_TAG, "[%s] no previous timer period set", __FUNCTION__);
    TSCont contp = root_context->scheduler_cont();
    if (contp != nullptr) {
      TSDebug(WASM_DEBUG_TAG, "[%s] scheduling continuation for timer", __FUNCTION__);
      TSContDataSet(contp, root_context);
      TSContScheduleOnPool(contp, static_cast<TSHRTime>(period.count()), TS_THREAD_POOL_NET);
    }
  }

  wasm->setTimerPeriod(root_context->id(), period);
  *timer_token_ptr = 0;
  TSMutexUnlock(wasm->mutex());
  return WasmResult::Ok;
}

BufferInterface *
Context::getBuffer(WasmBufferType type)
{
  switch (type) {
  case WasmBufferType::VmConfiguration:
    return buffer_.set(wasm_->vm_configuration());
  case WasmBufferType::PluginConfiguration:
    return buffer_.set(plugin_->plugin_configuration_);
  case WasmBufferType::CallData:
  case WasmBufferType::HttpRequestBody:
  case WasmBufferType::HttpResponseBody:
  case WasmBufferType::NetworkDownstreamData:
  case WasmBufferType::NetworkUpstreamData:
  case WasmBufferType::HttpCallResponseBody:
  case WasmBufferType::GrpcReceiveBuffer:
  default:
    unimplemented();
    return nullptr;
  }
}

// Metrics
WasmResult
Context::defineMetric(uint32_t metric_type, std::string_view name, uint32_t *metric_id_ptr)
{
  int idp                    = 0;
  TSStatSync ats_metric_type = TS_STAT_SYNC_COUNT;
  auto type                  = static_cast<MetricType>(metric_type);
  switch (type) {
  case MetricType::Counter:
    ats_metric_type = TS_STAT_SYNC_COUNT;
    break;
  case MetricType::Gauge:
    ats_metric_type = TS_STAT_SYNC_SUM;
    break;
  case MetricType::Histogram:
    ats_metric_type = TS_STAT_SYNC_AVG;
    break;
  default:
    TSError("[wasm][%s] Invalid metric type", __FUNCTION__);
    return WasmResult::BadArgument;
  }

  if (TSStatFindName(name.data(), &idp) == TS_ERROR) {
    idp = TSStatCreate(name.data(), TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, ats_metric_type);
    TSDebug(WASM_DEBUG_TAG, "[%s] creating stat: %.*s", __FUNCTION__, static_cast<int>(name.size()), name.data());
    *metric_id_ptr = idp;
  } else {
    TSError("[wasm][%s] Metric already exists", __FUNCTION__);
    *metric_id_ptr = idp;
  }

  return WasmResult::Ok;
}

WasmResult
Context::incrementMetric(uint32_t metric_id, int64_t offset)
{
  TSStatIntIncrement(metric_id, offset);
  return WasmResult::Ok;
}

WasmResult
Context::recordMetric(uint32_t metric_id, uint64_t value)
{
  TSStatIntSet(metric_id, value);
  return WasmResult::Ok;
}

WasmResult
Context::getMetric(uint32_t metric_id, uint64_t *value_ptr)
{
  *value_ptr = TSStatIntGet(metric_id);
  return WasmResult::Ok;
}

// Properties
WasmResult
Context::getProperty(std::string_view path, std::string *result)
{
  if (path.substr(0, p_plugin_root_id.size()) == p_plugin_root_id) {
    *result = this->plugin_->root_id_;
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for plugin_root_id: %.*s", __FUNCTION__, static_cast<int>((*result).size()),
            (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_plugin_name.size()) == p_plugin_name) {
    *result = this->plugin_->name_;
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for plugin_name: %.*s", __FUNCTION__, static_cast<int>((*result).size()),
            (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_plugin_vm_id.size()) == p_plugin_vm_id) {
    *result = this->plugin_->vm_id_;
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for plugin_vm_id: %.*s", __FUNCTION__, static_cast<int>((*result).size()),
            (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_node.size()) == p_node) {
    *result = pv_empty;
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for node property: empty string for now", __FUNCTION__);
    return WasmResult::Ok;
  } else if (path.substr(0, p_source_address.size()) == p_source_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *client_ip = nullptr;
    client_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_address(client_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_source_port.size()) == p_source_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *client_ip = nullptr;
    client_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_port(client_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_destination_address.size()) == p_destination_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnIncomingAddrGet(txnp_);
    print_address(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_destination_port.size()) == p_destination_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnIncomingAddrGet(txnp_);
    print_port(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_mtls.size()) == p_connection_mtls) {
    bool m = false;
    if (txnp_ == nullptr) {
      result->assign(reinterpret_cast<const char *>(&m), sizeof(bool));
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      m = true;
      X509_free(cert);
    }
    result->assign(reinterpret_cast<const char *>(&m), sizeof(bool));
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_requested_server_name.size()) == p_connection_requested_server_name) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp             = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn        = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj     = TSVConnSslConnectionGet(client_conn);
    SSL *ssl                   = reinterpret_cast<SSL *>(sslobj);
    char const *const sni_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (sni_name != nullptr) {
      result->assign(sni_name);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_tls_version.size()) == p_connection_tls_version) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    const char *ssl_protocol = "-";
    TSHttpSsn ssnp           = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn      = TSHttpSsnClientVConnGet(ssnp);

    if (TSVConnIsSsl(client_conn) != 0) {
      ssl_protocol = TSVConnSslProtocolGet(client_conn);
    }

    result->assign(ssl_protocol);
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_subject_local_certificate.size()) == p_connection_subject_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_subject_peer_certificate.size()) == p_connection_subject_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_dns_san_local_certificate.size()) == p_connection_dns_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_dns_san_peer_certificate.size()) == p_connection_dns_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_uri_san_local_certificate.size()) == p_connection_uri_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_uri_san_peer_certificate.size()) == p_connection_uri_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_address.size()) == p_upstream_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *server_ip = nullptr;
    server_ip                        = TSHttpTxnServerAddrGet(txnp_);
    print_address(server_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_port.size()) == p_upstream_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *server_ip = nullptr;
    server_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_port(server_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_local_address.size()) == p_upstream_local_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnOutgoingAddrGet(txnp_);
    print_address(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_local_port.size()) == p_upstream_local_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnOutgoingAddrGet(txnp_);
    print_port(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_tls_version.size()) == p_upstream_tls_version) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    const char *ssl_protocol = "-";
    TSHttpSsn ssnp           = TSHttpTxnSsnGet(txnp_);
    TSVConn server_conn      = TSHttpSsnServerVConnGet(ssnp);

    if (TSVConnIsSsl(server_conn) != 0) {
      ssl_protocol = TSVConnSslProtocolGet(server_conn);
    }

    result->assign(ssl_protocol);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_subject_local_certificate.size()) == p_upstream_subject_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_subject_peer_certificate.size()) == p_upstream_subject_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_dns_san_local_certificate.size()) == p_upstream_dns_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_dns_san_peer_certificate.size()) == p_upstream_dns_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_uri_san_local_certificate.size()) == p_upstream_uri_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_uri_san_peer_certificate.size()) == p_upstream_uri_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_peer_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_path.size()) == p_request_path) {
    TSMBuffer bufp    = nullptr;
    TSMLoc hdr_loc    = nullptr;
    TSMLoc url_loc    = nullptr;
    const char *path  = nullptr;
    int path_len      = 0;
    const char *query = nullptr;
    int query_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        path = TSUrlPathGet(bufp, url_loc, &path_len);
        std::string path_str(path, path_len);

        query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
        std::string query_str(query, query_len);

        if (query_len > 0) {
          result->assign("/" + path_str + "?" + query_str);
        } else {
          result->assign("/" + path_str);
        }
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_url_path.size()) == p_request_url_path) {
    TSMBuffer bufp   = nullptr;
    TSMLoc hdr_loc   = nullptr;
    TSMLoc url_loc   = nullptr;
    const char *path = nullptr;
    int path_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        path = TSUrlPathGet(bufp, url_loc, &path_len);
        std::string path_str(path, path_len);
        result->assign("/" + path_str);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_host.size()) == p_request_host) {
    TSMBuffer bufp   = nullptr;
    TSMLoc hdr_loc   = nullptr;
    TSMLoc url_loc   = nullptr;
    const char *host = nullptr;
    int host_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        host = TSUrlHostGet(bufp, url_loc, &host_len);
        if (host_len == 0) {
          const char *key   = "Host";
          const char *l_key = "host";
          int key_len       = 4;

          TSMLoc field_loc = nullptr;

          field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, key, key_len);
          if (field_loc != nullptr) {
            host = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &host_len);
            TSHandleMLocRelease(bufp, hdr_loc, field_loc);
          } else {
            field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, l_key, key_len);
            if (field_loc != nullptr) {
              host = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &host_len);
              TSHandleMLocRelease(bufp, hdr_loc, field_loc);
            }
          }
        }
        TSDebug(WASM_DEBUG_TAG, "[%s] request host value(%d): %.*s", __FUNCTION__, host_len, host_len, host);
        result->assign(host, host_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_scheme.size()) == p_request_scheme) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    TSMLoc url_loc     = nullptr;
    const char *scheme = nullptr;
    int scheme_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        scheme = TSUrlSchemeGet(bufp, url_loc, &scheme_len);
        result->assign(scheme, scheme_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_method.size()) == p_request_method) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    const char *method = nullptr;
    int method_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
      result->assign(method, method_len);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_query.size()) == p_request_query) {
    TSMBuffer bufp    = nullptr;
    TSMLoc hdr_loc    = nullptr;
    TSMLoc url_loc    = nullptr;
    const char *query = nullptr;
    int query_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
        result->assign(query, query_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_referer.size()) == p_request_referer) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, "Referer", result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_useragent.size()) == p_request_useragent) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, "User-Agent", result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_id.size()) == p_request_id) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      bool found = get_header(bufp, hdr_loc, "x-request-id", result);
      if (!found) {
        uint64_t id = TSHttpTxnIdGet(txnp_);
        result->assign(std::to_string(id));
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      uint64_t id = TSHttpTxnIdGet(txnp_);
      result->assign(std::to_string(id));
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_headers.size()) == p_request_headers) {
    std::string_view key_sv = path.substr(p_request_headers.size(), path.size() - p_request_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, key_sv, result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_protocol.size()) == p_request_protocol) {
    if (TSHttpTxnClientProtocolStackContains(txnp_, "h2") != nullptr) {
      *result = pv_http2;
    } else if (TSHttpTxnClientProtocolStackContains(txnp_, "http/1.0") != nullptr) {
      *result = pv_http10;
    } else if (TSHttpTxnClientProtocolStackContains(txnp_, "http/1.1") != nullptr) {
      *result = pv_http11;
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_time.size()) == p_request_time) {
    TSHRTime epoch = 0;
    if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_START, &epoch)) {
      double timestamp = static_cast<double>(epoch) / 1000000000;
      result->assign(reinterpret_cast<const char *>(&timestamp), sizeof(double));
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_duration.size()) == p_request_duration) {
    TSHRTime value = 0;
    TSHRTime epoch = 0;

    if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_START, &epoch)) {
      if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_FINISH, &value)) {
        double duration = static_cast<double>((value - epoch)) / 1000000000;
        result->assign(reinterpret_cast<const char *>(&duration), sizeof(double));
        return WasmResult::Ok;
      }
    }
    *result = pv_empty;
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_size.size()) == p_request_size) {
    int64_t bytes = TSHttpTxnClientReqBodyBytesGet(txnp_);
    result->assign(reinterpret_cast<const char *>(&bytes), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_total_size.size()) == p_request_total_size) {
    int h_bytes     = TSHttpTxnClientReqHdrBytesGet(txnp_);
    int64_t b_bytes = TSHttpTxnClientReqBodyBytesGet(txnp_);
    int64_t total   = h_bytes + b_bytes;
    result->assign(reinterpret_cast<const char *>(&total), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_code.size()) == p_response_code) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    int status     = 0;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      status = TSHttpHdrStatusGet(bufp, hdr_loc);
      result->assign(reinterpret_cast<const char *>(&status), sizeof(int));
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_code_details.size()) == p_response_code_details) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    const char *reason = nullptr;
    int reason_len     = 0;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      reason = TSHttpHdrReasonGet(bufp, hdr_loc, &reason_len);
      result->assign(reason, reason_len);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_headers.size()) == p_response_headers) {
    std::string_view key_sv = path.substr(p_response_headers.size(), path.size() - p_response_headers.size() - 1);
    TSMBuffer bufp          = nullptr;
    TSMLoc hdr_loc          = nullptr;
    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, key_sv, result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_size.size()) == p_response_size) {
    int64_t bytes = TSHttpTxnServerRespBodyBytesGet(txnp_);
    result->assign(reinterpret_cast<const char *>(&bytes), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_total_size.size()) == p_response_total_size) {
    int h_bytes     = TSHttpTxnServerRespHdrBytesGet(txnp_);
    int64_t b_bytes = TSHttpTxnServerRespBodyBytesGet(txnp_);
    int64_t total   = h_bytes + b_bytes;
    result->assign(reinterpret_cast<const char *>(&total), sizeof(int64_t));
    return WasmResult::Ok;
  } else {
    *result = pv_empty;
    TSDebug(WASM_DEBUG_TAG, "[%s] looking for unknown property: empty string", __FUNCTION__);
    return WasmResult::Ok;
  }
}

WasmResult
Context::setProperty(std::string_view key, std::string_view serialized_value)
{
  if (key.substr(0, p_request_url_path.size()) == p_request_url_path) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;
    std::string_view result;

    if (serialized_value.substr(0, 1) == "/") {
      result = serialized_value.substr(1);
    } else {
      result = serialized_value;
    }

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlPathSet(bufp, url_loc, result.data(), result.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_host.size()) == p_request_host) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlHostSet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_scheme.size()) == p_request_scheme) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlSchemeSet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_method.size()) == p_request_method) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      TSHttpHdrMethodSet(bufp, hdr_loc, serialized_value.data(), serialized_value.size());
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_query.size()) == p_request_query) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlHttpQuerySet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_headers.size()) == p_request_headers) {
    std::string_view key_sv = key.substr(p_request_headers.size(), key.size() - p_request_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      set_header(bufp, hdr_loc, key_sv, serialized_value);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_code.size()) == p_response_code) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      int64_t *status = reinterpret_cast<int64_t *>(const_cast<char *>(serialized_value.data()));
      TSHttpHdrStatusSet(bufp, hdr_loc, static_cast<TSHttpStatus>(*status));
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_code_details.size()) == p_response_code_details) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      TSHttpHdrReasonSet(bufp, hdr_loc, serialized_value.data(), serialized_value.size());
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_headers.size()) == p_response_headers) {
    std::string_view key_sv = key.substr(p_response_headers.size(), key.size() - p_response_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      set_header(bufp, hdr_loc, key_sv, serialized_value);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  }
  return WasmResult::Ok;
}

// send pre-made response
WasmResult
Context::sendLocalResponse(uint32_t response_code, std::string_view body_text, Pairs additional_headers,
                           GrpcStatusCode /* grpc_status */, std::string_view details)
{
  if (txnp_ == nullptr) {
    TSError("[wasm][%s] Can't send local response without a transaction", __FUNCTION__);
    return WasmResult::InternalFailure;
  } else {
    TSHttpTxnStatusSet(txnp_, static_cast<TSHttpStatus>(response_code));

    if (body_text.size() > 0) {
      TSHttpTxnErrorBodySet(txnp_, TSstrndup(body_text.data(), body_text.size()), body_text.size(),
                            nullptr); // Defaults to text/html
    }

    local_reply_headers_ = additional_headers;
    local_reply_details_ = details;
    local_reply_         = true;
  }
  return WasmResult::Ok;
}

WasmResult
Context::getSharedData(std::string_view key, std::pair<std::string, uint32_t> *data)
{
  return proxy_wasm::getGlobalSharedData().get(wasm_->vm_id(), key, data);
}

// Header/Trailer/Metadata Maps
WasmResult
Context::addHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  auto *field_loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
  if (TS_NULL_MLOC == field_loc) {
    if (TS_SUCCESS != TSMimeHdrFieldCreateNamed(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()), &field_loc)) {
      TSError("[wasm][%s] Cannot create named field", __FUNCTION__);
      return WasmResult::InternalFailure;
    }
  }
  if (TS_SUCCESS ==
      TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, field_loc, -1, value.data(), static_cast<int>(value.size()))) {
    TSMimeHdrFieldAppend(map.bufp, map.hdr_loc, field_loc);
    TSHandleMLocRelease(map.bufp, map.hdr_loc, field_loc);
    return WasmResult::Ok;
  } else {
    TSError("[wasm][%s] Cannot set field value", __FUNCTION__);
    TSHandleMLocRelease(map.bufp, map.hdr_loc, field_loc);
    return WasmResult::InternalFailure;
  }
}

WasmResult
Context::getHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    if (TS_NULL_MLOC != loc) {
      int vlen = 0;
      // TODO: add support for dups
      auto *v = TSMimeHdrFieldValueStringGet(map.bufp, map.hdr_loc, loc, 0, &vlen);
      std::string_view temp(v, vlen);
      *result = temp;
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
      return WasmResult::Ok;
    } else {
      *result = "";
      return WasmResult::Ok;
    }
  }
  return WasmResult::NotFound;
}

WasmResult
Context::getHeaderMapPairs(WasmHeaderMapType type, Pairs *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  int num = map.size();

  result->reserve(num);
  for (int i = 0; i < num; i++) {
    auto *loc = TSMimeHdrFieldGet(map.bufp, map.hdr_loc, i);
    int nlen  = 0;
    auto *n   = TSMimeHdrFieldNameGet(map.bufp, map.hdr_loc, loc, &nlen);
    int vlen  = 0;
    // TODO: add support for dups.
    auto *v = TSMimeHdrFieldValueStringGet(map.bufp, map.hdr_loc, loc, 0, &vlen);
    result->push_back(
      std::make_pair(std::string_view(n, static_cast<size_t>(nlen)), std::string_view(v, static_cast<size_t>(vlen))));
    TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
  }
  return WasmResult::Ok;
}

WasmResult
Context::setHeaderMapPairs(WasmHeaderMapType type, const Pairs &pairs)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }

  for (const auto &p : pairs) {
    std::string key(p.first);
    std::string value(p.second);

    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    if (loc != TS_NULL_MLOC) {
      int first = 1;
      while (loc != TS_NULL_MLOC) {
        auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
        if (first != 0) {
          first = 0;
          TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        } else {
          TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
        }
        TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
        loc = tmp;
      }
    } else if (TSMimeHdrFieldCreateNamed(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()), &loc) != TS_SUCCESS) {
      TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
      return WasmResult::InternalFailure;
    } else {
      TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
      TSMimeHdrFieldAppend(map.bufp, map.hdr_loc, loc);
    }

    if (loc != TS_NULL_MLOC) {
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
    }
  }

  return WasmResult::Ok;
}

WasmResult
Context::removeHeaderMapValue(WasmHeaderMapType type, std::string_view key)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    while (loc != TS_NULL_MLOC) {
      auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
      TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
      loc = tmp;
    }
  }

  return WasmResult::Ok;
}

WasmResult
Context::replaceHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));

    if (loc != TS_NULL_MLOC) {
      int first = 1;
      while (loc != TS_NULL_MLOC) {
        auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
        if (first != 0) {
          first = 0;
          TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        } else {
          TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
        }
        TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
        loc = tmp;
      }
    }
  }
  return WasmResult::Ok;
}

WasmResult
Context::getHeaderMapSize(WasmHeaderMapType type, uint32_t *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  *result = TSMimeHdrLengthGet(map.bufp, map.hdr_loc);
  return WasmResult::Ok;
}

HeaderMap
Context::getHeaderMap(WasmHeaderMapType type)
{
  HeaderMap map;
  switch (type) {
  case WasmHeaderMapType::RequestHeaders:
    if (txnp_ == nullptr) {
      return {};
    }
    if (TSHttpTxnClientReqGet(txnp_, &map.bufp, &map.hdr_loc) == TS_SUCCESS) {
      return map;
    }
    return {};
  case WasmHeaderMapType::RequestTrailers:
    return {};
  case WasmHeaderMapType::ResponseHeaders:
    if (txnp_ == nullptr) {
      return {};
    }
    if (TSHttpTxnServerRespGet(txnp_, &map.bufp, &map.hdr_loc) == TS_SUCCESS) {
      return map;
    }
    return {};
  case WasmHeaderMapType::ResponseTrailers:
    return {};
  default:
  case WasmHeaderMapType::GrpcReceiveTrailingMetadata:
  case WasmHeaderMapType::GrpcReceiveInitialMetadata:
  case WasmHeaderMapType::HttpCallResponseHeaders:
  case WasmHeaderMapType::HttpCallResponseTrailers:
    return {};
  }
}

} // namespace ats_wasm
