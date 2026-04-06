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

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace hrw4u
{

struct SourceLocation {
  std::string filename;
  std::string context;
  size_t      line   = 0;
  size_t      column = 0;
  size_t      length = 0;

  [[nodiscard]] std::string format() const;

  [[nodiscard]] bool
  is_valid() const
  {
    return line > 0;
  }
};

enum class ErrorSeverity { Warning, Error, Fatal };

struct ParseError {
  std::string    message;
  std::string    code;
  ErrorSeverity  severity = ErrorSeverity::Error;
  SourceLocation location;

  [[nodiscard]] std::string      format() const;
  [[nodiscard]] std::string_view severity_str() const;
};

using ErrorCallback = std::function<void(const ParseError &)>;

class ErrorCollector
{
public:
  ErrorCollector() = default;

  explicit ErrorCollector(ErrorCallback callback);
  void add_error(ParseError error);
  void add_error(ErrorSeverity severity, std::string message, SourceLocation location = {}, std::string code = {});

  void
  warning(std::string message, SourceLocation location = {})
  {
    add_error(ErrorSeverity::Warning, std::move(message), std::move(location));
  }

  void
  error(std::string message, SourceLocation location = {})
  {
    add_error(ErrorSeverity::Error, std::move(message), std::move(location));
  }

  void
  fatal(std::string message, SourceLocation location = {})
  {
    add_error(ErrorSeverity::Fatal, std::move(message), std::move(location));
  }

  [[nodiscard]] bool has_errors() const;
  [[nodiscard]] bool has_fatal() const;

  [[nodiscard]] bool
  has_messages() const
  {
    return !_errors.empty();
  }

  [[nodiscard]] size_t error_count() const;

  [[nodiscard]] const std::vector<ParseError> &
  errors() const
  {
    return _errors;
  }

  void clear();

  [[nodiscard]] std::string format_all() const;
  [[nodiscard]] std::string summary() const;

  void
  set_filename(std::string filename)
  {
    _current_filename = std::move(filename);
  }

  [[nodiscard]] const std::string &
  current_filename() const
  {
    return _current_filename;
  }

private:
  std::vector<ParseError> _errors;
  ErrorCallback           _callback;
  std::string             _current_filename;
};

class ParseException : public std::exception
{
public:
  explicit ParseException(ParseError error);
  explicit ParseException(std::string message, SourceLocation location = {});

  [[nodiscard]] const char *what() const noexcept override;

  [[nodiscard]] const ParseError &
  error() const
  {
    return _error;
  }

private:
  ParseError  _error;
  std::string _formatted;
};

} // namespace hrw4u
