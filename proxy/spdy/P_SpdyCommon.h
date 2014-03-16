
#ifndef __P_SPDY_COMMON_H__
#define __P_SPDY_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

#include "P_Net.h"
#include "ts/ts.h"
#include "ts/libts.h"
#include "ts/experimental.h"
#include <spdylay/spdylay.h>
using namespace std;

#define STATUS_200      "200 OK"
#define STATUS_304      "304 Not Modified"
#define STATUS_400      "400 Bad Request"
#define STATUS_404      "404 Not Found"
#define STATUS_405      "405 Method Not Allowed"
#define STATUS_500      "500 Internal Server Error"
#define DEFAULT_HTML    "index.html"
#define SPDYD_SERVER    "ATS Spdylay/" SPDYLAY_VERSION

#define atomic_fetch_and_add(a, b)  __sync_fetch_and_add(&a, b)
#define atomic_fetch_and_sub(a, b)  __sync_fetch_and_sub(&a, b)
#define atomic_inc(a)   atomic_fetch_and_add(a, 1)
#define atomic_dec(a)   atomic_fetch_and_sub(a, 1)

struct SpdyConfig {
  bool verbose;
  bool enable_tls;
  bool keep_host_port;
  int serv_port;
  int max_concurrent_streams;
  int initial_window_size;
  spdylay_session_callbacks callbacks;
};

struct Config {
  SpdyConfig spdy;
  int nr_accept_threads;
  int accept_no_activity_timeout;
  int no_activity_timeout_in;
};

// Spdy Name/Value pairs
class SpdyNV {
public:

  SpdyNV(TSFetchSM fetch_sm);
  ~SpdyNV();

public:
  const char **nv;

private:
  SpdyNV();
  void *mime_hdr;
  char status[64];
  char version[64];
};

string http_date(time_t t);
int spdy_config_load();

extern Config SPDY_CFG;
#endif

