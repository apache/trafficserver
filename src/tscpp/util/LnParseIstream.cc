/** @file

  Defines LnParseIstream class.

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

#include <tscpp/util/LnParseIstream.h>

namespace ts
{
LnParseIstream::Options const LnParseIstream::_default_options{'#'};

int
LnParseIstream::skipEmpty(Options const &opt, std::istream &is, int &line_count)
{
  int result;

  for (;;) {
    result = LnParseIstream(opt, is);
    if (NO_FIELDS != result) {
      break;
    }
    ++line_count;
  }

  return result;
}

char
LnParseIstream::skipBs(std::istream &is)
{
  if (!is.good()) {
    return '\0';
  }

  char c;

  // Skip any white space.
  //
  for (;;) {
    c = is.peek();
    if (!is.good()) {
      return '\0';
    }
    if ((c != ' ') && (c != '\t')) {
      break;
    }
    is.get(c);
    if (!is.good()) {
      return '\0';
    }
  }
  return c;
}

char const *
LnParseIstream::errStr(int result)
{
  switch (result) {
  case NO_FIELDS:
    return "no fields found";
  case EXTRA_FIELDS:
    return "extra fields found";
  case END_OF_FILE:
    return "unexpected end of file";
  case STREAM_ERROR:
    return "unexpected end of file";
  default:
    break;
  }
  return "missing fields";
}

bool
LnParseIstream::_streamGood()
{
  if (_is.good()) {
    return true;
  }
  _result = STREAM_ERROR;
  return false;
}

char
LnParseIstream::_skip_bs()
{
  if (!_streamGood()) {
    return '\0';
  }
  if (_is.peek() == std::istream::traits_type::eof()) {
    _result = 0 == _result ? END_OF_FILE : STREAM_ERROR;
    return '\0';
  }

  char c;

  // Skip any blankspace / comment.
  //
  for (;;) {
    c = _is.peek();
    if (!_streamGood()) {
      return '\0';
    }
    if (('\0' != _opt.comment_start) && (c == _opt.comment_start)) {
      do {
        _is.get(c);
        if (!_streamGood()) {
          return '\0';
        }
        c = _is.peek();
        if (!_streamGood()) {
          return '\0';
        }
      } while (c != '\n');
      if (std::istream::traits_type::eof() == c) {
        _result = STREAM_ERROR;
        return '\0';
      }
      return '\n';
    }
    if ((c != ' ') && (c != '\t')) {
      break;
    }
    _is.get(c);
    if (!_streamGood()) {
      return '\0';
    }
  }
  if (std::istream::traits_type::eof() == c) {
    _result = STREAM_ERROR;
    return '\0';
  }
  return c;
}

void
LnParseIstream::_x()
{
  char c = _skip_bs();

  if ('\0' == c) {
    return;
  }
  if ('\n' == c) {
    _is.get(c);
    static_cast<void>(_streamGood());
  } else {
    _result = EXTRA_FIELDS;
  }
}

bool
LnParseIstream::_extract(LnParseIstream::Custom &v)
{
  char c;

  do {
    c = _is.peek();
    if (!_streamGood()) {
      return false;
    }
    if (_is.peek() == std::istream::traits_type::eof()) {
      _result = STREAM_ERROR;
      return false;
    }
    switch (v.next(c)) {
    case Custom::CONTINUE:
      break;
    case Custom::DONE:
      return true;
    default:
      _result = STREAM_ERROR;
      return false;
    }
    if ('\n' == c) {
      if (!v.done()) {
        _result = STREAM_ERROR;
        return false;
      }
      return true;
    }
    _is.get(c);
  } while (_streamGood());

  return false;
}

LnParseIstream::Quoted::PeekedCharReaction
LnParseIstream::Quoted::next(char peeked_char)
{
  if (_quote_char == peeked_char) {
    switch (_state) {
    case _START:
      _state = _MID;
      return CONTINUE;

    case _QUOTE_PENDING:
      _state = _MID;
      break;

    case _MID:
      _state = _QUOTE_PENDING;
      return CONTINUE;
    }
  } else {
    switch (_state) {
    case _START:
      return ERROR;

    case _QUOTE_PENDING:
      return DONE;

    case _MID:
      break;
    }
  }
  value += peeked_char;
  return CONTINUE;
}

bool
LnParseIstream::Quoted::done()
{
  return _QUOTE_PENDING == _state;
}

LnParseIstream::OptQuoted::PeekedCharReaction
LnParseIstream::OptQuoted::next(char peeked_char)
{
  if (_START == _state) {
    _state = _quote_char == peeked_char ? _QUOTED : _STR;
  }
  if (_QUOTED == _state) {
    return Quoted::next(peeked_char);
  }
  if ((' ' == peeked_char) || ('\t' == peeked_char)) {
    return DONE;
  }
  value += peeked_char;
  return CONTINUE;
}

bool
LnParseIstream::OptQuoted::done()
{
  return _QUOTED == _state ? Quoted::done() : true;
}

} // namespace ts

/*

TODO

Add optional line_continue_char, and optional pointer to line number counter to keep it up to day with
corrent number of lines.

struct Options {
  char const line_continue_char;
  int * const line_num;
  char const comment_start;

  Options(char lcc = '\0', int *l_num = nullptr, comment = '#')
  : line_continue_char(llc), line_num(l_num), comment_start(comment) {}
};

*/
