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
#include <iostream>
#include <string>
#include <sstream>

#include "ts/ts.h"

#include "parser.h"

enum ParserState { PARSER_DEFAULT, PARSER_IN_QUOTE, PARSER_IN_REGEX, PARSER_IN_EXPANSION };

Parser::Parser(const std::string &original_line) : _cond(false), _empty(false)
{
  std::string line        = original_line;
  ParserState state       = PARSER_DEFAULT;
  bool extracting_token   = false;
  off_t cur_token_start   = 0;
  size_t cur_token_length = 0;

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
        _tokens.push_back(std::string(1, line[i]));
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
    } else if ((state != PARSER_IN_REGEX) && (line[i] == '"')) {
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
        return;
      }
    } else if (!extracting_token) {
      if (_tokens.empty() && line[i] == '#') {
        // this is a comment line (it may have had leading whitespace before the #)
        _empty = true;
        break;
      }

      if ((line[i] == '=') || (line[i] == '+')) {
        // These are always a separate token
        _tokens.push_back(std::string(1, line[i]));
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
      return;
    }
  }

  if (_tokens.empty()) {
    _empty = true;
  } else {
    preprocess(_tokens);
  }
}

// This is the core "parser", parsing rule sets
void
Parser::preprocess(std::vector<std::string> tokens)
{
  // Special case for "conditional" values
  if (tokens[0].substr(0, 2) == "%{") {
    _cond = true;
  } else if (tokens[0] == "cond") {
    _cond = true;
    tokens.erase(tokens.begin());
  }

  // Is it a condition or operator?
  if (_cond) {
    if ((tokens[0].substr(0, 2) == "%{") && (tokens[0][tokens[0].size() - 1] == '}')) {
      std::string s = tokens[0].substr(2, tokens[0].size() - 3);

      _op = s;
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
      return;
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

  // The last token might be the "flags" section
  if (tokens.size() > 0) {
    std::string m = tokens[tokens.size() - 1];

    if (!m.empty() && (m[0] == '[')) {
      if (m[m.size() - 1] == ']') {
        m = m.substr(1, m.size() - 2);
        if (m.find_first_of(',') != std::string::npos) {
          std::istringstream iss(m);
          std::string t;
          while (getline(iss, t, ',')) {
            _mods.push_back(t);
          }
        } else {
          _mods.push_back(m);
        }
      } else {
        // Syntax error
        TSError("[%s] mods have to be enclosed in []", PLUGIN_NAME);
        return;
      }
    }
  }
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

  return false;
}

const std::vector<std::string> &
Parser::get_tokens() const
{
  return _tokens;
}

SimpleTokenizer::SimpleTokenizer(const std::string &original_line)
{
  std::string line        = original_line;
  ParserState state       = PARSER_DEFAULT;
  bool extracting_token   = false;
  off_t cur_token_start   = 0;
  size_t cur_token_length = 0;

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

const std::vector<std::string> &
SimpleTokenizer::get_tokens() const
{
  return _tokens;
}
