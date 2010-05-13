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

#ifndef _URL_MAPPING_H_
#define _URL_MAPPING_H_

#include "AclFiltering.h"
#include <deque>
#include <map>
#include "Main.h"
#include "Error.h"
#include "URL.h"
#include "RemapPluginInfo.h"
#include "HttpTransact.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

/**
 * Used to store http referer strings (and/or regexp)
**/
class referer_info
{
public:
  referer_info(char *_ref, bool * error_flag = NULL, char *errmsgbuf = NULL, int errmsgbuf_size = 0);
   ~referer_info();
  referer_info *next;
  char *referer;
  int referer_size;
  bool any;                     /* any flag '*' */
  bool negative;                /* negative referer '~' */
  bool regx_valid;
  pcre* regx;
};

/**
 *
**/
class redirect_tag_str
{
public:
  redirect_tag_str():next(0), chunk_str(NULL), type(0)
  {
  }

   ~redirect_tag_str()
  {
    type = 0;
    if (chunk_str) {
      xfree(chunk_str);
      chunk_str = NULL;
    }
  }

  redirect_tag_str *next;
  char *chunk_str;
  char type;                    /* s - string, r - referer, t - url_to, f - url_from, o - origin url */
  static redirect_tag_str *parse_format_redirect_url(char *url);
};

/**
 *
**/
class url_mapping_ext
{
public:
  url_mapping_ext(url_mapping *);
  virtual ~ url_mapping_ext();
  const char *getToHost(int *lenp = NULL);
  const char *getToPath(int *lenp = NULL);
  URL *getFromURL();
  URL *getToURL();
private:
    url_mapping * m_mapping;
};

/**
 * Used to store the mapping for class UrlRewrite
**/
class url_mapping
{
public:
  url_mapping(int rank = 0);
  ~url_mapping();

  bool add_plugin(remap_plugin_info *);
  remap_plugin_info *get_plugin(unsigned int);

  ihandle *get_instance(remap_plugin_info *);
  ihandle *get_another_instance(remap_plugin_info * p);
  void delete_instance(remap_plugin_info * p);

  int from_path_len;
  URL fromURL;
  URL toURL;
  bool homePageRedirect;
  bool unique;                  // INKqa11970 - unique mapping
  bool default_redirect_url;
  bool optional_referer;
  bool negative_referer;
  bool no_negative_cache;       // @no_negative_cache option was used
  bool wildcard_from_scheme;    // from url is '/foo', only http or https for now
  int pristine_host_hdr;
  int chunking_enabled;
  char *tag;                    // tag
  char *filter_redirect_url;    // redirect url
  unsigned int map_id;
  referer_info *referer_list;
  redirect_tag_str *redir_chunk_list;
  acl_filter_rule *filter;      // acl filtering (list of rules)
  unsigned int _plugin_count;

  int getRank() const { return _rank; };

private:
    bool set_instance(remap_plugin_info *, ihandle *);
    std::deque<remap_plugin_info *>_plugin_list;
    std::map<remap_plugin_info *, ihandle *>_instance_map;
  int _cur_instance_count;
  int _rank;
};


#endif
