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
url_mapping::add_plugin(RemapPluginInfo *i, void *ih)
{
  _plugin_list.push_back(i);
  _instance_data.push_back(ih);

  return true;
}

/**
 *
 **/
RemapPluginInfo *
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
  void *ih           = get_instance(index);
  RemapPluginInfo *p = get_plugin(index);

  if (ih && p && p->delete_instance_cb) {
    p->delete_instance_cb(ih);
  }
}

/**
 *
 **/
url_mapping::~url_mapping()
{
  // Delete all instance data, this gets ugly because to delete the instance data, we also
  // must know which plugin this is associated with. Hence, looping with index instead of a
  // normal iterator. ToDo: Maybe we can combine them into another container.
  for (std::size_t i = 0; i < plugin_count(); ++i) {
    delete_instance(i);
  }

  // Destroy the URLs
  fromURL.destroy();
  toURL.destroy();
}

void
url_mapping::Print()
{
  char from_url_buf[131072], to_url_buf[131072];

  fromURL.string_get_buf(from_url_buf, (int)sizeof(from_url_buf));
  toURL.string_get_buf(to_url_buf, (int)sizeof(to_url_buf));
  printf("\t %s %s=> %s %s <%.*s> [plugins %s enabled; running with %zu plugins]\n", from_url_buf, unique ? "(unique)" : "",
         to_url_buf, homePageRedirect ? "(R)" : "", static_cast<int>(tag.size()), tag.data(), plugin_count() > 0 ? "are" : "not",
         plugin_count());
}

bool
RedirectChunk::parse(ts::TextView url, std::vector<self_type> &result)
{
  while (!url.empty()) {
    ts::TextView::size_type idx = 0;
    if (url.npos == idx || idx == url.size() - 1) { // all remaining text is a literal.
      result.emplace_back(url, 's');
      return true;
    }
    char c = url[idx + 1];
    if ('r' == c || 'f' == c || 't' == c || 'o' == c) {
      if (idx > 1) {
        result.emplace_back(url.substr(idx), 's');
      }
      result.emplace_back(ts::TextView{}, c);
      url.remove_prefix(idx + 2);
      idx = 0;
    }
  }
  return true;
}

ts::TextView
RefererInfo::parse(ts::TextView text, ts::FixedBufferWriter &errw)
{
  if (!text.empty()) {
    if (*text == '~') {
      negative = true;
      ++text;
    }
    referer = text;
    if (!text.empty()) {
      if (text == ANY_TAG) {
        any = true;
      } else {
        char const *err_msg = nullptr;
        int err_pos         = 0;
        regx                = pcre_compile(text.data(), PCRE_CASELESS, &err_msg, &err_pos, nullptr);
        if (!regx) {
          errw.write(err_msg);
          return errw.view();
        }
      }
    }
  }
  return {};
}

RefererInfo::~RefererInfo()
{
  if (regx) {
    pcre_free(regx);
  }
  regx = nullptr;
}
