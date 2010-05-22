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

#include "P_CacheTest.h"
#include <math.h>
Diags *diags;
#define DIAGS_LOG_FILE "diags.log"

//////////////////////////////////////////////////////////////////////////////
//
//      void reconfigure_diags()
//
//      This function extracts the current diags configuration settings from
//      records.config, and rebuilds the Diags data structures.
//
//////////////////////////////////////////////////////////////////////////////

static void
reconfigure_diags()
{
  int i;
  DiagsConfigState c;


  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug] = (diags->base_debug_tags != NULL);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != NULL);

  c.enabled[DiagsTagType_Debug] = 1;
  c.enabled[DiagsTagType_Action] = 1;
  diags->show_location = 1;


  // read output routing values
  for (i = 0; i < DiagsLevel_Count; i++) {

    c.outputs[i].to_stdout = 0;
    c.outputs[i].to_stderr = 1;
    c.outputs[i].to_syslog = 1;
    c.outputs[i].to_diagslog = 1;
  }

  //////////////////////////////
  // clear out old tag tables //
  //////////////////////////////

  diags->deactivate_all(DiagsTagType_Debug);
  diags->deactivate_all(DiagsTagType_Action);

  //////////////////////////////////////////////////////////////////////
  //                     add new tag tables 
  //////////////////////////////////////////////////////////////////////

  if (diags->base_debug_tags)
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  if (diags->base_action_tags)
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);

  ////////////////////////////////////
  // change the diags config values //
  ////////////////////////////////////
#if !defined (_WIN32) && !defined(__GNUC__) && !defined(hpux)
  diags->config = c;
#else
  memcpy(((void *) &diags->config), ((void *) &c), sizeof(DiagsConfigState));
#endif

}



static void
init_diags(char *bdt, char *bat)
{
  FILE *diags_log_fp;
  char diags_logpath[500];
  strcpy(diags_logpath, DIAGS_LOG_FILE);

  diags_log_fp = fopen(diags_logpath, "w");
  if (diags_log_fp) {
    int status;
    status = setvbuf(diags_log_fp, NULL, _IOLBF, 512);
    if (status != 0) {
      fclose(diags_log_fp);
      diags_log_fp = NULL;
    }
  }

  diags = NEW(new Diags(bdt, bat, diags_log_fp));

  if (diags_log_fp == NULL) {
    SrcLoc loc(__FILE__, __FUNCTION__, __LINE__);

    diags->print(NULL, DL_Warning, NULL, &loc,
                 "couldn't open diags log file '%s', " "will not log to this file", diags_logpath);
  }

  diags->print(NULL, DL_Status, "STATUS", NULL, "opened %s", diags_logpath);
  reconfigure_diags();

}

enum
{
  TOKEN_ERROR = -1,
  TOKEN_NONE,
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_NUMBER,
  TOKEN_STRING,
  TOKEN_SYMBOL
};

enum
{
  PARSE_START,
  PARSE_NUMBER,
  PARSE_STRING,
  PARSE_SYMBOL
};


static CacheSMTest **cache_sm_tests;

static char cache_test_id[128];
static Ptr<IOBufferData> cache_test_data;
static int cache_buffer_size_index;

ClassAllocator<PinnedDocEntry> pinnedDocEntryAllocator("PinnedDocEntry");

extern int cache_config_permit_pinning;

#define CHECK_BUF_SIZE (32*1024)
static char check_buf[CHECK_BUF_SIZE];

static int
check_content(CacheSMTest * test, IOBufferReader * buffer_reader)
{

  int read_avail = buffer_reader->read_avail();
  int i;
  int buf_ndx = 0;
  int read = 0;
  while (read_avail > 0) {
    int to_read = buffer_reader->block_read_avail();
    if (test->control->xcheck_content) {
      char *p = buffer_reader->start();
      for (i = 0; i < to_read; i += read) {
        buf_ndx = (test->idx + i) % ('z' - 'a');
        read = (to_read - i);
        if ((buf_ndx + read) > CHECK_BUF_SIZE)
          read = CHECK_BUF_SIZE - buf_ndx;
        if (memcmp(&p[i], &check_buf[buf_ndx], read)) {
          printf("Content check failure\n");
          return 0;
        }
      }
      test->idx += to_read;
    }
    buffer_reader->consume(to_read);
    read_avail -= to_read;
  }
  return 1;
}


int
runCacheTest(CacheTestControl * control)
{
  int buffer_size;
  int i;

  eventProcessor.schedule_in(control, HRTIME_SECONDS(2), ET_CALL);

  srand48(time(NULL));
  sprintf(cache_test_id, "%0.12f", drand48());

  buffer_size = control->buffer_size();

  cache_buffer_size_index = 0;
  while (BUFFER_SIZE_FOR_INDEX(cache_buffer_size_index) < buffer_size) {
    cache_buffer_size_index += 1;
    if (cache_buffer_size_index == DEFAULT_BUFFER_SIZES) {
      break;
    }
  }

  /* fill up the static buffer to use for filling the documents */
  for (i = 0; i < CHECK_BUF_SIZE; i++) {
    check_buf[i] = (i % ('z' - 'a')) + 'a';
  }

  if (control->xpin_percent) {
    if (!cache_config_permit_pinning) {
      printf("CacheTest:: Cannot Test pinning. Need to Enable %s\n", "proxy.config.cache.permit.pinning");
    }
    for (i = 0; i < PINNED_DOC_TABLES; i++) {
      control->pin_tables[i] = new PinnedDocTable();
      SET_CONTINUATION_HANDLER(control->pin_tables[i], &PinnedDocTable::cleanup);
      eventProcessor.schedule_every(control->pin_tables[i], HRTIME_SECONDS((control->xmean_pin_time / 2)));
    }
  }

  cache_test_data = new_IOBufferData(cache_buffer_size_index);
  memset(cache_test_data->data(), 0, cache_test_data->block_size());

  cache_sm_tests = NEW(new CacheSMTest *[control->users()]);

  for (i = 0; i < control->users(); i++) {
    cache_sm_tests[i] = NEW(new CacheSMTest(control, i));
    eventProcessor.schedule_imm(cache_sm_tests[i], ET_CALL);
  }
  return 0;
}


int
runCacheTest()
{
  printf("starting off the cache tester \n");
  CacheTestControl *control;
  control = NEW(new CacheTestControl);
  if (control->open("cache_test.config") < 0) {
    fprintf(stderr, "could not open cache_test.config file \n");
    return -1;
  }
  runCacheTest(control);
  return 0;
}


void
PinnedDocTable::insert(CacheKey * key, time_t pin_in_cache, int update)
{
  int b = key->word(3) % PINNED_DOC_TABLE_SIZE;

  PinnedDocEntry *e = bucket[b].head;
  ink_time_t now = (ink_get_based_hrtime() / HRTIME_SECOND);
  ink_time_t time = now + pin_in_cache;
  for (; e; e = e->link.next) {
    if (e->key == *key) {
      e->key = *key;
      // this is an update. We have to keep the smaller of the two times
      if (e->time > time)
        e->time = time;
      return;
    }
  }
  if (update)
    return;
  e = pinnedDocEntryAllocator.alloc();
  e->key = *key;
  e->time = time;
  bucket[b].enqueue(e);
}

int
PinnedDocTable::probe(CacheKey * key)
{
  int b = key->word(3) % PINNED_DOC_TABLE_SIZE;

  ink_time_t now = (ink_time_t) (ink_get_based_hrtime() / HRTIME_SECOND) + 2;
  PinnedDocEntry *e = bucket[b].head;
  for (; e; e = e->link.next) {
    if (e->key == *key && e->time > now) {
      return 1;
    }
  }
  return 0;
}

int
PinnedDocTable::remove(CacheKey * key)
{
  int b = key->word(3) % PINNED_DOC_TABLE_SIZE;

  PinnedDocEntry *e = bucket[b].head;
  for (; e; e = e->link.next) {
    if (e->key == *key) {
      bucket[b].remove(e);
      pinnedDocEntryAllocator.free(e);
      return 1;
    }
  }
  return 0;
}

int
PinnedDocTable::cleanup(int event, Event * e)
{

  ink_time_t now = (ink_time_t) (ink_get_based_hrtime() / HRTIME_SECOND);
  for (int b = 0; b < PINNED_DOC_TABLE_SIZE; b++) {
    PinnedDocEntry *e = bucket[b].head;
    while (e) {
      if (e->time < now) {
        PinnedDocEntry *n = e->link.next;
        bucket[b].remove(e);
        pinnedDocEntryAllocator.free(e);
        e = n;
        continue;
      }
      e = e->link.next;
    }
  }
  return 0;
}

PinnedDocTable *
CacheTestControl::getPinnedTable(INK_MD5 * md5)
{
  int table_idx = md5->word(0) % PINNED_DOC_TABLES;
  return pin_tables[table_idx];
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

CacheTestControl::CacheTestControl()
:Continuation(new_ProxyMutex())
{
  xusers = 1;
  xstate_machines = 1;
  xrequests = 1;
  xbuffer_size = 32 * 1024;
  xaverage_over = 1;
  xrun_length = 0;
  xmean_doc_size = 13 * 1024;
  xlast_cachable_id = 10000;
  xnum_hosts = 0;
  xhotset_probability = 1;
  xremove_probability = 0;
  xupdate_probability = 0;
  xpure_update_probability = 0;
  xcancel_probability = 0;
  xabort_probability = 0;
  xnum_alternates = 1;
  xcheck_content = 0;
  xfill_cache = 1;
  xhttp_req_probability = 1;

  last_elapsed = 0;
  n_average = 0;
  avg_ops_sec = 0;

  read_successes = 0;
  lread_successes = 0;
  read_failures = 0;
  vc_read_failures = 0;
  read_bytes = 0;
  read_time = 0;
  aio_successes = 0;
  laio_successes = 0;
  lookup_successes = 0;
  lookup_failures = 0;
  lookup_hit_time = 0;
  lookup_miss_time = 0;
  write_successes = 0;
  lwrite_successes = 0;
  write_failures = 0;
  vc_write_failures = 0;
  write_bytes = 0;
  write_time = 0;
  update_successes = 0;
  update_failures = 0;
  vc_update_failures = 0;

  SET_HANDLER(&CacheTestControl::print_stats_event);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

CacheTestControl::~CacheTestControl()
{
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::open(const char *path)
{
  FILE *fp;
  char buf[1024];
  char *p;
  int length;

  fp = fopen(path, "r");
  if (!fp) {
    return -1;
  }

  for (;;) {
    fgets(buf, 1024, fp);
    if (feof(fp)) {
      break;
    }

    p = buf;
    while ((*p == ' ') || (*p == '\t')) {
      p += 1;
    }
    if (p[0] == '#') {
      continue;
    }

    length = strlen(p);

    if (p[length - 1] != '\n') {
      break;
    }

    p[length - 1] = '\0';
    length -= 1;

    if (parse(p, length) < 0) {
      break;
    }
  }

  fclose(fp);

  start_time = ink_get_hrtime();
  last_time = start_time;

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::parse(const char *buf, int length)
{
  char symbol[1024];
  char hostname[1024];
  int res;

  res = get_left_paren(buf, length);
  if (res == TOKEN_NONE) {
    return 0;
  } else if (res != TOKEN_LEFT_PAREN) {
    return -1;
  }

  if (get_symbol(buf, length, symbol) != TOKEN_SYMBOL) {
    return -1;
  }

  if (strcmp(symbol, "users") == 0) {
    if (get_integer(buf, length, &xusers) != TOKEN_NUMBER) {
      return -1;
    }
    xstate_machines = xusers;
  } else if (strcmp(symbol, "requests") == 0) {
    if (get_integer(buf, length, (int *) &xrequests) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "buffer-size") == 0) {
    if (get_integer(buf, length, &xbuffer_size) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "average-over") == 0) {
    if (get_integer(buf, length, &xaverage_over) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "run-length") == 0) {
    if (get_integer(buf, length, &xrun_length) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "num-alternates") == 0) {
    if (get_integer(buf, length, &xnum_alternates) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "check-content") == 0) {
    if (get_integer(buf, length, &xcheck_content) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "fill-cache") == 0) {
    if (get_integer(buf, length, &xfill_cache) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "hotset-probability") == 0) {
    if (get_float(buf, length, &xhotset_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "remove-probability") == 0) {
    if (get_float(buf, length, &xremove_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "update-probability") == 0) {
    if (get_float(buf, length, &xupdate_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "pure-update-probability") == 0) {
    if (get_float(buf, length, &xpure_update_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "cancel-probability") == 0) {
    if (get_float(buf, length, &xcancel_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "abort-probability") == 0) {
    if (get_float(buf, length, &xabort_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "mean-doc-size") == 0) {
    if (get_integer(buf, length, &xmean_doc_size) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "http-requests") == 0) {
    if (get_float(buf, length, &xhttp_req_probability) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "pin-percent") == 0) {
    if (get_float(buf, length, &xpin_percent) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "mean-pin-time") == 0) {
    if (get_integer(buf, length, &xmean_pin_time) != TOKEN_NUMBER) {
      return -1;
    }
  } else if (strcmp(symbol, "host") == 0) {
    double host_prob;
    if (get_string(buf, length, hostname) != TOKEN_STRING) {
      return -1;
    }
    if (get_float(buf, length, &host_prob) != TOKEN_NUMBER) {
      return -1;
    }
    if (strcmp(hostname, ".") == 0) {
      host_array[xnum_hosts].name = xstrdup("www.foobar.com");
    } else {
      host_array[xnum_hosts].name = xstrdup(hostname);
    }
    if (xnum_hosts == 0) {
      host_array[xnum_hosts].xprev_host_prob = 0.0;
    } else {
      host_array[xnum_hosts].xprev_host_prob = host_array[xnum_hosts - 1].xnext_host_prob;
    }
    host_array[xnum_hosts].xnext_host_prob = host_array[xnum_hosts].xprev_host_prob + host_prob;
    xnum_hosts++;
    xlast_cachable_id = xusers;


  } else {
    return 0;
  }

  if (get_right_paren(buf, length) != TOKEN_RIGHT_PAREN) {
    return -1;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_token(const char *&buf, int &length, char **token_start, char **token_end)
{
  int state;
  bool decimal;
  char ch;

  state = PARSE_START;
  *token_start = NULL;
  *token_end = NULL;

  for (;;) {
    if (length == 0) {
      return TOKEN_NONE;
    }

    ch = *buf;
    buf += 1;
    length -= 1;

    switch (state) {
    case PARSE_START:
      if (ch == '(') {
        return TOKEN_LEFT_PAREN;
      } else if (ch == ')') {
        return TOKEN_RIGHT_PAREN;
      } else if (ch == '#') {
        return TOKEN_NONE;
      } else if (ch == '"') {
        state = PARSE_STRING;
        *token_start = (char *) buf;
      } else if (ParseRules::is_digit(ch) || (ch == '.')) {
        state = PARSE_NUMBER;
        buf -= 1;
        length += 1;
        *token_start = (char *) buf;
        decimal = false;
      } else if (!isspace(ch)) {
        state = PARSE_SYMBOL;
        buf -= 1;
        length += 1;
        *token_start = (char *) buf;
      }
      break;

    case PARSE_NUMBER:
      if (ParseRules::is_digit(ch) || (ch == '.')) {
        if (ch == '.') {
          if (decimal) {
            return TOKEN_ERROR;
          }
          decimal = true;
        }
      } else {
        buf -= 1;
        length += 1;
        *token_end = (char *) buf;
        return TOKEN_NUMBER;
      }
      break;

    case PARSE_STRING:
      if (ch == '"') {
        *token_end = (char *) buf;
        return TOKEN_STRING;
      }
      break;

    case PARSE_SYMBOL:
      if (isspace(ch)) {
        buf -= 1;
        length += 1;
        *token_end = (char *) buf;
        return TOKEN_SYMBOL;
      }
      break;
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_left_paren(const char *&buf, int &length)
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_LEFT_PAREN) {
    return token;
  }

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_right_paren(const char *&buf, int &length)
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_RIGHT_PAREN) {
    return token;
  }

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_float(const char *&buf, int &length, double *val)
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_NUMBER) {
    return token;
  }

  *val = atof(start);

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_integer(const char *&buf, int &length, int *val)
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_NUMBER) {
    return token;
  }

  *val = atoi(start);

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_string(const char *&buf, int &length, char str[1024])
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_STRING) {
    return token;
  }

  if ((end - start) >= 1024) {
    return TOKEN_ERROR;
  }

  strncpy(str, start, end - start - 1);
  str[end - start - 1] = '\0';

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::get_symbol(const char *&buf, int &length, char symbol[1024])
{
  char *start;
  char *end;
  int token;

  token = get_token(buf, length, &start, &end);
  if (token != TOKEN_SYMBOL) {
    return -1;
  }

  if ((end - start) >= 1024) {
    return -1;
  }

  strncpy(symbol, start, end - start);
  symbol[end - start] = '\0';

  return token;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::users()
{
  return xusers;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheTestControl::buffer_size()
{
  return xbuffer_size;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static const unsigned long real_seed[] = {
  0x991539b0, 0x16a5bce1, 0x6774a4ca, 0x3e01511a, 0x4e508aa5, 0x61048bff,
  0xf5500610, 0x846b710d, 0x6a198923, 0x896a97a5, 0xdb48f92b, 0x14898448,
  0x37ffd0f9, 0xb58bff8e, 0x59e170f5, 0xcf918a39, 0x09378c72, 0x52c7a45f,
  0x8d293e96, 0x1f4fc2ed, 0xc3db71a9, 0x39b44e06, 0xf8a44ee2, 0x4c8b8099,
  0x19edc30f, 0x87bf4bc3, 0xc9b240ca, 0xe9ee4aff, 0x4382aeca, 0x535b6b23,
  0x9a31901a, 0x32d9c004, 0x9b663161, 0x5da1f320, 0xde3b81bd, 0xdf0a6f91,
  0xf103bbdd, 0x48f340d5, 0x7449e544, 0xbeb1db88, 0xab5c58ef, 0x946554d3,
  0x8c2e67e4, 0xeb3d7973, 0xb11ee08a, 0x2d436b58, 0xda672dfb, 0x1588ca58,
  0xe369732c, 0x904f35c5, 0xd7158fa3, 0x6fa6f01d, 0x616e6b61, 0xac94efa6,
  0x36413f5c, 0xc622c260, 0xf5a42a7f, 0x8a88d741, 0xf5ad9cd3, 0x899921cf
};
static int real_seed_count = sizeof(real_seed) / sizeof(real_seed[0]);

static long
my_seed(long seed)
{
  return real_seed[seed % real_seed_count] + seed;
}

static double
my_drandom(long *state)
{
  /*
   * Compute x[n + 1] = (7^5 * x[n]) mod (2^31 - 1).
   * From "Random number generators: good ones are hard to find",
   * Park and Miller, Communications of the ACM, vol. 31, no. 10,
   * October 1988, p. 1195.
   */
  const long h = *state;
  const long hi = h / 127773;
  const long lo = h % 127773;
  const long x = 16807 * lo - 2836 * hi;

  *state = (x <= 0) ? (x + 0x7fffffff) : x;
  return *state / 2147483648.0;
}

int
CacheTestControl::get_request(CacheSMTest * test, char *buffer, int size)
{
  InkRand *gen = &this_ethread()->generator;
  int req_size;
  unsigned int id;
  long state;
  int val;
  double random_host_prob = gen->drandom();
  CacheTestHost *h;

  if (!xfill_cache) {
    val = ink_atomic_increment((int *) &xrequests, -1);
    if (val <= 1) {
      int users_left = ink_atomic_increment((int *) &xstate_machines, -1);
      if (users_left <= 1) {
        // printf("Cachetester finished successfully\n");
        exit(0);
      }
    }
  } else if (cache_bytes_used() >= cache_bytes_total()) {
    // printf("Cachetester finished successfully\n");
    exit(0);
  }
  if (xnum_hosts > 0) {
    int host_id = random_host_prob * xnum_hosts;
    while (true) {
      h = &host_array[host_id];
      if (h->xprev_host_prob > random_host_prob) {
        if (host_id == 0)
          break;
        host_id--;
        continue;
      }
      if (h->xnext_host_prob <= random_host_prob) {
        if (host_id == (xnum_hosts - 1))
          break;
        host_id++;
        continue;
      }
      // matched the host 
      break;
    }

    if (gen->drandom() <= xhttp_req_probability) {
      if (gen->drandom() <= xhotset_probability) {
        test->f.hit = 1;
        id = (unsigned int) (1 + h->xlast_cachable_id * gen->drandom());
      }

      else {
        id = 1 + ink_atomic_increment((volatile ink32 *) &h->xlast_cachable_id, 1);
      }
      id = 1 + (id << 1);
      sprintf(buffer, "http://%s/%s/%u", h->name, cache_test_id, id);
    }
  } else {
    if (gen->drandom() <= xhttp_req_probability) {
      if (gen->drandom() <= xhotset_probability) {
        test->f.hit = 1;
        id = (unsigned int) (1 + xlast_cachable_id * gen->drandom());
      } else {
        id = 1 + ink_atomic_increment((volatile ink32 *) &xlast_cachable_id, 1);
      }
      id = 1 + (id << 1);
      sprintf(buffer, "http://www.foobar.com/%s/%u", cache_test_id, id);
    }
  }

  state = my_seed(id);
  req_size = (int) (-xmean_doc_size * log(my_drandom(&state)));

  if (req_size < 1) {
    req_size = 1;
  } else if (req_size > (1024 * 1024)) {
    req_size = 1024 * 1024;
  }

  return req_size;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::read_success(ink_hrtime elapsed, int bytes)
{
  ink_atomic_increment(&read_successes, 1);
  ink_atomic_increment64(&read_bytes, bytes);
  ink_atomic_increment64(&read_time, ink_hrtime_to_msec(elapsed));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::read_failure()
{
  ink_atomic_increment(&read_failures, 1);
}

void
CacheTestControl::vc_read_failure()
{
  ink_atomic_increment(&vc_read_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::lookup_success(ink_hrtime elapsed)
{
  ink_atomic_increment(&lookup_successes, 1);
  ink_atomic_increment64(&lookup_hit_time, ink_hrtime_to_msec(elapsed));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::lookup_failure(ink_hrtime elapsed)
{
  ink_atomic_increment(&lookup_failures, 1);
  ink_atomic_increment64(&lookup_miss_time, ink_hrtime_to_msec(elapsed));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::write_success(ink_hrtime elapsed, int bytes)
{
  ink_atomic_increment(&write_successes, 1);
  ink_atomic_increment64(&write_bytes, bytes);
  ink_atomic_increment64(&write_time, ink_hrtime_to_msec(elapsed));
}


void
CacheTestControl::aio_success(ink_hrtime elapsed)
{
  ink_atomic_increment(&aio_successes, 1);
  ink_atomic_increment64(&aio_time, ink_hrtime_to_msec(elapsed));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::write_failure()
{
  ink_atomic_increment(&write_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
void
CacheTestControl::vc_write_failure()
{
  ink_atomic_increment(&vc_write_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


void
CacheTestControl::update_success()
{
  ink_atomic_increment(&update_successes, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::update_failure()
{
  ink_atomic_increment(&update_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheTestControl::vc_update_failure()
{
  ink_atomic_increment(&vc_update_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


void
CacheTestControl::pin_success()
{
  ink_atomic_increment(&pin_successes, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


void
CacheTestControl::pin_failure()
{
  ink_atomic_increment(&pin_failures, 1);
}

void
CacheTestControl::rw_failure()
{
  ink_atomic_increment(&rw_failures, 1);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static ink64 last_pread_count = 0;

int
CacheTestControl::print_stats_event(int event, void *edata)
{
  ink_hrtime now;
  double elapsedf;

  now = ink_get_hrtime();
  elapsedf = (double) (now - start_time) / (double) HRTIME_SECOND;

  print_stats();
  last_time = now;

  if ((int) elapsedf >= xrun_length && !xfill_cache)
    xrequests = 0;
  else if (xfill_cache && cache_bytes_used() >= cache_bytes_total())
    xrequests = 0;
  else {
    Event *e = (Event *) edata;
    e->schedule_in(HRTIME_SECONDS(2));
  }
  return EVENT_DONE;
}

int
CacheTestControl::print_stats(void)
{
  double total;
  double inst;
  int rsuccesses;
  int wsuccesses;
  ink_hrtime now;
  double elapsedf;

  now = ink_get_hrtime();
  elapsedf = (double) (now - start_time) / (double) HRTIME_SECOND;

  rsuccesses = read_successes;
  wsuccesses = write_successes;

  printf("[%0.2f] ", elapsedf);
  {
    printf("[%d %d %d] ", rsuccesses, wsuccesses, update_successes);
    printf("[%d %d %d] ", read_failures, write_failures, update_failures);
    printf("[%d %d %d] ", vc_read_failures, vc_write_failures, vc_update_failures);
  }
#ifdef FIXME_DIAGS
  Debug("cache_test_stats", "reads=%d %d writes=%d %d", read_successes, read_failures, write_successes, write_failures);
#endif
  if ((elapsedf >= 1.0) && ((rsuccesses + wsuccesses + aio_successes) >= 1)) {


    // RAM cache hit-rate
    {
      printf("[%0.2f%%] ", 100.0 * rsuccesses / (double) (rsuccesses + wsuccesses));
      ink64 frag_hits;
      ink64 frag_misses;
      double val;
      RecGetGlobalRawStatSum(cache_rsb, cache_ram_cache_hits_stat, &frag_hits);
      RecGetGlobalRawStatSum(cache_rsb, cache_ram_cache_misses_stat, &frag_misses);
      if ((frag_hits + frag_misses) == 0) {
        val = 0.0;
      } else {
        val = (double) (frag_hits) / (double) (frag_hits + frag_misses);
      }

      printf("[%0.2f%%] ", 100.0 * val);
      inst = ((rsuccesses - lread_successes) + (wsuccesses - lwrite_successes)) / (elapsedf - last_elapsed);
      total = (rsuccesses + wsuccesses) / elapsedf;


      if (n_average < xaverage_over)
        n_average += 1;
      avg_ops_sec = ((n_average - 1) * avg_ops_sec + inst) / n_average;
      printf("[%0.2f %0.2f ops/sec] ", avg_ops_sec, total);

      printf("[%0.2f MB/sec] ", (double) (read_bytes + write_bytes) / (1024.0 * 1024.0 * elapsedf));
      printf("[%0.2f write MB] ", (double) write_bytes / (1024.0 * 1024.0));
      printf("[%0.2f ms/req] ", (double) (read_time + write_time) / (double) (rsuccesses + wsuccesses));
      printf("[%0.2f ms/read req] ", (double) (read_time) / (double) (rsuccesses ? rsuccesses : 1));
      printf("[%0.2f ms/write req] ", (double) (write_time) / (double) (wsuccesses ? wsuccesses : 1));
      lread_successes = rsuccesses;
      lwrite_successes = wsuccesses;
      last_elapsed = elapsedf;
    }
    {
      ink64 cur_pread_count, cur_pread_sum;
      RecGetGlobalRawStatSum(cache_rsb, cache_pread_count_stat, &cur_pread_sum);
      cur_pread_count = cur_pread_sum;
      double diff = (double) (ink_hrtime_to_sec(now - last_time));

      if (diff > 0 && (cur_pread_count - last_pread_count)) {
        printf("[%0.2f pread calls/sec] ", (double) (cur_pread_count - last_pread_count) / (double) diff);

        last_pread_count = cur_pread_count;
      }
    }
    if (xpin_percent)
      printf("[pin %d %d %d]", pin_writes, pin_successes, pin_failures);
  }

  printf("\n");
  return 0;
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/


CacheSMTest::CacheSMTest(CacheTestControl * xcontrol, int xid)
:Continuation(new_ProxyMutex()),
timeout(NULL),
cache_action(NULL), control(xcontrol), cache_vc(NULL), buffer(NULL), buffer_reader(NULL), id(xid), m_timeout(NULL)
{
  SET_HANDLER(&CacheSMTest::event_handler);
#ifdef HTTP_CACHE
  params.cache_enable_default_vary_headers = true;
  params.cache_vary_default_text = "User-Agent";
  params.cache_vary_default_images = "User-Agent";
  params.cache_vary_default_other = "User-Agent";
#endif
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

CacheSMTest::~CacheSMTest()
{
  if (buffer_reader)
    buffer->dealloc_reader(buffer_reader);
  if (buffer)
    free_MIOBuffer(buffer);

}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheSMTest::make_request()
{
  ink_assert(cache_vc == NULL);
  InkRand *gen = &this_ethread()->generator;
#ifdef HTTP_CACHE
  HTTPHdr *hdr;
  MIMEField *field;
  URL url;
#endif
  Action *action;
  const char *t;
  static int aio_ifd = 0;
  ink_assert(cache_vc == NULL);

  if (CacheProcessor::IsCacheEnabled() != CACHE_INITIALIZED) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(100), ET_CALL);
    return;
  }

  total_size = control->get_request(this, urlstr, 1024);
  if (total_size < 0) {
    delete this;
    return;
  }
  done_size = 0;
  request_size = total_size;
  md5.encodeBuffer(urlstr, strlen(urlstr));
  idx = -((int) strlen(urlstr));

  if (buffer) {
    free_MIOBuffer(buffer);
  }
  buffer = new_empty_MIOBuffer(cache_buffer_size_index);
  buffer_reader = buffer->alloc_reader();
#ifdef HTTP_CACHE
  if (*urlstr == 'h') {
    f.http_request = 1;
    if (info.valid()) {
      info.destroy();
    }
    info.create();
    info.request_sent_time_set(1);
    info.response_received_time_set(1);

    hdr = &info.m_alt->m_request_hdr;
    hdr->create(HTTP_TYPE_REQUEST);
    hdr->version_set(HTTPVersion(1, 0));
    hdr->method_set(HTTP_METHOD_GET, strlen(HTTP_METHOD_GET));

    hdr->url_create(&url);
    t = urlstr;
    url.parse(&t, t + strlen(t));
    hdr->url_set(&url);

    field = hdr->field_create(MIME_FIELD_USER_AGENT, MIME_LEN_USER_AGENT);
    field->value_set_int(hdr->m_heap, hdr->m_mime, (int) (gen->drandom() * control->xnum_alternates));
    hdr->field_attach(field);

    hdr = &info.m_alt->m_response_hdr;
    hdr->create(HTTP_TYPE_RESPONSE);
    hdr->status_set(HTTP_STATUS_OK);
    hdr->reason_set("OK", 2);

    hdr = &info.m_alt->m_request_hdr;
    hdr->url_get(&url);
    start_time = ink_get_hrtime();
    action = cacheProcessor.open_read(this, &url, hdr, &params);
#endif
  }

  if ((action != ACTION_RESULT_DONE) && (gen->drandom() <= control->xcancel_probability)) {
    cache_action = action;
    timeout = eventProcessor.schedule_in(this, HRTIME_MSECONDS((int) (gen->drandom() * 50)), ET_CALL);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheSMTest::fill_buffer()
{
  int size;

  size = buffer->write_avail();
  if (control->xcheck_content) {
    char *p;
    int i;
    int buf_ndx = 0;
    int to_copy = 0;
    int towrite = size;
    while (towrite > 0) {
      int l = buffer->block_write_avail();
      p = buffer->end();
      for (i = 0; i < l; i += to_copy) {
        buf_ndx = (idx + i) % ('z' - 'a');
        to_copy = (l - i);
        if ((buf_ndx + to_copy) > CHECK_BUF_SIZE)
          to_copy = CHECK_BUF_SIZE - buf_ndx;
//              p[i] = ((idx + i) % ('z' - 'a')) + 'a';
        memcpy(&p[i], &check_buf[buf_ndx], to_copy);
      }
      idx += l;
      buffer->fill(l);
      towrite -= l;
    }
  } else {
    memset(buffer->end(), 'a', (size > 128) ? 128 : size);
    buffer->fill(size);
  }

}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
CacheSMTest::check_buffer()
{
  buffer->append_block(new_IOBufferBlock(cache_test_data, cache_test_data->block_size(), 0));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
CacheSMTest::event_handler(int event, void *edata)
{
  InkRand *gen = &this_ethread()->generator;
  Action *action;
  double prob;
  bool hard_error;

  switch (event) {
  case EVENT_IMMEDIATE:
    make_request();
    return EVENT_DONE;
  case EVENT_INTERVAL:
    timeout = NULL;
    if (cache_action) {
      cache_action->cancel();
      cache_action = NULL;
    }
    if (cache_vc) {
      cache_vc->do_io_close();
      cache_vc = NULL;
    }
    make_request();
    return EVENT_DONE;

  case CACHE_EVENT_LOOKUP:
    control->lookup_success(ink_get_hrtime() - start_time);
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;

  case CACHE_EVENT_LOOKUP_FAILED:
    control->lookup_failure(ink_get_hrtime() - start_time);
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;

  case CACHE_EVENT_OPEN_READ:
  case CACHE_EVENT_OPEN_READ_VIO:{
      ink_assert(cache_vc == NULL);
      bool is_read_vio = (event == CACHE_EVENT_OPEN_READ_VIO);

      cache_action = NULL;
      if (timeout) {
        timeout->cancel();
        timeout = NULL;
      }

      if (is_read_vio) {
        cache_vc = (CacheVConnection *) ((VIO *) edata)->vc_server;
      } else {
        cache_vc = (CacheVConnection *) edata;
      }

      prob = gen->drandom();
      if (prob < control->xpin_percent) {
        long state = my_seed(ink_get_based_hrtime());
        pin_in_cache = (int) (-control->xmean_pin_time * log(my_drandom(&state)));
      }
      if (pin_in_cache && prob <= (control->xremove_probability + control->xupdate_probability)) {
        // check if the document was pinned...
        // take lock for the hash table
        ink_time_t now = ink_get_based_hrtime() / HRTIME_SECOND;
        PinnedDocTable *pinned_doc_table = control->getPinnedTable(&md5);
        MUTEX_TRY_LOCK(lock, pinned_doc_table->mutex, mutex->thread_holding);
        if (!lock)
          // if we can't update the pin table, then
          // we should not do an update
          prob = 1.0;
        else {
          if (prob < control->xupdate_probability)
            pinned_doc_table->insert(&md5, pin_in_cache, true);
          else
            pinned_doc_table->remove(&md5);
        }
      }
#ifdef HTTP_CACHE
      if (prob <= (control->xremove_probability + control->xupdate_probability)) {
        CacheHTTPInfo *alternate;
        f.update = 1;
        if (f.http_request) {
          if (prob >= (control->xupdate_probability))
            f.remove = 1;
          if (!cache_vc->get_data(CACHE_DATA_HTTP_INFO, &alternate)) {
            ink_assert(false);
          }
          HTTPHdr h;
          URL u;
          info.request_get(&h);
          h.url_get(&u);
          action = cacheProcessor.open_write(this, 0, &u, &h, alternate, pin_in_cache);
        }
        if ((action != ACTION_RESULT_DONE) && (gen->drandom() <= control->xcancel_probability)) {
          cache_action = action;
          timeout = eventProcessor.schedule_in(this, HRTIME_MSECONDS((int) (gen->drandom() * 50)), ET_CALL);
        }
        return EVENT_DONE;
      }
#endif
      prob -= (control->xremove_probability + control->xupdate_probability);

      if (!cache_vc->get_data(CACHE_DATA_SIZE, &total_size)) {
        ink_assert(false);
      }
      done_size = 0;
      request_size = total_size;

      if (event == CACHE_EVENT_OPEN_READ) {
        cache_vc->do_io_read(this, request_size, buffer);
      }
      return EVENT_DONE;
    }
  case CACHE_EVENT_OPEN_READ_FAILED:
  case CACHE_EVENT_OPEN_READ_FAILED_IN_PROGRESS:{
      ink_assert(cache_vc == NULL);

      cache_action = NULL;
      if (timeout) {
        timeout->cancel();
        timeout = NULL;
      }

      control->read_failure();
      f.writing = true;
      if (control->xpin_percent && event == -(ECACHE_NO_DOC)) {
        // check if the document was pinned...
        // take lock for the hash table
        PinnedDocTable *pinned_doc_table = control->getPinnedTable(&md5);
        MUTEX_TRY_LOCK(lock, pinned_doc_table->mutex, mutex->thread_holding);
        if (lock) {
          if (pinned_doc_table->probe(&md5)) {
            control->pin_failure();
          }
        }
      }
      if (control->xpin_percent) {
        if (f.hit && gen->drandom() < control->xpin_percent) {
          long state = my_seed(ink_get_based_hrtime());
          pin_in_cache = (int) (-control->xmean_pin_time * log(my_drandom(&state)));
        }
      }
#ifdef HTTP_CACHE
      if (f.http_request) {

        HTTPHdr h;
        URL u;
        info.request_get(&h);
        h.url_get(&u);
        action = cacheProcessor.open_write(this, total_size, &u, &h, NULL, pin_in_cache);
      }
#endif
      if ((action != ACTION_RESULT_DONE) && (gen->drandom() <= control->xcancel_probability)) {
        cache_action = action;
        timeout = eventProcessor.schedule_in(this, HRTIME_MSECONDS((int) (gen->drandom() * 50)), ET_CALL);
        return EVENT_DONE;
      }
      return EVENT_DONE;
    }
  case VC_EVENT_READ_READY:
    ink_assert(cache_vc != NULL);
    ink_assert(cache_vc == ((VIO *) edata)->vc_server);

    if (gen->drandom() <= control->xabort_probability) {
      // control->read_failure ();
      cache_vc->do_io_close(-2);
      cache_vc = NULL;
      eventProcessor.schedule_imm(this, ET_CALL);
      return EVENT_DONE;
    } else {
      if (!check_content(this, buffer_reader)) {
        control->vc_read_failure();
        cache_vc->do_io_close(-2);
        cache_vc = NULL;
        eventProcessor.schedule_imm(this, ET_CALL);
        return EVENT_DONE;
      }
      cache_vc->reenable((VIO *) edata);
      return EVENT_CONT;
    }

  case VC_EVENT_READ_COMPLETE:
    ink_assert(cache_vc != NULL);
    ink_assert(cache_vc == ((VIO *) edata)->vc_server);

    done_size += request_size;

    if (done_size < total_size) {
      ink_assert(0);
      if ((total_size - done_size) < request_size) {
        request_size = total_size - done_size;
      }

      cache_vc->do_io_read(this, request_size, buffer);
      return EVENT_CONT;
    } else {
      if (!check_content(this, buffer_reader))
        control->vc_read_failure();
      else {
        if (control->xpin_percent) {
          // check if the document was pinned...
          // take lock for the hash table
          PinnedDocTable *pinned_doc_table;
          pinned_doc_table = control->getPinnedTable(&md5);
          MUTEX_TRY_LOCK(lock, pinned_doc_table->mutex, mutex->thread_holding);
          if (lock && pinned_doc_table->probe(&md5))
            control->pin_success();

        }
        control->read_success(ink_get_hrtime() - start_time, done_size);
        cache_vc->do_io_close();
        cache_vc = NULL;

        eventProcessor.schedule_imm(this, ET_CALL);
        return EVENT_DONE;
      }
    }
  case CACHE_EVENT_OPEN_WRITE:
    cache_action = NULL;
    if (timeout) {
      timeout->cancel();
      timeout = NULL;
    }

    if (cache_vc) {
      CacheVConnection *write_vc = (CacheVConnection *) edata;
#ifdef HTTP_CACHE
      if (f.http_request) {
        if (!f.remove) {
          CacheHTTPInfo alternate;
          CacheHTTPInfo *old_alt;

          if (!cache_vc->get_data(CACHE_DATA_HTTP_INFO, &old_alt)) {
            ink_assert(false);
          }
          alternate.copy(old_alt);

          write_vc->set_http_info(&alternate);
          alternate.clear();
          prob = gen->drandom();
          if (prob >= control->xpure_update_probability) {
            idx = -((int) strlen(urlstr));
            fill_buffer();
            cache_vc->do_io_close();
            cache_vc = write_vc;
            cache_vc->do_io_write(this, request_size, buffer_reader);
            return EVENT_DONE;
          }
        }
        // remove or update the header only
        control->update_success();
        write_vc->do_io_close();
        cache_vc->do_io_close();
        cache_vc = NULL;
        eventProcessor.schedule_imm(this, ET_CALL);
      } else
#endif
      {
        idx = -((int) strlen(urlstr));
        fill_buffer();
        cache_vc->do_io_close();
        cache_vc = (CacheVConnection *) edata;
        cache_vc->do_io_write(this, request_size, buffer_reader);
      }
    } else {
      idx = -((int) strlen(urlstr));
      fill_buffer();

      cache_vc = (CacheVConnection *) edata;
#ifdef HTTP_CACHE
      if (f.http_request)
        cache_vc->set_http_info(&info);
#endif
      cache_vc->do_io_write(this, request_size, buffer_reader);
#ifdef HTTP_CACHE
      info.clear();
#endif
    }
    return EVENT_DONE;

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    cache_action = NULL;
    if (timeout) {
      timeout->cancel();
      timeout = NULL;
    }
    if (cache_vc) {
      ink_assert(f.update);
      control->update_failure();
      cache_vc->do_io_close();
      cache_vc = NULL;
      eventProcessor.schedule_imm(this, ET_CALL);
    } else {
      control->write_failure();
      eventProcessor.schedule_imm(this, ET_CALL);
    }
    return EVENT_DONE;
  case AIO_EVENT_DONE:
    control->aio_success(ink_get_hrtime() - start_time);
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;
  case VC_EVENT_WRITE_READY:
    ink_assert(cache_vc != NULL);
    ink_assert(cache_vc == ((VIO *) edata)->vc_server);

    if (gen->drandom() <= control->xabort_probability) {
      // control->write_failure ();
      cache_vc->do_io_close(-2);
      cache_vc = NULL;
      eventProcessor.schedule_imm(this, ET_CALL);
      return EVENT_DONE;
    } else {
      fill_buffer();
      cache_vc->reenable((VIO *) edata);
      return EVENT_CONT;
    }

  case VC_EVENT_WRITE_COMPLETE:
    ink_assert(cache_vc != NULL);
    //ink_assert (cache_vc == (VIO *) edata->vc_server);

    done_size += request_size;
    if (done_size < total_size) {
      if ((total_size - done_size) < request_size) {
        request_size = total_size - done_size;
      }

      cache_vc->do_io_write(this, request_size, buffer_reader);
      return EVENT_CONT;
    } else {
      if (f.update)
        control->update_success();
      else
        control->write_success(ink_get_hrtime() - start_time, done_size);

      cache_vc->do_io_close();
      // insert in the pinned doc table

      if (pin_in_cache && !f.update) {
        // check if the document was pinned...
        // take lock for the hash table
        PinnedDocTable *pinned_doc_table = control->getPinnedTable(&md5);
        MUTEX_TRY_LOCK(lock, pinned_doc_table->mutex, mutex->thread_holding);
        if (lock)
          pinned_doc_table->insert(&md5, pin_in_cache, 0);
        ink_atomic_increment((int *) &control->pin_writes, 1);
      }

      cache_vc = NULL;

      eventProcessor.schedule_imm(this, ET_CALL);
      return EVENT_DONE;
    }

  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    ink_assert(cache_vc != NULL);
    hard_error = !((CacheVC *) cache_vc)->io.ok();
    cache_vc->do_io_close(-2);
    cache_vc = NULL;

    if (!hard_error) {
      if (f.writing) {
        if (f.update)
          control->vc_update_failure();
        else
          control->vc_write_failure();
      } else {
        if (pin_in_cache) {
          // check if the document was pinned...
          // take lock for the hash table
          PinnedDocTable *pinned_doc_table;
          pinned_doc_table = control->getPinnedTable(&md5);
          MUTEX_TRY_LOCK(lock, pinned_doc_table->mutex, mutex->thread_holding);
          if (lock && pinned_doc_table->probe(&md5))
            control->pin_failure();

        }

        control->vc_read_failure();
      }
    } else {
      printf("IO failure\n");
    }

    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;

  case CACHE_EVENT_REMOVE:
    control->update_success();
    cache_vc = NULL;
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;

  case CACHE_EVENT_REMOVE_FAILED:
    control->update_failure();
    cache_vc = NULL;
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;

  default:
    ink_assert(0);
    eventProcessor.schedule_imm(this, ET_CALL);
    return EVENT_DONE;
  }
}

#ifdef HTTP_CACHE
/*
 * create an internal directory for 
 * the header system
 */
static void
init_http_header()
{

  char internal_config_dir[PATH_NAME_MAX];

  sprintf(internal_config_dir, "./internal");

  url_init(internal_config_dir);
  mime_init(internal_config_dir);
  http_init(internal_config_dir);
}
#endif
extern int ink_aio_start();
int
main(int argc, char *argv[])
{
  int i;
  int num_net_threads = ink_number_of_processors();
  init_diags("", NULL);
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  ink_aio_init(AIO_MODULE_VERSION);
  ink_cache_init(CACHE_MODULE_VERSION);
  eventProcessor.start(num_net_threads);
  RecProcessStart();
  ink_aio_start();
  cacheProcessor.start();
#ifdef HTTP_CACHE
  init_http_header();
#endif
  runCacheTest();
  this_thread()->execute();
}
