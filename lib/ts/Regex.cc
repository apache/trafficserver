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

#include "libts.h"
#include "Regex.h"

DFA::~DFA()
{
  dfa_pattern * p = _my_patterns;
  dfa_pattern * t = p;
  
  while(p) {
    if (p->_pe)
      pcre_free(p->_pe);
    if (p->_re)
      pcre_free(p->_re);
    if(p->_p)
      ats_free(p->_p);
    t = p->_next;
    ats_free(p);
    p = t;
  } 
}

dfa_pattern *
DFA::build(const char *pattern, REFlags flags)
{
  const char *error;
  int erroffset;
  dfa_pattern* ret;
  
  ret = (dfa_pattern*)ats_malloc(sizeof(dfa_pattern));
  ret->_p = NULL;
  
  if (flags & RE_CASE_INSENSITIVE)
    ret->_re = pcre_compile(pattern, PCRE_CASELESS|PCRE_ANCHORED, &error, &erroffset, NULL);
  else 
    ret->_re = pcre_compile(pattern, PCRE_ANCHORED, &error, &erroffset, NULL);
  
  if (error) {
    ats_free(ret);
    return NULL;
  }
  
  ret->_pe = pcre_study(ret->_re, 0, &error);
  
  if (error) {
    ats_free(ret);
    return NULL;
  }
  
  ret->_idx = 0;
  ret->_p = ats_strndup(pattern, strlen(pattern));
  ret->_next = NULL;
  return ret;
}

int DFA::compile(const char *pattern, REFlags flags) {
  ink_assert(_my_patterns == NULL);
  _my_patterns = build(pattern,flags);
  if (_my_patterns) 
    return 0;
  else 
    return -1;
}

int
DFA::compile(const char **patterns, int npatterns, REFlags flags)
{
  const char *pattern;
  dfa_pattern *ret = NULL;
  dfa_pattern *end = NULL;
  int i;
  //char buf[128];
  
  for (i = 0; i < npatterns; i++) {
    pattern = patterns[i];
    //snprintf(buf,128,"%s",pattern);
    ret = build(pattern,flags);
    if (!ret) {
      continue;
    }
    
    if (!_my_patterns) {
      _my_patterns = ret;
      _my_patterns->_next = NULL;
      _my_patterns->_idx = i;
    }
    else { 
      end = _my_patterns;
      while( end->_next ) {
        end = end->_next;
      }
      end->_next = ret; //add to end
      ret->_idx = i;
    }
    
  }
  
  return 0;
}

int
DFA::match(const char *str) const
{
  return match(str,strlen(str));
}

int
DFA::match(const char *str, int length) const
{
  int rc;
  int ovector[30];
  //int wspace[20];
  dfa_pattern * p = _my_patterns;
  
  while(p) {
    rc = pcre_exec(p->_re, p->_pe, str, length , 0, 0, ovector, 30/*,wspace,20*/);
    if (rc > 0) {
      return p->_idx;
    }
    p = p->_next;
  }

  return -1;
}

