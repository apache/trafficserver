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

#include "P_Cache.h"
#include "ts/I_Layout.h"
#include "ts/HostLookup.h"
#include "ts/Tokenizer.h"

extern int gndisks;

matcher_tags CacheHosting_tags = {"hostname", "domain"};

/*************************************************************
 *   Begin class HostMatcher
 *************************************************************/

CacheHostMatcher::CacheHostMatcher(const char *name, CacheType typ) : data_array(NULL), array_len(-1), num_el(-1), type(typ)
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
CacheHostMatcher::Print()
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
  CacheHostRecord *d = (CacheHostRecord *)opaque_data;
  d->Print();
}

// void CacheHostMatcher::AllocateSpace(int num_entries)
//
//  Allocates the the HostLeaf and Data arrays
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
CacheHostMatcher::Match(char const *rdata, int rlen, CacheHostResult *result)
{
  void *opaque_ptr;
  CacheHostRecord *data_ptr;
  bool r;

  // Check to see if there is any work to do before makeing
  //   the stirng copy
  if (num_el <= 0) {
    return;
  }

  if (rlen == 0)
    return;
  char *data = (char *)ats_malloc(rlen + 1);
  memcpy(data, rdata, rlen);
  *(data + rlen) = '\0';
  HostLookupState s;

  r = host_lookup->MatchFirst(data, &s, &opaque_ptr);

  while (r == true) {
    ink_assert(opaque_ptr != NULL);
    data_ptr = (CacheHostRecord *)opaque_ptr;
    data_ptr->UpdateMatch(result, data);

    r = host_lookup->MatchNext(&s, &opaque_ptr);
  }
  ats_free(data);
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
  ink_assert(match_data != NULL);

  // Remove our consumed label from the parsed line
  if (line_info->dest_entry < MATCHER_MAX_TOKENS)
    line_info->line[0][line_info->dest_entry] = NULL;
  line_info->num_el--;

  // Fill in the parameter info
  cur_d = data_array + num_el;
  errNo = cur_d->Init(line_info, type);

  if (errNo) {
    // There was a problem so undo the effects this function
    memset(cur_d, 0, sizeof(CacheHostRecord));
    return;
  }
  Debug("cache_hosting", "hostname: %s, host record: %p", match_data, cur_d);
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

  config_tags = &CacheHosting_tags;
  ink_assert(config_tags != NULL);

  type         = typ;
  cache        = c;
  matcher_name = "[CacheHosting]";
  ;
  hostMatch = NULL;

  config_path = RecConfigReadConfigPath("proxy.config.cache.hosting_filename");
  ink_release_assert(config_path);

  m_numEntries = this->BuildTable(config_path);
}

CacheHostTable::~CacheHostTable()
{
  if (hostMatch != NULL) {
    delete hostMatch;
  }
}

// void ControlMatcher<Data, Result>::Print()
//
//   Debugging method
//
void
CacheHostTable::Print()
{
  printf("Control Matcher Table: %s\n", matcher_name);
  if (hostMatch != NULL) {
    hostMatch->Print();
  }
}

// void ControlMatcher<Data, Result>::Match(RequestData* rdata
//                                          Result* result)
//
//   Queries each table for the Result*
//
void
CacheHostTable::Match(char const *rdata, int rlen, CacheHostResult *result)
{
  hostMatch->Match(rdata, rlen, result);
}

int
CacheHostTable::config_callback(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                RecData /* data ATS_UNUSED */, void *cookie)
{
  CacheHostTable **ppt = (CacheHostTable **)cookie;
  eventProcessor.schedule_imm(new CacheHostTableConfig(ppt));
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
  // Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp;
  matcher_line *first = NULL;
  matcher_line *current;
  matcher_line *last = NULL;
  int line_num       = 0;
  int second_pass    = 0;
  int numEntries     = 0;
  const char *errPtr = NULL;

  // type counts
  int hostDomain = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    /* no hosting customers -- put all the volumes in the
       generic table */
    if (gen_host_rec.Init(type))
      Warning("Problems encountered while initializing the Generic Volume");
    return 0;
  }
  // First get the number of entries
  tmp = bufTok.iterFirst(&i_state);
  while (tmp != NULL) {
    line_num++;

    // skip all blank spaces at beginning of line
    while (*tmp && isspace(*tmp)) {
      tmp++;
    }

    if (*tmp != '#' && *tmp != '\0') {
      current = (matcher_line *)ats_malloc(sizeof(matcher_line));
      errPtr  = parseConfigLine((char *)tmp, current, config_tags);

      if (errPtr != NULL) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : %s", matcher_name, config_file_path,
                         line_num, errPtr);
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

        if (first == NULL) {
          ink_assert(last == NULL);
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
    /* no hosting customers -- put all the volumes in the
       generic table */

    if (gen_host_rec.Init(type)) {
      Warning("Problems encountered while initializing the Generic Volume");
    }

    if (first != NULL) {
      ats_free(first);
    }
    return 0;
  }

  if (hostDomain > 0) {
    hostMatch = new CacheHostMatcher(matcher_name, type);
    hostMatch->AllocateSpace(hostDomain);
  }
  // Traverse the list and build the records table
  int generic_rec_initd = 0;
  current               = first;
  while (current != NULL) {
    second_pass++;
    if ((current->type == MATCH_DOMAIN) || (current->type == MATCH_HOST)) {
      char *match_data = current->line[1][current->dest_entry];
      ink_assert(match_data != NULL);

      if (!strcasecmp(match_data, "*")) {
        // generic volume - initialize the generic hostrecord */
        // Make sure that the line_info is not bogus
        ink_assert(current->dest_entry < MATCHER_MAX_TOKENS);

        // Remove our consumed label from the parsed line
        if (current->dest_entry < MATCHER_MAX_TOKENS)
          current->line[0][current->dest_entry] = NULL;
        else
          Warning("Problems encountered while initializing the Generic Volume");

        current->num_el--;
        if (!gen_host_rec.Init(current, type))
          generic_rec_initd = 1;
        else
          Warning("Problems encountered while initializing the Generic Volume");

      } else {
        hostMatch->NewEntry(current);
      }
    } else {
      RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry with unknown type at line %d", matcher_name,
                       config_file_path, current->line_num);
    }

    // Deallocate the parsing structure
    last    = current;
    current = current->next;
    ats_free(last);
  }

  if (!generic_rec_initd) {
    const char *cache_type = (type == CACHE_HTTP_TYPE) ? "http" : "mixt";
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR,
                     "No Volumes specified for Generic Hostnames for %s documents: %s cache will be disabled", cache_type,
                     cache_type);
  }

  ink_assert(second_pass == numEntries);

  if (is_debug_tag_set("matcher")) {
    Print();
  }
  return numEntries;
}

int
CacheHostTable::BuildTable(const char *config_file_path)
{
  char *file_buf;
  int ret;

  file_buf = readIntoBuffer(config_file_path, matcher_name, NULL);

  if (file_buf == NULL) {
    Warning("Cannot read the config file: %s", config_file_path);
    gen_host_rec.Init(type);
    return 0;
  }

  ret = BuildTableFromString(config_file_path, file_buf);
  ats_free(file_buf);
  return ret;
}

int
CacheHostRecord::Init(CacheType typ)
{
  int i, j;
  extern Queue<CacheVol> cp_list;
  extern int cp_list_len;

  num_vols = 0;
  type     = typ;
  cp       = (CacheVol **)ats_malloc(cp_list_len * sizeof(CacheVol *));
  memset(cp, 0, cp_list_len * sizeof(CacheVol *));
  num_cachevols    = 0;
  CacheVol *cachep = cp_list.head;
  for (; cachep; cachep = cachep->link.next) {
    if (cachep->scheme == type) {
      Debug("cache_hosting", "Host Record: %p, Volume: %d, size: %" PRId64, this, cachep->vol_number, (int64_t)cachep->size);
      cp[num_cachevols] = cachep;
      num_cachevols++;
      num_vols += cachep->num_vols;
    }
  }
  if (!num_cachevols) {
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "error: No volumes found for Cache Type %d", type);
    return -1;
  }
  vols        = (Vol **)ats_malloc(num_vols * sizeof(Vol *));
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
    if (!label)
      continue;
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
            RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d :%s", "[CacheHosting]", config_file,
                             line_info->line_num, errptr);
            if (val != NULL) {
              ats_free(val);
            }
            return -1;
          }
        }
        if ((*s < '0') || (*s > '9')) {
          RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : bad token [%c]", "[CacheHosting]",
                           config_file, line_info->line_num, *s);
          if (val != NULL) {
            ats_free(val);
          }
          return -1;
        }
        s++;
      }
      s = val;
      num_cachevols++;
      cp = (CacheVol **)ats_malloc(num_cachevols * sizeof(CacheVol *));
      memset(cp, 0, num_cachevols * sizeof(CacheVol *));
      num_cachevols = 0;
      while (1) {
        char c = *s;
        if ((c == ',') || (c == '\0')) {
          *s            = '\0';
          volume_number = atoi(vol_no);

          cachep = cp_list.head;
          for (; cachep; cachep = cachep->link.next) {
            if (cachep->vol_number == volume_number) {
              is_vol_present = 1;
              if (cachep->scheme == type) {
                Debug("cache_hosting", "Host Record: %p, Volume: %d, size: %ld", this, volume_number,
                      (long)(cachep->size * STORE_BLOCK_SIZE));
                cp[num_cachevols] = cachep;
                num_cachevols++;
                num_vols += cachep->num_vols;
                break;
              }
            }
          }
          if (!is_vol_present) {
            RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : bad volume number [%d]",
                             "[CacheHosting]", config_file, line_info->line_num, volume_number);
            if (val != NULL) {
              ats_free(val);
            }
            return -1;
          }
          if (c == '\0')
            break;
          vol_no = s + 1;
        }
        s++;
      }
      if (val != NULL) {
        ats_free(val);
      }
      break;
    }

    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : bad token [%s]", "[CacheHosting]", config_file,
                     line_info->line_num, label);
    return -1;
  }

  if (i == MATCHER_MAX_TOKENS) {
    RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : No volumes specified", "[CacheHosting]",
                     config_file, line_info->line_num);
    return -1;
  }

  if (!num_vols) {
    return -1;
  }
  vols        = (Vol **)ats_malloc(num_vols * sizeof(Vol *));
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
CacheHostRecord::UpdateMatch(CacheHostResult *r, char * /* rd ATS_UNUSED */)
{
  r->record = this;
}

void
CacheHostRecord::Print()
{
}

void
ConfigVolumes::read_config_file()
{
  ats_scoped_str config_path;
  char *file_buf;

  config_path = RecConfigReadConfigPath("proxy.config.cache.volume_filename");
  ink_release_assert(config_path);

  file_buf = readIntoBuffer(config_path, "[CacheVolition]", NULL);
  if (file_buf == NULL) {
    Warning("Cannot read the config file: %s", (const char *)config_path);
    return;
  }

  BuildListFromString(config_path, file_buf);
  ats_free(file_buf);
  return;
}

void
ConfigVolumes::BuildListFromString(char *config_file_path, char *file_buf)
{
#define PAIR_ZERO 0
#define PAIR_ONE 1
#define PAIR_TWO 2
#define DONE 3
#define INK_ERROR -1

#define INK_ERROR_VOLUME -2 // added by YTS Team, yamsat for bug id 59632
  // Table build locals
  Tokenizer bufTok("\n");
  tok_iter_state i_state;
  const char *tmp;
  char *end;
  char *line_end = NULL;
  int line_num   = 0;
  int total      = 0; // added by YTS Team, yamsat for bug id 59632

  char volume_seen[256];
  int state                = 0; // changed by YTS Team, yamsat for bug id 59632
  int volume_number        = 0;
  CacheType scheme         = CACHE_NONE_TYPE;
  int size                 = 0;
  int in_percent           = 0;
  const char *matcher_name = "[CacheVolition]";

  memset(volume_seen, 0, sizeof(volume_seen));
  num_volumes        = 0;
  num_stream_volumes = 0;
  num_http_volumes   = 0;

  if (bufTok.Initialize(file_buf, SHARE_TOKS | ALLOW_EMPTY_TOKS) == 0) {
    // We have an empty file
    /* no volumes */
    return;
  }
  // First get the number of entries
  tmp = bufTok.iterFirst(&i_state);
  while (tmp != NULL) {
    state = PAIR_ZERO;
    line_num++;

    // skip all blank spaces at beginning of line
    while (1) {
      while (*tmp && isspace(*tmp)) {
        tmp++;
      }

      if (!(*tmp) && state == DONE) {
        /* add the config */

        ConfigVol *configp = new ConfigVol();
        configp->number    = volume_number;
        if (in_percent) {
          configp->percent    = size;
          configp->in_percent = 1;
        } else {
          configp->in_percent = 0;
        }
        configp->scheme = scheme;
        configp->size   = size;
        configp->cachep = NULL;
        cp_queue.enqueue(configp);
        num_volumes++;
        if (scheme == CACHE_HTTP_TYPE)
          num_http_volumes++;
        else
          num_stream_volumes++;
        Debug("cache_hosting", "added volume=%d, scheme=%d, size=%d percent=%d\n", volume_number, scheme, size, in_percent);
        break;
      }

      if (state == PAIR_ZERO) {
        if (*tmp == '\0' || *tmp == '#')
          break;
      } else {
        if (!(*tmp)) {
          RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : Unexpected end of line", matcher_name,
                           config_file_path, line_num);
          break;
        }
      }

      end = (char *)tmp;
      while (*end && !isspace(*end))
        end++;

      if (!(*end))
        line_end = end;
      else {
        line_end = end + 1;
        *end     = '\0';
      }
      char *eq_sign;

      eq_sign = (char *)strchr(tmp, '=');
      if (!eq_sign) {
        state = INK_ERROR;
      } else
        *eq_sign = '\0';

      switch (state) {
      case PAIR_ZERO:
        if (strcasecmp(tmp, "volume")) {
          state = INK_ERROR;
          break;
        }
        tmp += 7; // size of string volume including null
        volume_number = atoi(tmp);

        if (volume_number < 1 || volume_number > 255 || volume_seen[volume_number]) {
          const char *err;

          if (volume_number < 1 || volume_number > 255) {
            err = "Bad Volume Number";
          } else {
            err = "Volume Already Specified";
          }

          RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : %s [%d]", matcher_name, config_file_path,
                           line_num, err, volume_number);
          state = INK_ERROR;
          break;
        }

        volume_seen[volume_number] = 1;
        while (ParseRules::is_digit(*tmp))
          tmp++;
        state = PAIR_ONE;
        break;

      case PAIR_ONE:
        if (strcasecmp(tmp, "scheme")) {
          state = INK_ERROR;
          break;
        }
        tmp += 7; // size of string scheme including null

        if (!strcasecmp(tmp, "http")) {
          tmp += 4;
          scheme = CACHE_HTTP_TYPE;
        } else if (!strcasecmp(tmp, "mixt")) {
          tmp += 4;
          scheme = CACHE_RTSP_TYPE;
        } else {
          state = INK_ERROR;
          break;
        }

        state = PAIR_TWO;
        break;

      case PAIR_TWO:

        if (strcasecmp(tmp, "size")) {
          state = INK_ERROR;
          break;
        }
        tmp += 5;
        size = atoi(tmp);

        while (ParseRules::is_digit(*tmp))
          tmp++;

        if (*tmp == '%') {
          // added by YTS Team, yamsat for bug id 59632
          total += size;
          if (size > 100 || total > 100) {
            state = INK_ERROR_VOLUME;
            RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "Total volume size added upto more than 100 percent, No volumes created");
            break;
          }
          // ends here
          in_percent = 1;
          tmp++;
        } else
          in_percent = 0;
        state        = DONE;
        break;
      }

      if (state == INK_ERROR || *tmp) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "%s discarding %s entry at line %d : Invalid token [%s]", matcher_name,
                         config_file_path, line_num, tmp);
        break;
      }
      // added by YTS Team, yamsat for bug id 59632
      if (state == INK_ERROR_VOLUME || *tmp) {
        RecSignalWarning(REC_SIGNAL_CONFIG_ERROR, "Total volume size added upto more than 100 percent,No volumes created");
        break;
      }
      // ends here
      if (end < line_end)
        tmp++;
    }
    tmp = bufTok.iterNext(&i_state);
  }

  return;
}

/* Test the cache volumeing with different configurations */
#define MEGS_128 (128 * 1024 * 1024)
#define ROUND_TO_VOL_SIZE(_x) (((_x) + (MEGS_128 - 1)) & ~(MEGS_128 - 1))
extern CacheDisk **gdisks;
extern Queue<CacheVol> cp_list;
extern int cp_list_len;
extern ConfigVolumes config_volumes;
extern volatile int gnvol;

extern void cplist_init();
extern int cplist_reconfigure();
static int configs = 4;

Queue<CacheVol> saved_cp_list;
int saved_cp_list_len;
ConfigVolumes saved_config_volumes;
int saved_gnvol;

static int ClearConfigVol(ConfigVolumes *configp);
static int ClearCacheVolList(Queue<CacheVol> *cpl, int len);
static int create_config(RegressionTest *t, int i);
static int execute_and_verify(RegressionTest *t);
static void save_state();
static void restore_state();

EXCLUSIVE_REGRESSION_TEST(Cache_vol)(RegressionTest *t, int /* atype ATS_UNUSED */, int *status)
{
  save_state();
  srand48(time(NULL));
  *status = REGRESSION_TEST_PASSED;
  for (int i = 0; i < configs; i++) {
    if (create_config(t, i)) {
      if (execute_and_verify(t) == REGRESSION_TEST_FAILED) {
        *status = REGRESSION_TEST_FAILED;
      }
    }
  }
  restore_state();
  return;
}

int
create_config(RegressionTest *t, int num)
{
  int i       = 0;
  int vol_num = 1;
  // clear all old configurations before adding new test cases
  config_volumes.clear_all();
  switch (num) {
  case 0:
    for (i = 0; i < gndisks; i++) {
      CacheDisk *d = gdisks[i];
      int blocks   = d->num_usable_blocks;
      if (blocks < STORE_BLOCKS_PER_VOL) {
        rprintf(t, "Cannot run Cache_vol regression: not enough disk space\n");
        return 0;
      }
      /* create 128 MB volumes */
      for (; blocks >= STORE_BLOCKS_PER_VOL; blocks -= STORE_BLOCKS_PER_VOL) {
        if (vol_num > 255)
          break;
        ConfigVol *cp  = new ConfigVol();
        cp->number     = vol_num++;
        cp->scheme     = CACHE_HTTP_TYPE;
        cp->size       = 128;
        cp->in_percent = 0;
        cp->cachep     = 0;
        config_volumes.cp_queue.enqueue(cp);
        config_volumes.num_volumes++;
        config_volumes.num_http_volumes++;
      }
    }
    rprintf(t, "%d 128 Megabyte Volumes\n", vol_num - 1);
    break;

  case 1: {
    for (i = 0; i < gndisks; i++) {
      gdisks[i]->delete_all_volumes();
    }

    // calculate the total free space
    off_t total_space = 0;
    for (i = 0; i < gndisks; i++) {
      off_t vol_blocks = gdisks[i]->num_usable_blocks;
      /* round down the blocks to the nearest
         multiple of STORE_BLOCKS_PER_VOL */
      vol_blocks = (vol_blocks / STORE_BLOCKS_PER_VOL) * STORE_BLOCKS_PER_VOL;
      total_space += vol_blocks;
    }

    // make sure we have atleast 1280 M bytes
    if (total_space<(10 << 27)>> STORE_BLOCK_SHIFT) {
      rprintf(t, "Not enough space for 10 volume\n");
      return 0;
    }

    vol_num = 1;
    rprintf(t, "Cleared  disk\n");
    for (i = 0; i < 10; i++) {
      ConfigVol *cp  = new ConfigVol();
      cp->number     = vol_num++;
      cp->scheme     = CACHE_HTTP_TYPE;
      cp->size       = 10;
      cp->percent    = 10;
      cp->in_percent = 1;
      cp->cachep     = 0;
      config_volumes.cp_queue.enqueue(cp);
      config_volumes.num_volumes++;
      config_volumes.num_http_volumes++;
    }
    rprintf(t, "10 volume, 10 percent each\n");
  } break;

  case 2:
  case 3:

  {
    /* calculate the total disk space */
    InkRand *gen      = &this_ethread()->generator;
    off_t total_space = 0;
    vol_num           = 1;
    if (num == 2) {
      rprintf(t, "Random Volumes after clearing the disks\n");
    } else {
      rprintf(t, "Random Volumes without clearing the disks\n");
    }

    for (i = 0; i < gndisks; i++) {
      off_t vol_blocks = gdisks[i]->num_usable_blocks;
      /* round down the blocks to the nearest
         multiple of STORE_BLOCKS_PER_VOL */
      vol_blocks = (vol_blocks / STORE_BLOCKS_PER_VOL) * STORE_BLOCKS_PER_VOL;
      total_space += vol_blocks;

      if (num == 2) {
        gdisks[i]->delete_all_volumes();
      } else {
        gdisks[i]->cleared = 0;
      }
    }
    while (total_space > 0) {
      if (vol_num > 255)
        break;
      off_t modu = MAX_VOL_SIZE;
      if (total_space < (MAX_VOL_SIZE >> STORE_BLOCK_SHIFT)) {
        modu = total_space * STORE_BLOCK_SIZE;
      }

      off_t random_size = (gen->random() % modu) + 1;
      /* convert to 128 megs multiple */
      CacheType scheme = (random_size % 2) ? CACHE_HTTP_TYPE : CACHE_RTSP_TYPE;
      random_size      = ROUND_TO_VOL_SIZE(random_size);
      off_t blocks     = random_size / STORE_BLOCK_SIZE;
      ink_assert(blocks <= (int)total_space);
      total_space -= blocks;

      ConfigVol *cp = new ConfigVol();

      cp->number     = vol_num++;
      cp->scheme     = scheme;
      cp->size       = random_size >> 20;
      cp->percent    = 0;
      cp->in_percent = 0;
      cp->cachep     = 0;
      config_volumes.cp_queue.enqueue(cp);
      config_volumes.num_volumes++;
      if (cp->scheme == CACHE_HTTP_TYPE) {
        config_volumes.num_http_volumes++;
        rprintf(t, "volume=%d scheme=http size=%d\n", cp->number, cp->size);
      } else {
        config_volumes.num_stream_volumes++;
        rprintf(t, "volume=%d scheme=rtsp size=%d\n", cp->number, cp->size);
      }
    }
  } break;

  default:
    return 1;
  }
  return 1;
}

int
execute_and_verify(RegressionTest *t)
{
  cplist_init();
  cplist_reconfigure();

  /* compare the volumes */
  if (cp_list_len != config_volumes.num_volumes)
    return REGRESSION_TEST_FAILED;

  /* check that the volumes and sizes
     match the configuration */
  int matched   = 0;
  ConfigVol *cp = config_volumes.cp_queue.head;
  CacheVol *cachep;

  for (int i = 0; i < config_volumes.num_volumes; i++) {
    cachep = cp_list.head;
    while (cachep) {
      if (cachep->vol_number == cp->number) {
        if ((cachep->scheme != cp->scheme) || (cachep->size != (cp->size << (20 - STORE_BLOCK_SHIFT))) || (cachep != cp->cachep)) {
          rprintf(t, "Configuration and Actual volumes don't match\n");
          return REGRESSION_TEST_FAILED;
        }

        /* check that the number of volumes match the ones
           on disk */
        int d_no;
        int m_vols = 0;
        for (d_no = 0; d_no < gndisks; d_no++) {
          if (cachep->disk_vols[d_no]) {
            DiskVol *dp = cachep->disk_vols[d_no];
            if (dp->vol_number != cachep->vol_number) {
              rprintf(t, "DiskVols and CacheVols don't match\n");
              return REGRESSION_TEST_FAILED;
            }

            /* check the diskvolblock queue */
            DiskVolBlockQueue *dpbq = dp->dpb_queue.head;
            while (dpbq) {
              if (dpbq->b->number != cachep->vol_number) {
                rprintf(t, "DiskVol and DiskVolBlocks don't match\n");
                return REGRESSION_TEST_FAILED;
              }
              dpbq = dpbq->link.next;
            }

            m_vols += dp->num_volblocks;
          }
        }
        if (m_vols != cachep->num_vols) {
          rprintf(t, "Num volumes in CacheVol and DiskVol don't match\n");
          return REGRESSION_TEST_FAILED;
        }
        matched++;
        break;
      }
      cachep = cachep->link.next;
    }
  }

  if (matched != config_volumes.num_volumes) {
    rprintf(t, "Num of Volumes created and configured don't match\n");
    return REGRESSION_TEST_FAILED;
  }

  ClearConfigVol(&config_volumes);

  ClearCacheVolList(&cp_list, cp_list_len);

  for (int i = 0; i < gndisks; i++) {
    CacheDisk *d = gdisks[i];
    if (is_debug_tag_set("cache_hosting")) {
      Debug("cache_hosting", "Disk: %d: Vol Blocks: %u: Free space: %" PRIu64, i, d->header->num_diskvol_blks, d->free_space);
      for (int j = 0; j < (int)d->header->num_volumes; j++) {
        Debug("cache_hosting", "\tVol: %d Size: %" PRIu64, d->disk_vols[j]->vol_number, d->disk_vols[j]->size);
      }
      for (int j = 0; j < (int)d->header->num_diskvol_blks; j++) {
        Debug("cache_hosting", "\tBlock No: %d Size: %" PRIu64 " Free: %u", d->header->vol_info[j].number,
              d->header->vol_info[j].len, d->header->vol_info[j].free);
      }
    }
  }
  return REGRESSION_TEST_PASSED;
}

int
ClearConfigVol(ConfigVolumes *configp)
{
  int i         = 0;
  ConfigVol *cp = NULL;
  while ((cp = configp->cp_queue.dequeue())) {
    delete cp;
    i++;
  }
  if (i != configp->num_volumes) {
    Warning("failed");
    return 0;
  }
  configp->num_volumes        = 0;
  configp->num_http_volumes   = 0;
  configp->num_stream_volumes = 0;
  return 1;
}

int
ClearCacheVolList(Queue<CacheVol> *cpl, int len)
{
  int i        = 0;
  CacheVol *cp = NULL;
  while ((cp = cpl->dequeue())) {
    ats_free(cp->disk_vols);
    ats_free(cp->vols);
    delete (cp);
    i++;
  }

  if (i != len) {
    Warning("Failed");
    return 0;
  }
  return 1;
}

void
save_state()
{
  saved_cp_list     = cp_list;
  saved_cp_list_len = cp_list_len;
  memcpy(&saved_config_volumes, &config_volumes, sizeof(ConfigVolumes));
  saved_gnvol = gnvol;
  memset(&cp_list, 0, sizeof(Queue<CacheVol>));
  memset(&config_volumes, 0, sizeof(ConfigVolumes));
  gnvol = 0;
}

void
restore_state()
{
  cp_list     = saved_cp_list;
  cp_list_len = saved_cp_list_len;
  memcpy(&config_volumes, &saved_config_volumes, sizeof(ConfigVolumes));
  gnvol = saved_gnvol;
}
