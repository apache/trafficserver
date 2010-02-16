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
#ifndef _URL_MAPPING_PATH_INDEX_H

#define _URL_MAPPING_PATH_INDEX_H

#include <list>
#include <map>

#include "URL.h"
#include "UrlMapping.h"
#include "Trie.h"

class UrlMappingPathIndex
{
public:
  UrlMappingPathIndex() { };

  bool Insert(url_mapping *mapping);

  url_mapping *Search(URL *request_url, int request_port) const;

  typedef std::list<url_mapping *> MappingList;
  
  void GetMappings(MappingList &mapping_list) const;

  void Clear();

  virtual ~UrlMappingPathIndex() 
  {
    Clear();
  }


private:
  typedef Trie<url_mapping *> UrlMappingTrie;
  struct UrlMappingTrieKey 
  {
    URLType url_type;
    int port;
    UrlMappingTrieKey(URLType type, int p) : url_type(type), port(p) { };
    bool operator <(const UrlMappingTrieKey &rhs) const 
    {
      if (url_type == rhs.url_type) {
        return (port < rhs.port);
      }
      return (url_type < rhs.url_type);
    };
  };
  
  typedef std::map<UrlMappingTrieKey, UrlMappingTrie *> UrlMappingGroup;
  UrlMappingGroup m_tries;
    
  // make copy-constructor and assignment operator private
  // till we properly implement them
  UrlMappingPathIndex(const UrlMappingPathIndex &rhs) { };
  UrlMappingPathIndex &operator =(const UrlMappingPathIndex &rhs) { return *this; }

  inline UrlMappingTrie *_GetTrie(URL *url, URLType &url_type, int port) const
  {
    url_type = static_cast<URLType>(url->type_get());
    UrlMappingGroup::const_iterator group_iter = m_tries.find(UrlMappingTrieKey(url_type, port));
    if (group_iter != m_tries.end()) {
      return group_iter->second;
    }
    return 0;
  };
  
};

#endif // _URL_MAPPING_PATH_INDEX_H
