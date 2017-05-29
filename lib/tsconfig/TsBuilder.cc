/** @file

    Implementation of the handler for parsing events.

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

# include "TsBuilder.h"
# include "TsErrataUtil.h"
# include "TsConfigLexer.h"
# include "TsConfigGrammar.hpp"
# include <cstdlib>

// Prefix for text of our messages.
# define PRE "Configuration Parser: "

namespace {
/** Compress a string by removing escape characters.
    @return The new length of the string.
*/
size_t unescape_string(char* text, size_t len) {
  size_t zret = len;
  // quick check - if no escape char, do nothing.
  char* dst = static_cast<char*>(memchr(text, '\\', len));
  if (dst) {
    char* limit = text + len;
    char* src = dst + 1; // skip escape char
    for ( *dst++ = *src++ ; src < limit ; ++src ) {
      if ('\\' != *src) { *dst++ = *src;
      } else if (++src < limit) { *dst++ = *src;
      } else { *dst++ = '\\'; // trailing backslash.
    
}
}zret = dst - text;
  }
  return zret;
}
} // anon namespace

namespace ts { namespace config {

Builder&
Builder::init() {
  // Fill in each element to dispatch through the static
  // method. Callback data is a pointer to an entry in @c dispatch
  // which contains pointer to this object and a pointer to the
  // appropriate dispatch method.

  // Zero everything first, just to be safe.
  memset(_dispatch, 0, sizeof(_dispatch));
  memset(&_loc, 0, sizeof(_loc));

  for ( size_t i = 0 ; i < TS_CONFIG_N_EVENT_TYPES ; ++i) {
    _dispatch[i]._ptr = this;
    _handlers.handler[i]._f = &self::dispatch;
    _handlers.handler[i]._data = &(_dispatch[i]);
  }

  _dispatch[TsConfigEventGroupOpen]._method = &self::groupOpen;
  _dispatch[TsConfigEventGroupName]._method = &self::groupName;
  _dispatch[TsConfigEventGroupClose]._method = &self::groupClose;
  _dispatch[TsConfigEventListOpen]._method = &self::listOpen;
  _dispatch[TsConfigEventListClose]._method = &self::listClose;
  _dispatch[TsConfigEventPathOpen]._method = &self::pathOpen;
  _dispatch[TsConfigEventPathTag]._method = &self::pathTag;
  _dispatch[TsConfigEventPathIndex]._method = &self::pathIndex;
  _dispatch[TsConfigEventPathClose]._method = &self::pathClose;
  _dispatch[TsConfigEventLiteralValue]._method = &self::literalValue;
  _dispatch[TsConfigEventInvalidToken]._method = &self::invalidToken;

  _handlers.error._data = this;
  _handlers.error._f = &self::syntaxErrorDispatch;

  return *this;
}

// Error messages here have to just be logged, as they effectively report that
// the dispatcher can't find the builder object.
void
Builder::dispatch(void* data, Token* token) {
    if (data) {
        Handler* handler = reinterpret_cast<Handler*>(data);
        if (handler->_ptr) {
            if (handler->_method) {
                ((handler->_ptr)->*(handler->_method))(*token);
            } else {
                msg::logf(msg::WARN, PRE "Unable to dispatch event - no method.");
            }
        } else {
            msg::logf(msg::WARN, PRE "Unable to dispatch event - no builder.");
        }
    } else {
        msg::logf(msg::WARN, PRE "Unable to dispatch event - no handler.");
    }
}

int
Builder::syntaxErrorDispatch(void* data, char const* text) {
    return reinterpret_cast<Builder*>(data)->syntaxError(text);
}

int
Builder::syntaxError(char const* text) {
  msg::logf(_errata, msg::WARN,
    "Syntax error '%s' near line %d, column %d.",
    text, tsconfiglex_current_line(), tsconfiglex_current_col()
  );
  return 0;
}

Rv<Configuration>
Builder::build(Buffer const& buffer) {
  _v = _config.getRoot(); // seed current value.
  _errata.clear(); // no errors yet.
  tsconfig_parse_buffer(&_handlers, buffer._ptr, buffer._size);
  return MakeRv(_config, _errata);
}

void
Builder::groupOpen(Token const& token) {
    _v = _v.makeGroup(_name);
    _v.setSource(token._loc._line, token._loc._col);
}
void Builder::groupClose(Token const&) {
    _v = _v.getParent();
}
void Builder::groupName(Token const& token) {
    _name.set(token._s, token._n);
}
void Builder::listOpen(Token const& token) {
    _v = _v.makeList(_name);
    _v.setSource(token._loc._line, token._loc._col);
}
void Builder::listClose(Token const&) {
    _v = _v.getParent();
}

void Builder::pathOpen(Token const&) {
    _path.reset();
    _extent.reset();
}
void Builder::pathTag(Token const& token) {
    _path.append(Buffer(token._s, token._n));
    if (_extent._ptr) {
      _extent._size = token._s - _extent._ptr + token._n;
    } else {
      _extent.set(token._s, token._n);
      _loc = token._loc;
    }
}
void Builder::pathIndex(Token const& token){
    // We take advantage of the lexer - token will always be a valid
    // digit string that is followed by a non-digit or the FLEX
    // required double null at the end of the input buffer.
    _path.append(Buffer(nullptr, static_cast<size_t>(atol(token._s))));
    if (_extent._ptr) { _extent._size = token._s - _extent._ptr + token._n;
    } else { _extent.set(token._s, token._n);
}
}

void Builder::pathClose(Token const&) {
    Rv<Value> cv = _v.makePath(_path, _name);
    if (cv.isOK()) {
      cv.result().setText(_extent).setSource(_loc._line, _loc._col);
      // Terminate path. This will overwrite trailing whitespace or
      // the closing angle bracket, both of which are expendable.
      _extent._ptr[_extent._size] = 0;
    }
    _name.reset();
    _extent.reset();
}

void Builder::literalValue(Token const& token) {
    Rv<Value> cv;
    Buffer text(token._s, token._n);

    // It's just too painful to use these strings with standard
    // libraries without nul terminating. For strings we convert the
    // trailing quote. For integers we abuse the fact that the parser
    // can't reduce using this token before the lexer has read at
    // least one char ahead.

    // Note the nul is *not* included in the reported length.

    if (INTEGER == token._type) {
        cv = _v.makeInteger(text, _name);
        token._s[token._n] = 0;
    } else if (STRING == token._type) {
        ++text._ptr, text._size -= 2;  // Don't include the quotes.
        text._size = unescape_string(text._ptr, text._size);
        text._ptr[text._size] = 0; // OK because we have the trailing quote.
        cv = _v.makeString(text, _name);
    } else {
        msg::logf(_errata, msg::WARN, PRE "Unexpected literal type %d.", token._type);
    }
    if (!cv.isOK()) { _errata.pull(cv.errata());
}
    if (cv.result()) { cv.result().setSource(token._loc._line, token._loc._col);
}
    _name.set(nullptr,0); // used, so clear it.
}
void Builder::invalidToken(Token const&) { }

}} // namespace ts::config
