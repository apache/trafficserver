/** @file

  Diags for TLS

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

#include "iocore/net/SSLDiags.h"

#include <openssl/err.h>

#include "P_Net.h"
#include "SSLStats.h"
#include "P_SSLNetVConnection.h"

static DbgCtl ssl_diags_dbg_ctl{"ssl-diag"};

// return true if we have a stat for the error
static bool
increment_ssl_client_error(unsigned long err)
{
  // we only look for LIB_SSL errors atm
  if (ERR_LIB_SSL != ERR_GET_LIB(err)) {
    Metrics::Counter::increment(ssl_rsb.user_agent_other_errors);
    return false;
  }

  // error was in LIB_SSL, now just switch on REASON
  // (we ignore FUNCTION with the prejudice that we don't care what function
  // the error came from, hope that's ok?)
  switch (ERR_GET_REASON(err)) {
  case SSL_R_SSLV3_ALERT_CERTIFICATE_EXPIRED:
    Metrics::Counter::increment(ssl_rsb.user_agent_expired_cert);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_REVOKED:
    Metrics::Counter::increment(ssl_rsb.user_agent_revoked_cert);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN:
    Metrics::Counter::increment(ssl_rsb.user_agent_unknown_cert);
    break;
  case SSL_R_CERTIFICATE_VERIFY_FAILED:
    Metrics::Counter::increment(ssl_rsb.user_agent_cert_verify_failed);
    break;
  case SSL_R_SSLV3_ALERT_BAD_CERTIFICATE:
    Metrics::Counter::increment(ssl_rsb.user_agent_bad_cert);
    break;
  case SSL_R_TLSV1_ALERT_DECRYPTION_FAILED:
    Metrics::Counter::increment(ssl_rsb.user_agent_decryption_failed);
    break;
  case SSL_R_WRONG_VERSION_NUMBER:
    Metrics::Counter::increment(ssl_rsb.user_agent_wrong_version);
    break;
  case SSL_R_TLSV1_ALERT_UNKNOWN_CA:
    Metrics::Counter::increment(ssl_rsb.user_agent_unknown_ca);
    break;
  default:
    Metrics::Counter::increment(ssl_rsb.user_agent_other_errors);
    return false;
  }

  return true;
}

// return true if we have a stat for the error

static bool
increment_ssl_server_error(unsigned long err)
{
  // we only look for LIB_SSL errors atm
  if (ERR_LIB_SSL != ERR_GET_LIB(err)) {
    Metrics::Counter::increment(ssl_rsb.origin_server_other_errors);
    return false;
  }

  // error was in LIB_SSL, now just switch on REASON
  // (we ignore FUNCTION with the prejudice that we don't care what function
  // the error came from, hope that's ok?)
  switch (ERR_GET_REASON(err)) {
  case SSL_R_SSLV3_ALERT_CERTIFICATE_EXPIRED:
    Metrics::Counter::increment(ssl_rsb.origin_server_expired_cert);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_REVOKED:
    Metrics::Counter::increment(ssl_rsb.origin_server_revoked_cert);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN:
    Metrics::Counter::increment(ssl_rsb.origin_server_unknown_cert);
    break;
  case SSL_R_CERTIFICATE_VERIFY_FAILED:
    Metrics::Counter::increment(ssl_rsb.origin_server_cert_verify_failed);
    break;
  case SSL_R_SSLV3_ALERT_BAD_CERTIFICATE:
    Metrics::Counter::increment(ssl_rsb.origin_server_bad_cert);
    break;
  case SSL_R_TLSV1_ALERT_DECRYPTION_FAILED:
    Metrics::Counter::increment(ssl_rsb.origin_server_decryption_failed);
    break;
  case SSL_R_WRONG_VERSION_NUMBER:
    Metrics::Counter::increment(ssl_rsb.origin_server_wrong_version);
    break;
  case SSL_R_TLSV1_ALERT_UNKNOWN_CA:
    Metrics::Counter::increment(ssl_rsb.origin_server_unknown_ca);
    break;
  default:
    Metrics::Counter::increment(ssl_rsb.origin_server_other_errors);
    return false;
  }

  return true;
}

void
SSLDiagnostic(const SourceLocation &loc, bool debug, SSLNetVConnection *vc, const char *fmt, ...)
{
  unsigned long l;
  char buf[256];
  const char *file, *data;
  int line, flags;
  unsigned long es;
  va_list ap;
  ip_text_buffer ip_buf = {'\0'};

  if (vc) {
    ats_ip_ntop(vc->get_remote_addr(), ip_buf, sizeof(ip_buf));
  }

  es = reinterpret_cast<unsigned long>(pthread_self());
#ifdef HAVE_ERR_GET_ERROR_ALL
  while ((l = ERR_get_error_all(&file, &line, nullptr, &data, &flags)) != 0) {
#else
  // ERR_get_error_line_data is going to be deprecated since OpenSSL 3.0.0
  while ((l = ERR_get_error_line_data(&file, &line, &data, &flags)) != 0) {
#endif
    if (debug) {
      if (ssl_diags_dbg_ctl.on()) {
        diags()->print(ssl_diags_dbg_ctl.tag(), DL_Debug, &loc, "SSL::%lu:%s:%s:%d%s%s%s%s", es, ERR_error_string(l, buf), file,
                       line, (flags & ERR_TXT_STRING) ? ":" : "", (flags & ERR_TXT_STRING) ? data : "",
                       vc ? ": peer address is " : "", ip_buf);
      }
    } else {
      diags()->error(DL_Error, &loc, "SSL::%lu:%s:%s:%d%s%s%s%s", es, ERR_error_string(l, buf), file, line,
                     (flags & ERR_TXT_STRING) ? ":" : "", (flags & ERR_TXT_STRING) ? data : "", vc ? ": peer address is " : "",
                     ip_buf);
    }

    // Tally desired stats (only client/server connection stats, not init
    // issues where vc is nullptr)
    if (vc) {
      // get_context() == NET_VCONNECTION_OUT if ats is client (we update server stats)
      if (vc->get_context() == NET_VCONNECTION_OUT) {
        increment_ssl_server_error(l); // update server error stats
      } else {
        increment_ssl_client_error(l); // update client error stat
      }
    }
  }

  va_start(ap, fmt);
  if (debug) {
    if (ssl_diags_dbg_ctl.on()) {
      diags()->print_va(ssl_diags_dbg_ctl.tag(), DL_Debug, &loc, fmt, ap);
    }
  } else {
    diags()->error_va(DL_Error, &loc, fmt, ap);
  }
  va_end(ap);
}

const char *
SSLErrorName(int ssl_error)
{
#ifdef OPENSSL_IS_BORINGSSL
  const char *err_descr = SSL_error_description(ssl_error);
  return err_descr != nullptr ? err_descr : "unknown SSL error";
#else
  // Note: This needs some updates as well for quictls.
  static const char *names[] = {
    "SSL_ERROR_NONE",    "SSL_ERROR_SSL",         "SSL_ERROR_WANT_READ",    "SSL_ERROR_WANT_WRITE", "SSL_ERROR_WANT_X509_LOOKUP",
    "SSL_ERROR_SYSCALL", "SSL_ERROR_ZERO_RETURN", "SSL_ERROR_WANT_CONNECT", "SSL_ERROR_WANT_ACCEPT"};

  if (ssl_error < 0 || ssl_error >= static_cast<int>(countof(names))) {
    return "unknown SSL error";
  }

  return names[ssl_error];
#endif
}

void
SSLDebugBufferPrint_(const char *buffer, unsigned buflen, const char *message)
{
  if (message != nullptr) {
    fprintf(stdout, "%s\n", message);
  }
  for (unsigned ii = 0; ii < buflen; ii++) {
    putc(buffer[ii], stdout);
  }
  putc('\n', stdout);
}
