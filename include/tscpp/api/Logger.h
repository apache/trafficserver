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
 * @file Logger.h
 * @brief Helpers and Classes related to Logging
 *
 * @warning Log rolling doesn't work correctly in 3.2.x see:
 *   https://issues.apache.org/jira/browse/TS-1813
 *   Apply the patch in TS-1813 to correct log rolling in 3.2.x
 *
 */

#pragma once

#include <string>
#include "tscpp/api/noncopyable.h"

#if !defined(ATSCPPAPI_PRINTFLIKE)
#if defined(__GNUC__) || defined(__clang__)
/**
 * This macro will tell GCC that the function takes printf like arguments
 * this is helpful because it can produce better warning and error messages
 * when a user doesn't use the methods correctly.
 *
 * @private
 */
#define ATSCPPAPI_PRINTFLIKE(fmt, arg) __attribute__((format(printf, fmt, arg)))
#else
#define ATSCPPAPI_PRINTFLIKE(fmt, arg)
#endif
#endif

/**
 * A helper macro for Logger objects that allows you to easily add a debug level message
 * which will include file, line, and function name with the message. It's very easy to use:
 * \code
 *  // Suppose you have already created a Logger named logger:
 *  LOG_DEBUG(logger, "This is a test DEBUG message: %s", "hello");
 *  // Outputs [file.cc:125, function()] [DEBUG] This is a test DEBUG message: hello.
 * \endcode
 */
#define LOG_DEBUG(log, fmt, ...)                                                           \
  do {                                                                                     \
    (log).logDebug("[%s:%d, %s()] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (false)

/**
 * A helper macro for Logger objects that allows you to easily add a info level message
 * which will include file, line, and function name with the message. See example in LOG_DEBUG
 */
#define LOG_INFO(log, fmt, ...)                                                           \
  do {                                                                                    \
    (log).logInfo("[%s:%d, %s()] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (false)

/**
 * A helper macro for Logger objects that allows you to easily add a error level message
 * which will include file, line, and function name with the message.  See example in LOG_DEBUG
 */
#define LOG_ERROR(log, fmt, ...)                                                           \
  do {                                                                                     \
    (log).logError("[%s:%d, %s()] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (false)

/**
 * We forward declare this because if we didn't we end up writing our
 * own version to do the vsnprintf just to call TSDebug and have it do
 * an unnecessary vsnprintf.
 *
 * @private
 */
extern "C" void TSDebug(const char *tag, const char *fmt, ...) ATSCPPAPI_PRINTFLIKE(2, 3);

/**
 * We forward declare this because if we didn't we end up writing our
 * own version to do the vsnprintf just to call TSError and have it do
 * an unnecessary vsnprintf.
 *
 * @private
 */
extern "C" void TSError(const char *fmt, ...) ATSCPPAPI_PRINTFLIKE(1, 2);

// This is weird, but see the following:
//   http://stackoverflow.com/questions/5641427/how-to-make-preprocessor-generate-a-string-for-line-keyword
#define STRINGIFY0(x) #x
#define STRINGIFY(x) STRINGIFY0(x)
#define LINE_NO STRINGIFY(__LINE__)

/**
 * A helper macro to get access to the Diag messages available in traffic server. These can be enabled
 * via traffic_server -T "tag.*" or since this macro includes the file can you further refine to an
 * individual file or even a particular line! This can also be enabled via records.config.
 */
#define TS_DEBUG(tag, fmt, ...)                                                        \
  do {                                                                                 \
    TSDebug(tag "." __FILE__ ":" LINE_NO, "[%s()] " fmt, __FUNCTION__, ##__VA_ARGS__); \
  } while (false)

/**
 * A helper macro to get access to the error.log messages available in traffic server. This
 * will also output a DEBUG message visible via traffic_server -T "tag.*", or by enabling the
 * tag in records.config.
 */
#define TS_ERROR(tag, fmt, ...)                                                               \
  do {                                                                                        \
    TS_DEBUG(tag, "[ERROR] " fmt, ##__VA_ARGS__);                                             \
    TSError("[%s] [%s:%d, %s()] " fmt, tag, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  } while (false)

namespace atscppapi
{
struct LoggerState;

/**
 * @brief Create log files that are automatically rolled and cleaned up as space is required.
 *
 * Log files created using the Logger class will be placed in the same directory as
 * other log files, that directory is specified in records.config. All of the logging
 * configuration such as max space available for all logs includes any logs created
 * using the Logger class.
 *
 * Loggers are very easy to use and a full example is available in examples/logger_example/,
 * a simple example is:
 * \code
 * // See Logger::init() for an explanation of the init() parameters.
 * log.init("logger_example", true, true, Logger::LOG_LEVEL_DEBUG);
 * // You have two ways to log to a logger, you can log directly on the object itself:
 * log.logInfo("Hello World from: %s", argv[0]);
 * // Alternatively you can take advantage of the super helper macros for logging
 * // that will include the file, function, and line number automatically as part
 * // of the log message:
 * LOG_INFO(log, "Hello World with more info from: %s", argv[0]);
 * \endcode
 *
 * @warning Log rolling doesn't work correctly in 3.2.x see:
 *   https://issues.apache.org/jira/browse/TS-1813
 *   Apply the patch in TS-1813 to correct log rolling in 3.2.x
 *
 */
class Logger : noncopyable
{
public:
  /**
   * The available log levels
   */
  enum LogLevel {
    LOG_LEVEL_NO_LOG = 128, /**< This log level is used to disable all logging */
    LOG_LEVEL_DEBUG  = 1,   /**< This log level is used for DEBUG level logging (DEBUG + INFO + ERROR) */
    LOG_LEVEL_INFO   = 2,   /**< This log level is used for INFO level logging (INFO + ERROR) */
    LOG_LEVEL_ERROR  = 4    /**< This log level is used for ERROR level logging (ERROR ONLY) */
  };

  Logger();
  ~Logger();

  /**
   * You must always init() a Logger before you begin logging. If you do not call init() nothing will
   * happen.
   *
   * @param file The name of the file to create in the logging directory, if you do not specify an extension .log will be used.
   * @param add_timestamp Prepend a timestamp to the log lines, the default value is true.
   * @param rename_file If a file already exists by the same name it will attempt to rename using a scheme that appends .1, .2, and
   *so on,
   *   the default value for this argument is true.
   * @param level the default log level to use when creating the logger, this is set to LOG_LEVEL_INFO by default.
   * @param rolling_enabled if set to true this will enable log rolling on a periodic basis, this is enabled by default.
   * @param rolling_interval_seconds how frequently to roll the longs in seconds, this is set to 3600 by default (one hour).
   * @return returns true if the logger was successfully created and initialized.
   * @see LogLevel
   */
  bool init(const std::string &file, bool add_timestamp = true, bool rename_file = true, LogLevel level = LOG_LEVEL_INFO,
            bool rolling_enabled = true, int rolling_interval_seconds = 3600);

  /**
   * Allows you to change the rolling interval in seconds
   * @param seconds the number of seconds between rolls
   */
  void setRollingIntervalSeconds(int seconds);

  /**
   * @return the number of seconds between log rolls.
   */
  int getRollingIntervalSeconds() const;

  /**
   * Enables or disables log rolling
   * @param enabled true to enable log rolling, false to disable it.
   */
  void setRollingEnabled(bool enabled);

  /**
   * @return A boolean value which represents whether rolling is enabled or disabled.
   */
  bool isRollingEnabled() const;

  /**
   * Change the log level
   *
   * @param level the new log level to use
   * @see LogLevel
   */
  void setLogLevel(Logger::LogLevel level);

  /**
   * @return The current log level.
   * @see LogLevel
   */
  Logger::LogLevel getLogLevel() const;

  /**
   * This method allows you to flush any log lines that might have been buffered.
   * @warning This method can cause serious performance degradation so you should only
   * use it when absolutely necessary.
   */
  void flush();

  /**
   * This method writes a DEBUG level message to the log file, the LOG_DEBUG
   * macro in Logger.h should be used in favor of these when possible because it
   * will produce a much more rich debug message.
   *
   * Sample usage:
   * \code
   * log.logDebug("Hello you are %d years old", 27);
   * \endcode
   */
  void logDebug(const char *fmt, ...) ATSCPPAPI_PRINTFLIKE(2, 3);

  /**
   * This method writes an INFO level message to the log file, the LOG_INFO
   * macro in Logger.h should be used in favor of these when possible because it
   * will produce a much more rich info message.
   */
  void logInfo(const char *fmt, ...) ATSCPPAPI_PRINTFLIKE(2, 3);

  /**
   * This method writes an ERROR level message to the log file, the LOG_ERROR
   * macro in Logger.h should be used in favor of these when possible because it
   * will produce a much more rich error message.
   */
  void logError(const char *fmt, ...) ATSCPPAPI_PRINTFLIKE(2, 3);

private:
  LoggerState *state_; /**< Internal state for the Logger */
};

} // namespace atscppapi
