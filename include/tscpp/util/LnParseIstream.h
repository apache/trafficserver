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

#pragma once

#include <istream>
#include <string>
#include <type_traits>

namespace ts
{
/*
The LnParseIstream class is used to extract blankspace-separated (blankspace meaning spaces and tabs) data fields
on a single line from a text istream.  For example, if the file data.txt containts the line:

666     argh     3.14     "it's cold"

then the code:

using ts::LnParseIstream;
std::ifstream("/tmp/data.txt");
int i;
std::string s;
double d;
LnParseIstream::Quoted qs;
int result = LnParseIstream(is, i, s, d, qs);

would put 4 in result, 666 in i, 3.14 in d, and "it's cold" in qs.value .  If "it's cold" where missing from the
line, result would be 3, and the contents of qs would be unchanged.  In general, if not all fields are present,
the result will reflect the number of fields present on the line, and the variables corresponding to the
missing fields will not be changed.  If there are more fields on the line than non-const references passed to
LnParseIstream(), the result EXTRA_FIELDS will be returned.  (Blankspace before the first extra field will
be consumed from the istream.)  So the above code could also be written:

using ts::LnParseIstream;
std::ifstream("/tmp/data.txt");
int i;
std::string s;
double d;
LnParseIstream::Quoted qs;
int result = LnParseIstream(is, i, s);
if (result == LnParseIstream::EXTRA_FIELDS) {
  result = LnParseIstream(is, d, qs);
}

(Whenever result is non-negative, that means that the end-of-line character for the line was consumed.)

Except for fields whose type inherits from LnParseIstream::Custom, like Quoted and OptQuoted, the istream
extraction operator, >> , is used to read the characters for the field and convert them to the value to store
in the variable.  The extraction operator used is the one for the type of the variable.
*/
class LnParseIstream
{
public:
  // Result values when no fields were found or an error occurred.
  //
  static const int NO_FIELDS{0};     // No fields found but not end of file.
  static const int EXTRA_FIELDS{-1}; // More fields on line than variables in fieldList parameter.
  static const int END_OF_FILE{-2};  // No fields found due to end of file.
  static const int STREAM_ERROR{-3}; // Stream read or format error.

  // Behavior options.
  //
  struct Options {
    // Allow for (ignored) side comments on lines.  The comment should be preceeded by one or more blanks/tabs,
    // followed by this character.  The remainder of the line is the comment.  (Set this to the null character
    // to disable comments).
    //
    char const comment_start;

    Options(char comment) : comment_start(comment) {}
  };

  template <typename... FLTypes> LnParseIstream(Options const &opt, std::istream &is, FLTypes &... fieldList) : _opt(opt), _is(is)
  {
    _x(fieldList...);
  }

  // Use # as the default comment start character.
  //
  template <typename... FLTypes>
  LnParseIstream(std::istream &is, FLTypes &... fieldList) : LnParseIstream(_default_options, is, fieldList...)
  {
  }

  // Implicitly converts to result.
  //
  operator int() const { return _result; }

  // Skips lines with no fields, and maintain a line count.  Returns one of the named constants above (except for
  // NO_FIELDS).  Returns EXTRA_FIELDS on success, meaning the next line has more than zero fields.
  //
  static int skipEmpty(Options const &opt, std::istream &is, int &line_count);
  static int
  skipEmpty(std::istream &is, int &line_count)
  {
    return skipEmpty(_default_options, is, line_count);
  }

  // Consumes blankspace (blank and tab) characters until a non-blankspaced character is peeked as next.
  // Returns peeked char (or null charater if stream not good).  You only need to call this if you need to
  // peek at the first character of the next field to know what type it is.  Remember that the next peeked
  // character may be the start character for a comment (which you'd typically use skipEmtpy() to consume).
  //
  static char skipBs(std::istream &is);

  // Customs fields in the field list passed to the constructor must inherit from this type.
  //
  class Custom
  {
    friend class LnParseIstream;

  protected:
    enum PeekedCharReaction { CONTINUE, DONE, ERROR };

  private:
    // If next() returns CONTINUE, it means it consumes the next peeked character from the stream.  If it returns
    // DONE, the Custom field is complete, and it does not consume the next peeked character.  If it returns ERROR,
    // the result value for the LnParseIstream will be STREAM_ERROR.
    //
    virtual PeekedCharReaction next(char peeked_char) = 0;

    // This is called if the peeked next character is end-of-line.  If it returns false, then the result value for
    // the LnParseIstream instance will be STREAM_ERROR.
    //
    virtual bool
    done()
    {
      return true;
    }
  };

  // Quoted string field.  If there is no error, quoted string will be in value field, without the quotes.
  // Use two quote characters in a row for a single quote character within the string.  An empty quoted
  // string is permitted.
  //
  class Quoted : public Custom
  {
  public:
    Quoted(char quote_char = '"') : _quote_char(quote_char) {}

    std::string value;

  protected:
    const char _quote_char;

    PeekedCharReaction next(char peeked_char) override;

    bool done() override;

  private:
    enum { _START, _QUOTE_PENDING, _MID } _state{_START};
  };

  // Optional quoted string field.  If the first character is the quote character, it should be a Quoted field.
  // Otherwise, it's terminated by a space or tab, or the end of line.
  //
  class OptQuoted : public Quoted
  {
  public:
    OptQuoted(char quote_char = '"') : Quoted(quote_char) {}

  private:
    PeekedCharReaction next(char peeked_char) override;

    bool done() override;

    enum { _START, _QUOTED, _STR } _state{_START};
  };

  // Return nul-terminated string describing return value if it is erroneous.
  static char const *errStr(int result);

private:
  Options const &_opt;

  std::istream &_is;

  int _result = 0;

  static Options const _default_options;

  bool _streamGood();

  char _skip_bs();

  void _x();

  template <typename T, typename std::enable_if<!std::is_base_of_v<Custom, T>, int>::type = 0>
  bool
  _extract(T &v)
  {
    _is >> v;
    return _streamGood();
  }

  bool _extract(Custom &v);

  template <typename T, typename... TTail>
  void
  _x(T &field, TTail &... tail)
  {
    char c = _skip_bs();

    if ('\0' == c) {
      return;
    }

    if (std::istream::traits_type::eof() == c) {
      _result = STREAM_ERROR;
      return;
    }

    if ('\n' == c) {
      char c;
      _is.get(c);
      static_cast<void>(_streamGood());

    } else {
      if (_extract(field)) {
        ++_result;

        _x(tail...);
      }
    }
  }

}; // end class LnParseIstream

} // namespace ts
