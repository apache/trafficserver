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

#include "swoc/swoc_file.h"

#include "P_Cache.h"
#include "tscore/Layout.h"
#include "tscore/HostLookup.h"
#include "tscore/Tokenizer.h"
#include "tscore/Filenames.h"

namespace
{

DbgCtl dbg_ctl_cache_hosting{"cache_hosting"};
DbgCtl dbg_ctl_matcher{"matcher"};

} // end anonymous namespace

/*************************************************************
 *   Begin class HostMatcher
 *************************************************************/

CacheHostMatcher::CacheHostMatcher(const char *name, CacheType typ) : data_array(nullptr), array_len(-1), num_el(-1), type(typ)
{
  host_lookup = new HostLookup(name);
}

CacheHostMatcher::~CacheHostMatcher()
{
  delete host_lookup;
  delete[] data_array;
}

//
// template <class Data,class Result>
// void HostMatcher<Data,Result>::Print()
//
//  Debugging Method
//
void
CacheHostMatcher::Print() const
{
  printf("\tHost/Domain Matcher with %d elements\n", num_el);
  host_lookup->Print(PrintFunc);
}

//
// template <class Data,class Result>
// void CacheHostMatcher::PrintFunc(void* opaque_data)
//
//  Debugging Method
//
void
CacheHostMatcher::PrintFunc(void *opaque_data)
{
  CacheHostRecord *d = static_cast<CacheHostRecord *>(opaque_data);
  d->Print();
}

// void CacheHostMatcher::AllocateSpace(int num_entries)
//
//  Allocates the HostLeaf and Data arrays
//
void
CacheHostMatcher::AllocateSpace(int num_entries)
{
  // Should not have been allocated before
  ink_assert(array_len == -1);

  host_lookup->AllocateSpace(num_entries);

  data_array = new CacheHostRecord[num_entries];

  array_len = num_entries;
  num_el    = 0;
}

// void CacheHostMatcher::Match(RequestData* rdata, Result* result)
//
//  Searches our tree and updates argresult for each element matching
//    arg hostname
//
void
CacheHostMatcher::Match(const char *rdata, int rlen, CacheHostResult *result) const
{
  void *opaque_ptr;
  CacheHostRecord *data_ptr;
  bool r;

  // Check to see if there is any work to do before making
  //   the string copy
  if (num_el <= 0) {
    return;
  }

  if (rlen == 0) {
    return;
  }

  std::string_view data{rdata, static_cast<size_t>(rlen)};
  HostLookupState s;

  r = host_lookup->MatchFirst(data, &s, &opaque_ptr);

  while (r == true) {
    ink_assert(opaque_ptr != nullptr);
    data_ptr = static_cast<CacheHostRecord *>(opaque_ptr);
    data_ptr->UpdateMatch(result);

    r = host_lookup->MatchNext(&s, &opaque_ptr);
  }
}

//
// char* CacheHostMatcher::NewEntry(bool domain_record,
//          char* match_data, char* match_info, int line_num)
//
//   Creates a new host/domain record
//

void
CacheHostMatcher::NewEntry(matcher_line *line_info)
{
  CacheHostRecord *cur_d;
  int errNo;
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
  if (line_info->dest_entry < MATCHER_MAX_TOKENS) {
    line_info->line[0][line_info->dest_entry] = nullptr;
  }
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  errNo = cur_d->Init(line_info, type);

  if (errNo) {
    // There was a problem so undo the effects this function
    memset(static_cast<void *>(cur_d), 0, sizeof(CacheHostRecord));
    return;
  }
  Dbg(dbg_ctl_cache_hosting, "hostname: %s, host record: %p", match_data, cur_d);
  // Fill in the matching info
  host_lookup->NewEntry(match_data, (line_info->type == MATCH_DOMAIN) ? true : false, cur_d);

  num_el++;
  return;
}

/*************************************************************
 *   End class HostMatcher
 *************************************************************/

CacheHostTable::CacheHostTable(Cache *c, CacheType typ)
{
  ats_scoped_str config_path;

  type         = typ;
  cache        = c;
  matcher_name = "[CacheHosting]";

  config_path = RecConfigReadConfigPath("proxy.config.cache.hosting_filename");
  ink_release_assert(config_path);

  m_numEntries = this->BuildTable(config_path);
}

CacheHostTable::~CacheHostTable()
{
  if (hostMatch != nullptr) {
    delete hostMatch;
  }
}

// void ControlMatcher<Data, Result>::Print()
//
//   Debugging method
//
void
CacheHostTable::Print() const
{
  printf("Control Matcher Table: %s\n", matcher_name);
  if (hostMatch != nullptr) {
    hostMatch->Print();
  }
}

// void ControlMatcher<Data, Result>::Match(RequestData* rdata
//                                          Result* result)
//
//   Queries each table for the Result*
//
void
CacheHostTable::Match(const char *rdata, int rlen, CacheHostResult *result) const
{
  hostMatch->Match(rdata, rlen, result);
}

int
CacheHostTable::config_callback(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                RecData /* data ATS_UNUSED */, void *cookie)
{
  ReplaceablePtr<CacheHostTable> *ppt = static_cast<ReplaceablePtr<CacheHostTable> *>(cookie);
  eventProcessor.schedule_imm(new CacheHostTableConfig(ppt), ET_TASK);
  return 0;
}

int fstat_wrapper(int fd, struct stat *s);

// int ControlMatcher::BuildTable() {
//
//    Reads the cache.config file and build the records array
//      from it
//
int
CacheHostTable::BuildTableFromString(const char *config_file_path, char *file_buf)
{
  Note("%s loading ...", ts::filename::HOSTING);

  // Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp       = nullptr;
  matcher_line *first   = nullptr;
  matcher_line *current = nullptr;
  matcher_line *last    = nullptr;
  int line_num          = 0;
  int second_pass       = 0;
  int numEntries        = 0;
  const char *errPtr    = nullptr;

  // type counts
  int hostDomain = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    /* no hosting customers -- put all the volumes in the
       generic table */
    if (gen_host_rec.Init(type)) {
      Warning("Problems encountered while initializing the Generic Volume");
    }
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
      current = static_cast<matcher_line *>(ats_malloc(sizeof(matcher_line)));
      errPtr  = parseConfigLine(const_cast<char *>(tmp), current, &config_tags);

      if (errPtr != nullptr) {
        Warning("%s discarding %s entry at line %d : %s", matcher_name, config_file_path, line_num, errPtr);
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
        case MATCH_NONE:
        default:
          ink_assert(0);
        }

        if (first == nullptr) {
          ink_assert(last == nullptr);
          first = current;
          last  = current;
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
    /* no hosting customers -- put all the volumes in the
       generic table */

    if (gen_host_rec.Init(type)) {
      Warning("Problems encountered while initializing the Generic Volume");
    }
    Note("%s finished loading", ts::filename::HOSTING);
    return 0;
  }

  if (hostDomain > 0) {
    hostMatch = new CacheHostMatcher(matcher_name, type);
    hostMatch->AllocateSpace(hostDomain);
  }
  // Traverse the list and build the records table
  int generic_rec_initd = 0;
  current               = first;
  while (current != nullptr) {
    second_pass++;
    if ((current->type == MATCH_DOMAIN) || (current->type == MATCH_HOST)) {
      char *match_data = current->line[1][current->dest_entry];
      ink_assert(match_data != nullptr);

      if (!strcasecmp(match_data, "*")) {
        // generic volume - initialize the generic hostrecord */
        // Make sure that the line_info is not bogus
        ink_assert(current->dest_entry < MATCHER_MAX_TOKENS);

        // Remove our consumed label from the parsed line
        if (current->dest_entry < MATCHER_MAX_TOKENS) {
          current->line[0][current->dest_entry] = nullptr;
        } else {
          Warning("Problems encountered while initializing the Generic Volume");
        }

        current->num_el--;
        if (!gen_host_rec.Init(current, type)) {
          generic_rec_initd = 1;
        } else {
          Warning("Problems encountered while initializing the Generic Volume");
        }

      } else {
        hostMatch->NewEntry(current);
      }
    } else {
      Warning("%s discarding %s entry with unknown type at line %d", matcher_name, config_file_path, current->line_num);
    }

    // Deallocate the parsing structure
    last    = current;
    current = current->next;
    ats_free(last);
  }

  Note("%s finished loading", ts::filename::HOSTING);

  if (!generic_rec_initd) {
    const char *cache_type = (type == CACHE_HTTP_TYPE) ? "http" : "mixt";
    Warning("No Volumes specified for Generic Hostnames for %s documents: %s cache will be disabled", cache_type, cache_type);
  }

  ink_assert(second_pass == numEntries);

  if (dbg_ctl_matcher.on()) {
    Print();
  }
  return numEntries;
}

int
CacheHostTable::BuildTable(const char *config_file_path)
{
  std::error_code ec;
  std::string content{swoc::file::load(swoc::file::path{config_file_path}, ec)};

  if (ec) {
    switch (ec.value()) {
    case ENOENT:
      Warning("Cannot open the config file: %s - %s", config_file_path, strerror(ec.value()));
      break;
    default:
      Error("%s failed to load: %s", config_file_path, strerror(ec.value()));
      gen_host_rec.Init(type);
      return 0;
    }
  }

  return BuildTableFromString(config_file_path, content.data());
}

int
CacheHostRecord::Init(CacheType typ)
{
  int i, j;
  extern Queue<CacheVol> cp_list;
  extern int cp_list_len;

  num_vols = 0;
  type     = typ;
  cp       = static_cast<CacheVol **>(ats_malloc(cp_list_len * sizeof(CacheVol *)));
  memset(cp, 0, cp_list_len * sizeof(CacheVol *));
  num_cachevols    = 0;
  CacheVol *cachep = cp_list.head;
  for (; cachep; cachep = cachep->link.next) {
    if (cachep->scheme == type) {
      Dbg(dbg_ctl_cache_hosting, "Host Record: %p, Volume: %d, size: %" PRId64, this, cachep->vol_number, (int64_t)cachep->size);
      cp[num_cachevols] = cachep;
      num_cachevols++;
      num_vols += cachep->num_vols;
    }
  }
  if (!num_cachevols) {
    Warning("error: No volumes found for Cache Type %d", type);
    return -1;
  }
  vols        = static_cast<Stripe **>(ats_malloc(num_vols * sizeof(Stripe *)));
  int counter = 0;
  for (i = 0; i < num_cachevols; i++) {
    CacheVol *cachep1 = cp[i];
    for (j = 0; j < cachep1->num_vols; j++) {
      vols[counter++] = cachep1->vols[j];
    }
  }
  ink_assert(counter == num_vols);

  build_vol_hash_table(this);
  return 0;
}

int
CacheHostRecord::Init(matcher_line *line_info, CacheType typ)
{
  int i, j;
  extern Queue<CacheVol> cp_list;
  int is_vol_present = 0;
  char config_file[PATH_NAME_MAX];

  REC_ReadConfigString(config_file, "proxy.config.cache.hosting_filename", PATH_NAME_MAX);
  type = typ;
  for (i = 0; i < MATCHER_MAX_TOKENS; i++) {
    char *label = line_info->line[0][i];
    if (!label) {
      continue;
    }
    char *val;

    if (!strcasecmp(label, "volume")) {
      /* parse the list of volumes */
      val          = ats_strdup(line_info->line[1][i]);
      char *vol_no = val;
      char *s      = val;
      int volume_number;
      CacheVol *cachep;

      /* first find out the number of volumes */
      while (*s) {
        if (*s == ',') {
          num_cachevols++;
          s++;
          if (!(*s)) {
            const char *errptr = "A volume number expected";
            Warning("%s discarding %s entry at line %d :%s", "[CacheHosting]", config_file, line_info->line_num, errptr);
            if (val != nullptr) {
              ats_free(val);
            }
            return -1;
          }
        }
        if ((*s < '0') || (*s > '9')) {
          Warning("%s discarding %s entry at line %d : bad token [%c]", "[CacheHosting]", config_file, line_info->line_num, *s);
          if (val != nullptr) {
            ats_free(val);
          }
          return -1;
        }
        s++;
      }
      if (val == nullptr) {
        return -1;
      }
      s = val;
      num_cachevols++;
      cp = static_cast<CacheVol **>(ats_malloc(num_cachevols * sizeof(CacheVol *)));
      memset(cp, 0, num_cachevols * sizeof(CacheVol *));
      num_cachevols = 0;
      while (true) {
        char c = *s;
        if ((c == ',') || (c == '\0')) {
          *s            = '\0';
          volume_number = atoi(vol_no);

          cachep = cp_list.head;
          for (; cachep; cachep = cachep->link.next) {
            if (cachep->vol_number == volume_number) {
              is_vol_present = 1;
              if (cachep->scheme == type) {
                Dbg(dbg_ctl_cache_hosting, "Host Record: %p, Volume: %d, size: %ld", this, volume_number,
                    (long)(cachep->size * STORE_BLOCK_SIZE));
                cp[num_cachevols] = cachep;
                num_cachevols++;
                num_vols += cachep->num_vols;
                break;
              }
            }
          }
          if (!is_vol_present) {
            Warning("%s discarding %s entry at line %d : bad volume number [%d]", "[CacheHosting]", config_file,
                    line_info->line_num, volume_number);
            if (val != nullptr) {
              ats_free(val);
            }
            return -1;
          }
          if (c == '\0') {
            break;
          }
          vol_no = s + 1;
        }
        s++;
      }
      ats_free(val);

      break;
    }

    Warning("%s discarding %s entry at line %d : bad token [%s]", "[CacheHosting]", config_file, line_info->line_num, label);
    return -1;
  }

  if (i == MATCHER_MAX_TOKENS) {
    Warning("%s discarding %s entry at line %d : No volumes specified", "[CacheHosting]", config_file, line_info->line_num);
    return -1;
  }

  if (!num_vols) {
    return -1;
  }
  vols        = static_cast<Stripe **>(ats_malloc(num_vols * sizeof(Stripe *)));
  int counter = 0;
  for (i = 0; i < num_cachevols; i++) {
    CacheVol *cachep = cp[i];
    for (j = 0; j < cp[i]->num_vols; j++) {
      vols[counter++] = cachep->vols[j];
    }
  }
  ink_assert(counter == num_vols);

  build_vol_hash_table(this);
  return 0;
}

void
CacheHostRecord::UpdateMatch(CacheHostResult *r)
{
  r->record = this;
}

void
CacheHostRecord::Print() const
{
}
