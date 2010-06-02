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

#include "UrlMapping.h"

/**
 *
**/
url_mapping::url_mapping(int rank /* = 0 */)
  : from_path_len(0), fromURL(), homePageRedirect(false), unique(false), default_redirect_url(false),
    optional_referer(false), negative_referer(false), no_negative_cache(false), wildcard_from_scheme(false),
    pristine_host_hdr(-1), chunking_enabled(-1), tag(NULL), filter_redirect_url(NULL), referer_list(0),
    redir_chunk_list(0), filter(NULL), _plugin_count(0), _cur_instance_count(0), _rank(rank), _default_to_url()
{ /* nop */ ;
}


/**
 *
**/
bool url_mapping::add_plugin(remap_plugin_info * i)
{
  _plugin_list.push_back(i);
  _plugin_count++;
  return true;
}


/**
 *
**/
remap_plugin_info *
url_mapping::get_plugin(unsigned int index)
{
  Debug("url_rewrite", "get_plugin says we have %d plugins and asking for plugin %d", _plugin_count, index);
  remap_plugin_info *plugin = NULL;

  if (unlikely((index > _plugin_count) || index < 0)) {
    return NULL;
  }

  std::deque<remap_plugin_info *>::iterator i;
  unsigned int j = 0;

  for (i = _plugin_list.begin(); i != _plugin_list.end(); i++) {
    if (j == index) {
      plugin = *i;
      return plugin;
    }
    j++;
  }
  Debug("url_rewrite", "url_mapping::get_plugin could not find requested plugin");
  return NULL;
}


/**
 *
**/
bool url_mapping::set_instance(remap_plugin_info * p, ihandle * h)
{
  Debug("url_rewrite", "Adding handle: %x to instance map for plugin: %x (%s) [cur:%d]", h, p, p->path,
        _cur_instance_count);
  _instance_map[p] = h;
  return true;
}


/**
 *
**/
ihandle *
url_mapping::get_instance(remap_plugin_info * p)
{
  Debug("url_rewrite", "Requesting instance handle for plugin: %x [%s]", p, p->path);
  ihandle *h = _instance_map[p];

  Debug("url_rewrite", "Found instance handle: %x for plugin: %x [%s]", h, p, p->path);
  return h;
}

/**
 *
**/
ihandle *
url_mapping::get_another_instance(remap_plugin_info * p)
{
  ihandle *ih = NEW(new ihandle);

  _cur_instance_count++;
  if (_cur_instance_count >= 15) {
    Error("Cant have more than 15 remap handles!");
    Debug("url_rewrite", "Cant have more than 15 remap handles!");
    abort();
  }
  set_instance(p, ih);
  return ih;
}


/**
 *
**/
void
url_mapping::delete_instance(remap_plugin_info * p)
{
  Debug("url_rewrite", "Deleting instance handle and plugin for %x [%s]", p, p->path);
  _cur_instance_count--;
  ihandle *ih = get_instance(p);

  if (ih && p && p->fp_tsremap_delete_instance) {
    p->fp_tsremap_delete_instance(*ih);
    delete ih;
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

  if (tag) {
    tag = (char *) xfree_null(tag);
  }
  if (filter_redirect_url) {
    filter_redirect_url = (char *) xfree_null(filter_redirect_url);
  }
  while ((r = referer_list) != 0) {
    referer_list = r->next;
    delete r;
  }
  while ((rc = redir_chunk_list) != 0) {
    redir_chunk_list = rc->next;
    delete rc;
  }

  //iterate all plugins and delete them
  std::deque<remap_plugin_info *>::iterator i;
  remap_plugin_info *plugin = NULL;

  for (i = _plugin_list.begin(); i != _plugin_list.end(); i++) {
    plugin = *i;
    if (plugin)
      delete_instance(plugin);
  }

  while ((afr = filter) != NULL) {
    filter = afr->next;
    delete afr;
  }

  // Destroy the URLs
  fromURL.destroy();
  _default_to_url.destroy();
}

/**
 *
**/
redirect_tag_str *
redirect_tag_str::parse_format_redirect_url(char *url)
{
  char *c;
  redirect_tag_str *r, **rr;
  redirect_tag_str *list = 0;
  char type = 0;

  if (url && *url) {
    for (rr = &list; *(c = url) != 0;) {
      for (type = 's'; *c; c++) {
        if (c[0] == '%') {
          char tmp_type = (char) tolower((int) c[1]);
          if (tmp_type == 'r' || tmp_type == 'f' || tmp_type == 't' || tmp_type == 'o') {
            if (url == c)
              type = tmp_type;
            break;
          }
        }
      }
      r = NEW(new redirect_tag_str());
      if (likely(r)) {
        if ((r->type = type) == 's') {
          char svd = *c;
          *c = 0;
          r->chunk_str = xstrdup(url);
          *c = svd;
          url = c;
        } else
          url += 2;
        (*rr = r)->next = 0;
        rr = &(r->next);
        //printf("\t***********'%c' - '%s'*******\n",r->type,r->chunk_str ? r->chunk_str : "<NULL>");
      } else
        break;                  /* memory allocation error */
    }
  }
  return list;
}


/**
 *
**/
referer_info::referer_info(char *_ref, bool * error_flag, char *errmsgbuf, int errmsgbuf_size):next(0), referer(0), referer_size(0), any(false), negative(false),
regx_valid(false)
{
  const char *error;
  int erroffset;

  if (error_flag)
    *error_flag = false;
  regx = NULL;

  if (_ref) {
    if (*_ref == '~') {
      negative = true;
      _ref++;
    }
    if ((referer = xstrdup(_ref)) != 0) {
      referer_size = strlen(referer);
      if (!strcmp(referer, "*"))
        any = true;
      else {
        regx = pcre_compile(referer, PCRE_CASELESS, &error, &erroffset, NULL);
        if (!regx) {
          if (errmsgbuf && (errmsgbuf_size - 1) > 0)
            ink_strncpy(errmsgbuf, error, errmsgbuf_size - 1);
          if (error_flag)
            *error_flag = true;
        } else
          regx_valid = true;
      }
    }
  }
}


/**
 *
**/
referer_info::~referer_info()
{
  if (referer)
    xfree(referer);
  referer = 0;
  referer_size = 0;

  if (regx_valid) {
    pcre_free(regx);
    regx = NULL;
    regx_valid = false;
  }
}
