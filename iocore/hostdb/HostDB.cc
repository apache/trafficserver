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

#define _HOSTDB_CC_

#include "P_HostDB.h"
#include "I_Layout.h"

#ifndef NON_MODULAR
//char system_config_directory[512] = "etc/trafficserver";
#else
#include "Show.h"
#endif

// dxu: turn off all Diags.h 's function.
//#define Debug
//#define Warning
//#define Note

//
// Compilation Options
//
#define USE_MMH

#include "ink_apidefs.h"

HostDBProcessor hostDBProcessor;
int HostDBProcessor::hostdb_strict_round_robin = 0;
int hostdb_enable = true;
int hostdb_migrate_on_demand = true;
int hostdb_cluster = false;
int hostdb_cluster_round_robin = false;
int hostdb_lookup_timeout = 120;
int hostdb_insert_timeout = 160;
int hostdb_re_dns_on_reload = false;
int hostdb_ttl_mode = TTL_OBEY;
unsigned int hostdb_current_interval = 0;
unsigned int hostdb_ip_stale_interval = HOST_DB_IP_STALE;
unsigned int hostdb_ip_timeout_interval = HOST_DB_IP_TIMEOUT;
unsigned int hostdb_ip_fail_timeout_interval = HOST_DB_IP_FAIL_TIMEOUT;
unsigned int hostdb_serve_stale_but_revalidate = 0;
char hostdb_filename[PATH_NAME_MAX + 1] = DEFAULT_HOST_DB_FILENAME;
int hostdb_size = DEFAULT_HOST_DB_SIZE;
//int hostdb_timestamp = 0;
int hostdb_sync_frequency = 60;
int hostdb_disable_reverse_lookup = 0;

ClassAllocator<HostDBContinuation> hostDBContAllocator("hostDBContAllocator");

// Static configuration information

HostDBCache
  hostDB;

#ifdef NON_MODULAR
static  Queue <HostDBContinuation > remoteHostDBQueue[MULTI_CACHE_PARTITIONS];
#endif

static inline int
corrupt_debugging_callout(HostDBInfo * e, RebuildMC & r)
{
  Debug("hostdb", "corrupt %d part %d", (char *) &e->app.rr.offset - r.data, r.partition);
  return -1;
}


inline void
hostdb_cont_free(HostDBContinuation * cont)
{
  if (cont->pending_action)
    cont->pending_action->cancel();
  cont->mutex = 0;
  cont->action.mutex = 0;
  hostDBContAllocator.free(cont);
}


//
// Function Prototypes
//
#ifdef NON_MODULAR
static Action *
register_ShowHostDB(Continuation * c, HTTPHdr * h);
#endif


HostDBCache::HostDBCache()
{
  tag_bits = HOST_DB_TAG_BITS;
  max_hits = (1 << HOST_DB_HITS_BITS) - 1;
  version.ink_major = HOST_DB_CACHE_MAJOR_VERSION;
  version.ink_minor = HOST_DB_CACHE_MINOR_VERSION;
}


int
HostDBCache::rebuild_callout(HostDBInfo * e, RebuildMC & r)
{
  if (e->round_robin && e->reverse_dns)
    return corrupt_debugging_callout(e, r);
  if (e->reverse_dns) {
    if (e->data.hostname_offset < 0)
      return 0;
    if (e->data.hostname_offset > 0) {
      if (!valid_offset(e->data.hostname_offset - 1))
        return corrupt_debugging_callout(e, r);
      char *p = (char *) ptr(&e->data.hostname_offset, r.partition);
      if (!p)
        return corrupt_debugging_callout(e, r);
      char *s = p;
      while (*p && p - s < MAXDNAME) {
        if (!valid_heap_pointer(p))
          return corrupt_debugging_callout(e, r);
        p++;
      }
      if (p - s >= MAXDNAME)
        return corrupt_debugging_callout(e, r);
    }
  }
  if (e->round_robin) {
    if (e->app.rr.offset < 0)
      return 0;
    if (!valid_offset(e->app.rr.offset - 1))
      return corrupt_debugging_callout(e, r);
    HostDBRoundRobin *rr = (HostDBRoundRobin *) ptr(&e->app.rr.offset, r.partition);
    if (!rr)
      return corrupt_debugging_callout(e, r);
    if (rr->n > HOST_DB_MAX_ROUND_ROBIN_INFO || rr->n <= 0 ||
        rr->good > HOST_DB_MAX_ROUND_ROBIN_INFO || rr->good <= 0 || rr->good > rr->n)
      return corrupt_debugging_callout(e, r);
    for (int i = 0; i < rr->good; i++) {
      if (!valid_heap_pointer(((char *) &rr->info[i + 1]) - 1))
        return -1;
      if (!rr->info[i].ip())
        return corrupt_debugging_callout(e, r);
      if (rr->info[i].md5_high != e->md5_high ||
          rr->info[i].md5_low != e->md5_low || rr->info[i].md5_low_low != e->md5_low_low)
        return corrupt_debugging_callout(e, r);
    }
  }
  if (e->is_ip_timeout())
    return 0;
  return 1;
}


HostDBCache *
HostDBProcessor::cache()
{
  return &hostDB;
}


struct HostDBTestRR: public Continuation
{
  int fd;
  char b[512];
  int nb;
  int outstanding, success, failure;
  int in;

  int mainEvent(int event, Event * e)
  {
    if (event == EVENT_INTERVAL) {
      printf("HostDBTestRR: %d outstanding %d succcess %d failure\n", outstanding, success, failure);
    }
    if (event == EVENT_HOST_DB_LOOKUP) {
      outstanding--;
      if (e)
        success++;
      else
        failure++;
    }
    if (in)
      return EVENT_CONT;
    in = 1;
    while (outstanding < 40) {
      if (!nb)
        goto Lreturn;
      char *end = (char *) ink_memchr(b, '\n', nb);
      if (!end)
        read_some();
      end = (char *) ink_memchr(b, '\n', nb);
      if (!end)
        nb = 0;
      else {
        *end = 0;
        outstanding++;
        hostDBProcessor.getbyname_re(this, b);
        nb -= ((end + 1) - b);
        memcpy(b, end + 1, nb);
        if (!nb)
          read_some();
      }
    }
  Lreturn:
    in = 0;
    return EVENT_CONT;
  }


  void read_some()
  {
    nb = read(fd, b + nb, 512 - nb);
    ink_release_assert(nb >= 0);
  }


HostDBTestRR():Continuation(new_ProxyMutex()), nb(0), outstanding(0), success(0), failure(0), in(0) {
    printf("starting HostDBTestRR....\n");
    fd = open("hostdb_test.config", O_RDONLY, 0);
    ink_release_assert(fd >= 0);
    read_some();
    SET_HANDLER(&HostDBTestRR::mainEvent);
  }
};


struct HostDBSyncer: public Continuation
{
  int frequency;
  ink_hrtime start_time;

  int sync_event(int event, void *edata);
  int wait_event(int event, void *edata);

    HostDBSyncer();
};


HostDBSyncer::HostDBSyncer():
Continuation(new_ProxyMutex()), frequency(0), start_time(0)
{
  SET_HANDLER(&HostDBSyncer::sync_event);
  IOCORE_EstablishStaticConfigInt32(hostdb_sync_frequency, "proxy.config.cache.hostdb.sync_frequency");
}


int
HostDBSyncer::sync_event(int, void *)
{
  SET_HANDLER(&HostDBSyncer::wait_event);
  start_time = ink_get_hrtime();
  hostDBProcessor.cache()->sync_partitions(this);
  return EVENT_DONE;
}


int
HostDBSyncer::wait_event(int, void *)
{
  SET_HANDLER(&HostDBSyncer::sync_event);
  mutex->thread_holding->schedule_in_local(this, HRTIME_SECONDS(hostdb_sync_frequency));
  return EVENT_DONE;
}


int
HostDBCache::start(int flags)
{
  Store *hostDBStore;
  Span *hostDBSpan;
  char storage_path[PATH_NAME_MAX + 1];
  int storage_size = 0;

  bool reconfigure = ((flags & PROCESSOR_RECONFIGURE) ? true : false);
  bool fix = ((flags & PROCESSOR_FIX) ? true : false);

  // Read configuration
  // Command line overrides manager configuration.
  //
  IOCORE_ReadConfigInt32(hostdb_enable, "proxy.config.hostdb");
  IOCORE_ReadConfigString(hostdb_filename, "proxy.config.hostdb.filename", PATH_NAME_MAX);
  IOCORE_ReadConfigInt32(hostdb_size, "proxy.config.hostdb.size");

#if defined (_IOCORE_WIN32)
  // since the config directory is always based of TSBase, we make sure
  // all the internal paths correctly get set.
  char szPath[PATH_NAME_MAX + 1];
  IOCORE_ReadConfigString(szPath, "proxy.config.hostdb.storage_path", PATH_NAME_MAX);
  int i = 0;
  while (szPath[i] != 0) {
    if (szPath[i] == '/')
      szPath[i] = '\\';
    i++;
  }
  ink_strncpy(storage_path, system_root_dir, sizeof(storage_path));
  strcat(storage_path, DIR_SEP);
  strcat(storage_path, szPath);
#else
  IOCORE_ReadConfigString(storage_path, "proxy.config.hostdb.storage_path", PATH_NAME_MAX);
#endif
  IOCORE_ReadConfigInt32(storage_size, "proxy.config.hostdb.storage_size");

  if (storage_path[0] != '/') {
    Layout::relative_to(storage_path, PATH_NAME_MAX,
                        system_root_dir, storage_path);
  }

  Debug("hostdb", "Storage path is %s", storage_path);

  // XXX: Should this be W_OK?
  if (access(storage_path, R_OK) == -1) {
    ink_strncpy(storage_path, system_runtime_dir, sizeof(storage_path));
    if (access(storage_path, R_OK) == -1) {
      Warning("Unable to access() directory '%s': %d, %s", storage_path, errno, strerror(errno));
      Warning(" Please set 'proxy.config.hostdb.storage_path' or 'proxy.config.local_state_dir' ");
    }
  }
  hostDBStore = NEW(new Store);
  hostDBSpan = NEW(new Span);
  hostDBSpan->init(storage_path, storage_size);
  hostDBStore->add(hostDBSpan);

  Debug("hostdb", "Opening %s, size=%d", hostdb_filename, hostdb_size);
  if (open(hostDBStore, "hostdb.config", hostdb_filename, hostdb_size, reconfigure, fix, false /* slient */ ) < 0) {
    Note("reconfiguring host database");

    char p[PATH_NAME_MAX + 1];
    Layout::relative_to(p, PATH_NAME_MAX,
                        system_config_directory, "internal/hostdb.config");
    if (unlink(p) < 0)
      Debug("hostdb", "unable to unlink %s", p);

    delete hostDBStore;
    hostDBStore = NEW(new Store);
    hostDBSpan = NEW(new Span);
    hostDBSpan->init(storage_path, storage_size);
    hostDBStore->add(hostDBSpan);

    if (open(hostDBStore, "hostdb.config", hostdb_filename, hostdb_size, true, fix) < 0) {
      Warning("could not initialize host database. Host database will be disabled");
      hostdb_enable = 0;
      delete hostDBStore;
      return -1;
    }
  }
  HOSTDB_SET_DYN_COUNT(hostdb_bytes_stat, totalsize);
  //  XXX I don't see this being reference in the previous function calls, so I am going to delete it -bcall
  delete hostDBStore;
  return 0;
}


// Start up the Host Database processor.
// Load configuration, register configuration and statistics and
// open the cache.
//
int
HostDBProcessor::start(int)
{
  //bool found = false;
  hostDB.alloc_mutexes();

  if (hostDB.start(0) < 0)
    return -1;

#ifdef NON_MODULAR
  if (auto_clear_hostdb_flag)
    hostDB.clear();
#endif

  HOSTDB_SET_DYN_COUNT(hostdb_total_entries_stat, hostDB.totalelements);

#ifdef NON_MODULAR
  statPagesManager.register_http("hostdb", register_ShowHostDB);
#endif

  //
  // Register configuration callback, and establish configuation links
  //
  IOCORE_EstablishStaticConfigInt32(hostdb_ttl_mode, "proxy.config.hostdb.ttl_mode");
  IOCORE_EstablishStaticConfigInt32(hostdb_disable_reverse_lookup, "proxy.config.hostdb.disable_reverse_lookup");
  IOCORE_EstablishStaticConfigInt32(hostdb_re_dns_on_reload, "proxy.config.hostdb.re_dns_on_reload");
  IOCORE_EstablishStaticConfigInt32(hostdb_migrate_on_demand, "proxy.config.hostdb.migrate_on_demand");
  IOCORE_EstablishStaticConfigInt32(hostdb_strict_round_robin, "proxy.config.hostdb.strict_round_robin");
  IOCORE_EstablishStaticConfigInt32(hostdb_cluster, "proxy.config.hostdb.cluster");
  IOCORE_EstablishStaticConfigInt32(hostdb_cluster_round_robin, "proxy.config.hostdb.cluster.round_robin");
  IOCORE_EstablishStaticConfigInt32(hostdb_lookup_timeout, "proxy.config.hostdb.lookup_timeout");
  IOCORE_EstablishStaticConfigInt32U(hostdb_ip_timeout_interval, "proxy.config.hostdb.timeout");
  IOCORE_EstablishStaticConfigInt32U(hostdb_ip_stale_interval, "proxy.config.hostdb.verify_after");
  IOCORE_EstablishStaticConfigInt32U(hostdb_ip_fail_timeout_interval, "proxy.config.hostdb.fail.timeout");
  IOCORE_EstablishStaticConfigInt32U(hostdb_serve_stale_but_revalidate, "proxy.config.hostdb.serve_stale_for");

  //
  // Set up hostdb_current_interval
  //
  hostdb_current_interval = (unsigned int)
    (ink_get_based_hrtime() / HOST_DB_TIMEOUT_INTERVAL);
  //hostdb_timestamp = time(NULL);

  HostDBContinuation *b = hostDBContAllocator.alloc();
  SET_CONTINUATION_HANDLER(b, (HostDBContHandler) & HostDBContinuation::backgroundEvent);
  b->mutex = new_ProxyMutex();
  eventProcessor.schedule_every(b, HOST_DB_TIMEOUT_INTERVAL, ET_DNS);

  //
  // Sync HostDB
  //
  eventProcessor.schedule_imm(NEW(new HostDBSyncer));
  return 0;
}


void
HostDBContinuation::init(char *hostname, int len,
                         int aip, int aport, INK_MD5 & amd5, Continuation * cont, void *pDS, bool is_srv, int timeout)
{
  if (hostname) {
    memcpy(name, hostname, len);
    name[len] = 0;
  } else
    name[0] = 0;
  dns_lookup_timeout = timeout;
  namelen = len;
  is_srv_lookup = is_srv;
  ip = aip;
  port = aport;
  md5 = amd5;
  mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
  m_pDS = pDS;
  if (cont) {
    action = cont;
  } else {
    //ink_assert(!"this sucks");
    action.mutex = mutex;
  }
}


void
make_md5(INK_MD5 & md5, char *hostname, int len, int port, char *pDNSServers, int srv)
{
#ifdef USE_MMH
  MMH_CTX ctx;
  ink_code_incr_MMH_init(&ctx);
  ink_code_incr_MMH_update(&ctx, hostname, len);
  unsigned short p = port;
  p = htons(p);
  ink_code_incr_MMH_update(&ctx, (char *) &p, 2);
  ink_code_incr_MMH_update(&ctx, (char *) &srv, 4);     /* FIXME: check this */
  if (pDNSServers)
    ink_code_incr_MMH_update(&ctx, pDNSServers, strlen(pDNSServers));
  ink_code_incr_MMH_final((char *) &md5, &ctx);
#else
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  ink_code_incr_md5_update(&ctx, hostname, len);
  unsigned short p = port;
  p = htons(p);
  ink_code_incr_md5_update(&ctx, (char *) &p, 2);
  ink_code_incr_MMH_update(&ctx, (char *) &srv, 4);     /* FIXME: check this */
  if (pDNSServers)
    ink_code_incr_md5_update(&ctx, pDNSServers, strlen(pDNSServers));
  ink_code_incr_md5_final((char *) &md5, &ctx);
#endif
}


static bool
reply_to_cont(Continuation * cont, HostDBInfo * ar)
{
  const char *reason = "none";
  HostDBInfo *r = ar;

  if (r == NULL) {
    cont->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
    return false;
  }

  if (r->failed()) {
    if (r->is_srv && r->srv_count) {
      cont->handleEvent(EVENT_SRV_LOOKUP, NULL);
      return false;
    }
    cont->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
    return false;
  } else {
    if (r->reverse_dns) {
      if (!r->hostname()) {
        reason = "missing hostname";
        ink_assert(!"missing hostname");
        goto Lerror;
      }
      Debug("hostdb", "hostname = %s", r->hostname());
    }
    if (r->round_robin) {
      if (!r->rr()) {
        reason = "missing round-robin";
        ink_assert(!"missing round-robin");
        goto Lerror;
      }
      Debug("hostdb", "RR of %d with %d good, 1st IP = %X", r->rr()->n, r->rr()->good, r->ip());
    }
    if (r->is_srv && r->srv_count) {
      cont->handleEvent(EVENT_SRV_LOOKUP, r);
      if (!r->full)
        goto Ldelete;
      return true;
    } else if (r->is_srv) {
      /* failure case where this is an SRV lookup, but we got no records back  -- this is handled properly in process_srv_info */
      cont->handleEvent(EVENT_SRV_LOOKUP, r);
      return true;
    }
    cont->handleEvent(EVENT_HOST_DB_LOOKUP, r);
    if (!r->full)
      goto Ldelete;
    return true;
  }
Lerror:
  if (r->is_srv && r->srv_count) {
    cont->handleEvent(EVENT_SRV_LOOKUP, r);
  }
  cont->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
Ldelete:
  Warning("bogus entry deleted from HostDB: %s", reason);
  hostDB.delete_block(ar);
  return false;
}


HostDBInfo *
probe(ProxyMutex *mutex, INK_MD5 & md5, char *hostname, int len, int ip, int port, void *pDS, bool ignore_timeout,
      bool is_srv_lookup)
{
  ink_debug_assert(this_ethread() == hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets))->thread_holding);
  if (hostdb_enable) {
    uint64_t folded_md5 = fold_md5(md5);
    HostDBInfo *r = hostDB.lookup_block(folded_md5, hostDB.levels);
    Debug("hostdb", "probe %s %llX %d [ignore_timeout = %d]", hostname, folded_md5, !!r, ignore_timeout);
    if (r && md5[1] == r->md5_high) {

      // Check for timeout (fail probe)
      //
      if (r->is_deleted()) {
        Debug("hostdb", "HostDB entry was set as deleted");
        return NULL;
      } else if (r->failed()) {
        Debug("hostdb", "%s failed", hostname);
        if (r->is_ip_fail_timeout()) {
          Debug("hostdb", "fail timeout %u", r->ip_interval());
          return NULL;
        }
      } else if (!ignore_timeout && r->is_ip_timeout() && !r->serve_stale_but_revalidate()) {
        Debug("hostdb", "timeout %u %u %u", r->ip_interval(), r->ip_timestamp, r->ip_timeout_interval);
        HOSTDB_INCREMENT_DYN_STAT(hostdb_ttl_expires_stat);
        return NULL;
      }
//error conditions
      if (r->reverse_dns && !r->hostname()) {
        Debug("hostdb", "missing reverse dns");
        hostDB.delete_block(r);
        return NULL;
      }
      if (r->round_robin && !r->rr()) {
        Debug("hostdb", "missing round-robin");
        hostDB.delete_block(r);
        return NULL;
      }
      // Check for stale (revalidate offline if we are the owner)
      // -or-
      // we are beyond our TTL but we choose to serve for another N seconds [hostdb_serve_stale_but_revalidate seconds]
      if ((!ignore_timeout && r->is_ip_stale()
#ifdef NON_MODULAR
           && !cluster_machine_at_depth(master_hash(md5))
#endif
           && !r->reverse_dns) || (r->is_ip_timeout() && r->serve_stale_but_revalidate())) {
        Debug("hostdb", "stale %u %u %u, using it and refreshing it", r->ip_interval(),
              r->ip_timestamp, r->ip_timeout_interval);
        r->refresh_ip();
        if (!is_dotted_form_hostname(hostname)) {
          HostDBContinuation *c = hostDBContAllocator.alloc();
          c->init(hostname, len, ip, port, md5, NULL, pDS, is_srv_lookup, 0);
          c->do_dns();
        }
      }

      r->hits++;
      if (!r->hits)
        r->hits--;
      return r;
    }
  }
  return NULL;
}


//
// Insert a HostDBInfo into the database
// A null value indicates that the block is empty.
//
HostDBInfo *
HostDBContinuation::insert(unsigned int attl)
{
  ink_debug_assert(this_ethread() == hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets))->thread_holding);
  uint64_t folded_md5 = fold_md5(md5);
  // remove the old one to prevent buildup
  HostDBInfo *old_r = hostDB.lookup_block(folded_md5, 3);
  if (old_r)
    hostDB.delete_block(old_r);
  HostDBInfo *r = hostDB.insert_block(folded_md5, NULL, 0);
  Debug("hostdb_insert", "inserting in bucket %d", (int) (folded_md5 % hostDB.buckets));
  r->md5_high = md5[1];
  if (attl > HOST_DB_MAX_TTL)
    attl = HOST_DB_MAX_TTL;
  r->ip_timeout_interval = attl;
  r->ip_timestamp = hostdb_current_interval;
  Debug("hostdb", "inserting for: %s: (md5: %llX) now: %u timeout: %u ttl: %u", name, folded_md5, r->ip_timestamp,
        r->ip_timeout_interval, attl);
  return r;
}


//
// Get an entry by either name or IP
//
Action *
HostDBProcessor::getby(Continuation * cont,
                       char *hostname, int len, int port, unsigned int ip, bool aforce_dns, int dns_lookup_timeout)
{
  INK_MD5 md5;
  char *pServerLine = 0;
  void *pDS = 0;
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  HOSTDB_INCREMENT_DYN_STAT(hostdb_total_lookups_stat);

  if ((!hostdb_enable || (hostname && !*hostname)) || (hostdb_disable_reverse_lookup && ip)) {
    MUTEX_TRY_LOCK(lock, cont->mutex, thread);
    if (!lock)
      goto Lretry;
    cont->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
    return ACTION_RESULT_DONE;
  }
#ifdef SPLIT_DNS
  if (hostname && SplitDNSConfig::isSplitDNSEnabled()) {
    char *scan = hostname;
    for (; *scan != '\0' && (ParseRules::is_digit(*scan) || '.' == *scan); scan++);
    if ('\0' != *scan) {
      void *pSD = (void *) SplitDNSConfig::acquire();
      if (0 != pSD) {
        pDS = ((SplitDNS *) pSD)->getDNSRecord(hostname);

        if (0 != pDS) {
          pServerLine = ((DNSServer *) pDS)->x_dns_ip_line;
        }
      }
      SplitDNSConfig::release((SplitDNS *) pSD);
    }
  }
#endif // SPLIT_DNS

  // if it is by name, INK_MD5 the name
  //
  if (hostname) {
    if (!len)
      len = strlen(hostname);
    make_md5(md5, hostname, len, port, pServerLine);
  } else {

    // INK_MD5 the ip, pad on both sizes with 0's
    // so that it does not intersect the string space
    //
    // suvasv: Changed from this
    //    uint64_t dummy = ip << 16;
    //  to uint64_t dummy = ip*64*1024 for bug INKqa10029.
    //  Problem was that ip << 16 would not work for architectures with
    //  a different byte order. This takes cares of all byte orders.
    uint64_t dummy = ((uint64_t) ip) * 64 * 1024;
    md5.encodeBuffer((char *) &dummy, 8);
  }

  // Attempt to find the result in-line, for level 1 hits
  //
  if (!aforce_dns) {
    // find the partition lock
    //
    // TODO: Could we reuse the "mutex" above safely? I think so, but not sure.
    ProxyMutex *bmutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
    MUTEX_TRY_LOCK(lock, bmutex, thread);
    MUTEX_TRY_LOCK(lock2, cont->mutex, thread);

    // If we can get the lock and a level 1 probe succeeds, return
    //
    if (lock && lock2) {
      HostDBInfo *r = probe(bmutex, md5, hostname, len, ip, port, pDS);
      if (r) {
        Debug("hostdb", "immediate answer for %s", hostname ? hostname : "<addr>");
        HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);
        reply_to_cont(cont, r);
        return ACTION_RESULT_DONE;
      }
    }
  }
  Debug("hostdb", "delaying force %d answer for %s", aforce_dns, hostname);

Lretry:
  // Otherwise, create a continuation to do a deeper probe in the background
  //
  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hostname, len, ip, port, md5, cont, pDS, false, dns_lookup_timeout);
  c->action = cont;
  c->force_dns = aforce_dns;
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::probeEvent);

  // Since ProxyMutexPtr has a cast operator, gcc-3.x get upset
  // about ambiguity when doing this comparison, so by reversing
  // the operands, I force it to pick the cast operation /leif.
  if (thread->mutex == cont->mutex) {
    thread->schedule_in(c, MUTEX_RETRY_DELAY);
  } else {
    dnsProcessor.thread->schedule_imm(c);
  }

  return &c->action;
}


// Wrapper from getbyname to getby
//
Action *
HostDBProcessor::getbyname_re(Continuation * cont, char *ahostname, int len, int port, int flags)
{
  bool force_dns = false;
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  if (flags & HOSTDB_FORCE_DNS_ALWAYS)
    force_dns = true;
  else if (flags & HOSTDB_FORCE_DNS_RELOAD) {
    force_dns = (hostdb_re_dns_on_reload ? true : false);
    if (force_dns)
      HOSTDB_INCREMENT_DYN_STAT(hostdb_re_dns_on_reload_stat);
  }
  return getby(cont, ahostname, len, port, 0, force_dns);
}


/* Support SRV records */
Action *
HostDBProcessor::getSRVbyname_imm(Continuation * cont, process_srv_info_pfn process_srv_info,
                                  char *hostname, int len, int port, int flags, int dns_lookup_timeout)
{
  ink_debug_assert(cont->mutex->thread_holding == this_ethread());
  bool force_dns = false;
  EThread *thread = cont->mutex->thread_holding;
  ProxyMutex *mutex = thread->mutex;

  if (flags & HOSTDB_FORCE_DNS_ALWAYS)
    force_dns = true;
  else if (flags & HOSTDB_FORCE_DNS_RELOAD) {
    force_dns = (hostdb_re_dns_on_reload ? true : false);
    if (force_dns)
      HOSTDB_INCREMENT_DYN_STAT(hostdb_re_dns_on_reload_stat);
  }

  INK_MD5 md5;
  void *pDS = 0;

  HOSTDB_INCREMENT_DYN_STAT(hostdb_total_lookups_stat);

  if (!hostdb_enable || !*hostname) {
    (cont->*process_srv_info) (NULL);
    return ACTION_RESULT_DONE;
  }

  if (!len)
    len = strlen(hostname);

  make_md5(md5, hostname, len, port, 0, 1);

  // Attempt to find the result in-line, for level 1 hits
  if (!force_dns) {
    // find the partition lock
    ProxyMutex *bucket_mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
    MUTEX_TRY_LOCK(lock, bucket_mutex, thread);

    // If we can get the lock and a level 1 probe succeeds, return
    if (lock) {
      HostDBInfo *r = probe(bucket_mutex, md5, hostname, len, 1, port, pDS, false, true);
      if (r) {
        Debug("hostdb", "immediate SRV answer for %s from hostdb", hostname);
        Debug("dns_srv", "immediate SRV answer for %s from hostdb", hostname);
        HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);
        (cont->*process_srv_info) (r);
        return ACTION_RESULT_DONE;
      }
    }
  }

  Debug("dns_srv", "delaying (force=%d) SRV answer for %s [timeout = %d]", force_dns, hostname, dns_lookup_timeout);

  // Otherwise, create a continuation to do a deeper probe in the background
  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hostname, len, 0, port, md5, cont, pDS, true, dns_lookup_timeout);
  c->force_dns = force_dns;
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::probeEvent);

  if (thread->mutex == cont->mutex) {
    thread->schedule_in(c, MUTEX_RETRY_DELAY);
  } else {
    dnsProcessor.thread->schedule_imm(c);
  }

  return &c->action;
}


// Wrapper from getbyname to getby
//
Action *
HostDBProcessor::getbyname_imm(Continuation * cont, process_hostdb_info_pfn process_hostdb_info,
                               char *hostname, int len, int port, int flags, int dns_lookup_timeout)
{
  ink_debug_assert(cont->mutex->thread_holding == this_ethread());
  bool force_dns = false;
  EThread *thread = cont->mutex->thread_holding;
  ProxyMutex *mutex = thread->mutex;

  if (flags & HOSTDB_FORCE_DNS_ALWAYS)
    force_dns = true;
  else if (flags & HOSTDB_FORCE_DNS_RELOAD) {
    force_dns = (hostdb_re_dns_on_reload ? true : false);
    if (force_dns)
      HOSTDB_INCREMENT_DYN_STAT(hostdb_re_dns_on_reload_stat);
  }

  INK_MD5 md5;
  void *pDS = 0;
  HOSTDB_INCREMENT_DYN_STAT(hostdb_total_lookups_stat);

  if (!hostdb_enable || !*hostname) {
    (cont->*process_hostdb_info) (NULL);
    return ACTION_RESULT_DONE;
  }

  if (!len)
    len = strlen(hostname);

#ifdef SPLIT_DNS
  if (SplitDNSConfig::isSplitDNSEnabled()) {
    char *scan = hostname;
    char *pServerLine = 0;
    for (; *scan != '\0' && (ParseRules::is_digit(*scan) || '.' == *scan); scan++);
    if ('\0' != *scan) {
      void *pSD = (void *) SplitDNSConfig::acquire();
      if (0 != pSD) {
        pDS = ((SplitDNS *) pSD)->getDNSRecord(hostname);

        if (0 != pDS) {
          pServerLine = ((DNSServer *) pDS)->x_dns_ip_line;
        }
      }
      SplitDNSConfig::release((SplitDNS *) pSD);
    }
    make_md5(md5, hostname, len, port, pServerLine);
  } else
#endif // SPLIT_DNS
    make_md5(md5, hostname, len, port, 0);

  // Attempt to find the result in-line, for level 1 hits
  if (!force_dns) {
    // find the partition lock
    ProxyMutex *bucket_mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
    MUTEX_TRY_LOCK(lock, bucket_mutex, thread);

    // If we can get the lock and a level 1 probe succeeds, return
    if (lock) {
      HostDBInfo *r = probe(bucket_mutex, md5, hostname, len, 0, port, pDS);
      if (r) {
        Debug("hostdb", "immediate answer for %s", hostname ? hostname : "<addr>");
        HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);
        (cont->*process_hostdb_info) (r);
        return ACTION_RESULT_DONE;
      }
    }
  }

  Debug("hostdb", "delaying force %d answer for %s [timeout %d]", force_dns, hostname, dns_lookup_timeout);

  // Otherwise, create a continuation to do a deeper probe in the background
  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hostname, len, 0, port, md5, cont, pDS, false, dns_lookup_timeout);
  c->force_dns = force_dns;
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::probeEvent);

  thread->schedule_in(c, MUTEX_RETRY_DELAY);

  return &c->action;
}


static void
do_setby(HostDBInfo * r, HostDBApplicationInfo * app, char *hostname, unsigned int ip)
{
  HostDBRoundRobin *rr = r->rr();

  if (rr) {
    ink_assert(hostname);
    for (int i = 0; i < rr->n; i++) {
      if (rr->info[i].ip() == ip) {
        Debug("hostdb", "immediate setby for %s", hostname ? hostname : "<addr>");
        rr->info[i].app.allotment.application1 = app->allotment.application1;
        rr->info[i].app.allotment.application2 = app->allotment.application2;
        return;
      }
    }
  } else {
    if (r->reverse_dns || (!r->round_robin && r->ip() == ip)) {
      Debug("hostdb", "immediate setby for %s", hostname ? hostname : "<addr>");
      r->app.allotment.application1 = app->allotment.application1;
      r->app.allotment.application2 = app->allotment.application2;
    }
  }
}


void
HostDBProcessor::setby(char *hostname, int len, int port, unsigned int ip, HostDBApplicationInfo * app)
{
  if (!hostdb_enable)
    return;

  INK_MD5 md5;

  // if it is by name, INK_MD5 the name
  //
  if (hostname) {
    if (!len)
      len = strlen(hostname);
    make_md5(md5, hostname, len, port);
  } else {

    // INK_MD5 the ip, pad on both sizes with 0's
    // so that it does not intersect the string space
    //

    // suvasv: Changed from this
    //    uint64_t dummy = ip << 16;
    //  to uint64_t dummy = ip*64*1024 for bug INKqa10029.
    //  Problem was that ip << 16 would not work for architectures with
    //  a different byte order. This takes cares of all byte orders.
    uint64_t dummy = ((uint64_t) ip) * 64 * 1024;
    md5.encodeBuffer((char *) &dummy, 8);
  }

  // Attempt to find the result in-line, for level 1 hits

  ProxyMutex *mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
  EThread *thread = this_ethread();
  MUTEX_TRY_LOCK(lock, mutex, thread);

  if (lock) {
    HostDBInfo *r = probe(mutex, md5, hostname, len, ip, port, 0);
    if (r)
      do_setby(r, app, hostname, ip);
    return;
  }
  // Create a continuation to do a deaper probe in the background

  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hostname, len, ip, port, md5, NULL);
  c->app.allotment.application1 = app->allotment.application1;
  c->app.allotment.application2 = app->allotment.application2;
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::setbyEvent);
  thread->schedule_in(c, MUTEX_RETRY_DELAY);
}


int
HostDBContinuation::setbyEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(e);
  HostDBInfo *r = probe(mutex, md5, name, namelen, ip, port, 0);

  if (r)
    do_setby(r, &app, name, ip);
  hostdb_cont_free(this);
  return EVENT_DONE;
}


static int
remove_round_robin(HostDBInfo * r, char *hostname, unsigned int ip)
{
  if (r) {
    if (!r->round_robin)
      return false;
    HostDBRoundRobin *rr = r->rr();
    if (!rr)
      return false;
    for (int i = 0; i < rr->good; i++) {
      if (rr->info[i].ip() == ip) {
        Debug("hostdb", "Deleting %u.%u.%u.%u from '%s' round robin DNS entry",
              ((unsigned char *) &ip)[0], ((unsigned char *) &ip)[1],
              ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3], hostname);
        HostDBInfo tmp = rr->info[i];
        rr->info[i] = rr->info[rr->good - 1];
        rr->info[rr->good - 1] = tmp;
        rr->good--;
        if (rr->good <= 0) {
          hostDB.delete_block(r);
          return false;
        } else {
          if (diags->on("hostdb")) {
            int bufsize = (HOST_DB_MAX_ROUND_ROBIN_INFO * 16) * 2;
            char *rr_ip_list = (char *) alloca(bufsize);
            char *p = rr_ip_list;
            for (int n = 0; n < rr->good; n++) {
              unsigned int rr_ip = rr->info[n].ip();
              unsigned char *pip = (unsigned char *) &rr_ip;
              int nbytes = snprintf(p, bufsize, "%hhu.%hhu.%hhu.%hhu ", pip[0], pip[1], pip[2], pip[3]);
              p += nbytes;
              bufsize -= nbytes;
            }
            Note("'%s' round robin DNS entry updated, entries=%d, IP list: %s", hostname, rr->good, rr_ip_list);
          }
        }
        return true;
      }
    }
  }
  return false;
}


Action *
HostDBProcessor::failed_connect_on_ip_for_name(Continuation * cont, unsigned int ip, char *hostname, int len, int port)
{
  INK_MD5 md5;
  char *pServerLine = 0;
  void *pDS = 0;

#ifdef SPLIT_DNS
  SplitDNS *pSD = 0;
  if (hostname && SplitDNSConfig::isSplitDNSEnabled()) {
    pSD = SplitDNSConfig::acquire();

    if (0 != pSD) {
      pDS = pSD->getDNSRecord(hostname);
      pServerLine = ((DNSServer *) pDS)->x_dns_ip_line;
    }
    SplitDNSConfig::release(pSD);
  }
#endif // SPLIT_DNS

  make_md5(md5, hostname, len, port, pServerLine);
  ProxyMutex *mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));
  EThread *thread = this_ethread();
  MUTEX_TRY_LOCK(lock, mutex, thread);
  if (lock) {
    if (!hostdb_enable || NULL == pDS) {
      if (cont)
        cont->handleEvent(EVENT_HOST_DB_IP_REMOVED, (void *) NULL);
      return ACTION_RESULT_DONE;
    }
#ifdef SPLIT_DNS
    HostDBInfo *r = probe(mutex, md5, hostname, len, ip, port, pDS);
#else
    HostDBInfo *r = probe(mutex, md5, hostname, len, ip, port, 0);
#endif
    bool res = (remove_round_robin(r, hostname, ip) ? true : false);
    if (cont)
      cont->handleEvent(EVENT_HOST_DB_IP_REMOVED, res ? (void *) &ip : (void *) NULL);
    return ACTION_RESULT_DONE;
  }
  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hostname, len, ip, port, md5, cont, pDS);
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::removeEvent);
  thread->schedule_in(c, MUTEX_RETRY_DELAY);
  return &c->action;
}


int
HostDBContinuation::removeEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  Continuation *cont = action.continuation;

  MUTEX_TRY_LOCK(lock, cont ? (ProxyMutex *) cont->mutex : (ProxyMutex *) NULL, e->ethread);
  if (!lock) {
    e->schedule_in(HOST_DB_RETRY_PERIOD);
    return EVENT_CONT;
  }
  if (!action.cancelled) {
    if (!hostdb_enable) {
      if (cont)
        cont->handleEvent(EVENT_HOST_DB_IP_REMOVED, (void *) NULL);
    } else {
      HostDBInfo *r = probe(mutex, md5, name, namelen, ip, port, m_pDS);
      bool res = (remove_round_robin(r, name, ip) ? true : false);
      if (cont)
        cont->handleEvent(EVENT_HOST_DB_IP_REMOVED, res ? (void *) &ip : (void *) NULL);
    }
  }
  hostdb_cont_free(this);
  return EVENT_DONE;
}


// Lookup done, insert into the local table, return data to the
// calling continuation or to the calling cluster node.
//
HostDBInfo *
HostDBContinuation::lookup_done(int aip, char *aname, bool around_robin, unsigned int ttl_seconds, SRVHosts * srv)
{
  HostDBInfo *i = NULL;

  ink_debug_assert(this_ethread() == hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets))->thread_holding);
  if (!aip || !aname || !aname[0]) {
    if (is_byname()) {
      Debug("hostdb", "lookup_done() failed for '%s'", name);
    } else if (is_srv()) {
      Debug("dns_srv", "SRV failed for '%s'", name);
    } else {
      Debug("hostdb", "failed for %u.%u.%u.%u",
            ((unsigned char *) &ip)[0], ((unsigned char *) &ip)[1],
            ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3]);
    }
    i = insert(hostdb_ip_fail_timeout_interval);        // currently ... 0
    i->round_robin = false;
    i->reverse_dns = !is_byname() && !is_srv();
  } else {
    switch (hostdb_ttl_mode) {
    default:
      ink_assert(!"bad TTL mode");
    case TTL_OBEY:
      break;
    case TTL_IGNORE:
      ttl_seconds = hostdb_ip_timeout_interval * 60;
      break;
    case TTL_MIN:
      if (hostdb_ip_timeout_interval * 60 < ttl_seconds)
        ttl_seconds = hostdb_ip_timeout_interval * 60;
      break;
    case TTL_MAX:
      if (hostdb_ip_timeout_interval * 60 > ttl_seconds)
        ttl_seconds = hostdb_ip_timeout_interval * 60;
      break;
    }
    HOSTDB_SUM_DYN_STAT(hostdb_ttl_stat, ttl_seconds);
    if (!ttl_seconds)
      ttl_seconds = 1;          // www.barnsandnobel.com is lame
    i = insert(ttl_seconds);
    if (is_byname()) {
      Debug("hostdb", "done %u.%u.%u.%u TTL %d",
            ((unsigned char *) &aip)[0], ((unsigned char *) &aip)[1],
            ((unsigned char *) &aip)[2], ((unsigned char *) &aip)[3], ttl_seconds);
      i->ip() = aip;
      i->round_robin = around_robin;
      i->reverse_dns = false;
      if (name != aname) {
        ink_strncpy(name, aname, MAXDNAME);
      }
      i->is_srv = false;
    } else if (is_srv()) {

      i->ip() = aip;            /* this doesnt matter w. srv records -- setting to 1 so Md5 works */

      i->reverse_dns = false;

      if (srv) {                //failed case: srv == NULL
        i->srv_count = srv->getCount();
      } else {
        i->srv_count = 0;
      }

      if (i->srv_count <= 0) {
        i->round_robin = false;
      } else {
        i->round_robin = true;
      }

      i->is_srv = true;

      if (name != aname) {
        ink_strncpy(name, aname, MAXDNAME);
      }

    } else {
      Debug("hostdb", "done '%s' TTL %d", aname, ttl_seconds);
      const size_t s_size = strlen(aname) + 1;
      void *s = hostDB.alloc(&i->data.hostname_offset, s_size);
      if (s) {
        ink_strncpy((char *) s, aname, s_size);
        i->round_robin = false;
        i->reverse_dns = true;
        i->is_srv = false;
      } else {
        ink_assert(!"out of room in hostdb data area");
        Warning("out of room in hostdb for reverse DNS data");
        hostDB.delete_block(i);
        return NULL;
      }
    }
  }
#ifdef NON_MODULAR
  if (from_cont)
    do_put_response(from, i, from_cont);
#endif
  ink_assert(!i->round_robin || !i->reverse_dns);
  return i;
}


int
HostDBContinuation::dnsPendingEvent(int event, Event * e)
{
  ink_debug_assert(this_ethread() == hostDB.lock_for_bucket(fold_md5(md5) % hostDB.buckets)->thread_holding);
  if (timeout) {
    timeout->cancel(this);
    timeout = NULL;
  }
  if (event == EVENT_INTERVAL) {
    // we timed out, return a failure to the user
    MUTEX_TRY_LOCK_FOR(lock, action.mutex, ((Event *) e)->ethread, action.continuation);
    if (!lock) {
      ((Event *) e)->schedule_in(HOST_DB_RETRY_PERIOD);
      return EVENT_CONT;
    }
    if (!action.cancelled && action.continuation)
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
    hostDB.pending_dns_for_hash(md5).remove(this);
    hostdb_cont_free(this);
    return EVENT_DONE;
  } else {
    SET_HANDLER((HostDBContHandler) & HostDBContinuation::probeEvent);
    return probeEvent(EVENT_INTERVAL, NULL);
  }
}

static int
restore_info(HostDBInfo * r, HostDBInfo * old_r, HostDBInfo & old_info, HostDBRoundRobin * old_rr_data)
{
  if (old_rr_data) {
    for (int j = 0; j < old_rr_data->n; j++)
      if (old_rr_data->info[j].ip() == r->ip()) {
        r->app = old_rr_data->info[j].app;
        return true;
      }
  } else if (old_r)
    if (old_info.ip() == r->ip()) {
      r->app = old_info.app;
      return true;
    }
  return false;
}


// DNS lookup result state
//
int
HostDBContinuation::dnsEvent(int event, HostEnt * e)
{
  ink_debug_assert(this_ethread() == hostDB.lock_for_bucket(fold_md5(md5) % hostDB.buckets)->thread_holding);
  if (timeout) {
    timeout->cancel(this);
    timeout = NULL;
  }
  EThread *thread = mutex->thread_holding;
  if (event == EVENT_INTERVAL) {
    if (!action.continuation) {
      // give up on insert, it has been too long
      remove_trigger_pending_dns();
      hostdb_cont_free(this);
      return EVENT_DONE;
    }
    MUTEX_TRY_LOCK_FOR(lock, action.mutex, thread, action.continuation);
    if (!lock) {
      timeout = thread->schedule_in(this, HOST_DB_RETRY_PERIOD);
      return EVENT_CONT;
    }
    if (!action.cancelled && action.continuation)
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
    action = NULL;
    // do not exit yet, wait to see if we can insert into DB
    timeout = thread->schedule_in(this, HRTIME_SECONDS(hostdb_insert_timeout));
    return EVENT_DONE;
  } else {
    bool failed = !e;

    bool rr;
    if (is_srv()) {
      rr = !failed && (e->srv_hosts.getCount() > 0);
    } else
      rr = !failed && e->ent.h_addr_list[1];

    pending_action = NULL;

    ttl = failed ? 0 : e->ttl / 60;
    int ttl_seconds = failed ? 0 : e->ttl;      //ebalsa: moving to second accuracy

    HostDBInfo *old_r = probe(mutex, md5, name, namelen, ip, port, m_pDS, true);
    HostDBInfo old_info;
    if (old_r)
      old_info = *old_r;
    HostDBRoundRobin *old_rr_data = old_r ? old_r->rr() : NULL;

    int n = 0, nn = 0, first = -1;
    if (rr) {
      if (is_srv() && !failed) {
        n = e->srv_hosts.getCount();
      } else {
        for (; nn < HOST_DB_MAX_ROUND_ROBIN_INFO && e->ent.h_addr_list[nn]; nn++)
          if (*(unsigned int *) e->ent.h_addr_list[nn]) {
            if (first < 0)
              first = nn;
            n++;
          } else
            Warning("0.0.0.0 removed from round-robin list for '%s'", name);
        if (first < 0) {
          failed = true;
          rr = false;
        }
      }
    } else {
      first = 0;
    }

    HostDBInfo *r = NULL;
    if (is_byname())
      r =
        lookup_done(failed ? 0 : *(unsigned int *) e->ent.h_addr_list[first], name, rr, ttl_seconds,
                    failed ? 0 : &e->srv_hosts);
    else if (is_srv())
      r = lookup_done(1,        /* junk: FIXME: is the code in lookup_done() wrong to NEED this? */
                      name,     /* hostname */
                      rr,       /* is round robin, doesnt matter for SRV since we recheck getCount() inside lookup_done() */
                      ttl_seconds,      /* ttl in seconds */
                      failed ? 0 : &e->srv_hosts);
    else
      r = lookup_done(failed ? 0 : ip, failed ? name : e->ent.h_name, false, ttl_seconds, failed ? 0 : &e->srv_hosts);

    if (rr) {
      int s = HostDBRoundRobin::size(n, is_srv());
      HostDBRoundRobin *rr_data = (HostDBRoundRobin *) hostDB.alloc(&r->app.rr.offset, s);
      Debug("hostdb", "allocating %d bytes for %d RR at %lX %d", s, n, rr_data, r->app.rr.offset);
      if (rr_data) {
        int i = 0, ii = 0;
        if (is_srv()) {
          SortableQueue<SRV> *q = e->srv_hosts.getHosts();
          if (q) {
            for (i = 0; i < n; i++) {

              SRV *t = q->dequeue();

              rr_data->info[i].ip() = 1;
              rr_data->info[i].round_robin = 0;
              rr_data->info[i].reverse_dns = 0;

              rr_data->info[i].srv_weight = t->getWeight();
              rr_data->info[i].srv_priority = t->getPriority();
              rr_data->info[i].srv_port = t->getPort();

              ink_strncpy(rr_data->rr_srv_hosts[i], t->getHost(), MAXDNAME);
              rr_data->rr_srv_hosts[i][MAXDNAME - 1] = '\0';
              rr_data->info[i].is_srv = true;

              rr_data->info[i].full = 1;
              rr_data->info[i].md5_high = r->md5_high;
              rr_data->info[i].md5_low = r->md5_low;
              rr_data->info[i].md5_low_low = r->md5_low_low;
              SRVAllocator.free(t);
              Debug("dns_srv", "inserted SRV RR record into HostDB with TTL: %d seconds", ttl_seconds);
            }
          }
        } else {
          for (; ii < nn; ii++) {
            if (*(unsigned int *) e->ent.h_addr_list[ii]) {
              rr_data->info[i].ip() = *(unsigned int *) e->ent.h_addr_list[ii];
              rr_data->info[i].full = 1;
              rr_data->info[i].round_robin = 0;
              rr_data->info[i].reverse_dns = 0;
              rr_data->info[i].md5_high = r->md5_high;
              rr_data->info[i].md5_low = r->md5_low;
              rr_data->info[i].md5_low_low = r->md5_low_low;
              if (!restore_info(&rr_data->info[i], old_r, old_info, old_rr_data)) {
                rr_data->info[i].app.allotment.application1 = 0;
                rr_data->info[i].app.allotment.application2 = 0;
              }
              i++;
            }
          }
        }
        rr_data->good = rr_data->n = n;
        rr_data->current = 0;
      } else {
        ink_assert(!"out of room in hostdb data area");
        Warning("out of room in hostdb for round-robin DNS data");
        r->round_robin = 0;
      }
    }
    if (!failed && !rr)
      restore_info(r, old_r, old_info, old_rr_data);
    ink_assert(!r || !r->round_robin || !r->reverse_dns);
    ink_assert(failed || !r->round_robin || r->app.rr.offset);

#ifdef NON_MODULAR
    // if we are not the owner, put on the owner
    //
    ClusterMachine *m = cluster_machine_at_depth(master_hash(md5));
    if (m)
      do_put_response(m, r, NULL);
#endif

    // try to callback the user
    //
    if (action.continuation) {
      MUTEX_TRY_LOCK_FOR(lock, action.mutex, thread, action.continuation);
      if (!lock) {
        remove_trigger_pending_dns();
        SET_HANDLER((HostDBContHandler) & HostDBContinuation::probeEvent);
        thread->schedule_in(this, HOST_DB_RETRY_PERIOD);
        return EVENT_CONT;
      }
      if (!action.cancelled)
        reply_to_cont(action.continuation, r);
    }
    // wake up everyone else who is waiting
    remove_trigger_pending_dns();

    // all done
    //
    hostdb_cont_free(this);
    return EVENT_DONE;
  }
}


#ifdef NON_MODULAR
//
// HostDB Get Message
// Used to lookup host information on a remote node in the cluster
//
struct HostDB_get_message
{
  INK_MD5 md5;
  unsigned int ip;
  int port;
  Continuation *cont;
  int namelen;
  char name[MAXDNAME];
};


//
// Make a get message
//
int
HostDBContinuation::make_get_message(char *buf, int size)
{
  ink_assert(size >= (int) sizeof(HostDB_get_message));

  HostDB_get_message *msg = (HostDB_get_message *) buf;
  msg->md5 = md5;
  msg->port = htonl(port);
  msg->ip = htonl(ip);
  msg->cont = this;

  // name
  ink_strncpy(msg->name, name, sizeof(msg->name));

  // length
  int len = sizeof(HostDB_get_message) - MAXDNAME + strlen(name) + 1;

  return len;
}


//
// Make and send a get message
//
bool HostDBContinuation::do_get_response(Event * e)
{
  NOWARN_UNUSED(e);
  if (!hostdb_cluster)
    return false;

  // find an appropriate Machine
  //
  ClusterMachine *
    m = NULL;

  if (hostdb_migrate_on_demand)
    m = cluster_machine_at_depth(master_hash(md5), &probe_depth, past_probes);
  else {
    if (probe_depth)
      return false;
    m = cluster_machine_at_depth(master_hash(md5));
    probe_depth = 1;
  }

  if (!m)
    return false;

  // Make message
  //
  HostDB_get_message
    msg;
  memset(&msg, 0, sizeof(msg));
  int
    len = make_get_message((char *) &msg, sizeof(HostDB_get_message));

  // Setup this continuation, with a timeout
  //
  remoteHostDBQueue[key_partition()].enqueue(this);
  SET_HANDLER((HostDBContHandler) & HostDBContinuation::clusterEvent);
  timeout = mutex->thread_holding->schedule_in(this, HOST_DB_CLUSTER_TIMEOUT);

  // Send the message
  //
  clusterProcessor.invoke_remote(m, GET_HOSTINFO_CLUSTER_FUNCTION, (char *) &msg, len);

  return true;
}


//
// HostDB Put Message
// This message is used in a response to a cluster node for
// Host inforamation.
//
struct HostDB_put_message
{
  INK_MD5
    md5;
  unsigned int
    ip;
  unsigned int
    ttl;
  int
    port;
  unsigned int
    missing:
    1;
  unsigned int
    round_robin:
    1;
  Continuation *
    cont;
  unsigned int
    application1;
  unsigned int
    application2;
  int
    namelen;
  char
    name[MAXDNAME];
};


//
// Build the put message
//
int
HostDBContinuation::make_put_message(HostDBInfo * r, Continuation * c, char *buf, int size)
{
  ink_assert(size >= (int) sizeof(HostDB_put_message));

  HostDB_put_message *msg = (HostDB_put_message *) buf;
  memset(msg, 0, sizeof(HostDB_put_message));

  msg->md5 = md5;
  msg->cont = c;
  if (r) {
    msg->ip = htonl(r->ip());
    msg->application1 = r->app.allotment.application1;
    msg->application2 = r->app.allotment.application2;
    msg->missing = false;
    msg->round_robin = r->round_robin;
    msg->ttl = r->ip_time_remaining();
  } else {
    msg->missing = true;
  }

  // name
  ink_strncpy(msg->name, name, sizeof(msg->name));
  msg->port = port;

  // length
  int len = sizeof(HostDB_put_message) - MAXDNAME + strlen(name) + 1;

  return len;
}


//
// Build the put message and send it
//
void
HostDBContinuation::do_put_response(ClusterMachine * m, HostDBInfo * r, Continuation * c)
{
  // don't remote fill round-robin DNS entries
  // if configured not to cluster them
  if (!c && r->round_robin && !hostdb_cluster_round_robin)
    return;

  HostDB_put_message msg;
  int len = make_put_message(r, c, (char *) &msg, sizeof(HostDB_put_message));

  clusterProcessor.invoke_remote(m, PUT_HOSTINFO_CLUSTER_FUNCTION, (char *) &msg, len);

}
#endif // NON_MODULAR


//
// Probe state
//
int
HostDBContinuation::probeEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  ink_assert(!link.prev && !link.next);
  EThread *t = e ? e->ethread : this_ethread();

  MUTEX_TRY_LOCK_FOR(lock, action.mutex, t, action.continuation);
  if (!lock) {
    mutex->thread_holding->schedule_in(this, HOST_DB_RETRY_PERIOD);
    return EVENT_CONT;
  }

  if (action.cancelled) {
    hostdb_cont_free(this);
    return EVENT_DONE;
  }

  if (!hostdb_enable || (!*name && !ip)) {
    if (action.continuation)
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, NULL);
#ifdef NON_MODULAR
    if (from)
      do_put_response(from, 0, from_cont);
#endif
    hostdb_cont_free(this);
    return EVENT_DONE;
  }

  if (!force_dns) {

    // Do the probe
    //
    HostDBInfo *r = probe(mutex, md5, name, namelen, ip, port, m_pDS);

    if (r)
      HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);

#ifdef NON_MODULAR
    if (action.continuation && r)
      reply_to_cont(action.continuation, r);

    // Respond to any remote node
    //
    if (from)
      do_put_response(from, r, from_cont);
#endif

    // If it suceeds or it was a remote probe, we are done
    //
    if (r || from) {
      hostdb_cont_free(this);
      return EVENT_DONE;
    }
#ifdef NON_MODULAR
    // If it failed, do a remote probe
    //
    if (do_get_response(e))
      return EVENT_CONT;
#endif
  }
  // If there are no remote nodes to probe, do a DNS lookup
  //
  do_dns();
  return EVENT_DONE;
}


int
HostDBContinuation::set_check_pending_dns()
{
  Queue<HostDBContinuation> &q = hostDB.pending_dns_for_hash(md5);
  HostDBContinuation *c = q.head;
  for (; c; c = (HostDBContinuation *) c->link.next) {
    if (md5 == c->md5) {
      Debug("hostdb", "enqueuing additional request");
      q.enqueue(this);
      return false;
    }
  }
  q.enqueue(this);
  return true;
}


void
HostDBContinuation::remove_trigger_pending_dns()
{
  Queue<HostDBContinuation> &q = hostDB.pending_dns_for_hash(md5);
  q.remove(this);
  HostDBContinuation *c = q.head;
  Queue<HostDBContinuation> qq;
  while (c) {
    HostDBContinuation *n = (HostDBContinuation *) c->link.next;
    if (md5 == c->md5) {
      Debug("hostdb", "dequeuing additional request");
      q.remove(c);
      qq.enqueue(c);
    }
    c = n;
  }
  while ((c = qq.dequeue()))
    c->handleEvent(EVENT_IMMEDIATE, NULL);
}


//
// Query the DNS processor
//
void
HostDBContinuation::do_dns()
{
  ink_assert(!action.cancelled);
  if (is_byname()) {
    Debug("hostdb", "DNS %s", name);
    unsigned int tip = ink_inet_addr(name);
    // check 127.0.0.1 format
    if ((int) tip != -1) {
      if (action.continuation) {
        HostDBInfo *r = lookup_done(tip, name, false, HOST_DB_MAX_TTL, NULL);
        reply_to_cont(action.continuation, r);
      }
      hostdb_cont_free(this);
      return;
    }
  }
  if (hostdb_lookup_timeout)
    timeout = mutex->thread_holding->schedule_in(this, HRTIME_SECONDS(hostdb_lookup_timeout));
  else
    timeout = NULL;
  if (set_check_pending_dns()) {
    SET_HANDLER((HostDBContHandler) & HostDBContinuation::dnsEvent);
    if (is_byname()) {
      DNSHandler *dnsH = 0;
#ifdef SPLIT_DNS
      if (m_pDS)
        dnsH = (DNSHandler *) (((DNSServer *) (m_pDS))->x_dnsH);
#endif
      pending_action = dnsProcessor.gethostbyname(this, name, dnsH, dns_lookup_timeout);
    } else if (is_srv()) {
      DNSHandler *dnsH = 0;
      Debug("dns_srv", "SRV lookup of %s", name);
      pending_action = dnsProcessor.getSRVbyname(this, name, dnsH, dns_lookup_timeout);
    } else {
      Debug("hostdb", "DNS IP %u.%u.%u.%u",
            ((unsigned char *) &ip)[0], ((unsigned char *) &ip)[1],
            ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3]);
      pending_action = dnsProcessor.gethostbyaddr(this, ip, dns_lookup_timeout);
    }
  } else {
    SET_HANDLER((HostDBContHandler) & HostDBContinuation::dnsPendingEvent);
  }
}

#ifdef NON_MODULAR


//
// Handle the response (put message)
//
int
HostDBContinuation::clusterResponseEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  if (from_cont) {
    HostDBContinuation *c;
    for (c = (HostDBContinuation *) remoteHostDBQueue[key_partition()].head; c; c = (HostDBContinuation *) c->link.next)
      if (c == from_cont)
        break;

    // Check to see that we have not already timed out
    //
    if (c) {
      action = c;
      from_cont = 0;
      MUTEX_TRY_LOCK(lock, c->mutex, e->ethread);
      MUTEX_TRY_LOCK(lock2, c->action.mutex, e->ethread);
      if (!lock || !lock2) {
        e->schedule_in(HOST_DB_RETRY_PERIOD);
        return EVENT_CONT;
      }
      bool failed = missing || (round_robin && !hostdb_cluster_round_robin);
      action.continuation->handleEvent(EVENT_HOST_DB_GET_RESPONSE, failed ? 0 : this);
    }
  } else {
    action = 0;
    // just a remote fill
    ink_assert(!missing);
    lookup_done(ip, name, false, ttl, NULL);
  }
  hostdb_cont_free(this);
  return EVENT_DONE;
}


//
// Wait for the response (put message)
//
int
HostDBContinuation::clusterEvent(int event, Event * e)
{
  // remove ourselves from the queue
  //
  remoteHostDBQueue[key_partition()].remove(this);

  switch (event) {
  default:
    ink_assert(!"bad case");
    hostdb_cont_free(this);
    return EVENT_DONE;

    // handle the put response, e is really a HostDBContinuation *
    //
  case EVENT_HOST_DB_GET_RESPONSE:
    if (timeout) {
      timeout->cancel(this);
      timeout = NULL;
    }
    if (e) {
      HostDBContinuation *c = (HostDBContinuation *) e;
      HostDBInfo *r = lookup_done(c->ip, c->name, false, c->ttl, NULL);
      r->app.allotment.application1 = c->app.allotment.application1;
      r->app.allotment.application2 = c->app.allotment.application2;

      HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);

      if (!action.cancelled) {
        if (reply_to_cont(action.continuation, r)) {
          // if we are not the owner and neither was the sender,
          // fill the owner
          //
          if (hostdb_migrate_on_demand) {
            ClusterMachine *m = cluster_machine_at_depth(master_hash(md5));
            if (m && m != c->from)
              do_put_response(m, r, NULL);
          }
        }
      }
      hostdb_cont_free(this);
      return EVENT_DONE;
    }
    return failed_cluster_request(e);

    // did not get the put message in time
    //
  case EVENT_INTERVAL:{
      MUTEX_TRY_LOCK_FOR(lock, action.mutex, e->ethread, action.continuation);
      if (!lock) {
        e->schedule_in(HOST_DB_RETRY_PERIOD);
        return EVENT_CONT;
      }
      return failed_cluster_request(e);
    }
  }
}


int
HostDBContinuation::failed_cluster_request(Event * e)
{
  if (action.cancelled) {
    hostdb_cont_free(this);
    return EVENT_DONE;
  }
  // Attempt another remote probe
  //
  if (do_get_response(e))
    return EVENT_CONT;

  // Otherwise, do a DNS lookup
  //
  do_dns();
  return EVENT_DONE;
}


void
get_hostinfo_ClusterFunction(ClusterMachine * from, void *data, int len)
{
  NOWARN_UNUSED(len);
  void *pDS = 0;
  HostDB_get_message *msg = (HostDB_get_message *) data;

#ifdef SPLIT_DNS
  SplitDNS *pSD = 0;
  char *hostname = msg->name;
  if (hostname && SplitDNSConfig::isSplitDNSEnabled()) {
    pSD = SplitDNSConfig::acquire();

    if (0 != pSD) {
      pDS = pSD->getDNSRecord(hostname);
    }
    SplitDNSConfig::release(pSD);
  }
#endif // SPLIT_DNS

  HostDBContinuation *c = hostDBContAllocator.alloc();
  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::probeEvent);
  c->from = from;
  c->from_cont = msg->cont;

  /* -----------------------------------------
     we make a big assumption here! we presume
     that all the machines in the cluster are
     set to use the same configuration for
     DNS servers
     ----------------------------------------- */


  c->init(msg->name, msg->namelen, ntohl(msg->ip), ntohl(msg->port), msg->md5, NULL, pDS);
  TEST(printf("get_hostinfo_ClusterFunction %s %d\n", msg->name, msg->ip));
  c->mutex = hostDB.lock_for_bucket(fold_md5(msg->md5) % hostDB.buckets);
  c->action.mutex = c->mutex;
  dnsProcessor.thread->schedule_imm(c);
}


void
put_hostinfo_ClusterFunction(ClusterMachine * from, void *data, int len)
{
  NOWARN_UNUSED(len);
  HostDB_put_message *msg = (HostDB_put_message *) data;
  HostDBContinuation *c = hostDBContAllocator.alloc();

  SET_CONTINUATION_HANDLER(c, (HostDBContHandler) & HostDBContinuation::clusterResponseEvent);
  c->init(msg->name, msg->namelen, ntohl(msg->ip), ntohl(msg->port), msg->md5, NULL);
  TEST(printf("put_hostinfo_ClusterFunction %s %d\n", msg->name, msg->ip));
  c->mutex = hostDB.lock_for_bucket(fold_md5(msg->md5) % hostDB.buckets);
  c->from_cont = msg->cont;     // cannot use action if cont freed due to timeout
  c->missing = msg->missing;
  c->round_robin = msg->round_robin;
  c->ttl = msg->ttl;
  c->from = from;
  dnsProcessor.thread->schedule_imm(c);
}
#endif // NON_MODULAR


//
// Background event
// Just increment the current_interval.  Might do other stuff
// here, like move records to the current position in the cluster.
//
int
HostDBContinuation::backgroundEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(e);
  hostdb_current_interval++;

  return EVENT_CONT;
}

bool HostDBInfo::match(INK_MD5 & md5, int bucket, int buckets)
{
  NOWARN_UNUSED(bucket);
  if (md5[1] != md5_high)
    return false;

  uint64_t
    folded_md5 = fold_md5(md5);
  uint64_t
    ttag = folded_md5 / buckets;

  if (!ttag)
    ttag = 1;
  struct
  {
    unsigned int
      md5_low_low:
      24;
    unsigned int
      md5_low;
  } tmp;
  tmp.md5_low_low = (unsigned int) ttag;
  tmp.md5_low = (unsigned int) (ttag >> 24);
  return tmp.md5_low_low == md5_low_low && tmp.md5_low == md5_low;
}


char *
HostDBInfo::hostname()
{
  if (!reverse_dns)
    return NULL;

  return (char *) hostDB.ptr(&data.hostname_offset, hostDB.ptr_to_partition((char *) this));
}


HostDBRoundRobin *
HostDBInfo::rr()
{
  if (!round_robin)
    return NULL;

  HostDBRoundRobin *r = (HostDBRoundRobin *) hostDB.ptr(&app.rr.offset, hostDB.ptr_to_partition((char *) this));

  if (r && (r->n > HOST_DB_MAX_ROUND_ROBIN_INFO || r->n <= 0 || r->good > HOST_DB_MAX_ROUND_ROBIN_INFO || r->good <= 0)) {
    ink_assert(!"bad round-robin");
    return NULL;
  }
  return r;
}


int
HostDBInfo::heap_size()
{
  if (reverse_dns) {
    char *h = hostname();

    if (h)
      return strlen(h) + 1;
  } else if (round_robin) {
    HostDBRoundRobin *r = rr();

    if (r)
      // this is a bit conservative, we might want to resurect them later
      return HostDBRoundRobin::size(r->n, this->is_srv);
  }
  return 0;
}


int *
HostDBInfo::heap_offset_ptr()
{
  if (reverse_dns)
    return &data.hostname_offset;

  if (round_robin)
    return &app.rr.offset;

  return NULL;
}


#ifdef NON_MODULAR
ClusterMachine *
HostDBContinuation::master_machine(ClusterConfiguration * cc)
{
  return cc->machine_hash((int) (md5[1] >> 32));
}
#endif // NON_MODULAR


#ifdef NON_MODULAR
struct ShowHostDB;
typedef int (ShowHostDB::*ShowHostDBEventHandler) (int event, Event * data);
struct ShowHostDB: public ShowCont
{
  char *name;
  unsigned int ip;
  bool force;

  int showMain(int event, Event * e)
  {
    CHECK_SHOW(begin("HostDB"));
    CHECK_SHOW(show("<form method = GET action = \"./name\">\n"
                    "Lookup by name (e.g. trafficserver.apache.org):<br>\n"
                    "<input type=text name=name size=64 maxlength=256>\n"
                    "</form>\n"
                    "<form method = GET action = \"./ip\">\n"
                    "Lookup by IP (e.g. 127.0.0.1):<br>\n"
                    "<input type=text name=ip size=64 maxlength=256>\n"
                    "</form>\n"
                    "<form method = GET action = \"./nameforce\">\n"
                    "Force DNS by name (e.g. trafficserver.apache.org):<br>\n"
                    "<input type=text name=name size=64 maxlength=256>\n" "</form>\n"));
    return complete(event, e);
  }


  int showLookup(int event, Event * e)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(e);
    SET_HANDLER(&ShowHostDB::showLookupDone);
    if (name)
      hostDBProcessor.getbyname_re(this, name, 0, force ? HostDBProcessor::HOSTDB_FORCE_DNS_ALWAYS : 0);
    else
      hostDBProcessor.getbyaddr_re(this, ip);
    return EVENT_CONT;
  }


  int showOne(HostDBInfo * r, bool rr, int event, Event * e)
  {
    CHECK_SHOW(show("<table border=1>\n"));
    CHECK_SHOW(show("<tr><td>%s</td><td>%s%s</td></tr>\n",
                    "Type", r->round_robin ? "Round-Robin" : "", r->reverse_dns ? "Reverse DNS" : "DNS"));
    CHECK_SHOW(show("<tr><td>%s</td><td>%u</td></tr>\n", "App1", r->app.allotment.application1));
    CHECK_SHOW(show("<tr><td>%s</td><td>%u</td></tr>\n", "App2", r->app.allotment.application2));
    if (!rr) {
      CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Stale", r->is_ip_stale()? "Yes" : "No"));
      CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Timed-Out", r->is_ip_timeout()? "Yes" : "No"));
      CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "TTL", r->ip_time_remaining()));
    }
    if (r->reverse_dns) {
      CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Hostname", r->hostname()? r->hostname() : "<none>"));
    } else {
      CHECK_SHOW(show("<tr><td>%s</td><td>%u.%u.%u.%u</td></tr>\n", "IP", PRINT_IP(r->ip())));
    }
    CHECK_SHOW(show("</table>\n"));
    return EVENT_CONT;
  }


  int showLookupDone(int event, Event * e)
  {
    HostDBInfo *r = (HostDBInfo *) e;

    CHECK_SHOW(begin("HostDB Lookup"));
    if (name) {
      CHECK_SHOW(show("<H2>%s</H2>\n", name));
    } else {
      CHECK_SHOW(show("<H2>%u.%u.%u.%u</H2>\n", PRINT_IP(ip)));
    }
    if (r) {
      showOne(r, false, event, e);
      if (r->round_robin) {
        HostDBRoundRobin *rr_data = r->rr();
        if (rr_data) {
          CHECK_SHOW(show("<table border=1>\n"));
          CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Total", rr_data->n));
          CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Good", rr_data->good));
          CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Current", rr_data->current));
          CHECK_SHOW(show("</table>\n"));

          for (int i = 0; i < rr_data->n; i++)
            showOne(&rr_data->info[i], true, event, e);
        }
      }
    } else {
      if (name) {
        CHECK_SHOW(show("<H2>%u.%u.%u.%u Not Found</H2>\n", PRINT_IP(ip)));
      } else {
        CHECK_SHOW(show("<H2>%s Not Found</H2>\n", name));
      }
    }
    return complete(event, e);
  }


ShowHostDB(Continuation * c, HTTPHdr * h):
  ShowCont(c, h), name(0), ip(0), force(0) {
    SET_HANDLER(&ShowHostDB::showMain);
  }
};

#define STR_LEN_EQ_PREFIX(_x,_l,_s) (!ptr_len_ncasecmp(_x,_l,_s,sizeof(_s)-1))


static Action *
register_ShowHostDB(Continuation * c, HTTPHdr * h)
{
  ShowHostDB *s = new ShowHostDB(c, h);
  int path_len;
  const char *path = h->url_get()->path_get(&path_len);

  SET_CONTINUATION_HANDLER(s, &ShowHostDB::showMain);
  if (STR_LEN_EQ_PREFIX(path, path_len, "ip")) {
    s->force = !ptr_len_ncasecmp(path + 3, path_len - 3, "force", 5);
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg = xstrndup(query, query_len);
    char *gn = NULL;
    if (s->sarg)
      gn = (char *) ink_memchr(s->sarg, '=', strlen(s->sarg));
    if (gn)
      s->ip = ink_inet_addr(gn + 1);
    SET_CONTINUATION_HANDLER(s, &ShowHostDB::showLookup);
  } else if (STR_LEN_EQ_PREFIX(path, path_len, "name")) {
    s->force = !ptr_len_ncasecmp(path + 5, path_len - 5, "force", 5);
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg = xstrndup(query, query_len);
    char *gn = NULL;
    if (s->sarg)
      gn = (char *) ink_memchr(s->sarg, '=', strlen(s->sarg));
    if (gn)
      s->name = gn + 1;
    SET_CONTINUATION_HANDLER(s, &ShowHostDB::showLookup);
  }
  this_ethread()->schedule_imm(s);
  return &s->action;
}
#endif // NON_MODULAR


#define HOSTDB_TEST_MAX_OUTSTANDING 100
#define HOSTDB_TEST_LENGTH          100000

struct HostDBTestReverse;
typedef int (HostDBTestReverse::*HostDBTestReverseHandler) (int, void *);
struct HostDBTestReverse: public Continuation
{
  int outstanding;
  int total;
#if TS_HAS_LRAND48_R
  struct drand48_data dr;
#endif

  int mainEvent(int event, Event * e)
  {
    if (event == EVENT_HOST_DB_LOOKUP) {
      HostDBInfo *i = (HostDBInfo *) e;
      if (i)
          printf("HostDBTestReverse: reversed %s\n", i->hostname());
        outstanding--;
    }
    while (outstanding < HOSTDB_TEST_MAX_OUTSTANDING && total < HOSTDB_TEST_LENGTH)
    {
      long l = 0;
#if TS_HAS_LRAND48_R
      lrand48_r(&dr, &l);
#else
      l = lrand48();
#endif
      unsigned int ip = (unsigned int) l;
      outstanding++;
      total++;
      if (!(outstanding % 1000))
        printf("HostDBTestReverse: %d\n", total);
      hostDBProcessor.getbyaddr_re(this, ip);
    }
    if (!outstanding) {
      printf("HostDBTestReverse: done\n");
      delete this;
    }
    return EVENT_CONT;
  }
HostDBTestReverse():Continuation(new_ProxyMutex()), outstanding(0), total(0) {
    SET_HANDLER((HostDBTestReverseHandler) & HostDBTestReverse::mainEvent);
#if TS_HAS_SRAND48_R
    srand48_r(time(NULL), &dr);
#else
    srand48(time(NULL));
#endif
  }
};


#if TS_HAS_TESTS
void
run_HostDBTest()
{
  if (is_action_tag_set("hostdb_test_rr"))
    eventProcessor.schedule_every(new HostDBTestRR, HRTIME_SECONDS(1), ET_NET);
  if (is_action_tag_set("hostdb_test_reverse")) {
    eventProcessor.schedule_imm(new HostDBTestReverse, ET_CACHE);
  }
}
#endif


RecRawStatBlock *hostdb_rsb;

void
ink_hostdb_init(ModuleVersion v)
{
  static int init_called = 0;

  ink_release_assert(!checkModuleVersion(v, HOSTDB_MODULE_VERSION));
  if (init_called)
    return;

  init_called = 1;
  // do one time stuff
  // create a stat block for HostDBStats
  hostdb_rsb = RecAllocateRawStatBlock((int) HostDB_Stat_Count);

  //
  // Register stats
  //

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.total_entries",
                     RECD_INT, RECP_NULL, (int) hostdb_total_entries_stat, RecRawStatSyncCount);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.total_lookups",
                     RECD_INT, RECP_NULL, (int) hostdb_total_lookups_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.total_hits",
                     RECD_INT, RECP_NON_PERSISTENT, (int) hostdb_total_hits_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.ttl", RECD_FLOAT, RECP_NULL, (int) hostdb_ttl_stat, RecRawStatSyncAvg);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.ttl_expires",
                     RECD_INT, RECP_NULL, (int) hostdb_ttl_expires_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.re_dns_on_reload",
                     RECD_INT, RECP_NULL, (int) hostdb_re_dns_on_reload_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS,
                     "proxy.process.hostdb.bytes", RECD_INT, RECP_NULL, (int) hostdb_bytes_stat, RecRawStatSyncCount);
}
