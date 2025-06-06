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
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Interface for the config line parser
//
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <charconv>
#include <optional>
#include <limits>
#include <memory>

#include "ts/ts.h"
#include "lulu.h"

///////////////////////////////////////////////////////////////////////////////
// Simple wrapper, for dealing with raw configurations, and the compiled
// configurations.
class HRW4UPipe : public std::streambuf
{
public:
  explicit HRW4UPipe(FILE *pipe) : _pipe(pipe) { setg(_buffer, _buffer, _buffer); }

  ~HRW4UPipe() override { close(); }

  void
  set_pid(pid_t pid)
  {
    _pid = pid;
  }

  int
  exit_status() const
  {
    return _exit_code;
  }

  void
  close()
  {
    if (_pipe) {
      fclose(_pipe);
      _pipe = nullptr;
    }

    if (_pid > 0) {
      int status = -1;
      waitpid(_pid, &status, 0);
      if (WIFEXITED(status)) {
        _exit_code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        _exit_code = 128 + WTERMSIG(status);
      } else {
        _exit_code = -1;
      }
      _pid = -1;
    }
  }

protected:
  int
  underflow() override
  {
    if (!_pipe) {
      return traits_type::eof();
    }

    size_t n = fread(_buffer, 1, sizeof(_buffer), _pipe);
    if (n == 0) {
      return traits_type::eof();
    }

    setg(_buffer, _buffer, _buffer + n);
    return traits_type::to_int_type(*gptr());
  }

private:
  char  _buffer[65536];
  FILE *_pipe      = nullptr;
  pid_t _pid       = -1;
  int   _exit_code = -1;
};

struct ConfReader {
  std::unique_ptr<std::istream> stream;
  std::shared_ptr<HRW4UPipe>    pipebuf;
};

std::optional<ConfReader> openConfig(const std::string &filename);

///////////////////////////////////////////////////////////////////////////////
//
class Parser
{
public:
  Parser() = default; // No from/to URLs for this parser
  Parser(char *from_url, char *to_url) : _from_url(from_url), _to_url(to_url) {}

  // noncopyable
  Parser(const Parser &)         = delete;
  void operator=(const Parser &) = delete;

  // These are not const char *, because, you know, everything else with argv is a char *
  char *
  from_url() const
  {
    return _from_url;
  }

  char *
  to_url() const
  {
    return _to_url;
  }

  bool
  empty() const
  {
    return _empty;
  }

  bool
  is_cond() const
  {
    return _cond;
  }

  bool
  is_else() const
  {
    return _else;
  }

  const std::string &
  get_op() const
  {
    return _op;
  }

  std::string &
  get_arg()
  {
    return _arg;
  }

  const std::string &
  get_value() const
  {
    return _val;
  }

  bool
  mod_exist(const std::string &m) const
  {
    return std::find(_mods.begin(), _mods.end(), m) != _mods.end();
  }

  bool cond_is_hook(TSHttpHookID &hook) const;

  const std::vector<std::string> &
  get_tokens() const
  {
    return _tokens;
  }

  bool parse_line(const std::string &original_line);

  // We chose to have this take a std::string, since some of these conversions can not take a TextView easily
  template <typename NumericT>
  static NumericT
  parseNumeric(const std::string &s)
  {
    if (s.size() == 0) {
      return 0; // For the case where we have conditions that are "values".
    }

    try {
      if constexpr (std::is_same_v<NumericT, int>) {
        return std::stoi(s);
      } else if constexpr (std::is_same_v<NumericT, long>) {
        return std::stol(s);
      } else if constexpr (std::is_same_v<NumericT, long long>) {
        return std::stoll(s);
      } else if constexpr (std::is_same_v<NumericT, int8_t> || std::is_same_v<NumericT, int16_t> ||
                           std::is_same_v<NumericT, int32_t> || std::is_same_v<NumericT, int64_t>) {
        long long val = std::stoll(s);
        if (val < std::numeric_limits<NumericT>::min() || val > std::numeric_limits<NumericT>::max()) {
          throw std::out_of_range("Value out of range for signed type");
        }
        return static_cast<NumericT>(val);
      } else if constexpr (std::is_same_v<NumericT, unsigned long>) {
        return std::stoul(s);
      } else if constexpr (std::is_same_v<NumericT, unsigned long long>) {
        return std::stoull(s);
      } else if constexpr (std::is_same_v<NumericT, uint8_t> || std::is_same_v<NumericT, uint16_t> ||
                           std::is_same_v<NumericT, uint32_t> || std::is_same_v<NumericT, uint64_t>) {
        unsigned long long val = std::stoull(s);
        if (val > std::numeric_limits<NumericT>::max()) {
          throw std::out_of_range("Value out of range for unsigned type");
        }
        return static_cast<NumericT>(val);
      } else if constexpr (std::is_same_v<NumericT, float>) {
        return std::stof(s);
      } else if constexpr (std::is_same_v<NumericT, double>) {
        return std::stod(s);
      } else if constexpr (std::is_same_v<NumericT, long double>) {
        return std::stold(s);
      } else {
        static_assert(ALWAYS_FALSE_V<NumericT>, "Unsupported numeric type");
      }
    } catch (const std::exception &e) {
      throw std::runtime_error("Failed to parse numeric value: \"" + s + "\"");
    }
  }

private:
  bool preprocess(std::vector<std::string> tokens);

  bool                     _cond     = false;
  bool                     _else     = false;
  bool                     _empty    = false;
  char                    *_from_url = nullptr;
  char                    *_to_url   = nullptr;
  std::vector<std::string> _mods;
  std::string              _op;
  std::string              _arg;
  std::string              _val;

protected:
  std::vector<std::string> _tokens;
};

class HRWSimpleTokenizer
{
public:
  explicit HRWSimpleTokenizer(const std::string &line);

  // noncopyable
  HRWSimpleTokenizer(const HRWSimpleTokenizer &) = delete;
  void operator=(const HRWSimpleTokenizer &)     = delete;

  const std::vector<std::string> &
  get_tokens() const
  {
    return _tokens;
  }

protected:
  std::vector<std::string> _tokens;
};
