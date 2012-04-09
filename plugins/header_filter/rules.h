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
// 
// Class for holding a set of configurations. There would be one global configuration, used
// by all hooks / requests, and one optional configuration for each remap rule.
//
#ifndef __RULES_H__
#define __RULES_H__ 1

#include "ts/ts.h"
#include "ink_config.h"

#include <string>
#include <string.h>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include "lulu.h"

namespace HeaderFilter {

// The delimiters might look arbitrary, but are choosen to make parsing trivial
enum QualifierTypes {
  QUAL_NONE = 0,
  QUAL_REGEX = 1,		// Regular expression, /match/
  QUAL_STRING = 2,		// Full string, "match"
  QUAL_PREFIX = 3,		// Sub-string prefix, [match*
  QUAL_POSTFIX = 4,		// Sub-string postfix, *match]
  // This is a semi-hack, but whatever (for now, until we get Lua module done!
  QUAL_ADD = 5,			// Add the header +string+
  QUAL_SET = 6			// Set the header =string=, leaving only one header with the new value
};


class RulesEntry
{
public:
  RulesEntry(const std::string& s, const std::string& q, QualifierTypes type, bool inverse, int options)
    : _header(NULL), _h_len(0), _qualifier(NULL), _q_len(0), _q_type(type), _rex(NULL), _extra(NULL),
      _inverse(inverse), _options(options), _next(NULL)
  {
    if (s.length() > 0) {
      _header = TSstrdup(s.c_str());
      _h_len = s.length();
    }
    
    if (q.length() > 0) {
      _qualifier = TSstrdup(q.c_str());
      _q_len = q.length();
      if (_q_type == QUAL_REGEX) {
        const char* error;
        int erroffset;

        _rex = pcre_compile(_qualifier,           // the pattern
                            _options,             // default options
                            &error,               // for error message
                            &erroffset,           // for error offset
                            NULL);                // use default character tables
        if (!_rex)
          TSError("header_filter: PCRE failed on %s at offset %d: %s\n", _qualifier, erroffset, error);
      }
    }

    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for RulesEntry, header is %s, qualifier is %s", _header, _qualifier);
  }

  ~RulesEntry()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for RulesEntry");
    delete _next; // Potentially "deep" recursion, but should be OK.
    if (_header)
      TSfree(_header);

    if (QUAL_REGEX == _q_type) {
      if (_rex)
        pcre_free(_rex);
      if (_extra)
        pcre_free(_extra);
    }
    if (_qualifier)
      TSfree(_qualifier);
  }

  void append(RulesEntry* entry);
  void execute(TSMBuffer& reqp, TSMLoc& hdr_loc) const; // This is really the meat of the app
  RulesEntry* next() const { return _next; }

private:
  DISALLOW_COPY_AND_ASSIGN(RulesEntry);

  char* _header;
  size_t _h_len;
  char* _qualifier;
  size_t _q_len;
  QualifierTypes _q_type;
  pcre* _rex;
  pcre_extra* _extra;
  bool _inverse;
  int _options;
  RulesEntry* _next;
};


class Rules
{
public:
  Rules()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Rules");
    memset(_entries, 0, sizeof(_entries));
  }

  virtual ~Rules();

  RulesEntry* add_entry(const TSHttpHookID hook, const std::string& s, const std::string& q="", QualifierTypes type = QUAL_NONE,
                        bool inverse=false, int options=0);
  bool parse_file(const char* filename);

  bool supported_hook(const TSHttpHookID hook) const
  {
    return ((hook == TS_HTTP_READ_REQUEST_HDR_HOOK) ||
	    (hook == TS_HTTP_SEND_REQUEST_HDR_HOOK) ||
	    (hook == TS_HTTP_READ_RESPONSE_HDR_HOOK) ||
	    (hook == TS_HTTP_SEND_RESPONSE_HDR_HOOK));
  }

  void execute(TSMBuffer& reqp, TSMLoc& hdr_loc, const TSHttpHookID hook) const;

private:
  DISALLOW_COPY_AND_ASSIGN(Rules);

  RulesEntry* _entries[TS_HTTP_LAST_HOOK]; // One possible set of entries for each hook
};

} // End of namespace ::HeaderFilter


#endif // __RULES_H__



/*
  local variables:
  mode: C++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-comment-only-line-offset: 0
  c-file-offsets: ((statement-block-intro . +)
  (label . 0)
  (statement-cont . +)
  (innamespace . 0))
  end:

  Indent with: /usr/bin/indent -ncs -nut -npcs -l 120 logstats.cc
*/
