/** @file

  Implementaion of ts::detail::DiagsTagHelper class, defiend in Diags.h.

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

#include <memory>
#include <list>
#include <unordered_map>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>

#include <tscore/Diags.h>
#include <tscore/Regex.h>

#include "OneWriterMultiReader.h"

// NOTE: access to instances of DiagEnabled is presumed to be atomic.

namespace ts
{
namespace detail
{
  struct DiagsTagHelper::Data {
    class TagMap
    {
    private:
      using _Map = std::unordered_map<std::string_view, DiagEnabled *>;

    public:
      // These make TagMap BasicLockable.  TagMap must be locked when it is being iterated over.
      // This lock must be protected by a mutex.
      //
      void
      lock()
      {
        _iter_read_lock.lock();
      }
      void
      unlock()
      {
        _iter_read_lock.unlock();
      }

      // Use these to iterate over TagMap (without adding adding or removing entries).
      //
      _Map::iterator
      begin()
      {
        return _m.begin();
      }
      _Map::iterator
      end()
      {
        return _m.end();
      }

      // Returns nullptr if tag not yet in map.
      //
      DiagEnabled *
      get_tag_enabled(char const *tag)
      {
        ts::ExclusiveWriterMultiReader::ReadLock rl(_map_mutex);

        auto it = _m.find(tag);

        return it != _m.end() ? it->second : nullptr;
      }

      DiagEnabled *
      new_tag(char const *tag)
      {
        ts::ExclusiveWriterMultiReader::WriteLock wl(_map_mutex);

        // Check and see if already added.
        //
        auto it = _m.find(tag);
        if (it != _m.end()) {
          return it->second;
        }

        auto ta = _tag_allocation();

        int sz  = std::strlen(tag) + 1;
        char *p = new char[sz];
        std::memcpy(p, tag, sz);
        ta->tag.reset(p);

        _m.emplace(p, &(ta->enabled_flag));

        return &(ta->enabled_flag);
      }

    private:
      _Map _m;

      ts::ExclusiveWriterMultiReader _map_mutex;

      ts::ExclusiveWriterMultiReader::ReadLock _iter_read_lock{_map_mutex, std::defer_lock};

      struct TagAllocation {
        std::unique_ptr<char[]> tag;
        DiagEnabled enabled_flag{false};
      };

      static const unsigned Tags_per_list_elem = 64;

      struct TagAllocationBlock {
        TagAllocation arr[Tags_per_list_elem];
      };

      // Reduce heap overhead by allocating for multiple tags at a time.  Put the allocations in a list so they
      // will be cleaned up at program termination.
      //
      std::list<TagAllocationBlock> _tag_space;

      unsigned _num_left_in_list_elem{0};

      TagAllocation *
      _tag_allocation()
      {
        if (!_num_left_in_list_elem) {
          _tag_space.emplace_back();
          _num_left_in_list_elem = Tags_per_list_elem;
        }
        --_num_left_in_list_elem;
        return &(_tag_space.back().arr[_num_left_in_list_elem]);
      }
    };

    TagMap tag_map;

    std::unique_ptr<DFA> activated_tags;

    // This mutex also ensures that the TagMap lock for iteration is only taken by one thread at a time.
    //
    std::mutex activated_tags_mutex;
  };

  DiagsTagHelper::DiagsTagHelper()
  {
    d[0] = new Data;
    d[1] = new Data;
  }

  DiagsTagHelper::~DiagsTagHelper()
  {
    delete d[0];
    delete d[1];
  }

  DiagEnabled const *
  DiagsTagHelper::flag_for_tag(const char *tag, DiagsTagType mode)
  {
    auto result = d[mode]->tag_map.get_tag_enabled(tag);

    if (result) {
      return result;
    }

    result = d[mode]->tag_map.new_tag(tag);

    std::lock_guard<std::mutex> lg(d[mode]->activated_tags_mutex);

    *result = d[mode]->activated_tags.get() ? (d[mode]->activated_tags.get()->match(tag) != -1) : false;

    return result;
  }

  void
  DiagsTagHelper::activate_taglist(const char *taglist, DiagsTagType mode)
  {
    std::lock_guard<std::mutex> lg(d[mode]->activated_tags_mutex);

    if (taglist) {
      d[mode]->activated_tags.reset(new DFA);
      d[mode]->activated_tags.get()->compile(taglist);
    } else {
      d[mode]->activated_tags.reset();
    }

    std::lock_guard<Data::TagMap> lg2(d[mode]->tag_map);

    // Locking tag_map for iteration takes a read lock on tag_map.  This will block any write
    // lock.  So the enable for a new tag will be set based on the new DFA.  Likewise, this
    // read lock will block on a current write lock.  So the new tag's enable flag will be
    // set based on the new tag list by the following iteration loop.

    for (auto &it : d[mode]->tag_map) {
      *it.second = d[mode]->activated_tags.get() ? (d[mode]->activated_tags.get()->match(it.first.data()) != -1) : false;
    }
  }

} // end namespace detail
} // end namespace ts
