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
#include "ink_config.h"
#include "UrlMappingPathIndex.h"

bool
UrlMappingPathIndex::Insert(url_mapping *mapping)
{
  URLType url_type;
  int port = (mapping->fromURL).port_get();
  UrlMappingTrie *trie;
  int from_path_len;
  const char *from_path;

  trie = _GetTrie(&(mapping->fromURL), url_type, port);
  
  if (!trie) {
    trie = new UrlMappingTrie();
    m_tries.insert(UrlMappingGroup::value_type(UrlMappingTrieKey(url_type, port), trie));
    Debug("UrlMappingPathIndex::Insert", "Created new trie for url type, port combo <%d, %d>", url_type, port);
  }
  
  from_path = mapping->fromURL.path_get(&from_path_len);
  if (!trie->Insert(from_path, mapping, mapping->getRank(), from_path_len)) {
    Error("Couldn't insert into trie!");
    return false;
  }
  Debug("UrlMappingPathIndex::Insert", "Inserted new element!");
  return true;
}

url_mapping *
UrlMappingPathIndex::Search(URL *request_url, int request_port) const
{
  url_mapping **retval = 0;
  URLType url_type;
  UrlMappingTrie *trie;
  int path_len;
  const char *path;

  trie = _GetTrie(request_url, url_type, request_port);
  
  if (!trie) {
    Debug("UrlMappingPathIndex::Search", "No mappings exist for url type, port combo <%d, %d>",
          url_type, request_port);
    goto lFail;
  }

  path = request_url->path_get(&path_len);
  if (!trie->Search(path, retval, path_len)) {
    Debug("UrlMappingPathIndex::Search", "Couldn't find entry for url with path [%.*s]", path_len, path);
    goto lFail;
  }
  return *retval;
  
lFail:
  return 0;
}

void
UrlMappingPathIndex::GetMappings(MappingList &mapping_list) const
{
  for (UrlMappingGroup::const_iterator group_iter = m_tries.begin(); 
       group_iter != m_tries.end(); ++group_iter) {
    const UrlMappingTrie::ValuePointerList &value_pointers = group_iter->second->GetValues();
    for (UrlMappingTrie::ValuePointerList::const_iterator list_iter = value_pointers.begin();
         list_iter != value_pointers.end(); ++list_iter) {
      mapping_list.push_back(*(*list_iter));
    }
  }
}

void
UrlMappingPathIndex::Clear()
{
  for (UrlMappingGroup::iterator group_iter = m_tries.begin(); group_iter != m_tries.end(); ++group_iter) {
    delete group_iter->second;
  }
  m_tries.clear();
}
