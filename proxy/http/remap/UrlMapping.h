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
#include "tscpp/util/TextView.h"
#include "tscpp/util/IntrusiveDList.h"

/** Used to store http referer strings (and/or regexp)
 **/
class RefererInfo
{
  using self_type = RefererInfo; ///< Self reference type.
public:
  RefererInfo() = default;
  RefererInfo(self_type &&that) : referer(that.referer), any(that.any), negative(that.negative), regx(that.regx)
  {
    that.regx = nullptr;
    that.referer.clear();
  }
  RefererInfo(self_type const &that) = delete;
  ~RefererInfo();
  self_type *_next = nullptr; ///< Intrusive forward link.
  self_type *_prev = nullptr; ///< Intrusive backward link.
  /// Intrusive link descriptor.
  using Linkage = ts::IntrusiveLinkage<self_type>;
  /// List of instances of this type.
  using List = ts::IntrusiveDList<Linkage>;

  static constexpr ts::TextView ANY_TAG{"*"};

  /** Parse @a text into a referer and flags.
   *
   * @param text Referer text to parse.
   * @param errw Error reporting buffer.
   * @return An empty view on success, an error string on failure.
   *
   * @note @a text must be null terminated (a C-string) with the null immediately following the view.
   * This is required by PCRE.
   */
  ts::TextView parse(ts::TextView text, ts::FixedBufferWriter &errw);

  ts::TextView referer;
  bool any      = false; /* any flag '*' */
  bool negative = false; /* negative referer '~' */
  pcre *regx    = nullptr;
};

/** Tokenized referer format string.
 *
 */
class RedirectChunk
{
  using self_type = RedirectChunk;

public:
  RedirectChunk() = default;
  /** Construct an initialized chunkd of type @a c and @a text.
   *
   * @param text Text for the chunk.
   * @param c Type of the chunk.
   */
  RedirectChunk(ts::TextView text, char c) noexcept : chunk(text), type(c) {}

  /// Text, if any, for this chunk.
  ts::TextView chunk;

  /** Type of chunk.
   * - 's' : string (literal)
   * - 'r' : referer
   * - 'f' : from URL
   * - 't' : to URL
   * - 'o' : origin URL
   */
  char type = 's'; /* s - string, r - referer, t - url_to, f - url_from, o - origin url */

  /** Parse the @a url into a vector of chunks in @a result.
   *
   * @param url [in] The URL (text string) to parse.
   * @param result [out] Parsed chunks are placed here.
   * @return @c true on success, @c false on error.
   */
  static bool parse(ts::TextView url, std::vector<self_type> &result);
};

/**
 * Used to store the mapping for class UrlRewrite
 **/
class url_mapping
{
public:
  ~url_mapping();

  bool add_plugin(RemapPluginInfo *i, void *ih);
  RemapPluginInfo *get_plugin(std::size_t) const;
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
  URL toURL; // Default TO-URL (from remap.config)
  bool homePageRedirect     = false;
  bool unique               = false; // INKqa11970 - unique mapping
  bool default_redirect_url = false;
  bool optional_referer     = false;
  bool negative_referer     = false;
  bool wildcard_from_scheme = false;   // from url is '/foo', only http or https for now
  ts::TextView tag          = nullptr; // tag
  ts::TextView filter_redirect_url;    // redirect url when referer filtering enabled
  unsigned int map_id = 0;
  RefererInfo::List referer_list;
  std::vector<RedirectChunk> redirect_chunks;
  bool ip_allow_check_enabled_p = false;
  /// List of filters that applyt to this remapping, in precedence order.
  std::vector<RemapFilter *> filters;
  LINK(url_mapping, link); // For use with the main Queue linked list holding all the mapping

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
  std::vector<RemapPluginInfo *> _plugin_list;
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
    _toURLPtr = m ? &(m->toURL) : nullptr;
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
