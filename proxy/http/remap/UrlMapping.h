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

#include <vector>

#include "tscore/ink_config.h"
#include "AclFiltering.h"
#include "URL.h"
#include "RemapPluginInfo.h"
#include "tscore/Regex.h"
#include "tscore/List.h"

/**
 * Used to store http referer strings (and/or regexp)
 **/
class referer_info
{
public:
  referer_info(char *_ref, bool *error_flag = nullptr, char *errmsgbuf = nullptr, int errmsgbuf_size = 0);
  ~referer_info();
  referer_info *next;
  char *referer;
  int referer_size;
  bool any;      /* any flag '*' */
  bool negative; /* negative referer '~' */
  bool regx_valid;
  pcre *regx;
};

/**
 *
 **/
class redirect_tag_str
{
public:
  redirect_tag_str() : next(nullptr), chunk_str(nullptr), type(0) {}
  ~redirect_tag_str()
  {
    type = 0;
    if (chunk_str) {
      ats_free(chunk_str);
      chunk_str = nullptr;
    }
  }

  redirect_tag_str *next;
  char *chunk_str;
  char type; /* s - string, r - referer, t - url_to, f - url_from, o - origin url */
  static redirect_tag_str *parse_format_redirect_url(char *url);
};

/**
 * Used to store the mapping for class UrlRewrite
 **/
class url_mapping
{
public:
  ~url_mapping();

  bool add_plugin(remap_plugin_info *i, void *ih);
  remap_plugin_info *get_plugin(std::size_t) const;
  void *get_instance(std::size_t) const;

  std::size_t
  plugin_count() const
  {
    return _plugin_list.size();
  }

  void delete_instance(unsigned int index);
  void Print();

  int from_path_len = 0;
  URL fromURL;
  URL toUrl; // Default TO-URL (from remap.config)
  bool homePageRedirect              = false;
  bool unique                        = false; // INKqa11970 - unique mapping
  bool default_redirect_url          = false;
  bool optional_referer              = false;
  bool negative_referer              = false;
  bool wildcard_from_scheme          = false;   // from url is '/foo', only http or https for now
  char *tag                          = nullptr; // tag
  char *filter_redirect_url          = nullptr; // redirect url when referer filtering enabled
  unsigned int map_id                = 0;
  referer_info *referer_list         = nullptr;
  redirect_tag_str *redir_chunk_list = nullptr;
  bool ip_allow_check_enabled_p      = false;
  acl_filter_rule *filter            = nullptr; // acl filtering (list of rules)
  LINK(url_mapping, link);                      // For use with the main Queue linked list holding all the mapping

  int
  getRank() const
  {
    return _rank;
  };
  void
  setRank(int rank)
  {
    _rank = rank;
  };

private:
  std::vector<remap_plugin_info *> _plugin_list;
  std::vector<void *> _instance_data;
  int _rank = 0;
};

/**
 * UrlMappingContainer wraps a url_mapping object and allows a caller to rewrite the target URL.
 * This is used while evaluating remap rules.
 **/
class UrlMappingContainer
{
public:
  UrlMappingContainer() : _mapping(nullptr), _toURLPtr(nullptr), _heap(nullptr) {}
  explicit UrlMappingContainer(HdrHeap *heap) : _mapping(nullptr), _toURLPtr(nullptr), _heap(heap) {}
  ~UrlMappingContainer() { deleteToURL(); }
  URL *
  getToURL() const
  {
    return _toURLPtr;
  };
  URL *
  getFromURL() const
  {
    return _mapping ? &(_mapping->fromURL) : nullptr;
  };

  url_mapping *
  getMapping() const
  {
    return _mapping;
  };

  void
  set(url_mapping *m)
  {
    deleteToURL();
    _mapping  = m;
    _toURLPtr = m ? &(m->toUrl) : nullptr;
  }

  void
  set(HdrHeap *heap)
  {
    _heap = heap;
  }

  URL *
  createNewToURL()
  {
    ink_assert(_heap != nullptr);
    deleteToURL();
    _toURL.create(_heap);
    _toURLPtr = &_toURL;
    return _toURLPtr;
  }

  void
  deleteToURL()
  {
    if (_toURLPtr == &_toURL) {
      _toURL.clear();
    }
  }

  void
  clear()
  {
    deleteToURL();
    _mapping  = nullptr;
    _toURLPtr = nullptr;
    _heap     = nullptr;
  }

  // noncopyable, non-assignable
  UrlMappingContainer(const UrlMappingContainer &orig) = delete;
  UrlMappingContainer &operator=(const UrlMappingContainer &rhs) = delete;

private:
  url_mapping *_mapping;
  URL *_toURLPtr;
  URL _toURL;
  HdrHeap *_heap;
};
