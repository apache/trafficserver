/** @file

  LogMessage declaration.

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

#include "DiagsTypes.h"
#include "SourceLocation.h"
#include "Throttler.h"

#include <atomic>
#include <chrono>
#include <functional>

constexpr const bool IS_THROTTLED = true;

/** A class implementing stateful logging behavior. */
class LogMessage : public Throttler
{
public:
  /** Create a LogMessage, optionally with throttling applied to it.
   *
   * If configured with throttling, the system's throttling value will be used
   * and the throttling value will dynamically change as the user configures
   * different values for throttling.
   *
   * @param[in] is_throttled Whether to apply throttling to the message. If
   * true, the system default log throttling interval will be used.
   */
  LogMessage(bool is_throttled = false);

  /** Create a LogMessage with an explicit throttling interval.
   *
   * For this message, throttling will be configured with the designated amount
   * and will not change as the system's configured throttling interval
   * changes.
   *
   * @param[in] throttling_interval The minimum number of desired
   * milliseconds between log events. 0 implies no throttling.
   */
  LogMessage(std::chrono::milliseconds throttling_interval);

  /* TODO: Add BufferWriter overloads for these. */
  void diag(const char *tag, SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(4, 5);
  void debug(const char *tag, SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(4, 5);
  void status(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void note(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void warning(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void error(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void fatal(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void alert(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);
  void emergency(SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(3, 4);

  void message(DiagsLevel level, SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(4, 5);
  void print(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, ...) TS_PRINTFLIKE(5, 6);

  void diag_va(const char *tag, SourceLocation const &loc, const char *fmt, va_list args);
  void debug_va(const char *tag, SourceLocation const &loc, const char *fmt, va_list args);
  void status_va(SourceLocation const &loc, const char *fmt, va_list args);
  void note_va(SourceLocation const &loc, const char *fmt, va_list args);
  void warning_va(SourceLocation const &loc, const char *fmt, va_list args);
  void error_va(SourceLocation const &loc, const char *fmt, va_list args);
  void fatal_va(SourceLocation const &loc, const char *fmt, va_list args);
  void alert_va(SourceLocation const &loc, const char *fmt, va_list args);
  void emergency_va(SourceLocation const &loc, const char *fmt, va_list args);
  void message_va(DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args);

  /** Set a new system-wide default log throttling interval.
   *
   * @param[in] new_interval The new log throttling interval.
   */
  static void set_default_log_throttling_interval(std::chrono::milliseconds new_interval);

  /** Set a new system-wide default debug log throttling interval.
   *
   * @param[in] new_interval The new debug log throttling interval.
   */
  static void set_default_debug_throttling_interval(std::chrono::milliseconds new_interval);

private:
  using log_function_f = std::function<void(const char *fmt, va_list args)>;

  /** Encapsulate common message handling logic in a helper function.
   *
   * @param[in] current_configured_interval The applicable log throttling
   * interval for this message.
   *
   * @param[in] log_function The function to use to emit the log message if it
   * is not throttled.
   *
   * @param[in] fmt The format string for the log message.
   *
   * @param[in] args The parameters for the above format string.
   */
  void message_helper(std::chrono::microseconds current_configured_interval, const log_function_f &log_function, const char *fmt,
                      va_list args);

  /** Message handling for non-debug logs. */
  void standard_message_helper(DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args);

  /** Same as above, but catered for the diag and debug variants.
   *
   * Note that this uses the diags-log variant which takes a debug tag.
   */
  void message_debug_helper(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args);

  /** Same as above, but uses the tag-ignoring diags->print variant. */
  void message_print_helper(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args);

private:
  /** Whether the throttling value was explicitly set by the user.
   *
   * If the user explicitly set a throttling value, then it will not change as
   * the configured log throttling values change.
   */
  bool const _throttling_value_is_explicitly_set;

  /** Whether throttling should be applied to this message. */
  bool const _is_throttled;

  /** The configured, system-wide default log throttling value. */
  static std::atomic<std::chrono::milliseconds> _default_log_throttling_interval;

  /** The configured, system-wide default debug log throttling value. */
  static std::atomic<std::chrono::milliseconds> _default_debug_throttling_interval;
};
