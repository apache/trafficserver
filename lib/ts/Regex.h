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

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif


enum REFlags
{
  RE_CASE_INSENSITIVE = 1
};

typedef struct __pat {
  int _idx;
  pcre *_re;
  pcre_extra *_pe;
  char *_p;
  __pat * _next;
} dfa_pattern;

class DFA
{
public:
  DFA():_my_patterns(0) {
  }
  
  ~DFA();

  int compile(const char *pattern, REFlags flags = (REFlags) 0);
  int compile(const char **patterns, int npatterns, REFlags flags = (REFlags) 0);
  dfa_pattern * build(const char *pattern, REFlags flags = (REFlags) 0);
  
  int match(const char *str) const;
  int match(const char *str, int length) const;

private:
  dfa_pattern * _my_patterns;
};


#endif /* __TS_REGEX_H__ */
