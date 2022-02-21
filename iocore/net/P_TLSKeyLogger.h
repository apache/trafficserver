/**

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

#pragma once

#ifndef OPENSSL_IS_BORINGSSL
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>

#include <memory>
#include <mutex>
#include <shared_mutex>

/** A class for handling TLS secrets logging. */
class TLSKeyLogger
{
public:
  TLSKeyLogger(const TLSKeyLogger &) = delete;
  TLSKeyLogger &operator=(const TLSKeyLogger &) = delete;

  ~TLSKeyLogger()
  {
    std::unique_lock lock{_mutex};
    close_keylog_file();
  }

  /** A callback for TLS secret key logging.
   *
   * This is the callback registered with OpenSSL's SSL_CTX_set_keylog_callback
   * to log TLS secrets if the user enabled that feature. For more information
   * about this callback, see OpenSSL's documentation of
   * SSL_CTX_set_keylog_callback.
   *
   * @param[in] ssl The SSL object associated with the connection.
   * @param[in] line The line to place in the keylog file.
   */
  static void
  ssl_keylog_cb(const SSL *ssl, const char *line)
  {
    instance().log(line);
  }

  /** Return whether TLS key logging is enabled.
   *
   * @return True if TLS session key logging is enabled, false otherwise.
   */
  static bool
  is_enabled()
  {
    return instance()._fd >= 0;
  }

  /** Enable keylogging.
   *
   * @param[in] keylog_file The path to the file to log TLS secrets to.
   */
  static void
  enable_keylogging(const char *keylog_file)
  {
    instance().enable_keylogging_internal(keylog_file);
  }

  /** Disable TLS secrets logging. */
  static void
  disable_keylogging()
  {
    instance().disable_keylogging_internal();
  }

private:
  TLSKeyLogger() = default;

  /** Return the TLSKeyLogger singleton.
   *
   * We use a getter rather than a class static singleton member so that the
   * construction of the singleton delayed until after TLS configuration is
   * processed.
   */
  static TLSKeyLogger &
  instance()
  {
    static TLSKeyLogger instance;
    return instance;
  }

  /** Close the file descriptor for the key log file.
   *
   * @note This assumes that a unique lock has been acquired for _mutex.
   */
  void close_keylog_file();

  /** A TLS secret line to log to the keylog file.
   *
   * @param[in] line A line to log to the keylog file.
   */
  void log(const char *line);

  /** Enable TLS keylogging in the instance singleton. */
  void enable_keylogging_internal(const char *keylog_file);

  /** Disable TLS keylogging in the instance singleton. */
  void disable_keylogging_internal();

private:
  /** A file descriptor for the log file receiving the TLS secrets. */
  int _fd = -1;

  /** A mutex to coordinate dynamically changing TLS logging config changes and
   * logging to the TLS log file. */
  std::shared_mutex _mutex;
};
