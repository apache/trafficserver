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
// Implemenation details for the rules class.
//
#include <fstream>
#include <ts/ts.h>

#include "rules.h"

namespace HeaderFilter {

const char* QUAL_DELIMITERS = "!/\"*[+=";


// RulesEntry implementations
void
RulesEntry::append(RulesEntry* entry)
{
  RulesEntry* n = this;

  while (NULL != n->_next)
    n = n->_next;
  n->_next = entry;
}

inline void
add_header(TSMBuffer& reqp, TSMLoc& hdr_loc, const char* hdr, int hdr_len, const char* val, int val_len)
{
  if (val_len <= 0) {
    TSDebug(PLUGIN_NAME, "\tWould set header %s to an empty value, skipping", hdr);
  } else {
    TSMLoc new_field;

    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(reqp, hdr_loc, hdr, hdr_len, &new_field)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, new_field, -1,  val, val_len))
        if (TS_SUCCESS == TSMimeHdrFieldAppend(reqp, hdr_loc, new_field))
          TSDebug(PLUGIN_NAME, "\tAdded header %s: %s", hdr, val);
      TSHandleMLocRelease(reqp, hdr_loc, new_field);
    }
  }
}

void
RulesEntry::execute(TSMBuffer& reqp, TSMLoc& hdr_loc) const
{
  if (_q_type == QUAL_ADD) {
    add_header(reqp, hdr_loc, _header, _h_len, _qualifier, _q_len);
  } else {
    TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, _header, _h_len);
    bool first_set = true;

    if (!field && _q_type == QUAL_SET) {
      add_header(reqp, hdr_loc, _header, _h_len, _qualifier, _q_len);
    } else {
      while (field) {
        TSMLoc tmp;
        int val_len = 0;
        const char* val = NULL;
        bool nuke = false;
      
        if (_q_type != QUAL_NONE)
          val = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field, -1, &val_len);

        switch (_q_type) {
        case QUAL_NONE:
          nuke = true;
          break;
        case QUAL_REGEX:
          if (val_len > 0) {
            nuke = pcre_exec(_rex,            // the compiled pattern
                             _extra,          // Extra data from study (maybe)
                             val,                // the subject string
                             val_len,            // the length of the subject
                             0,                  // start at offset 0 in the subject
                             0,                  // default options
                             NULL,               // no output vector for substring information
                             0) >= 0;
          }
          break;

        case QUAL_STRING:
          if (static_cast<size_t>(val_len) == _q_len) {
            if (_options & PCRE_CASELESS) {
              nuke = !strncasecmp(_qualifier, val, val_len);
            } else {
              nuke = !memcmp(_qualifier, val, val_len);
            }
          }
          break;

        case QUAL_PREFIX:
          if (static_cast<size_t>(val_len) >= _q_len) {
            if (_options & PCRE_CASELESS) {
              nuke = !strncasecmp(_qualifier, val, _q_len);
            } else {
              nuke = !memcmp(_qualifier, val, _q_len);
            }
          }
          break;

        case QUAL_POSTFIX: 
          if (static_cast<size_t>(val_len) >= _q_len) {
            if (_options & PCRE_CASELESS) {
              nuke = !strncasecmp(_qualifier, val + val_len - _q_len, _q_len);
            } else {
              nuke = !memcmp(_qualifier, val + val_len - _q_len, _q_len);
            }
          }
          break;
        case QUAL_SET:
          if (first_set) {
            nuke = false;
            first_set = false;
            if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(reqp, hdr_loc, field, -1, _qualifier, _q_len))
              TSDebug(PLUGIN_NAME, "\tSet header:  %s: %s", _header, _qualifier);
          } else {
            // Nuke all other "duplicates" of this header
            nuke = true;
          }
        
        default:
          break;
        }

        tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
        if (_inverse)
          nuke = !nuke;
        if (nuke) {
          if (TS_SUCCESS == TSMimeHdrFieldDestroy(reqp, hdr_loc, field))
            TSDebug(PLUGIN_NAME, "\tDeleting header %.*s", static_cast<int>(_h_len), _header);
        }
        TSHandleMLocRelease(reqp, hdr_loc, field);
        field = tmp;
      }
    }
  }
}


// Rules class implementations
Rules::~Rules()
{
  TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Rules");

  for (int i = 0; i < TS_HTTP_LAST_HOOK; ++i)
    delete _entries[i];
}

RulesEntry*
Rules::add_entry(const TSHttpHookID hook, const std::string& s, const std::string& q, QualifierTypes type, bool inverse, int options)
{
  RulesEntry* e = new(RulesEntry)(s, q, type, inverse, options);

  TSAssert(supported_hook(hook));
  if (NULL == _entries[hook]) {
    _entries[hook] = e;
  } else {
    _entries[hook]->append(e);
  }
  
  return e;
}

bool
Rules::parse_file(const char* filename)
{
  std::ifstream f;
  TSHttpHookID hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
  int lineno = 0;

  // TODO: Should we support "glob" here, to specify more than one filename?
  // TODO: Should we support a 'default' prefix here for the rules?
  f.open(filename, std::ios::in);
  if (!f.is_open()) {
    TSError("unable to open %s", filename);
    return false;
  }
  TSDebug(PLUGIN_NAME, "Parsing config file %s", filename);
  while (!f.eof()) {
    bool inverse = false;
    int options = 0;
    QualifierTypes type = QUAL_NONE;
    std::string line, word, qualifier;
    std::string::size_type pos1, pos2;

    getline(f, line);
    ++lineno;
    if (line.empty())
      continue;

    pos1 = line.find_first_not_of(" \t\n");
    if (pos1 != std::string::npos) {
      if (line[pos1] == '#') {
        continue; // Skip comments
      } else {
        pos2 = line.find_first_of("# \t\n", pos1+1); // end of word
        if (pos2 == std::string::npos) {
          word = line.substr(pos1);
          pos1 = pos2;
        } else {
          word = line.substr(pos1, pos2-pos1);
          pos1 = line.find_first_of(QUAL_DELIMITERS, pos2+1);
        }

        if (word == "[READ_REQUEST_HDR]") {
          hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
        } else if (word == "[SEND_REQUEST_HDR]") {
          hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
        } else if (word == "[READ_RESPONSE_HDR]") {
          hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
        } else if (word == "[SEND_RESPONSE_HDR]") {
          hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
        } else  if (word.empty()) {
          // Error case, but shouldn't happen?
        } else {
          // Treat everything else as headers (+ possibly a qualifier)
          if (pos1 != std::string::npos) { // Found a specifier
            if (line[pos1] == '!') {
              inverse = true;
              pos1 =  line.find_first_of(QUAL_DELIMITERS, pos1+1);
            }
            if (pos1 != std::string::npos) {
              char trailer = ' ';

              switch (line[pos1]) {
              case '/':
                type = QUAL_REGEX;
                trailer = '/';
                break;
              case '"':
                type = QUAL_STRING;
                trailer = '"';
                break;
              case '*':
                type = QUAL_POSTFIX;
                trailer = ']';
                break;
              case '[':
                type = QUAL_PREFIX;
                trailer = '*';
                break;
              case '+':
                type = QUAL_ADD;
                inverse = false; // Can never inverse the add operator
                trailer = '+';
                break;
              case '=':
                type = QUAL_SET;
                inverse = false; // Can never inverse the set operator
                trailer = '=';
                break;
              default:
                // TODO: Error case?
                break;
              }
                
              pos2 = line.find_last_of(trailer);
              if (pos2 != std::string::npos) {
                qualifier = line.substr(pos1+1, pos2-pos1-1);
                if (line[pos2+1] == 'i')
                  options |= PCRE_CASELESS;
                TSDebug(PLUGIN_NAME, "Adding '%s' to hook %d, type is %d, qualifier is %c %s (%c)",
                        word.c_str(), hook, type, inverse ? '!' : ' ', qualifier.c_str(), options & PCRE_CASELESS ? 'i' : ' ');
                add_entry(hook, word, qualifier, type, inverse, options);
              } else {
                TSError("Missing trailing delimiter in qualifier");
              }
            } else {
              TSError("Missing leading delimiter in qualifier");
            }
          } else {
            // No qualifier, so we'll nuke this header for all values
            TSDebug(PLUGIN_NAME, "Adding %s to hook %d (unqualified)", word.c_str(), hook);
            add_entry(hook, word);
          }
        }
      }
    }
  }

  return true;
}


void
Rules::execute(TSMBuffer& reqp, TSMLoc& hdr_loc, const TSHttpHookID hook) const
{
  TSAssert(supported_hook(hook));

  if (_entries[hook]) {
    RulesEntry* n = _entries[hook];

    TSDebug(PLUGIN_NAME, "Executing rules(s) for hook %d", hook);
    do {
      n->execute(reqp, hdr_loc);
    } while (NULL != (n = n->next()));
  }
}


} // End of namespace ::HeaderFilter


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
