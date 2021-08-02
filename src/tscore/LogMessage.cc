/** @file

  LogMessage implementation.

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

#include "tscore/LogMessage.h"

#include "tscore/Diags.h"

using namespace std::chrono_literals;

std::atomic<std::chrono::milliseconds> LogMessage::_default_log_throttling_interval{0ms};
std::atomic<std::chrono::milliseconds> LogMessage::_default_debug_throttling_interval{0ms};

// static
void
LogMessage::set_default_log_throttling_interval(std::chrono::milliseconds new_interval)
{
  _default_log_throttling_interval = new_interval;
}

// static
void
LogMessage::set_default_debug_throttling_interval(std::chrono::milliseconds new_interval)
{
  _default_debug_throttling_interval = new_interval;
}

void
LogMessage::message_helper(std::chrono::microseconds current_configured_interval, const log_function_f &log_function,
                           const char *fmt, va_list args)
{
  if (!_is_throttled) {
    // If throttling is disabled, make this operation as efficient as possible.
    // Simply log and exit without consulting the Throttler API.
    //
    // If the user changes the throttling value from some non-zero value to
    // zero, then we may miss out on some "The following message was
    // suppressed" logs. However we accept this as a tradeoff to make this
    // common case as fast as possible.
    log_function(fmt, args);
    return;
  }
  if (!_throttling_value_is_explicitly_set) {
    set_throttling_interval(current_configured_interval);
  }
  uint64_t number_of_suppressions = 0;
  if (is_throttled(number_of_suppressions)) {
    // The messages are the same and but we're still within the throttling
    // interval. Suppress this message.
    return;
  }
  // If we get here, the message should not be suppressed.
  if (number_of_suppressions > 0) {
    // We use no format parameters, so we just need an empty va_list.
    va_list empty_args{};
    std::string message =
      std::string("The following message was suppressed ") + std::to_string(number_of_suppressions) + std::string(" times.");
    log_function(message.c_str(), empty_args);
  }
  log_function(fmt, args);
}

void
LogMessage::standard_message_helper(DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args)
{
  message_helper(
    _default_log_throttling_interval.load(),
    [level, &loc](const char *fmt, va_list args) { diags->error_va(level, &loc, fmt, args); }, fmt, args);
}

void
LogMessage::message_debug_helper(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args)
{
  message_helper(
    _default_debug_throttling_interval.load(),
    [tag, level, &loc](const char *fmt, va_list args) { diags->log_va(tag, level, &loc, fmt, args); }, fmt, args);
}

void
LogMessage::message_print_helper(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args)
{
  message_helper(
    _default_debug_throttling_interval.load(),
    [tag, level, &loc](const char *fmt, va_list args) { diags->print_va(tag, level, &loc, fmt, args); }, fmt, args);
}

LogMessage::LogMessage(bool is_throttled)
  // Turn throttling off by default. Each log event will check the configured
  // throttling interval.
  : Throttler{std::chrono::milliseconds{0}}, _throttling_value_is_explicitly_set{false}, _is_throttled{is_throttled}
{
}

LogMessage::LogMessage(std::chrono::milliseconds throttling_interval)
  : Throttler{throttling_interval}, _throttling_value_is_explicitly_set{true}, _is_throttled{throttling_interval != 0ms}
{
}

void
LogMessage::diag(const char *tag, SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  message_debug_helper(tag, DL_Diag, loc, fmt, args);
  va_end(args);
}

void
LogMessage::debug(const char *tag, SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  message_debug_helper(tag, DL_Debug, loc, fmt, args);
  va_end(args);
}

void
LogMessage::status(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Status, loc, fmt, args);
  va_end(args);
}

void
LogMessage::note(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Note, loc, fmt, args);
  va_end(args);
}

void
LogMessage::warning(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Warning, loc, fmt, args);
  va_end(args);
}

void
LogMessage::error(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Error, loc, fmt, args);
  va_end(args);
}

void
LogMessage::fatal(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Fatal, loc, fmt, args);
  va_end(args);
}

void
LogMessage::alert(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Alert, loc, fmt, args);
  va_end(args);
}

void
LogMessage::emergency(SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(DL_Emergency, loc, fmt, args);
  va_end(args);
}

void
LogMessage::message(DiagsLevel level, SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  standard_message_helper(level, loc, fmt, args);
  va_end(args);
}

void
LogMessage::print(const char *tag, DiagsLevel level, SourceLocation const &loc, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  message_print_helper(tag, level, loc, fmt, args);
  va_end(args);
}

void
LogMessage::diag_va(const char *tag, SourceLocation const &loc, const char *fmt, va_list args)
{
  message_debug_helper(tag, DL_Diag, loc, fmt, args);
}

void
LogMessage::debug_va(const char *tag, SourceLocation const &loc, const char *fmt, va_list args)
{
  message_debug_helper(tag, DL_Debug, loc, fmt, args);
}

void
LogMessage::status_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Status, loc, fmt, args);
}

void
LogMessage::note_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Note, loc, fmt, args);
}

void
LogMessage::warning_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Warning, loc, fmt, args);
}

void
LogMessage::error_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Error, loc, fmt, args);
}

void
LogMessage::fatal_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Fatal, loc, fmt, args);
}

void
LogMessage::alert_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Alert, loc, fmt, args);
}

void
LogMessage::emergency_va(SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(DL_Emergency, loc, fmt, args);
}

void
LogMessage::message_va(DiagsLevel level, SourceLocation const &loc, const char *fmt, va_list args)
{
  standard_message_helper(level, loc, fmt, args);
}
