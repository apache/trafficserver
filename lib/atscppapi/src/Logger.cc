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
 * @file Logger.cc
 * @warning log rolling doesn't work correctly in 3.2.x see:
 *   https://issues.apache.org/jira/browse/TS-1813
 *   Apply the patch in TS-1813 to correct log rolling in 3.2.x
 */

#include "atscppapi/Logger.h"
#include <cstdarg>
#include <vector>
#include <cstdio>
#include <string>
#include <cstring>
#include <ts/ts.h>
#include "atscppapi/noncopyable.h"
#include "logging_internal.h"

using std::vector;
using std::string;

using atscppapi::Logger;

/**
 * @private
 */
struct atscppapi::LoggerState : noncopyable {
  std::string filename_;
  bool add_timestamp_;
  bool rename_file_;
  volatile Logger::LogLevel level_;
  bool rolling_enabled_;
  int rolling_interval_seconds_;
  TSTextLogObject text_log_obj_;
  bool initialized_;

  LoggerState()
    : add_timestamp_(false),
      rename_file_(false),
      level_(Logger::LOG_LEVEL_NO_LOG),
      rolling_enabled_(false),
      rolling_interval_seconds_(-1),
      text_log_obj_(NULL),
      initialized_(false){};
  ~LoggerState(){};
};

namespace
{
// Since the TSTextLog API doesn't support override the log file sizes (I will add this to TS api at some point)
// we will use the roll size specified by default in records.config.
const int ROLL_ON_TIME = 1; // See RollingEnabledValues in LogConfig.h
}

/*
 * These have default values specified for add_timestamp and rename_file in Logger.h
 */
Logger::Logger()
{
  state_ = new LoggerState();
}

Logger::~Logger()
{
  if (state_->initialized_ && state_->text_log_obj_) {
    TSTextLogObjectDestroy(state_->text_log_obj_);
  }

  delete state_;
}

/*
 * These have default values specified for rolling_enabled and rolling_interval_seconds in Logger.h
 */
bool
Logger::init(const string &file, bool add_timestamp, bool rename_file, LogLevel level, bool rolling_enabled,
             int rolling_interval_seconds)
{
  if (state_->initialized_) {
    LOG_ERROR("Attempt to reinitialize a logger named '%s' that's already been initialized to '%s'.", file.c_str(),
              state_->filename_.c_str());
    return false;
  }
  state_->filename_                 = file;
  state_->add_timestamp_            = add_timestamp;
  state_->rename_file_              = rename_file;
  state_->level_                    = level;
  state_->rolling_enabled_          = rolling_enabled;
  state_->rolling_interval_seconds_ = rolling_interval_seconds;
  state_->initialized_              = true; // set this to true always - we are not providing re-init() after a failed init()

  int mode = 0;
  if (state_->add_timestamp_) {
    mode |= TS_LOG_MODE_ADD_TIMESTAMP;
  }

  if (!state_->rename_file_) {
    mode |= TS_LOG_MODE_DO_NOT_RENAME;
  }

  TSReturnCode result = TSTextLogObjectCreate(state_->filename_.c_str(), mode, &state_->text_log_obj_);

  if (result == TS_SUCCESS) {
    TSTextLogObjectRollingEnabledSet(state_->text_log_obj_, state_->rolling_enabled_ ? ROLL_ON_TIME : 0);
    TSTextLogObjectRollingIntervalSecSet(state_->text_log_obj_, state_->rolling_interval_seconds_);
    LOG_DEBUG("Initialized log [%s]", state_->filename_.c_str());
  } else {
    state_->level_ = LOG_LEVEL_NO_LOG;
    LOG_ERROR("Failed to initialize for log [%s]", state_->filename_.c_str());
  }

  return result == TS_SUCCESS;
}

void
Logger::setLogLevel(Logger::LogLevel level)
{
  if (state_->initialized_) {
    state_->level_ = level;
    LOG_DEBUG("Set log level to %d for log [%s]", level, state_->filename_.c_str());
  }
}

Logger::LogLevel
Logger::getLogLevel() const
{
  if (!state_->initialized_) {
    LOG_ERROR("Not initialized");
  }
  return state_->level_;
}

void
Logger::setRollingIntervalSeconds(int seconds)
{
  if (state_->initialized_) {
    state_->rolling_interval_seconds_ = seconds;
    TSTextLogObjectRollingIntervalSecSet(state_->text_log_obj_, seconds);
    LOG_DEBUG("Set rolling interval for log [%s] to %d seconds", state_->filename_.c_str(), seconds);
  } else {
    LOG_ERROR("Not initialized!");
  }
}

int
Logger::getRollingIntervalSeconds() const
{
  if (!state_->initialized_) {
    LOG_ERROR("Not initialized");
  }
  return state_->rolling_interval_seconds_;
}

void
Logger::setRollingEnabled(bool enabled)
{
  if (state_->initialized_) {
    state_->rolling_enabled_ = enabled;
    TSTextLogObjectRollingEnabledSet(state_->text_log_obj_, enabled ? ROLL_ON_TIME : 0);
    LOG_DEBUG("Rolling for log [%s] is now %s", state_->filename_.c_str(), (enabled ? "true" : "false"));
  } else {
    LOG_ERROR("Not initialized!");
  }
}

bool
Logger::isRollingEnabled() const
{
  if (!state_->initialized_) {
    LOG_ERROR("Not initialized!");
  }
  return state_->rolling_enabled_;
}

void
Logger::flush()
{
  if (state_->initialized_) {
    TSTextLogObjectFlush(state_->text_log_obj_);
  } else {
    LOG_ERROR("Not initialized!");
  }
}

namespace
{
const int DEFAULT_BUFFER_SIZE_FOR_VARARGS = 8 * 1024;

// We use a macro here because varargs would be a pain to forward via a helper
// function
#define TS_TEXT_LOG_OBJECT_WRITE(level)                                                                               \
  char buffer[DEFAULT_BUFFER_SIZE_FOR_VARARGS];                                                                       \
  int n;                                                                                                              \
  va_list ap;                                                                                                         \
  while (true) {                                                                                                      \
    va_start(ap, fmt);                                                                                                \
    n = vsnprintf(&buffer[0], sizeof(buffer), fmt, ap);                                                               \
    va_end(ap);                                                                                                       \
    if (n > -1 && n < static_cast<int>(sizeof(buffer))) {                                                             \
      LOG_DEBUG("logging a " level " to '%s' with length %d", state_->filename_.c_str(), n);                          \
      TSTextLogObjectWrite(state_->text_log_obj_, const_cast<char *>("[" level "] %s"), buffer);                      \
    } else {                                                                                                          \
      LOG_ERROR("Unable to log " level " message to '%s' due to size exceeding %zu bytes", state_->filename_.c_str(), \
                sizeof(buffer));                                                                                      \
    }                                                                                                                 \
    return;                                                                                                           \
  }

} /* end anonymous namespace */

void
Logger::logDebug(const char *fmt, ...)
{
  if (state_->level_ <= LOG_LEVEL_DEBUG) {
    TS_TEXT_LOG_OBJECT_WRITE("DEBUG");
  }
}

void
Logger::logInfo(const char *fmt, ...)
{
  if (state_->level_ <= LOG_LEVEL_INFO) {
    TS_TEXT_LOG_OBJECT_WRITE("INFO");
  }
}

void
Logger::logError(const char *fmt, ...)
{
  if (state_->level_ <= LOG_LEVEL_ERROR) {
    TS_TEXT_LOG_OBJECT_WRITE("ERROR");
  }
}
