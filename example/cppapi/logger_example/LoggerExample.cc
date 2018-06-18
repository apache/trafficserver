/**
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

/**
 * @warning log rolling doesn't work correctly in 3.2.x, see:
 *   https://issues.apache.org/jira/browse/TS-1813, Apply the patch in
 *   TS-1813 to correct log rolling in 3.2.x
 */

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <cstring>

using namespace atscppapi;
using std::string;

namespace
{
Logger log;
GlobalPlugin *plugin;
} // namespace

/*
 * You should always take advantage of the LOG_DEBUG, LOG_INFO, and LOG_ERROR
 * macros available in Logger.h, they are easy to use as you can see below
 * and will provide detailed information about the logging site such as
 * filename, function name, and line number of the message
 */

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    memset(big_buffer_6kb_, 'a', sizeof(big_buffer_6kb_));
    big_buffer_6kb_[sizeof(big_buffer_6kb_) - 1] = '\0';

    memset(big_buffer_14kb_, 'a', sizeof(big_buffer_14kb_));
    big_buffer_14kb_[sizeof(big_buffer_14kb_) - 1] = '\0';

    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    LOG_DEBUG(log,
              "handleReadRequestHeadersPostRemap.\n"
              "\tRequest URL: %s\n"
              "\tRequest Path: %s\n"
              "\tRequest Query: %s\n"
              "\tRequest Method: %s",
              transaction.getClientRequest().getUrl().getUrlString().c_str(),
              transaction.getClientRequest().getUrl().getPath().c_str(), transaction.getClientRequest().getUrl().getQuery().c_str(),
              HTTP_METHOD_STRINGS[transaction.getClientRequest().getMethod()].c_str());

    // Next, to demonstrate how you can change logging levels:
    if (transaction.getClientRequest().getUrl().getPath() == "change_log_level") {
      if (transaction.getClientRequest().getUrl().getQuery().find("level=debug") != string::npos) {
        log.setLogLevel(Logger::LOG_LEVEL_DEBUG);
        LOG_DEBUG(log, "Changed log level to DEBUG");
      } else if (transaction.getClientRequest().getUrl().getQuery().find("level=info") != string::npos) {
        log.setLogLevel(Logger::LOG_LEVEL_INFO);
        LOG_INFO(log, "Changed log level to INFO");
      } else if (transaction.getClientRequest().getUrl().getQuery().find("level=error") != string::npos) {
        log.setLogLevel(Logger::LOG_LEVEL_ERROR);
        LOG_ERROR(log, "Changed log level to ERROR");
      }
    }

    // One drawback to using the Traffic Server Text Loggers is that you're limited in the size of the log
    // lines, this limit is now set at 8kb for atscppapi, but this limit might be removed in the future.
    LOG_INFO(log, "This message will be dropped (see error.log) because it's just too big: %s", big_buffer_14kb_);

    // This should work though:
    LOG_INFO(log, "%s", big_buffer_6kb_);

    transaction.resume();
  }

private:
  char big_buffer_6kb_[6 * 1024];
  char big_buffer_14kb_[14 * 1024];
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_Logger", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  // Create a new logger
  // This will create a log file with the name logger_example.log (since we left off
  //    the extension it will automatically add .log)
  //
  // The second argument is timestamp, which will force a timestamp on every log message
  //  this is enabled by default.
  // The third argument is renaming enabled, which means if a log already exists with that
  //  name it will try logger_example.1 and so on, this is enabled by default.
  // The fourth argument is the initial logging level this can always be changed with log.setLogLevel().
  //  the default log level is LOG_LEVEL_INFO.
  // The fifth argument is to enable log rolling, this is enabled by default.
  // The sixth argument is the frequency in which we will roll the logs, 300 seconds is very low,
  //  the default for this argument is 3600.
  log.init("logger_example", true, true, Logger::LOG_LEVEL_DEBUG, true, 300);

  // Now that we've initialized a logger we can do all kinds of fun things on it:
  log.setRollingEnabled(true);        // already done via log.init, just an example.
  log.setRollingIntervalSeconds(300); // already done via log.init

  // You have two ways to log to a logger, you can log directly on the object itself:
  log.logInfo("Hello World from: %s", argv[0]);

  // Alternatively you can take advantage of the super helper macros for logging
  // that will include the file, function, and line number automatically as part
  // of the log message:
  LOG_INFO(log, "Hello World with more info from: %s", argv[0]);

  // This will hurt performance, but it's an option that's always available to you
  // to force flush the logs. Otherwise TrafficServer will flush the logs around
  // once every second. You should really avoid flushing the log unless it's really necessary.
  log.flush();

  plugin = new GlobalHookPlugin();
}
