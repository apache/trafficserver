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
url_mapping::add_plugin(remap_plugin_info *i, void *ih)
{
  _plugin_list.push_back(i);
  _instance_data.push_back(ih);

  return true;
}

/**
 *
 **/
remap_plugin_info *
url_mapping::get_plugin(std::size_t index) const
{
  Debug("url_rewrite", "get_plugin says we have %zu plugins and asking for plugin %zu", plugin_count(), index);
  if (index < _plugin_list.size()) {
    return _plugin_list[index];
  }
  return nullptr;
}

void *
url_mapping::get_instance(std::size_t index) const
{
  if (index < _instance_data.size()) {
    return _instance_data[index];
  }
  return nullptr;
}

/**
 *
 **/
void
url_mapping::delete_instance(unsigned int index)
{
  void *ih             = get_instance(index);
  remap_plugin_info *p = get_plugin(index);

  if (ih && p && p->fp_tsremap_delete_instance) {
    p->fp_tsremap_delete_instance(ih);
  }
}

/**
 *
 **/
url_mapping::~url_mapping()
{
  referer_info *r;
  redirect_tag_str *rc;
  acl_filter_rule *afr;

  tag                 = (char *)ats_free_null(tag);
  filter_redirect_url = (char *)ats_free_null(filter_redirect_url);

  while ((r = referer_list) != nullptr) {
    referer_list = r->next;
    delete r;
  }

  while ((rc = redir_chunk_list) != nullptr) {
    redir_chunk_list = rc->next;
    delete rc;
  }

  // Delete all instance data, this gets ugly because to delete the instance data, we also
  // must know which plugin this is associated with. Hence, looping with index instead of a
  // normal iterator. ToDo: Maybe we can combine them into another container.
  for (std::size_t i = 0; i < plugin_count(); ++i) {
    delete_instance(i);
  }

  // Delete filters
  while ((afr = filter) != nullptr) {
    filter = afr->next;
    delete afr;
  }

  // Destroy the URLs
  fromURL.destroy();
  toUrl.destroy();
}

void
url_mapping::Print()
{
  char from_url_buf[131072], to_url_buf[131072];

  fromURL.string_get_buf(from_url_buf, (int)sizeof(from_url_buf));
  toUrl.string_get_buf(to_url_buf, (int)sizeof(to_url_buf));
  printf("\t %s %s=> %s %s <%s> [plugins %s enabled; running with %zu plugins]\n", from_url_buf, unique ? "(unique)" : "",
         to_url_buf, homePageRedirect ? "(R)" : "", tag ? tag : "", plugin_count() > 0 ? "are" : "not", plugin_count());
}

/**
 *
 **/
redirect_tag_str *
redirect_tag_str::parse_format_redirect_url(char *url)
{
  char *c;
  redirect_tag_str *r, **rr;
  redirect_tag_str *list = nullptr;
  char type              = 0;

  if (url && *url) {
    for (rr = &list; *(c = url) != 0;) {
      for (type = 's'; *c; c++) {
        if (c[0] == '%') {
          char tmp_type = (char)tolower((int)c[1]);
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
        // printf("\t***********'%c' - '%s'*******\n",r->type,r->chunk_str ? r->chunk_str : "<NULL>");
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
