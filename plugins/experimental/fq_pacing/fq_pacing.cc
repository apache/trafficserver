/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cerrno>
#include <getopt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/remap_version.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

static const char *PLUGIN_NAME = "fq_pacing";

static DbgCtl dbg_ctl{PLUGIN_NAME};

// Sanity check max rate at 100Gbps
#define MAX_PACING_RATE 100000000000

struct fq_pacing_cfg_t {
  unsigned long pacing_rate;
};

struct fq_pacing_cont_t {
  int client_fd;
};

// Copied from ts/ink_sock.cc since that function is not exposed to plugins
int
safe_setsockopt(int s, int level, int optname, char *optval, int optlevel)
{
  int r;
  do {
    r = setsockopt(s, level, optname, optval, optlevel);
  } while (r < 0 && (errno == EAGAIN || errno == EINTR));
  return r;
}

static int
fq_is_default_qdisc()
{
  int     fd        = -1;
  ssize_t bytes     = 0;
  char    buffer[5] = {};
  int     rc        = 0;

  fd = open("/proc/sys/net/core/default_qdisc", O_RDONLY);
  if (fd < 0) {
    return 0;
  }

  bytes = read(fd, buffer, sizeof(buffer) - 1);
  if (bytes > 0) {
    buffer[bytes] = 0;
  } else {
    close(fd);
    return 0;
  }

  if (buffer[2] == '\n') {
    buffer[2] = 0;
  }

  rc = (strncmp(buffer, "fq", sizeof(buffer)) == 0);
  close(fd);
  return (rc);
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"fq_pacing";
  info.vendor_name   = (char *)"Cisco Systems";
  info.support_email = (char *)"omdbuild@cisco.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[fq_pacing] plugin registration failed");
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);

  if (!fq_is_default_qdisc()) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - fq qdisc is not active");
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  fq_pacing_cfg_t *cfg         = nullptr;
  unsigned long    pacing_rate = 0;

  Dbg(dbg_ctl, "Instantiating a new remap.config plugin rule");

  if (argc > 1) {
    int                        c;
    static const struct option longopts[] = {
      {"rate",  required_argument, nullptr, 'r'},
      {nullptr, 0,                 nullptr, 0  }
    };

    // The "-" in optstring is required to prevent permutation of argv, which
    // makes the plugin loader crashy
    while ((c = getopt_long(argc, (char *const *)argv, "-r:", longopts, nullptr)) != -1) {
      switch (c) {
      case 'r':
        errno       = 0;
        pacing_rate = strtoul(optarg, nullptr, 0);
        if (errno != 0) {
          snprintf(errbuf, errbuf_size - 1, "[TsRemapNewInstance] input pacing value is not a valid positive integer");
          return TS_ERROR;
        }

        break;
      }
    }
  }

  if (pacing_rate > MAX_PACING_RATE) {
    snprintf(errbuf, errbuf_size - 1, "[TsRemapNewInstance] input pacing value is too large (%lu), max(%lu)", pacing_rate,
             MAX_PACING_RATE);
    return TS_ERROR;
  }

  cfg = TSRalloc<fq_pacing_cfg_t>();
  memset(cfg, 0, sizeof(*cfg));
  cfg->pacing_rate = pacing_rate;
  *ih              = cfg;
  Dbg(dbg_ctl, "Setting pacing rate to %lu", pacing_rate);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  TSError("[fq_pacing] Cleaning up...");

  if (instance != nullptr) {
    TSfree(static_cast<fq_pacing_cfg_t *>(instance));
  }
}

static int
reset_pacing_cont(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp     = static_cast<TSHttpTxn>(edata);
  auto      txn_data = static_cast<fq_pacing_cont_t *>(TSContDataGet(contp));

#ifdef SO_MAX_PACING_RATE
  unsigned int pacing_off = ~0U;
  if (txn_data->client_fd > 0) {
    Dbg(dbg_ctl, "Disabling SO_MAX_PACING_RATE for client_fd=%d", txn_data->client_fd);
    int res = 0;
    res     = safe_setsockopt(txn_data->client_fd, SOL_SOCKET, SO_MAX_PACING_RATE, reinterpret_cast<char *>(&pacing_off),
                              sizeof(pacing_off));
    // EBADF indicates possible client abort
    if ((res < 0) && (errno != EBADF)) {
      TSError("[fq_pacing] Error disabling SO_MAX_PACING_RATE, errno=%d", errno);
    }
  }
#endif

  TSfree(txn_data);
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  if (TSHttpTxnClientProtocolStackContains(txnp, TS_PROTO_TAG_HTTP_2_0) != nullptr) {
    Dbg(dbg_ctl, "Skipping plugin execution for HTTP/2 requests");
    return TSREMAP_NO_REMAP;
  }

  int client_fd = 0;
  if (TSHttpTxnClientFdGet(txnp, &client_fd) != TS_SUCCESS) {
    TSError("[fq_pacing] Error getting client fd");
  }

#ifdef SO_MAX_PACING_RATE
  fq_pacing_cfg_t *cfg = static_cast<fq_pacing_cfg_t *>(instance);
  int              res = 0;

  res = safe_setsockopt(client_fd, SOL_SOCKET, SO_MAX_PACING_RATE, reinterpret_cast<char *>(&cfg->pacing_rate),
                        sizeof(cfg->pacing_rate));
  if ((res < 0)) {
    TSError("[fq_pacing] Error setting SO_MAX_PACING_RATE, errno=%d", errno);
  }
  Dbg(dbg_ctl, "Setting SO_MAX_PACING_RATE for client_fd=%d to %lu Bps", client_fd, cfg->pacing_rate);
#endif

  // Reset pacing at end of transaction in case session is
  // reused for another delivery service w/o pacing
  TSCont cont = TSContCreate(reset_pacing_cont, nullptr);

  auto txn_data       = TSRalloc<fq_pacing_cont_t>();
  txn_data->client_fd = client_fd;
  TSContDataSet(cont, txn_data);

  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, cont);
  return TSREMAP_NO_REMAP;
}
