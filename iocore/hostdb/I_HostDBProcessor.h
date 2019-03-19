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

#pragma once

#include "tscore/HashFNV.h"
#include "tscore/ink_time.h"
#include "tscore/CryptoHash.h"
#include "tscore/ink_align.h"
#include "tscore/ink_resolver.h"
#include "I_EventSystem.h"
#include "SRV.h"
#include "P_RefCountCache.h"

// Event returned on a lookup
#define EVENT_HOST_DB_LOOKUP (HOSTDB_EVENT_EVENTS_START + 0)
#define EVENT_HOST_DB_IP_REMOVED (HOSTDB_EVENT_EVENTS_START + 1)
#define EVENT_HOST_DB_GET_RESPONSE (HOSTDB_EVENT_EVENTS_START + 2)

#define EVENT_SRV_LOOKUP (SRV_EVENT_EVENTS_START + 0)
#define EVENT_SRV_IP_REMOVED (SRV_EVENT_EVENTS_START + 1)
#define EVENT_SRV_GET_RESPONSE (SRV_EVENT_EVENTS_START + 2)

//
// Data
//
struct HostDBContinuation;

//
// The host database stores host information, most notably the
// IP address.
//
// Since host information is relatively small, we can afford to have
// a reasonable size memory cache, and use a (relatively) sparce
// disk representation to decrease # of seeks.
//
extern int hostdb_enable;
extern ink_time_t hostdb_current_interval;
extern unsigned int hostdb_ip_stale_interval;
extern unsigned int hostdb_ip_timeout_interval;
extern unsigned int hostdb_ip_fail_timeout_interval;
extern unsigned int hostdb_serve_stale_but_revalidate;
extern unsigned int hostdb_round_robin_max_count;

static inline unsigned int
makeHostHash(const char *string)
{
  ink_assert(string && *string);

  if (string && *string) {
    ATSHash32FNV1a fnv;
    fnv.update(string, strlen(string), ATSHash::nocase());
    fnv.final();
    return fnv.get();
  }

  return 0;
}

//
// Types
//

//
// This structure contains the host information required by
// the application.  Except for the initial fields it
// is treated as opacque by the database.
//

union HostDBApplicationInfo {
  struct application_data_allotment {
    unsigned int application1;
    unsigned int application2;
  } allotment;

  //////////////////////////////////////////////////////////
  // http server attributes in the host database          //
  //                                                      //
  // http_version       - one of HttpVersion_t            //
  // pipeline_max       - max pipeline.     (up to 127).  //
  //                      0 - no keep alive               //
  //                      1 - no pipeline, only keepalive //
  // keep_alive_timeout - in seconds. (up to 63 seconds). //
  // last_failure       - UNIX time for the last time     //
  //                      we tried the server & failed    //
  // fail_count         - Number of times we tried and    //
  //                       and failed to contact the host //
  //////////////////////////////////////////////////////////
  struct http_server_attr {
    unsigned int http_version : 3;
    unsigned int pipeline_max : 7;
    unsigned int keepalive_timeout : 6;
    unsigned int fail_count : 8;
    unsigned int unused1 : 8;
    unsigned int last_failure : 32;
  } http_data;

  enum HttpVersion_t {
    HTTP_VERSION_UNDEFINED = 0,
    HTTP_VERSION_09        = 1,
    HTTP_VERSION_10        = 2,
    HTTP_VERSION_11        = 3,
  };

  struct application_data_rr {
    unsigned int offset;
  } rr;
};

struct HostDBRoundRobin;

struct SRVInfo {
  unsigned int srv_offset : 16;
  unsigned int srv_weight : 16;
  unsigned int srv_priority : 16;
  unsigned int srv_port : 16;
  unsigned int key;
};

struct HostDBInfo : public RefCountObj {
  /** Internal IP address data.
      This is at least large enough to hold an IPv6 address.
  */

  static HostDBInfo *
  alloc(int size = 0)
  {
    size += sizeof(HostDBInfo);
    int iobuffer_index = iobuffer_size_to_index(size);
    ink_release_assert(iobuffer_index >= 0);
    void *ptr = ioBufAllocator[iobuffer_index].alloc_void();
    memset(ptr, 0, size);
    HostDBInfo *ret     = new (ptr) HostDBInfo();
    ret->iobuffer_index = iobuffer_index;
    return ret;
  }

  void
  free() override
  {
    Debug("hostdb", "freeing %d bytes at [%p]", (1 << (7 + iobuffer_index)), this);
    ioBufAllocator[iobuffer_index].free_void((void *)(this));
  }

  // return a version number-- so we can manage compatibility with the marshal/unmarshal
  static ts::VersionNumber
  version()
  {
    return ts::VersionNumber(1, 0);
  }

  static HostDBInfo *
  unmarshall(char *buf, unsigned int size)
  {
    if (size < sizeof(HostDBInfo)) {
      return nullptr;
    }
    HostDBInfo *ret = HostDBInfo::alloc(size - sizeof(HostDBInfo));
    int buf_index   = ret->iobuffer_index;
    memcpy((void *)ret, buf, size);
    // Reset the refcount back to 0, this is a bit ugly-- but I'm not sure we want to expose a method
    // to mess with the refcount, since this is a fairly unique use case
    ret                 = new (ret) HostDBInfo();
    ret->iobuffer_index = buf_index;
    return ret;
  }

  // return expiry time (in seconds since epoch)
  ink_time_t
  expiry_time() const
  {
    return ip_timestamp + ip_timeout_interval + hostdb_serve_stale_but_revalidate;
  }

  sockaddr *
  ip()
  {
    return &data.ip.sa;
  }

  sockaddr const *
  ip() const
  {
    return &data.ip.sa;
  }

  char *hostname() const;
  char *perm_hostname() const;
  char *srvname(HostDBRoundRobin *rr) const;

  /// Check if this entry is an element of a round robin entry.
  /// If @c true then this entry is part of and was obtained from a round robin root. This is useful if the
  /// address doesn't work - a retry can probably get a new address by doing another lookup and resolving to
  /// a different element of the round robin.
  bool
  is_rr_elt() const
  {
    return 0 != round_robin_elt;
  }

  HostDBRoundRobin *rr();

  unsigned int
  ip_interval() const
  {
    return (hostdb_current_interval - ip_timestamp) & 0x7FFFFFFF;
  }

  int
  ip_time_remaining() const
  {
    return static_cast<int>(ip_timeout_interval) - static_cast<int>(this->ip_interval());
  }

  bool
  is_ip_stale() const
  {
    return ip_timeout_interval >= 2 * hostdb_ip_stale_interval && ip_interval() >= hostdb_ip_stale_interval;
  }

  bool
  is_ip_timeout() const
  {
    return ip_interval() >= ip_timeout_interval;
  }

  bool
  is_ip_fail_timeout() const
  {
    return ip_interval() >= hostdb_ip_fail_timeout_interval;
  }

  void
  refresh_ip()
  {
    ip_timestamp = hostdb_current_interval;
  }

  bool
  serve_stale_but_revalidate() const
  {
    // the option is disabled
    if (hostdb_serve_stale_but_revalidate <= 0) {
      return false;
    }

    // ip_timeout_interval == DNS TTL
    // hostdb_serve_stale_but_revalidate == number of seconds
    // ip_interval() is the number of seconds between now() and when the entry was inserted
    if ((ip_timeout_interval + hostdb_serve_stale_but_revalidate) > ip_interval()) {
      Debug("hostdb", "serving stale entry %d | %d | %d as requested by config", ip_timeout_interval,
            hostdb_serve_stale_but_revalidate, ip_interval());
      return true;
    }

    // otherwise, the entry is too old
    return false;
  }

  /*
   * Given the current time `now` and the fail_window, determine if this real is alive
   */
  bool
  is_alive(ink_time_t now, int32_t fail_window)
  {
    unsigned int last_failure = app.http_data.last_failure;

    if (last_failure == 0 || (unsigned int)(now - fail_window) > last_failure) {
      return true;
    } else {
      // Entry is marked down.  Make sure some nasty clock skew
      //  did not occur.  Use the retry time to set an upper bound
      //  as to how far in the future we should tolerate bogus last
      //  failure times.  This sets the upper bound that we would ever
      //  consider a server down to 2*down_server_timeout
      if ((unsigned int)(now + fail_window) < last_failure) {
        app.http_data.last_failure = 0;
        return false;
      }
      return false;
    }
  }

  bool
  is_failed() const
  {
    return !((is_srv && data.srv.srv_offset) || (reverse_dns && data.hostname_offset) || ats_is_ip(ip()));
  }

  void
  set_failed()
  {
    if (is_srv) {
      data.srv.srv_offset = 0;
    } else if (reverse_dns) {
      data.hostname_offset = 0;
    } else {
      ats_ip_invalidate(ip());
    }
  }

  int iobuffer_index;

  uint64_t key;

  // Application specific data. NOTE: We need an integral number of
  // these per block. This structure is 32 bytes. (at 200k hosts =
  // 8 Meg). Which gives us 7 bytes of application information.
  HostDBApplicationInfo app;

  union {
    IpEndpoint ip;                ///< IP address / port data.
    unsigned int hostname_offset; ///< Some hostname thing.
    SRVInfo srv;
  } data;

  unsigned int hostname_offset; // always maintain a permanent copy of the hostname for non-rev dns records.

  unsigned int ip_timestamp;

  unsigned int ip_timeout_interval; // bounded between 1 and HOST_DB_MAX_TTL (0x1FFFFF, 24 days)

  unsigned int is_srv : 1;
  unsigned int reverse_dns : 1;

  unsigned int round_robin : 1;     // This is the root of a round robin block
  unsigned int round_robin_elt : 1; // This is an address in a round robin block
};

struct HostDBRoundRobin {
  /** Total number (to compute space used). */
  short rrcount = 0;

  /** Number which have not failed a connect. */
  short good = 0;

  unsigned short current    = 0;
  ink_time_t timed_rr_ctime = 0;

  // This is the equivalent of a variable length array, we can't use a VLA because
  // HostDBInfo is a non-POD type-- so this is the best we can do.
  HostDBInfo &
  info(short n)
  {
    ink_assert(n < rrcount && n >= 0);
    return *((HostDBInfo *)((char *)this + sizeof(HostDBRoundRobin)) + n);
  }

  // Return the allocation size of a HostDBRoundRobin struct suitable for storing
  // "count" HostDBInfo records.
  static unsigned
  size(unsigned count, unsigned srv_len = 0)
  {
    ink_assert(count > 0);
    return INK_ALIGN((sizeof(HostDBRoundRobin) + (count * sizeof(HostDBInfo)) + srv_len), 8);
  }

  /** Find the index of @a addr in member @a info.
      @return The index if found, -1 if not found.
  */
  int index_of(sockaddr const *addr);
  HostDBInfo *find_ip(sockaddr const *addr);
  // Find the srv target
  HostDBInfo *find_target(const char *target);
  /** Select the next entry after @a addr.
      @note If @a addr isn't an address in the round robin nothing is updated.
      @return The selected entry or @c nullptr if @a addr wasn't present.
   */
  HostDBInfo *select_next(sockaddr const *addr);
  HostDBInfo *select_best_http(sockaddr const *client_ip, ink_time_t now, int32_t fail_window);
  HostDBInfo *select_best_srv(char *target, InkRand *rand, ink_time_t now, int32_t fail_window);
  HostDBRoundRobin() {}
};

struct HostDBCache;
struct HostDBHash;

// Prototype for inline completion functionf or
//  getbyname_imm()
typedef void (Continuation::*cb_process_result_pfn)(HostDBInfo *r);

Action *iterate(Continuation *cont);

/** The Host Databse access interface. */
struct HostDBProcessor : public Processor {
  friend struct HostDBSync;
  // Public Interface

  // Lookup Hostinfo by name
  //    cont->handleEvent( EVENT_HOST_DB_LOOKUP, HostDBInfo * ); on success
  //    cont->handleEVent( EVENT_HOST_DB_LOOKUP, 0); on failure
  // Failure occurs when the host cannot be DNS-ed
  // NOTE: Will call the continuation back before returning if data is in the
  //       cache.  The HostDBInfo * becomes invalid when the callback returns.
  //       The HostDBInfo may be changed during the callback.

  enum {
    HOSTDB_DO_NOT_FORCE_DNS   = 0,
    HOSTDB_ROUND_ROBIN        = 0,
    HOSTDB_FORCE_DNS_RELOAD   = 1,
    HOSTDB_FORCE_DNS_ALWAYS   = 2,
    HOSTDB_DO_NOT_ROUND_ROBIN = 4
  };

  /// Optional parameters for getby...
  struct Options {
    typedef Options self;                                  ///< Self reference type.
    int port                    = 0;                       ///< Target service port (default 0 -> don't care)
    int flags                   = HOSTDB_DO_NOT_FORCE_DNS; ///< Processing flags (default HOSTDB_DO_NOT_FORCE_DNS)
    int timeout                 = 0;                       ///< Timeout value (default 0 -> default timeout)
    HostResStyle host_res_style = HOST_RES_IPV4;           ///< How to query host (default HOST_RES_IPV4)

    Options() {}
    /// Set the flags.
    self &
    setFlags(int f)
    {
      flags = f;
      return *this;
    }
  };

  /// Default options.
  static Options const DEFAULT_OPTIONS;

  HostDBProcessor() {}
  inkcoreapi Action *getbyname_re(Continuation *cont, const char *hostname, int len, Options const &opt = DEFAULT_OPTIONS);

  Action *getbynameport_re(Continuation *cont, const char *hostname, int len, Options const &opt = DEFAULT_OPTIONS);

  Action *getSRVbyname_imm(Continuation *cont, cb_process_result_pfn process_srv_info, const char *hostname, int len,
                           Options const &opt = DEFAULT_OPTIONS);

  Action *getbyname_imm(Continuation *cont, cb_process_result_pfn process_hostdb_info, const char *hostname, int len,
                        Options const &opt = DEFAULT_OPTIONS);

  Action *iterate(Continuation *cont);

  /** Lookup Hostinfo by addr */
  Action *getbyaddr_re(Continuation *cont, sockaddr const *aip);

  /** Set the application information (fire-and-forget). */
  void
  setbyname_appinfo(char *hostname, int len, int port, HostDBApplicationInfo *app)
  {
    sockaddr_in addr;
    ats_ip4_set(&addr, INADDR_ANY, port);
    setby(hostname, len, ats_ip_sa_cast(&addr), app);
  }

  void
  setbyaddr_appinfo(sockaddr const *addr, HostDBApplicationInfo *app)
  {
    this->setby(nullptr, 0, addr, app);
  }

  void
  setbyaddr_appinfo(in_addr_t ip, HostDBApplicationInfo *app)
  {
    sockaddr_in addr;
    ats_ip4_set(&addr, ip);
    this->setby(nullptr, 0, ats_ip_sa_cast(&addr), app);
  }

  /** Configuration. */
  static int hostdb_strict_round_robin;
  static int hostdb_timed_round_robin;

  // Processor Interface
  /* hostdb does not use any dedicated event threads
   * currently. Dont pass any value to start
   */
  int start(int no_of_additional_event_threads = 0, size_t stacksize = DEFAULT_STACKSIZE) override;

  // Private
  HostDBCache *cache();

private:
  Action *getby(Continuation *cont, cb_process_result_pfn cb_process_result, HostDBHash &hash, Options const &opt);

public:
  /** Set something.
      @a aip can carry address and / or port information. If setting just
      by a port value, the address should be set to INADDR_ANY which is of
      type IPv4.
   */
  void setby(const char *hostname,      ///< Hostname.
             int len,                   ///< Length of hostname.
             sockaddr const *aip,       ///< Address and/or port.
             HostDBApplicationInfo *app ///< I don't know.
  );

  void setby_srv(const char *hostname, int len, const char *target, HostDBApplicationInfo *app);
};

void run_HostDBTest();

extern inkcoreapi HostDBProcessor hostDBProcessor;

void ink_hostdb_init(ts::ModuleVersion version);
