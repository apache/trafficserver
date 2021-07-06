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

#include "tscore/ink_defs.h"
#include "UrlMapping.h"
#include "records/I_RecCore.h"
#include "tscore/ink_cap.h"

/**
 *
 **/
bool
url_mapping::add_plugin_instance(RemapPluginInst *i)
{
  _plugin_inst_list.push_back(i);
  return true;
}

/**
 *
 **/
RemapPluginInst *
url_mapping::get_plugin_instance(std::size_t index) const
{
  Debug("url_rewrite", "get_plugin says we have %zu plugins and asking for plugin %zu", _plugin_inst_list.size(), index);
  if (index < _plugin_inst_list.size()) {
    return _plugin_inst_list[index];
  }
  return nullptr;
}

/**
 *
 **/
url_mapping::~url_mapping()
{
  referer_info *r;
  redirect_tag_str *rc;
  acl_filter_rule *afr;

  tag                 = static_cast<char *>(ats_free_null(tag));
  filter_redirect_url = static_cast<char *>(ats_free_null(filter_redirect_url));

  while ((r = referer_list) != nullptr) {
    referer_list = r->next;
    delete r;
  }

  while ((rc = redir_chunk_list) != nullptr) {
    redir_chunk_list = rc->next;
    delete rc;
  }

  // Delete filters
  while ((afr = filter) != nullptr) {
    filter = afr->next;
    delete afr;
  }

  // Destroy the URLs
  fromURL.destroy();
  toURL.destroy();
}

void
url_mapping::Print() const
{
  char from_url_buf[131072], to_url_buf[131072];

  fromURL.string_get_buf(from_url_buf, static_cast<int>(sizeof(from_url_buf)));
  toURL.string_get_buf(to_url_buf, static_cast<int>(sizeof(to_url_buf)));
  printf("\t %s %s=> %s %s <%s> [plugins %s enabled; running with %zu plugins]\n", from_url_buf, unique ? "(unique)" : "",
         to_url_buf, homePageRedirect ? "(R)" : "", tag ? tag : "", _plugin_inst_list.size() > 0 ? "are" : "not",
         _plugin_inst_list.size());
}

std::string
url_mapping::PrintRemapHitCount() const
{
  std::string result = "{\"fromURL\": \"" + remapKey + "\", \"hit_count\": " + std::to_string(_hitCount) + "}";
  return result;
}

/**
 *
 **/
redirect_tag_str *
redirect_tag_str::parse_format_redirect_url(char *url)
{
  char *c;
  redirect_tag_str *r;
  redirect_tag_str *list = nullptr;

  if (url && *url) {
    for (redirect_tag_str **rr = &list; *(c = url) != 0;) {
      char type = 0;
      for (type = 's'; *c; c++) {
        if (c[0] == '%') {
          char tmp_type = static_cast<char>(tolower(static_cast<int>(c[1])));
          if (tmp_type == 'r' || tmp_type == 'f' || tmp_type == 't' || tmp_type == 'o') {
            if (url == c) {
              type = tmp_type;
            }
            break;
          }
        }
      }
      r = new redirect_tag_str();
      if (likely(r)) {
        if ((r->type = type) == 's') {
          char svd     = *c;
          *c           = 0;
          r->chunk_str = ats_strdup(url);
          *c           = svd;
          url          = c;
        } else {
          url += 2;
        }
        (*rr = r)->next = nullptr;
        rr              = &(r->next);
      } else {
        break; /* memory allocation error */
      }
    }
  }
  return list;
}

/**
 *
 **/
referer_info::referer_info(char *_ref, bool *error_flag, char *errmsgbuf, int errmsgbuf_size)
  : next(nullptr), referer(nullptr), referer_size(0), any(false), negative(false), regx_valid(false)
{
  const char *error;
  int erroffset;

  if (error_flag) {
    *error_flag = false;
  }
  regx = nullptr;

  if (_ref) {
    if (*_ref == '~') {
      negative = true;
      _ref++;
    }
    if ((referer = ats_strdup(_ref)) != nullptr) {
      referer_size = strlen(referer);
      if (!strcmp(referer, "*")) {
        any = true;
      } else {
        regx = pcre_compile(referer, PCRE_CASELESS, &error, &erroffset, nullptr);
        if (!regx) {
          if (errmsgbuf && (errmsgbuf_size - 1) > 0) {
            ink_strlcpy(errmsgbuf, error, errmsgbuf_size);
          }
          if (error_flag) {
            *error_flag = true;
          }
        } else {
          regx_valid = true;
        }
      }
    }
  }
}

/**
 *
 **/
referer_info::~referer_info()
{
  ats_free(referer);
  referer      = nullptr;
  referer_size = 0;

  if (regx_valid) {
    pcre_free(regx);
    regx       = nullptr;
    regx_valid = false;
  }
}
