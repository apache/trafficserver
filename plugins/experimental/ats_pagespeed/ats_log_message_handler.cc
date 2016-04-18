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

#include "ats_log_message_handler.h"

#include <ts/ts.h>

#include <unistd.h>

#include <limits>
#include <string>

#include "base/debug/debugger.h"
#include "base/debug/stack_trace.h"
#include "base/logging.h"
#include "net/instaweb/public/version.h"
#include "net/instaweb/util/public/string_util.h"

// Make sure we don't attempt to use LOG macros here, since doing so
// would cause us to go into an infinite log loop.
#undef LOG
#define LOG USING_LOG_HERE_WOULD_CAUSE_INFINITE_RECURSION

namespace
{
bool
LogMessageHandler(int severity, const char *file, int line, size_t message_start, const GoogleString &str)
{
  GoogleString message = str;
  if (severity == logging::LOG_FATAL) {
    if (base::debug::BeingDebugged()) {
      base::debug::BreakDebugger();
    } else {
      base::debug::StackTrace trace;
      std::ostringstream stream;
      trace.OutputToStream(&stream);
      message.append(stream.str());
    }
  }

  // Trim the newline off the end of the message string.
  size_t last_msg_character_index = message.length() - 1;
  if (message[last_msg_character_index] == '\n') {
    message.resize(last_msg_character_index);
  }

  TSDebug("ats-speed-vlog", "[%s] %s", net_instaweb::kModPagespeedVersion, message.c_str());

  if (severity == logging::LOG_FATAL) {
    // Crash the process to generate a dump.
    base::debug::BreakDebugger();
  }

  return true;
}

} // namespace

namespace net_instaweb
{
namespace log_message_handler
{
  const int kDebugLogLevel = -2;

  void
  Install()
  {
    logging::SetLogMessageHandler(&LogMessageHandler);

    // All VLOG(2) and higher will be displayed as DEBUG logs if the nginx log
    // level is DEBUG.
    // TODO(oschaaf): from config
    // if (log->log_level >= NGX_LOG_DEBUG) {
    logging::SetMinLogLevel(-2);
    //}
  }

} // namespace log_message_handler

} // namespace net_instaweb
