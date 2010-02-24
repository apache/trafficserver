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

#ifndef __TS_REGEX_H__
#define __TS_REGEX_H__

#if (HOST_OS == darwin) // FIXME: includes case sensitve(?)
#include </usr/include/regex.h>
#endif

/*
  regular expression rules:

  x            - match the character 'x'
  .            - any character
  [xyz]        - a "character class"; in this case, the pattern
                 matches either an 'x', a 'y' or a 'z'
  [abj-oZ]     - a "character class" with a range in it; matches
                 an 'a', a 'b', any letter from 'j' through 'o',
                 or a 'Z'
  [^A-Z]       - a "negated character class", i.e., any character
                 but those in the class.
  r*           - zero or more r's, where r is any regular expression
  r+           - one or more r's
  r?           - zero or one r's (that is, "an optional r")
  r{2,5}       - anywhere from two to five r's
  r{2,}        - two or more r's
  r{4}         - exactly 4 r's
  "[xyz]\"foo" - the literal string: [xyz]"foo
  \X           - if X is an 'a', 'b', 'f', 'n', 'r', 't', or 'v',
                 then the ANSI-C interpretation of \x. Otherwise,
                 a literal 'X'. (Used to escape operators such as
                 '*')
  \0           - A NUL character
  \123         - the character with octal value 123
  \x2a         - the character with hexadecimal value 2a
  (r)          - match an r; parentheses are used to override
                 precedence
  rs           - the regular expresion r followed by the regular
                 expression s
  r|s          - either an r or an s
  #<n>#        - inserts an "end" node causing regular expression
                 matching to stop when it is reached and for the
		 value n to be returned.
*/


#include "ink_port.h"
#include "DynArray.h"


enum REFlags
{
  RE_CASE_INSENSITIVE = 1 << 0,
  RE_NO_WILDCARDS = 1 << 1
};


class DFA
{
public:
  DFA();
  ~DFA();

  int compile(const char *pattern, REFlags flags = (REFlags) 0);
  int compile(const char **patterns, int npatterns, REFlags flags = (REFlags) 0);
  int compile(const char *filename, const char **patterns, int npatterns, REFlags flags = (REFlags) 0);

  int match(const char *str);
  int match(const char *str, int length);
  int match(const char *&str, int length, int &state);
  int size();

private:
    DynArray<ink32> basetbl;
    DynArray<ink32> accepttbl;
    DynArray<ink32> nexttbl;
    DynArray<ink32> checktbl;
};


#endif /* __TS_REGEX_H__ */
