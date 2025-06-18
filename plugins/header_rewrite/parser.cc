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
// parser.cc: implementation of the config parser
//
//
#include <utility>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "ts/ts.h"
#include "tscore/Layout.h"

#include "parser.h"

enum ParserState { PARSER_DEFAULT, PARSER_IN_QUOTE, PARSER_IN_REGEX, PARSER_IN_EXPANSION, PARSER_IN_BRACE, PARSER_IN_PAREN };

bool
Parser::parse_line(const std::string &original_line)
{
  std::string line             = original_line;
  ParserState state            = PARSER_DEFAULT;
  bool        extracting_token = false;
  off_t       cur_token_start  = 0;
  size_t      cur_token_length = 0;

  for (size_t i = 0; i < line.size(); ++i) {
    if ((state == PARSER_DEFAULT) && (std::isspace(line[i]) || ((line[i] == '=')))) {
      if (extracting_token) {
        cur_token_length = i - cur_token_start;
        if (cur_token_length > 0) {
          _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        }
        extracting_token = false;
        state            = PARSER_DEFAULT;
      } else if (!std::isspace(line[i])) {
        // we got a standalone =, > or <
        _tokens.emplace_back(1, line[i]);
      }
    } else if ((state != PARSER_IN_QUOTE) && (line[i] == '/')) {
      // Deal with regexes, nothing gets escaped / quoted in here
      if ((state != PARSER_IN_REGEX) && !extracting_token) {
        state            = PARSER_IN_REGEX;
        extracting_token = true;
        cur_token_start  = i;
      } else if ((state == PARSER_IN_REGEX) && extracting_token && (line[i - 1] != '\\')) {
        cur_token_length = i - cur_token_start + 1;
        _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        state            = PARSER_DEFAULT;
        extracting_token = false;
      }
    } else if ((state != PARSER_IN_REGEX) && (line[i] == '\\')) {
      // Escaping
      if (!extracting_token) {
        extracting_token = true;
        cur_token_start  = i;
      }
      line.erase(i, 1);
    } else if ((state != PARSER_IN_REGEX) && (state != PARSER_IN_PAREN) && (line[i] == '"')) {
      if ((state != PARSER_IN_QUOTE) && !extracting_token) {
        state            = PARSER_IN_QUOTE;
        extracting_token = true;
        cur_token_start  = i + 1; // Eat the leading quote
      } else if ((state == PARSER_IN_QUOTE) && extracting_token) {
        cur_token_length = i - cur_token_start;
        _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        state            = PARSER_DEFAULT;
        extracting_token = false;
      } else {
        // Malformed expression / operation, ignore ...
        TSError("[%s] malformed line \"%s\", ignoring", PLUGIN_NAME, line.c_str());
        _tokens.clear();
        _empty = true;
        return false;
      }
    } else if ((state == PARSER_DEFAULT) && ((i == 0 || line[i - 1] != '%') && line[i] == '{')) {
      state            = PARSER_IN_BRACE;
      extracting_token = true;
      cur_token_start  = i;
    } else if ((state == PARSER_IN_BRACE) && (line[i] == '}')) {
      cur_token_length = i - cur_token_start + 1;
      _tokens.push_back(line.substr(cur_token_start, cur_token_length));
      state            = PARSER_DEFAULT;
      extracting_token = false;
    } else if ((state == PARSER_DEFAULT) && (line[i] == '(')) {
      state            = PARSER_IN_PAREN;
      extracting_token = true;
      cur_token_start  = i;
    } else if ((state == PARSER_IN_PAREN) && (line[i] == ')')) {
      cur_token_length = i - cur_token_start + 1;
      _tokens.push_back(line.substr(cur_token_start, cur_token_length));
      state            = PARSER_DEFAULT;
      extracting_token = false;
    } else if (!extracting_token) {
      if (_tokens.empty() && line[i] == '#') {
        // this is a comment line (it may have had leading whitespace before the #)
        _empty = true;
        break;
      }

      if ((line[i] == '=') || (line[i] == '+')) {
        // These are always a separate token
        _tokens.emplace_back(1, line[i]);
        continue;
      }

      extracting_token = true;
      cur_token_start  = i;
    }
  }

  if (extracting_token) {
    if (state != PARSER_IN_QUOTE) {
      /* we hit the end of the line while parsing a token, let's add it */
      _tokens.push_back(line.substr(cur_token_start));
    } else {
      // unterminated quote, error case.
      TSError("[%s] malformed line, unterminated quotation: \"%s\", ignoring", PLUGIN_NAME, line.c_str());
      _tokens.clear();
      _empty = true;
      return false;
    }
  }

  if (_tokens.empty()) {
    _empty = true;
  } else {
    return preprocess(_tokens);
  }

  return true;
}

// This is the main "parser", a helper function to the above tokenizer. NOTE: this modifies (possibly) the tokens list,
// therefore, we pass in a copy of the parsers tokens here, such that the original token list is retained (useful for tests etc.).
bool
Parser::preprocess(std::vector<std::string> tokens)
{
  // The last token might be the "flags" section, lets consume it if it is
  if (tokens.size() > 0) {
    std::string m = tokens[tokens.size() - 1];

    if (!m.empty() && (m[0] == '[')) {
      if (m[m.size() - 1] == ']') {
        m = m.substr(1, m.size() - 2);
        if (m.find_first_of(',') != std::string::npos) {
          std::istringstream iss(m);
          std::string        t;
          while (getline(iss, t, ',')) {
            _mods.push_back(t);
          }
        } else {
          _mods.push_back(m);
        }
        tokens.pop_back(); // consume it, so we don't concatenate it into the value
      } else {
        // Syntax error
        TSError("[%s] mods have to be enclosed in []", PLUGIN_NAME);
        return false;
      }
    }
  }

  // Special case for "conditional" values
  if (tokens[0].substr(0, 2) == "%{") {
    _cond = true;
  } else if (tokens[0] == "cond") {
    _cond = true;
    tokens.erase(tokens.begin());
  } else if (tokens[0] == "else") {
    _cond = false;
    _else = true;

    return true;
  }

  // Is it a condition or operator?
  if (_cond) {
    if ((tokens[0].substr(0, 2) == "%{") && (tokens[0][tokens[0].size() - 1] == '}')) {
      _op = tokens[0].substr(2, tokens[0].size() - 3);
      if (tokens.size() > 2 && (tokens[1][0] == '=' || tokens[1][0] == '>' || tokens[1][0] == '<')) {
        // cond + [=<>] + argument
        _arg = tokens[1] + tokens[2];
      } else if (tokens.size() > 1) {
        // This is for the regular expression, which for some reason has its own handling?? ToDo: Why ?
        _arg = tokens[1];
      } else {
        // This would be for hook conditions, which has no argument.
        _arg = "";
      }
    } else {
      TSError("[%s] conditions must be embraced in %%{}", PLUGIN_NAME);
      TSError("[%s] token: '%s'", PLUGIN_NAME, tokens[0].c_str());
      return false;
    }
  } else {
    // Operator has no qualifiers, but could take an optional second argument
    _op = tokens[0];
    if (tokens.size() > 1) {
      _arg = tokens[1];

      if (tokens.size() > 2) {
        for (auto it = tokens.begin() + 2; it != tokens.end(); it++) {
          _val = _val + *it;
          if (std::next(it) != tokens.end()) {
            _val = _val + " ";
          }
        }
      } else {
        _val = "";
      }
    } else {
      _arg = "";
      _val = "";
    }
  }

  return true;
}

// Check if the operator is a condition, a hook, and if so, which hook. If the cond is not a hook
// we do not modify the hook itself, and return false.
bool
Parser::cond_is_hook(TSHttpHookID &hook) const
{
  if (!_cond) {
    return false;
  }

  if ("READ_RESPONSE_HDR_HOOK" == _op) {
    hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    return true;
  }
  if ("READ_REQUEST_HDR_HOOK" == _op) {
    hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
    return true;
  }
  if ("READ_REQUEST_PRE_REMAP_HOOK" == _op) {
    hook = TS_HTTP_PRE_REMAP_HOOK;
    return true;
  }
  if ("SEND_REQUEST_HDR_HOOK" == _op) {
    hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    return true;
  }
  if ("SEND_RESPONSE_HDR_HOOK" == _op) {
    hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    return true;
  }
  if ("REMAP_PSEUDO_HOOK" == _op) {
    hook = TS_REMAP_PSEUDO_HOOK;
    return true;
  }
  if ("TXN_START_HOOK" == _op) {
    hook = TS_HTTP_TXN_START_HOOK;
    return true;
  }
  if ("TXN_CLOSE_HOOK" == _op) {
    hook = TS_HTTP_TXN_CLOSE_HOOK;
    return true;
  }

  return false;
}

HRWSimpleTokenizer::HRWSimpleTokenizer(const std::string &line)
{
  ParserState state            = PARSER_DEFAULT;
  bool        extracting_token = false;
  off_t       cur_token_start  = 0;
  size_t      cur_token_length = 0;

  for (size_t i = 0; i < line.size(); ++i) {
    extracting_token = true;
    switch (state) {
    case PARSER_DEFAULT:
      if ((line[i] == '{') || (line[i] == '<')) {
        if (line[i - 1] == '%') {
          // pickup what we currently have
          cur_token_length = i - cur_token_start - 1;
          if (cur_token_length > 0) {
            _tokens.push_back(line.substr(cur_token_start, cur_token_length));
          }

          cur_token_start  = i - 1;
          state            = PARSER_IN_EXPANSION;
          extracting_token = false;
        }
      }
      break;
    case PARSER_IN_EXPANSION:
      if ((line[i] == '}') || (line[i] == '>')) {
        cur_token_length = i - cur_token_start + 1;
        if (cur_token_length > 0) {
          _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        }
        cur_token_start  = i + 1;
        state            = PARSER_DEFAULT;
        extracting_token = false;
      }
      break;
    default:
      break;
    }
  }

  // take what was left behind
  if (extracting_token) {
    _tokens.push_back(line.substr(cur_token_start));
  }
}

// This is the universal configuration reader, which can read both
// a raw file, as well as executing an external compiler (hrw4u) to parse
// the configuration file.
namespace
{
void
_log_stderr(int fd)
{
  char        buffer[512];
  std::string partial;

  while (ssize_t n = read(fd, buffer, sizeof(buffer))) {
    if (n <= 0) {
      break;
    }
    partial.append(buffer, n);
    size_t pos = 0;
    while ((pos = partial.find('\n')) != std::string::npos) {
      std::string line = partial.substr(0, pos);
      TSError("[header_rewrite: hrw4u] %s", line.c_str());
      partial.erase(0, pos + 1);
    }
  }

  if (!partial.empty()) {
    TSError("[hrw4u] stderr: %s", partial.c_str());
  }

  close(fd);
}
} // namespace

std::optional<ConfReader>
openConfig(const std::string &filename)
{
  namespace fs             = std::filesystem;
  const std::string suffix = ".hrw4u";
  std::string       hrw4u  = Layout::get()->bindir + "/traffic_hrw4u";

  static const bool has_compiler = [hrw4u]() {
    fs::path        path(hrw4u);
    std::error_code ec;
    auto            status = fs::status(path, ec);
    auto            perms  = status.permissions();
    return fs::exists(path, ec) && fs::is_regular_file(path, ec) && (perms & fs::perms::owner_exec) != fs::perms::none;
  }();

  if (filename.ends_with(suffix) && has_compiler) {
    int pipe_fds[2];
    int stderr_pipe[2];

    if (pipe(pipe_fds) != 0 || pipe(stderr_pipe) != 0) {
      TSError("[header_rewrite] failed to create pipe for hrw4u compiler: %s", strerror(errno));
      return std::nullopt;
    }

    pid_t pid = fork();
    if (pid < 0) {
      TSError("[header_rewrite] failed to fork for hrw4u compiler: %s", strerror(errno));
      return std::nullopt;
    } else if (pid == 0) {
      dup2(pipe_fds[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(pipe_fds[0]);
      close(stderr_pipe[0]);

      const char *argv[] = {hrw4u.c_str(), filename.c_str(), nullptr};
      execvp(argv[0], const_cast<char **>(argv));
      _exit(127); // child exec failed
    }

    // Parent
    close(pipe_fds[1]);
    close(stderr_pipe[1]);

    _log_stderr(stderr_pipe[0]);

    auto pipebuf = std::make_shared<HRW4UPipe>(fdopen(pipe_fds[0], "r"));
    pipebuf->set_pid(pid);
    auto stream = std::make_unique<std::istream>(pipebuf.get());

    return ConfReader{.stream = std::move(stream), .pipebuf = std::move(pipebuf)};
  } else {
    auto file = std::make_unique<std::ifstream>(filename);
    if (!file->is_open()) {
      return std::nullopt;
    }

    return ConfReader{.stream = std::move(file), .pipebuf = nullptr};
  }
}
