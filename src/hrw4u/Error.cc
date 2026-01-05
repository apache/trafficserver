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

#include "hrw4u/Error.h"
#include <iomanip>
#include <sstream>

namespace hrw4u
{

std::string
SourceLocation::format() const
{
  std::ostringstream ss;

  if (!filename.empty()) {
    ss << filename << ":";
  }
  if (line > 0) {
    ss << line;
    if (column > 0) {
      ss << ":" << column;
    }
  }
  return ss.str();
}

std::string_view
ParseError::severity_str() const
{
  switch (severity) {
  case ErrorSeverity::Warning:
    return "warning";
  case ErrorSeverity::Error:
    return "error";
  case ErrorSeverity::Fatal:
    return "fatal";
  }
  return "unknown";
}

std::string
ParseError::format() const
{
  std::ostringstream ss;

  if (location.is_valid()) {
    ss << location.format() << ": ";
  }

  ss << severity_str() << ": " << message;

  if (!code.empty()) {
    ss << " [" << code << "]";
  }

  if (!location.context.empty() && location.line > 0) {
    ss << "\n";
    ss << std::setw(4) << location.line << " | " << location.context;

    if (location.column > 0) {
      ss << "\n     | ";
      for (size_t i = 0; i < location.column; ++i) {
        ss << ' ';
      }
      ss << '^';
    }
  }

  return ss.str();
}

ErrorCollector::ErrorCollector(ErrorCallback callback) : _callback(std::move(callback)) {}

void
ErrorCollector::add_error(ParseError error)
{
  if (!error.location.filename.empty() && _current_filename.empty()) {
    _current_filename = error.location.filename;
  } else if (error.location.filename.empty() && !_current_filename.empty()) {
    error.location.filename = _current_filename;
  }

  if (_callback) {
    _callback(error);
  }

  _errors.push_back(std::move(error));
}

void
ErrorCollector::add_error(ErrorSeverity severity, std::string message, SourceLocation location, std::string code)
{
  add_error(
    ParseError{.message = std::move(message), .code = std::move(code), .severity = severity, .location = std::move(location)});
}

bool
ErrorCollector::has_errors() const
{
  for (const auto &err : _errors) {
    if (err.severity != ErrorSeverity::Warning) {
      return true;
    }
  }

  return false;
}

bool
ErrorCollector::has_fatal() const
{
  for (const auto &err : _errors) {
    if (err.severity == ErrorSeverity::Fatal) {
      return true;
    }
  }

  return false;
}

size_t
ErrorCollector::error_count() const
{
  size_t count = 0;

  for (const auto &err : _errors) {
    if (err.severity != ErrorSeverity::Warning) {
      ++count;
    }
  }

  return count;
}

void
ErrorCollector::clear()
{
  _errors.clear();
}

std::string
ErrorCollector::format_all() const
{
  std::ostringstream ss;

  for (const auto &err : _errors) {
    ss << err.format() << "\n";
  }

  return ss.str();
}

std::string
ErrorCollector::summary() const
{
  size_t             warnings = 0;
  size_t             errors   = 0;
  std::ostringstream ss;

  for (const auto &err : _errors) {
    if (err.severity == ErrorSeverity::Warning) {
      ++warnings;
    } else {
      ++errors;
    }
  }

  ss << errors << " error(s), " << warnings << " warning(s)";

  return ss.str();
}

ParseException::ParseException(ParseError error) : _error(std::move(error)), _formatted(_error.format()) {}

ParseException::ParseException(std::string message, SourceLocation location)
  : _error{.message = std::move(message), .severity = ErrorSeverity::Fatal, .location = std::move(location)},
    _formatted(_error.format())
{
}

const char *
ParseException::what() const noexcept
{
  return _formatted.c_str();
}

} // namespace hrw4u
