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

/**
 * @file fetch_policy_lru.cc
 * @brief LRU fetch policy.
 */

#include "fetch_policy_lru.h"
#include "common.h"

inline const char *
FetchPolicyLru::name()
{
  return "lru";
}

bool
FetchPolicyLru::init(const char *parameters)
{
  if (nullptr == parameters) {
    /* Leave defaults */
  } else {
    size_t size = 0;

    /* look for buckets first */
    const char *sizeStr = parameters;
    const char *delim   = strchr(parameters, ',');

    if (nullptr == delim) {
      /* no divider specified, set the buckets */
      size = getValue(sizeStr, strlen(sizeStr));
    } else {
      /* set the buckets */
      size = getValue(sizeStr, delim - sizeStr);
    }

    /* Defaults are considered minimum */
    static const char *defaultStr = " (default)";
    bool useDefault               = false;

    /* Make sure size is not larger than what std::list is physically able to hold */
    LruList::size_type realMax = _list.max_size();
    if (size > realMax) {
      PrefetchDebug("size: %lu is not feasible, cutting to %lu", size, realMax);
      size = realMax;
    }
    /* Guarantee minimum value */
    if (size > _maxSize) {
      _maxSize = size;
    } else {
      useDefault = true;
      PrefetchError("size: %lu is not a good value", size);
    };

    PrefetchDebug("initialized %s fetch policy: size: %lu%s", name(), _maxSize, (useDefault ? defaultStr : ""));
  }

  return true;
}

inline size_t
FetchPolicyLru::getMaxSize()
{
  return _maxSize;
}

inline size_t
FetchPolicyLru::getSize()
{
  return _size;
}

bool
FetchPolicyLru::acquire(const std::string &url)
{
  bool ret = false;

  LruHash hash;
  hash.init(url.c_str(), url.length());

  LruMapIterator it = _map.find(&hash);

  if (_map.end() != it) {
    PrefetchDebug("recently used LRU entry, moving to front");

    /* We have an entry in the LRU */
    PrefetchAssert(_list.size() > 0);

    /* Move to the front of the list */
    _list.splice(_list.begin(), _list, it->second);

    /* Don't trigger fetch if the url is found amongst the most recently used ones */
    ret = false;
  } else {
    /* New LRU entry */
    if (_size >= _maxSize) {
      /* Move the last (least recently used) element to the front and remove it from the hash table. */
      _list.splice(_list.begin(), _list, --_list.end());
      _map.erase(&(*_list.begin()));
      PrefetchDebug("reused the least recently used LRU entry");
    } else {
      /* With this implementation we are never removing LRU elements from the list but just updating the front element of the list
       * so the following addition should happen at most FetchPolicyLru::_maxSize number of times */
      _list.push_front(NULL_LRU_ENTRY);
      _size++;
      PrefetchDebug("created a new LRU entry, size=%d", (int)_size);
    }
    /* Update the "new" or the most recently used LRU entry and add it to the hash */
    *_list.begin()          = hash;
    _map[&(*_list.begin())] = _list.begin();

    /* Trigger fetch since the URL is not amongst the most recently used ones */
    ret = true;
  }

  log("acquire", url, ret);
  return ret;
}

bool
FetchPolicyLru::release(const std::string &url)
{
  log("release", url, true);
  return true;
}
