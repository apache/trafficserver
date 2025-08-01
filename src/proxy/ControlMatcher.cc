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

#include "swoc/bwf_ip.h"
#include "swoc/swoc_file.h"

#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"
#include "proxy/ControlMatcher.h"
#include "proxy/CacheControl.h"
#include "proxy/ParentSelection.h"
#include "tscore/HostLookup.h"
#include "proxy/hdrs/HTTP.h"
#include "../iocore/dns/P_SplitDNSProcessor.h"

namespace
{
DbgCtl dbg_ctl_matcher("matcher");
}

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
  host_lookup = std::make_unique<HostLookup>(name);
}

template <class Data, class MatchResult> HostMatcher<Data, MatchResult>::~HostMatcher() {}

//
// template <class Data,class MatchResult>
// void HostMatcher<Data,MatchResult>::Print()
//
//  Debugging Method
//
template <class Data, class MatchResult>
void
HostMatcher<Data, MatchResult>::Print() const
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
//  Allocates the HostLeaf and Data arrays
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
HostMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result) const
{
  void *opaque_ptr;
  Data *data_ptr;
  bool  r;

  // Check to see if there is any work to do before making the string copy
  if (this->num_el <= 0) {
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
  Data  *cur_d;
  Result error = Result::ok();
  char  *match_data;

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
}

//
// UrlMatcher<Data,MatchResult>::~UrlMatcher()
//
template <class Data, class MatchResult> UrlMatcher<Data, MatchResult>::~UrlMatcher()
{
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
UrlMatcher<Data, MatchResult>::Print() const
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
  Data  *cur_d;
  char  *pattern;
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

  if (url_ht.find(pattern) != url_ht.end()) {
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
    url_ht.emplace(url_str[num_el], url_value[num_el]);
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
UrlMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result) const
{
  char *url_str;

  // Check to see there is any work to before we copy the URL
  if (num_el <= 0) {
    return;
  }

  url_str = rdata->get_string();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = ats_strdup("");
  }

  if (auto it = url_ht.find(url_str); it != url_ht.end()) {
    Dbg(dbg_ctl_matcher, "%s Matched %s with url at line %d", matcher_name, url_str, data_array[it->second].line_num);
    data_array[it->second].UpdateMatch(result, rdata);
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
// void RegexMatcher<Data,MatchResult>::Print() const
//
//   Debugging function
//
template <class Data, class MatchResult>
void
RegexMatcher<Data, MatchResult>::Print() const
{
  printf("\tRegex Matcher with %d elements\n", num_el);
  for (int i = 0; i < num_el; i++) {
    printf("\t\tRegex: %s\n", regex_strings[i].c_str());
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

  regex_array.resize(num_entries);
  regex_strings.resize(num_entries);

  data_array = new Data[num_entries];

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
  Data       *cur_d;
  char       *pattern;
  std::string error_msg;
  int         erroffset;
  Result      error = Result::ok();

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
  regex_array[num_el].compile(pattern, error_msg, erroffset);
  if (regex_array[num_el].empty()) {
    return Result::failure("%s regular expression error at line %d position %d : %s", matcher_name, line_info->line_num, erroffset,
                           error_msg.c_str());
  }
  regex_strings[num_el] = pattern;

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);

  if (error.failed()) {
    // There was a problem so undo the effects this function
    regex_strings[num_el] = ""; // reset the string
  } else {
    num_el++;
  }

  return error;
}

//
// void RegexMatcher<Data,MatchResult>::Match(RequestData* rdata, MatchResult* result)
//
//   Conducts a linear search through the regex array and
//     updates arg result for each regex that matches arg URL
//
template <class Data, class MatchResult>
void
RegexMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result) const
{
  // Check to see there is any work to before we copy the URL
  if (num_el <= 0) {
    return;
  }

  char *url_str = rdata->get_string();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = ats_strdup("");
  }

  // INKqa12980
  // The function unescapifyStr() is already called in
  // HttpRequestData::get_string(); therefore, no need to call again here.
  for (int i = 0; i < num_el; i++) {
    if (regex_array[i].exec(url_str) == true) {
      Dbg(dbg_ctl_matcher, "%s Matched %s with regex at line %d", matcher_name, url_str, data_array[i].line_num);
      data_array[i].UpdateMatch(result, rdata);
    } else {
      // An error has occurred
      Warning("Error matching regex at line %d.", data_array[i].line_num);
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
HostRegexMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result) const
{
  const char *url_str;

  // Check to see there is any work to before we copy the URL
  if (this->num_el <= 0) {
    return;
  }

  url_str = rdata->get_host();

  // Can't do a regex match with a NULL string so
  //  use an empty one instead
  if (url_str == nullptr) {
    url_str = "";
  }
  for (int i = 0; i < num_el; i++) {
    if (this->regex_array[i].exec(url_str) == true) {
      Dbg(dbg_ctl_matcher, "%s Matched %s with regex at line %d", const_cast<char *>(this->matcher_name), url_str,
          this->data_array[i].line_num);
      this->data_array[i].UpdateMatch(result, rdata);
    } else {
      // An error has occurred
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
  Data         *cur_d;
  char         *match_data;
  swoc::IPRange addrs;
  Result        error = Result::ok();

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
  if (!addrs.load(match_data)) {
    return Result::failure("%s not a valid IP address range at %s line %d", matcher_name, file_name, line_info->line_num);
  }

  // Remove our consumed label from the parsed line
  line_info->line[0][line_info->dest_entry] = nullptr;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  error = cur_d->Init(line_info);
  if (!error.failed()) {
    ip_addrs.mark(addrs, cur_d);
    ++num_el;
  }

  return error;
}

//
// void IpMatcherData,MatchResult>::Match(in_addr_t addr, RequestData* rdata, MatchResult* result)
//
template <class Data, class MatchResult>
void
IpMatcher<Data, MatchResult>::Match(sockaddr const *addr, RequestData *rdata, MatchResult *result) const
{
  if (auto [range, data]{*ip_addrs.find(swoc::IPAddr(addr))}; !range.empty()) {
    ink_assert(data != nullptr);
    data->UpdateMatch(result, rdata);
  }
}

template <class Data, class MatchResult>
void
IpMatcher<Data, MatchResult>::Print() const
{
  std::string tmp;
  std::cout << swoc::bwprint(tmp, "\tIp Matcher with {} elements, {} ranges.", num_el, ip_addrs.count()) << std::endl;
  for (auto &&[range, data] : ip_addrs) {
    std::cout << swoc::bwprint(tmp, "Range {}", range);
    data->Print();
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

template <class Data, class MatchResult> ControlMatcher<Data, MatchResult>::~ControlMatcher() {}

// void ControlMatcher<Data, MatchResult>::Print()
//
//   Debugging method
//
template <class Data, class MatchResult>
void
ControlMatcher<Data, MatchResult>::Print() const
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
//                                          MatchResult* result) const
//
//   Queries each table for the MatchResult*
//
template <class Data, class MatchResult>
void
ControlMatcher<Data, MatchResult>::Match(RequestData *rdata, MatchResult *result) const
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

// int ControlMatcher::BuildTable()
//
//    Reads the cache.config file and build the records array
//      from it
//
template <class Data, class MatchResult>
int
ControlMatcher<Data, MatchResult>::BuildTableFromString(char *file_buf)
{
  // Table build locals
  Tokenizer      bufTok("\n");
  tok_iter_state i_state;
  const char    *tmp;
  matcher_line  *first = nullptr;
  matcher_line  *current;
  matcher_line  *last        = nullptr;
  int            line_num    = 0;
  int            second_pass = 0;
  int            numEntries  = 0;

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

      current = static_cast<matcher_line *>(ats_malloc(sizeof(matcher_line)));
      errptr  = parseConfigLine(const_cast<char *>(tmp), current, config_tags);

      if (errptr != nullptr) {
        if (config_tags != &socks_server_tags) {
          Result error =
            Result::failure("%s discarding %s entry at line %d : %s", matcher_name, config_file_path, line_num, errptr);
          Error("%s", error.message());
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
    reMatch = std::make_unique<RegexMatcher<Data, MatchResult>>(matcher_name, config_file_path);
    reMatch->AllocateSpace(regex);
  }

  if ((flags & ALLOW_URL_TABLE) && url > 0) {
    urlMatch = std::make_unique<UrlMatcher<Data, MatchResult>>(matcher_name, config_file_path);
    urlMatch->AllocateSpace(url);
  }

  if ((flags & ALLOW_HOST_TABLE) && hostDomain > 0) {
    hostMatch = std::make_unique<HostMatcher<Data, MatchResult>>(matcher_name, config_file_path);
    hostMatch->AllocateSpace(hostDomain);
  }

  if ((flags & ALLOW_IP_TABLE) && ip > 0) {
    ipMatch = std::make_unique<IpMatcher<Data, MatchResult>>(matcher_name, config_file_path);
    ipMatch->AllocateSpace(ip);
  }

  if ((flags & ALLOW_HOST_REGEX_TABLE) && hostregex > 0) {
    hrMatch = std::make_unique<HostRegexMatcher<Data, MatchResult>>(matcher_name, config_file_path);
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

    // Check to see if there was an error in creating the NewEntry
    if (error.failed()) {
      Error("%s", error.message());
    }

    // Deallocate the parsing structure
    last    = current;
    current = current->next;
    ats_free(last);
  }

  ink_assert(second_pass == numEntries);

  if (dbg_ctl_matcher.on()) {
    Print();
  }
  return numEntries;
}

template <class Data, class MatchResult>
int
ControlMatcher<Data, MatchResult>::BuildTable()
{
  std::error_code ec;
  std::string     content{swoc::file::load(swoc::file::path{config_file_path}, ec)};
  if (ec) {
    switch (ec.value()) {
    case ENOENT:
      Warning("ControlMatcher - Cannot open config file: %s - %s", config_file_path, strerror(ec.value()));
      break;
    default:
      Error("ControlMatcher - %s failed to load: %s", config_file_path, strerror(ec.value()));
      return 1;
    }
  }

  return BuildTableFromString(content.data());
}

/****************************************************************
 *    TEMPLATE INSTANTIATIONS GO HERE
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
