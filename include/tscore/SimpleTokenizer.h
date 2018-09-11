/** @file

  A brief file description

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

#include <cstdlib>
#include <cstring>
#include <cctype>
#include "tscore/ink_memory.h"

/*-----------------------------------------------------------------------------
  SimpleTokenizer

  This class provides easy token parsing from an input string. It supports:

  1- ignoring (or not) of null fields
  2- left whitespace trimming
  3- right whitespace trimming
  4- escaping the delimiter character with a user defined escape character

  The class has two constructors, one that defines the input string,
  and another one that does not. If the latter is used, then the
  setString method should be used to set the data string.

  Both constructors set the delimiter, the operation mode (which
  defines bullets 1-3 above), and the escape character.

  The available methods are:

  void setString(char *s)
  sets the data string to s. The mode specified upon construction of the
  tokenizer determines whether s is copied or not.

  char *getNext()
  returns the next token, or NULL if there are no more tokens. This method
  uses the delimiter specified upon object construction.

  char *getNext(char delimiter)
  similar to getNext(), but allows the user to change the delimiter (just for
  this call).

  char *getNext(int count)
  get the next count tokens as a single token (ignoring the delimiters in
  between).

  char *getNext(char delimiter, int count)
  this is similar to getNext(int count) but allows user to specify the
  delimiter.

  IMPORTANT: the char pointers returned by the SimpleTokenizer are valid
  ONLY during the lifetime of the object. The copy of the input string
  is destroyed by the object's destructor.

  char *getRest()
  returns the rest of the tokens all together. Advances pointer so a
  subsequent call to getNext returns NULL;

  char *peekAtRestOfString()
  returns the rest of the input string, but DOES NOT advance pointer so a
  subsequent call to getNext does return the next token (if there is still
  one).

  size_t getNumTokensRemaining()
  returns the number of tokens remaining in the string (using the delimiter
  specified upon object construction).

  size_t getNumTokensRemaining(char delimiter)
  similar to the above, but allows the user to change the delimiter (just for
  this call).

  Note that multiple delimiters are not supported (more than one per call).

  examples:

  SimpleTokenizer tok("one    two\\ and\\ three four:   five : six");
  tok.getNumTokensRemaining() --> 5     note calculation is done assuming
                                        space is the delimiter
  tok.getNext() -> "one"
  tok.getNext() -> "two and three"
  tok.getNext(':') -> "four"
  tok.peekAtRestOfString() -> "   five  : six"
  tok.getNext(':') -> "five"

  SimpleTokenizer tok(",  with null fields ,,,", ',',
                      CONSIDER_NULL_FIELDS | KEEP_WHITESPACE);
  tok.getNext() -> ""
  tok.getNext() -> "  with null fields "
  tok.getNumTokensRemaining() -> 3

  ---------------------------------------------------------------------------*/

class SimpleTokenizer
{
public:
  // by default, null fields are disregarded, whitespace is trimmed left
  // and right, and input string is copied (not overwritten)
  //
  enum {
    CONSIDER_NULL_FIELDS   = 1,
    KEEP_WHITESPACE_LEFT   = 2,
    KEEP_WHITESPACE_RIGHT  = 4,
    KEEP_WHITESPACE        = KEEP_WHITESPACE_LEFT + KEEP_WHITESPACE_RIGHT,
    OVERWRITE_INPUT_STRING = 8
  };

  SimpleTokenizer(char delimiter = ' ', unsigned mode = 0, char escape = '\\')
    : _data(nullptr), _delimiter(delimiter), _mode(mode), _escape(escape), _start(0), _length(0)
  {
  }

  // NOTE: The input strring 's' is overwritten for mode OVERWRITE_INPUT_STRING.
  SimpleTokenizer(const char *s, char delimiter = ' ', unsigned mode = 0, char escape = '\\')
    : _data(nullptr), _delimiter(delimiter), _mode(mode), _escape(escape)
  {
    setString(s);
  }

  ~SimpleTokenizer() { _clearData(); }
  void
  setString(const char *s)
  {
    _clearData();

    _start  = 0;
    _length = strlen(s);
    _data   = (_mode & OVERWRITE_INPUT_STRING ? const_cast<char *>(s) : ats_strdup(s));

    // to handle the case where there is a null field at the end of the
    // input string, we replace the null character at the end of the
    // string with the delimiter (and consider the string to be one
    // character larger).
    //
    _data[_length++] = _delimiter;
  };
  char *
  getNext(int count = 1)
  {
    return _getNext(_delimiter, false, count);
  };
  char *
  getNext(char delimiter, int count = 1)
  {
    return _getNext(delimiter, false, count);
  }
  char *
  getRest()
  {
    // there can't be more than _length tokens, so we get the rest
    // of the tokens by requesting _length of them
    //
    return _getNext(_delimiter, false, _length);
  }
  size_t
  getNumTokensRemaining()
  {
    return _getNumTokensRemaining(_delimiter);
  };
  size_t
  getNumTokensRemaining(char delimiter)
  {
    return _getNumTokensRemaining(delimiter);
  };
  char *
  peekAtRestOfString()
  {
    _data[_length - 1] = 0;
    return (_start < _length ? &_data[_start] : &_data[_length - 1]);
  }

private:
  char *_data; // a pointer to the input data itself,
  // or to a copy of it
  char _delimiter; // the token delimiter
  unsigned _mode;  // flags that determine the
  // mode of operation
  char _escape;  // the escape character
  size_t _start; // pointer to the start of the next
  // token
  size_t _length; // the length of _data

  void
  _clearData()
  {
    if (_data && !(_mode & OVERWRITE_INPUT_STRING)) {
      ats_free(_data);
    }
  }

  char *
  _getNext(char delimiter, bool countOnly = false, int numTokens = 1)
  {
    char *next = nullptr;

    if (_start < _length) {
      // set start
      //
      bool hasEsc = false; // escape character seen
      while (_start < _length &&
             ((!(_mode & CONSIDER_NULL_FIELDS) &&
               (_data[_start] == delimiter && !(_start && (_data[_start - 1] == _escape ? (hasEsc = true) : 0)))) ||
              (!(_mode & KEEP_WHITESPACE_LEFT) && isspace(_data[_start])))) {
        ++_start;
      }

      if (_start < _length) // data still available
      {
        // update the extra delimiter just in case the function
        // is called with a different delimiter from the previous one
        //
        _data[_length - 1] = delimiter;

        next = &_data[_start];

        // set end
        //
        size_t end     = _start;
        int delimCount = 0;
        while (end < _length && (_data[end] != delimiter || (end && (_data[end - 1] == _escape ? (hasEsc = true) : 0)) ||
                                 ((++delimCount < numTokens) && (end < _length - 1)))) {
          ++end;
        }

        _start = end + 1;

        // there can be delimiters at the end if the number of tokens
        // requested is larger than 1, remove them if the
        // CONSIDER_NULL_FIELDS flag is not set
        //
        if (!(_mode & CONSIDER_NULL_FIELDS)) {
          while (_data[--end] == delimiter)
            ;
          ++end;
        }

        if (!(_mode & KEEP_WHITESPACE_RIGHT)) {
          while (isspace(_data[--end]))
            ;
          ++end;
        }

        if (!countOnly) {
          _data[end] = 0;

          // remove escape characters only if the number of
          // delimiters is one
          //
          if (hasEsc && delimCount == 1) {
            int numEscape = 0, i = 0;
            while (next[i]) {
              if (next[i] == _escape) {
                ++numEscape;
              } else {
                next[i - numEscape] = next[i];
              }
              ++i;
            }
            _data[end - numEscape] = 0;
          }
        }
      }
    }
    return next;
  };

  size_t
  _getNumTokensRemaining(char delimiter)
  {
    size_t startSave = _start; // save current position
    size_t count     = 0;
    while (_getNext(delimiter, true)) {
      ++count;
    };
    _start = startSave;
    return count;
  };
};
