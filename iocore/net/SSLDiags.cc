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

#include "SSLDiags.h"

#include <openssl/err.h>

#include "P_Net.h"
#include "SSLStats.h"

// return true if we have a stat for the error
static bool
increment_ssl_client_error(unsigned long err)
{
  // we only look for LIB_SSL errors atm
  if (ERR_LIB_SSL != ERR_GET_LIB(err)) {
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_other_errors_stat);
    return false;
  }

  // error was in LIB_SSL, now just switch on REASON
  // (we ignore FUNCTION with the prejudice that we don't care what function
  // the error came from, hope that's ok?)
  switch (ERR_GET_REASON(err)) {
  case SSL_R_SSLV3_ALERT_CERTIFICATE_EXPIRED:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_expired_cert_stat);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_REVOKED:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_revoked_cert_stat);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_unknown_cert_stat);
    break;
  case SSL_R_CERTIFICATE_VERIFY_FAILED:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_cert_verify_failed_stat);
    break;
  case SSL_R_SSLV3_ALERT_BAD_CERTIFICATE:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_bad_cert_stat);
    break;
  case SSL_R_TLSV1_ALERT_DECRYPTION_FAILED:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_decryption_failed_stat);
    break;
  case SSL_R_WRONG_VERSION_NUMBER:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_wrong_version_stat);
    break;
  case SSL_R_TLSV1_ALERT_UNKNOWN_CA:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_unknown_ca_stat);
    break;
  default:
    SSL_INCREMENT_DYN_STAT(ssl_user_agent_other_errors_stat);
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
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_other_errors_stat);
    return false;
  }

  // error was in LIB_SSL, now just switch on REASON
  // (we ignore FUNCTION with the prejudice that we don't care what function
  // the error came from, hope that's ok?)
  switch (ERR_GET_REASON(err)) {
  case SSL_R_SSLV3_ALERT_CERTIFICATE_EXPIRED:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_expired_cert_stat);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_REVOKED:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_revoked_cert_stat);
    break;
  case SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_unknown_cert_stat);
    break;
  case SSL_R_CERTIFICATE_VERIFY_FAILED:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_cert_verify_failed_stat);
    break;
  case SSL_R_SSLV3_ALERT_BAD_CERTIFICATE:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_bad_cert_stat);
    break;
  case SSL_R_TLSV1_ALERT_DECRYPTION_FAILED:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_decryption_failed_stat);
    break;
  case SSL_R_WRONG_VERSION_NUMBER:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_wrong_version_stat);
    break;
  case SSL_R_TLSV1_ALERT_UNKNOWN_CA:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_unknown_ca_stat);
    break;
  default:
    SSL_INCREMENT_DYN_STAT(ssl_origin_server_other_errors_stat);
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
  while ((l = ERR_get_error_line_data(&file, &line, &data, &flags)) != 0) {
    if (debug) {
      if (unlikely(diags->on())) {
        diags->log("ssl-diag", DL_Debug, &loc, "SSL::%lu:%s:%s:%d%s%s%s%s", es, ERR_error_string(l, buf), file, line,
                   (flags & ERR_TXT_STRING) ? ":" : "", (flags & ERR_TXT_STRING) ? data : "", vc ? ": peer address is " : "",
                   ip_buf);
      }
    } else {
      diags->error(DL_Error, &loc, "SSL::%lu:%s:%s:%d%s%s%s%s", es, ERR_error_string(l, buf), file, line,
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
    diags->log_va("ssl-diag", DL_Debug, &loc, fmt, ap);
  } else {
    diags->error_va(DL_Error, &loc, fmt, ap);
  }
  va_end(ap);
}

const char *
SSLErrorName(int ssl_error)
{
  static const char *names[] = {
    "SSL_ERROR_NONE",    "SSL_ERROR_SSL",         "SSL_ERROR_WANT_READ",    "SSL_ERROR_WANT_WRITE", "SSL_ERROR_WANT_X509_LOOKUP",
    "SSL_ERROR_SYSCALL", "SSL_ERROR_ZERO_RETURN", "SSL_ERROR_WANT_CONNECT", "SSL_ERROR_WANT_ACCEPT"};

  if (ssl_error < 0 || ssl_error >= static_cast<int>(countof(names))) {
    return "unknown SSL error";
  }

  return names[ssl_error];
}

void
SSLDebugBufferPrint(const char *tag, const char *buffer, unsigned buflen, const char *message)
{
  if (is_debug_tag_set(tag)) {
    if (message != nullptr) {
      fprintf(stdout, "%s\n", message);
    }
    for (unsigned ii = 0; ii < buflen; ii++) {
      putc(buffer[ii], stdout);
    }
    putc('\n', stdout);
  }
}
