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

/*****************************************************************************
 *
 *  ControlMatcher.cc - Implementation of general purpose matcher
 *
 *
 ****************************************************************************/

#include <sys/types.h>

#include "ts/ink_config.h"
#include "ts/MatcherUtils.h"
#include "ts/Tokenizer.h"
#include "ProxyConfig.h"
#include "ControlMatcher.h"
#include "CacheControl.h"
#include "ParentSelection.h"
#include "ts/HostLookup.h"
#include "HTTP.h"
#include "URL.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "P_Cache.h"
#include "P_SplitDNS.h"

/****************************************************************
 *   Place all template instantiations at the bottom of the file
 ****************************************************************/

// HttpRequestData accessors
//   Can not be inlined due being virtual functions
//
char *
HttpRequestData::get_string()
{
  char *str = hdr->url_string_get(nullptr);

  if (str) {
    unescapifyStr(str);
  }
  return str;
}

const char *
HttpRequestData::get_host()
{
  return hostname_str;
}

sockaddr const *
HttpRequestData::get_ip()
{
  return &dest_ip.sa;
}

sockaddr const *
HttpRequestData::get_client_ip()
{
  return &src_ip.sa;
}

/*************************************************************
 *   Begin class HostMatcher
 *************************************************************/

template <class Data, class MatchResult>
HostMatcher<Data, MatchResult>::HostMatcher(const char *name, const char *filename) : BaseMatcher<Data>(name, filename)
{
  host_lookup = new HostLookup(name);
}

template <class Data, class MatchResult> HostMatcher<Data, MatchResult>::~HostMatcher()
{
  delete host_lookup;
}

//
// template <class Data,class MatchResult>
// void HostMatcher<Data,MatchResult>::Print()
//
//  Debugging Method
//
template <class Data, class MatchResult>
void
HostMatcher<Data, MatchResult>::Print()
{
  printf("\tHost/Domain Matcher with %d elements\n", num_el);
  host_lookup->Print(PrintFunc);
}

//
// template <class Data,class MatchResult>
// void HostMatcher<Data,MatchResult>::PrintFunc(void* opaque_data)
//
//  Debugging Method
//
template <class Data, class MatchResult>
void
HostMatcher<Data, MatchResult>::PrintFunc(void *opaque_data)
{
  Data *d = (Data *)opaque_data;
  d->Print();
}

// void HostMatcher<Data,MatchResult>::AllocateSpace(int num_entries)
//
//  Allocates the the HostLeaf and Data arrays
//
template <class Data, class MatchResult>
void
HostMatcher<Data, MatchResult>::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  host_lookup->AllocateSpace(num_entries);

  data_array = new Data[num_entries];
  array_len  = num_entries;
  num_el     = 0;
}

// void HostMatcher<Data,MatchResult>::Match(RequestData* rdata, MatchResult* result)
//
//  Searches our tree and updates argresult for each element matching
//    arg hostname
//
template <class Data, class MatchResult>
void
HostMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result)
{
  void *opaque_ptr;
  Data *data_ptr;
  bool r;

  // Check to see if there is any work to do before makeing
  //   the stirng copy
  if (num_el <= 0) {
    return;
  }

  HostLookupState s;

  r = host_lookup->MatchFirst(rdata->get_host(), &s, &opaque_ptr);

  while (r == true) {
    ink_assert(opaque_ptr != nullptr);
    data_ptr = (Data *)opaque_ptr;
    data_ptr->UpdateMatch(result, rdata);

    r = host_lookup->MatchNext(&s, &opaque_ptr);
  }
}

//
// Result HostMatcher<Data,MatchResult>::NewEntry(bool domain_record,
//          char* match_data, char* match_info, int line_num)
//
//   Creates a new host/domain record
//
//   If successful, returns NULL
//   If not, returns a pointer to malloc allocated error string
//     that the caller MUST DEALLOCATE
//
template <class Data, class MatchResult>
Result
HostMatcher<Data, MatchResult>::NewEntry(matcher_line *line_info)
{
  Data *cur_d;
  Result error = Result::ok();
  char *match_data;

  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  match_data = line_info->line[1][line_info->dest_entry];

  // Make sure that the line_info is not bogus
  ink_assert(line_info->dest_entry < MATCHER_MAX_TOKENS);
  ink_assert(match_data != nullptr);

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);
  if (error.failed()) {
    // There was a problem so undo the effects this function
    new (cur_d) Data(); // reconstruct
  } else {
    // Fill in the matching info
    host_lookup->NewEntry(match_data, (line_info->type == MATCH_DOMAIN) ? true : false, cur_d);
    num_el++;
  }

  return error;
}

/*************************************************************
 *   End class HostMatcher
 *************************************************************/

//
// UrlMatcher<Data,MatchResult>::UrlMatcher()
//
template <class Data, class MatchResult>
UrlMatcher<Data, MatchResult>::UrlMatcher(const char *name, const char *filename) : BaseMatcher<Data>(name, filename)
{
  url_ht = ink_hash_table_create(InkHashTableKeyType_String);
}

//
// UrlMatcher<Data,MatchResult>::~UrlMatcher()
//
template <class Data, class MatchResult> UrlMatcher<Data, MatchResult>::~UrlMatcher()
{
  ink_hash_table_destroy(url_ht);
  for (int i = 0; i < num_el; i++) {
    ats_free(url_str[i]);
  }
  delete[] url_str;
  delete[] url_value;
}

//
// void UrlMatcher<Data,MatchResult>::Print()
//
//   Debugging function
//
template <class Data, class MatchResult>
void
UrlMatcher<Data, MatchResult>::Print()
{
  printf("\tUrl Matcher with %d elements\n", num_el);
  for (int i = 0; i < num_el; i++) {
    printf("\t\tUrl: %s\n", url_str[i]);
    data_array[i].Print();
  }
}

//
// void UrlMatcher<Data,MatchResult>::AllocateSpace(int num_entries)
//
template <class Data, class MatchResult>
void
UrlMatcher<Data, MatchResult>::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  data_array = new Data[num_entries];
  url_value  = new int[num_entries];
  url_str    = new char *[num_entries];
  memset(url_str, 0, sizeof(char *) * num_entries);
  array_len = num_entries;
  num_el    = 0;
}

//
// Result UrlMatcher<Data,MatchResult>::NewEntry(matcher_line* line_info)
//
template <class Data, class MatchResult>
Result
UrlMatcher<Data, MatchResult>::NewEntry(matcher_line *line_info)
{
  Data *cur_d;
  char *pattern;
  int *value;
  Result error = Result::ok();

  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  pattern = line_info->line[1][line_info->dest_entry];
  // Make sure that the line_info is not bogus
  ink_assert(line_info->dest_entry < MATCHER_MAX_TOKENS);
  ink_assert(pattern != nullptr);

  if (ink_hash_table_lookup(url_ht, pattern, (void **)&value)) {
    return Result::failure("%s url expression error (have exist) at line %d position", matcher_name, line_info->line_num);
  }

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);
  if (error.failed()) {
    url_str[num_el]   = ats_strdup(pattern);
    url_value[num_el] = num_el;
    ink_hash_table_insert(url_ht, url_str[num_el], (void *)&url_value[num_el]);
    num_el++;
  }

  return error;
}

//
// void UrlMatcher<Data,MatchResult>::Match(RD* rdata, MatchResult* result)
//
//   Coduncts a linear search through the regex array and
//     updates arg result for each regex that matches arg URL
//
template <class Data, class MatchResult>
void
UrlMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result)
{
  char *url_str;
  int *value;

  // Check to see there is any work to before we copy the
  //   URL
  if (num_el <= 0) {
    return;
  }

  url_str = rdata->get_string();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = ats_strdup("");
  }

  if (ink_hash_table_lookup(url_ht, url_str, (void **)&value)) {
    Debug("matcher", "%s Matched %s with url at line %d", matcher_name, url_str, data_array[*value].line_num);
    data_array[*value].UpdateMatch(result, rdata);
  }

  ats_free(url_str);
}

//
// RegexMatcher<Data,MatchResult>::RegexMatcher()
//
template <class Data, class MatchResult>
RegexMatcher<Data, MatchResult>::RegexMatcher(const char *name, const char *filename) : BaseMatcher<Data>(name, filename)
{
}

//
// RegexMatcher<Data,MatchResult>::~RegexMatcher()
//
template <class Data, class MatchResult> RegexMatcher<Data, MatchResult>::~RegexMatcher()
{
  for (int i = 0; i < num_el; i++) {
    pcre_free(re_array[i]);
    ats_free(re_str[i]);
  }
  delete[] re_str;
  ats_free(re_array);
}

//
// void RegexMatcher<Data,MatchResult>::Print()
//
//   Debugging function
//
template <class Data, class MatchResult>
void
RegexMatcher<Data, MatchResult>::Print()
{
  printf("\tRegex Matcher with %d elements\n", num_el);
  for (int i = 0; i < num_el; i++) {
    printf("\t\tRegex: %s\n", re_str[i]);
    data_array[i].Print();
  }
}

//
// void RegexMatcher<Data,MatchResult>::AllocateSpace(int num_entries)
//
template <class Data, class MatchResult>
void
RegexMatcher<Data, MatchResult>::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  re_array = (pcre **)ats_malloc(sizeof(pcre *) * num_entries);
  memset(re_array, 0, sizeof(pcre *) * num_entries);

  data_array = new Data[num_entries];

  re_str = new char *[num_entries];
  memset(re_str, 0, sizeof(char *) * num_entries);

  array_len = num_entries;
  num_el    = 0;
}

//
// Result RegexMatcher<Data,MatchResult>::NewEntry(matcher_line* line_info)
//
template <class Data, class MatchResult>
Result
RegexMatcher<Data, MatchResult>::NewEntry(matcher_line *line_info)
{
  Data *cur_d;
  char *pattern;
  const char *errptr;
  int erroffset;
  Result error = Result::ok();

  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  pattern = line_info->line[1][line_info->dest_entry];
  // Make sure that the line_info is not bogus
  ink_assert(line_info->dest_entry < MATCHER_MAX_TOKENS);
  ink_assert(pattern != nullptr);

  // Create the compiled regular expression
  re_array[num_el] = pcre_compile(pattern, 0, &errptr, &erroffset, nullptr);
  if (!re_array[num_el]) {
    return Result::failure("%s regular expression error at line %d position %d : %s", matcher_name, line_info->line_num, erroffset,
                           errptr);
  }
  re_str[num_el] = ats_strdup(pattern);

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);

  if (error.failed()) {
    // There was a problem so undo the effects this function
    ats_free(re_str[num_el]);
    re_str[num_el] = nullptr;
    pcre_free(re_array[num_el]);
    re_array[num_el] = nullptr;
  } else {
    num_el++;
  }

  return error;
}

//
// void RegexMatcher<Data,MatchResult>::Match(RequestData* rdata, MatchResult* result)
//
//   Coduncts a linear search through the regex array and
//     updates arg result for each regex that matches arg URL
//
template <class Data, class MatchResult>
void
RegexMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result)
{
  char *url_str;
  int r;

  // Check to see there is any work to before we copy the
  //   URL
  if (num_el <= 0) {
    return;
  }

  url_str = rdata->get_string();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = ats_strdup("");
  }
  // INKqa12980
  // The function unescapifyStr() is already called in
  // HttpRequestData::get_string(); therefore, no need to call again here.
  // unescapifyStr(url_str);

  for (int i = 0; i < num_el; i++) {
    r = pcre_exec(re_array[i], nullptr, url_str, strlen(url_str), 0, 0, nullptr, 0);
    if (r > -1) {
      Debug("matcher", "%s Matched %s with regex at line %d", matcher_name, url_str, data_array[i].line_num);
      data_array[i].UpdateMatch(result, rdata);
    } else if (r < -1) {
      // An error has occured
      Warning("Error [%d] matching regex at line %d.", r, data_array[i].line_num);
    } // else it's -1 which means no match was found.
  }
  ats_free(url_str);
}

//
// HostRegexMatcher<Data,MatchResult>::HostRegexMatcher()
//
template <class Data, class MatchResult>
HostRegexMatcher<Data, MatchResult>::HostRegexMatcher(const char *name, const char *filename)
  : RegexMatcher<Data, MatchResult>(name, filename)
{
}

//
// void HostRegexMatcher<Data,MatchResult>::Match(RequestData* rdata, MatchResult* result)
//
//   Conducts a linear search through the regex array and
//     updates arg result for each regex that matches arg host_regex
//
template <class Data, class MatchResult>
void
HostRegexMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result)
{
  const char *url_str;
  int r;

  // Check to see there is any work to before we copy the
  //   URL
  if (this->num_el <= 0) {
    return;
  }

  url_str = rdata->get_host();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = "";
  }
  for (int i = 0; i < this->num_el; i++) {
    r = pcre_exec(this->re_array[i], nullptr, url_str, strlen(url_str), 0, 0, nullptr, 0);
    if (r != -1) {
      Debug("matcher", "%s Matched %s with regex at line %d", const_cast<char *>(this->matcher_name), url_str,
            this->data_array[i].line_num);
      this->data_array[i].UpdateMatch(result, rdata);
    } else {
      // An error has occured
      Warning("error matching regex at line %d", this->data_array[i].line_num);
    }
  }
}

//
// IpMatcher<Data,MatchResult>::IpMatcher()
//
template <class Data, class MatchResult>
IpMatcher<Data, MatchResult>::IpMatcher(const char *name, const char *filename) : BaseMatcher<Data>(name, filename)
{
}

//
// void IpMatcher<Data,MatchResult>::AllocateSpace(int num_entries)
//
template <class Data, class MatchResult>
void
IpMatcher<Data, MatchResult>::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  data_array = new Data[num_entries];

  array_len = num_entries;
  num_el    = 0;
}

//
// Result IpMatcher<Data,MatchResult>::NewEntry(matcher_line* line_info)
//
//    Inserts a range the ip lookup table.
//        Creates new table levels as needed
//
//    Returns NULL is all was OK.  On error returns, a malloc
//     allocated error string which the CALLEE is responsible
//     for deallocating
//
template <class Data, class MatchResult>
Result
IpMatcher<Data, MatchResult>::NewEntry(matcher_line *line_info)
{
  Data *cur_d;
  const char *errptr;
  char *match_data;
  IpEndpoint addr1, addr2;
  Result error = Result::ok();

  // Make sure space has been allocated
  ink_assert(num_el >= 0);
  ink_assert(array_len >= 0);

  // Make sure we do not overrun the array;
  ink_assert(num_el < array_len);

  match_data = line_info->line[1][line_info->dest_entry];

  // Make sure that the line_info is not bogus
  ink_assert(line_info->dest_entry < MATCHER_MAX_TOKENS);
  ink_assert(match_data != nullptr);

  // Extract the IP range
  errptr = ExtractIpRange(match_data, &addr1.sa, &addr2.sa);
  if (errptr != nullptr) {
    return Result::failure("%s %s at %s line %d", matcher_name, errptr, file_name, line_info->line_num);
  }

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);
  if (!error.failed()) {
    ip_map.mark(&addr1.sa, &addr2.sa, cur_d);
    ++num_el;
  }

  return error;
}

//
// void IpMatcherData,MatchResult>::Match(in_addr_t addr, RequestData* rdata, MatchResult* result)
//
template <class Data, class MatchResult>
void
IpMatcher<Data, MatchResult>::Match(sockaddr const *addr, RequestData *rdata, MatchResult *result)
{
  void *raw;
  if (ip_map.contains(addr, &raw)) {
    Data *cur = static_cast<Data *>(raw);
    ink_assert(cur != nullptr);
    cur->UpdateMatch(result, rdata);
  }
}

template <class Data, class MatchResult>
void
IpMatcher<Data, MatchResult>::Print()
{
  printf("\tIp Matcher with %d elements, %zu ranges.\n", num_el, ip_map.count());
  for (IpMap::iterator spot(ip_map.begin()), limit(ip_map.end()); spot != limit; ++spot) {
    char b1[INET6_ADDRSTRLEN], b2[INET6_ADDRSTRLEN];
    printf("\tRange %s - %s ", ats_ip_ntop(spot->min(), b1, sizeof b1), ats_ip_ntop(spot->max(), b2, sizeof b2));
    static_cast<Data *>(spot->data())->Print();
  }
}

template <class Data, class MatchResult>
ControlMatcher<Data, MatchResult>::ControlMatcher(const char *file_var, const char *name, const matcher_tags *tags, int flags_in)
{
  flags = flags_in;
  ink_assert(flags & (ALLOW_HOST_TABLE | ALLOW_REGEX_TABLE | ALLOW_URL_TABLE | ALLOW_IP_TABLE));

  config_tags = tags;
  ink_assert(config_tags != nullptr);

  matcher_name        = name;
  config_file_path[0] = '\0';

  if (!(flags & DONT_BUILD_TABLE)) {
    ats_scoped_str config_path(RecConfigReadConfigPath(file_var));

    ink_release_assert(config_path);
    ink_strlcpy(config_file_path, config_path, sizeof(config_file_path));
  }

  reMatch   = nullptr;
  urlMatch  = nullptr;
  hostMatch = nullptr;
  ipMatch   = nullptr;
  hrMatch   = nullptr;

  if (!(flags & DONT_BUILD_TABLE)) {
    m_numEntries = this->BuildTable();
  } else {
    m_numEntries = 0;
  }
}

template <class Data, class MatchResult> ControlMatcher<Data, MatchResult>::~ControlMatcher()
{
  delete reMatch;
  delete urlMatch;
  delete hostMatch;
  delete ipMatch;
  delete hrMatch;
}

// void ControlMatcher<Data, MatchResult>::Print()
//
//   Debugging method
//
template <class Data, class MatchResult>
void
ControlMatcher<Data, MatchResult>::Print()
{
  printf("Control Matcher Table: %s\n", matcher_name);
  if (hostMatch != nullptr) {
    hostMatch->Print();
  }
  if (reMatch != nullptr) {
    reMatch->Print();
  }
  if (urlMatch != nullptr) {
    urlMatch->Print();
  }
  if (ipMatch != nullptr) {
    ipMatch->Print();
  }
  if (hrMatch != nullptr) {
    hrMatch->Print();
  }
}

// void ControlMatcher<Data, MatchResult>::Match(RequestData* rdata
//                                          MatchResult* result)
//
//   Queries each table for the MatchResult*
//
template <class Data, class MatchResult>
void
ControlMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result)
{
  if (hostMatch != nullptr) {
    hostMatch->Match(rdata, result);
  }
  if (reMatch != nullptr) {
    reMatch->Match(rdata, result);
  }
  if (urlMatch != nullptr) {
    urlMatch->Match(rdata, result);
  }
  if (ipMatch != nullptr) {
    ipMatch->Match(rdata->get_ip(), rdata, result);
  }
  if (hrMatch != nullptr) {
    hrMatch->Match(rdata, result);
  }
}

// int ControlMatcher::BuildTable() {
//
//    Reads the cache.config file and build the records array
//      from it
//
template <class Data, class MatchResult>
int
ControlMatcher<Data, MatchResult>::BuildTableFromString(char *file_buf)
{
  // Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp;
  matcher_line *first = nullptr;
  matcher_line *current;
  matcher_line *last = nullptr;
  int line_num       = 0;
  int second_pass    = 0;
  int numEntries     = 0;
  bool alarmAlready  = false;

  // type counts
  int hostDomain = 0;
  int regex      = 0;
  int url        = 0;
  int ip         = 0;
  int hostregex  = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    return 0;
  }
  // First get the number of entries
  tmp = bufTok.iterFirst(&i_state);
  while (tmp != nullptr) {
    line_num++;

    // skip all blank spaces at beginning of line
    while (*tmp && isspace(*tmp)) {
      tmp++;
    }

    if (*tmp != '#' && *tmp != '\0') {
      const char *errptr;

      current = (matcher_line *)ats_malloc(sizeof(matcher_line));
      errptr  = parseConfigLine((char *)tmp, current, config_tags);

      if (errptr != nullptr) {
        if (config_tags != &socks_server_tags) {
          Result error =
            Result::failure("%s discarding %s entry at line %d : %s", matcher_name, config_file_path, line_num, errptr);
          SignalError(error.message(), alarmAlready);
        }
        ats_free(current);
      } else {
        // Line parsed ok.  Figure out what the destination
        //  type is and link it into our list
        numEntries++;
        current->line_num = line_num;

        switch (current->type) {
        case MATCH_HOST:
        case MATCH_DOMAIN:
          hostDomain++;
          break;
        case MATCH_IP:
          ip++;
          break;
        case MATCH_REGEX:
          regex++;
          break;
        case MATCH_URL:
          url++;
          break;
        case MATCH_HOST_REGEX:
          hostregex++;
          break;
        case MATCH_NONE:
        default:
          ink_assert(0);
        }

        if (first == nullptr) {
          ink_assert(last == nullptr);
          first = last = current;
        } else {
          last->next = current;
          last       = current;
        }
      }
    }
    tmp = bufTok.iterNext(&i_state);
  }

  // Make we have something to do before going on
  if (numEntries == 0) {
    ats_free(first);
    return 0;
  }
  // Now allocate space for the record pointers
  if ((flags & ALLOW_REGEX_TABLE) && regex > 0) {
    reMatch = new RegexMatcher<Data, MatchResult>(matcher_name, config_file_path);
    reMatch->AllocateSpace(regex);
  }

  if ((flags & ALLOW_URL_TABLE) && url > 0) {
    urlMatch = new UrlMatcher<Data, MatchResult>(matcher_name, config_file_path);
    urlMatch->AllocateSpace(url);
  }

  if ((flags & ALLOW_HOST_TABLE) && hostDomain > 0) {
    hostMatch = new HostMatcher<Data, MatchResult>(matcher_name, config_file_path);
    hostMatch->AllocateSpace(hostDomain);
  }

  if ((flags & ALLOW_IP_TABLE) && ip > 0) {
    ipMatch = new IpMatcher<Data, MatchResult>(matcher_name, config_file_path);
    ipMatch->AllocateSpace(ip);
  }

  if ((flags & ALLOW_HOST_REGEX_TABLE) && hostregex > 0) {
    hrMatch = new HostRegexMatcher<Data, MatchResult>(matcher_name, config_file_path);
    hrMatch->AllocateSpace(hostregex);
  }
  // Traverse the list and build the records table
  current = first;
  while (current != nullptr) {
    Result error = Result::ok();

    second_pass++;
    if ((flags & ALLOW_HOST_TABLE) && current->type == MATCH_DOMAIN) {
      error = hostMatch->NewEntry(current);
    } else if ((flags & ALLOW_HOST_TABLE) && current->type == MATCH_HOST) {
      error = hostMatch->NewEntry(current);
    } else if ((flags & ALLOW_REGEX_TABLE) && current->type == MATCH_REGEX) {
      error = reMatch->NewEntry(current);
    } else if ((flags & ALLOW_URL_TABLE) && current->type == MATCH_URL) {
      error = urlMatch->NewEntry(current);
    } else if ((flags & ALLOW_IP_TABLE) && current->type == MATCH_IP) {
      error = ipMatch->NewEntry(current);
    } else if ((flags & ALLOW_HOST_REGEX_TABLE) && current->type == MATCH_HOST_REGEX) {
      error = hrMatch->NewEntry(current);
    } else {
      error =
        Result::failure("%s discarding %s entry with unknown type at line %d", matcher_name, config_file_path, current->line_num);
    }

    // Check to see if there was an error in creating
    //   the NewEntry
    if (error.failed()) {
      SignalError(error.message(), alarmAlready);
    }

    // Deallocate the parsing structure
    last    = current;
    current = current->next;
    ats_free(last);
  }

  ink_assert(second_pass == numEntries);

  if (is_debug_tag_set("matcher")) {
    Print();
  }
  return numEntries;
}

template <class Data, class MatchResult>
int
ControlMatcher<Data, MatchResult>::BuildTable()
{
  // File I/O Locals
  char *file_buf;
  int ret;

  file_buf = readIntoBuffer(config_file_path, matcher_name, nullptr);

  if (file_buf == nullptr) {
    return 1;
  }

  ret = BuildTableFromString(file_buf);
  ats_free(file_buf);
  return ret;
}

/****************************************************************
 *    TEMPLATE INSTANTIATIONS GO HERE
 *
 *  We have to explicitly instantiate the templates so that
 *   everything works on with dec ccx, sun CC, and g++
 *
 *  Details on the different comipilers:
 *
 *  dec ccx: Does not seem to instantiate anything automatically
 *         so it needs all templates manually instantiated
 *
 *  sun CC: Automatic instantiation works but since we make
 *         use of the templates in other files, instantiation
 *         only occurs when those files are compiled, breaking
 *         the dependency system.  Explict instantiation
 *         in this file causes the templates to be reinstantiated
 *         when this file changes.
 *
 *         Also, does not give error messages about template
 *           compliation problems.  Requires the -verbose=template
 *           flage to error messages
 *
 *  g++: Requires instantiation to occur in the same file as the
 *         the implementation.  Instantiating ControlMatcher
 *         automatically instatiatiates the other templates since
 *         ControlMatcher makes use of them
 *
 ****************************************************************/

template class ControlMatcher<ParentRecord, ParentResult>;
template class HostMatcher<ParentRecord, ParentResult>;
template class RegexMatcher<ParentRecord, ParentResult>;
template class UrlMatcher<ParentRecord, ParentResult>;
template class IpMatcher<ParentRecord, ParentResult>;
template class HostRegexMatcher<ParentRecord, ParentResult>;

template class ControlMatcher<SplitDNSRecord, SplitDNSResult>;
template class HostMatcher<SplitDNSRecord, SplitDNSResult>;
template class RegexMatcher<SplitDNSRecord, SplitDNSResult>;
template class UrlMatcher<SplitDNSRecord, SplitDNSResult>;
template class IpMatcher<SplitDNSRecord, SplitDNSResult>;
template class HostRegexMatcher<SplitDNSRecord, SplitDNSResult>;

template class ControlMatcher<CacheControlRecord, CacheControlResult>;
template class HostMatcher<CacheControlRecord, CacheControlResult>;
template class RegexMatcher<CacheControlRecord, CacheControlResult>;
template class UrlMatcher<CacheControlRecord, CacheControlResult>;
template class IpMatcher<CacheControlRecord, CacheControlResult>;
