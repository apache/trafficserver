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

Parser::Parser(const std::string &line) : _cond(false), _empty(false)
{
  TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Parser");
  bool inquote            = false;
  bool extracting_token   = false;
  off_t cur_token_start   = 0;
  size_t cur_token_length = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    if (!inquote && (std::isspace(line[i]) || (line[i] == '=' || line[i] == '>' || line[i] == '<'))) {
      if (extracting_token) {
        cur_token_length = i - cur_token_start;

        if (cur_token_length) {
          _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        }

        extracting_token = false;
      } else if (!std::isspace(line[i])) {
        /* we got a standalone =, > or < */
        _tokens.push_back(std::string(1, line[i]));
      }
      continue; /* always eat whitespace */
    } else if (line[i] == '"') {
      if (!inquote && !extracting_token) {
        inquote          = true;
        extracting_token = true;
        cur_token_start  = i + 1; /* eat the leading quote */
        continue;
      } else if (inquote && extracting_token) {
        cur_token_length = i - cur_token_start;
        _tokens.push_back(line.substr(cur_token_start, cur_token_length));
        inquote          = false;
        extracting_token = false;
      } else {
        /* malformed */
        TSError("[%s] malformed line \"%s\" ignoring...", PLUGIN_NAME, line.c_str());
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

      if (line[i] == '=' || line[i] == '>' || line[i] == '<') {
        /* these are always a seperate token */
        _tokens.push_back(std::string(1, line[i]));
        continue;
      }

      extracting_token = true;
      cur_token_start  = i;
    }
  }

  if (extracting_token) {
    if (inquote) {
      // unterminated quote, error case.
      TSError("[%s] malformed line, unterminated quotation: \"%s\" ignoring...", PLUGIN_NAME, line.c_str());
      _tokens.clear();
      _empty = true;
      return;
    } else {
      /* we hit the end of the line while parsing a token, let's add it */
      _tokens.push_back(line.substr(cur_token_start));
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
      if (tokens.size() > 2 && (tokens[1][0] == '=' || tokens[1][0] == '>' || tokens[1][0] == '<')) { // cond + (=/</>) + argument
        _arg = tokens[1] + tokens[2];
      } else if (tokens.size() > 1) {
        _arg = tokens[1];
      } else
        _arg = "";
    } else {
      TSError("[%s] conditions must be embraced in %%{}", PLUGIN_NAME);
      return;
    }
  } else {
    // Operator has no qualifiers, but could take an optional second argumetn
    _op = tokens[0];
    if (tokens.size() > 1) {
      _arg = tokens[1];
      if (tokens.size() > 2)
        _val = tokens[2];
      else
        _val = "";
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
        TSError("[%s] mods have to be embraced in []", PLUGIN_NAME);
        return;
      }
    }
  }
}
