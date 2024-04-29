/** @file

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

#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"
#include "ts/ts.h"

#include <cstdio>
#include <cstdlib>

#include <cctype>
#include <climits>
#include <ts/ts.h>
#include <cstring>
#include <cinttypes>
#include <getopt.h>
#include <sys/stat.h>
#include <ctime>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <fstream>
#include <ts/remap.h>
#include <dirent.h>
#include <cstdint>
#include <sys/types.h>
#include <chrono>

#if __has_include(<sys/sysinfo.h>)
#include <sys/sysinfo.h>
#endif

#include <climits>

#define PLUGIN_NAME "system_stats"
#define DEBUG_TAG   PLUGIN_NAME

// Time in MS to grab the system stats
#define SYSTEM_STATS_TIMEOUT 5000

// Load Average Strings
#define LOAD_AVG_ONE_MIN     "plugin." PLUGIN_NAME ".loadavg.one"
#define LOAD_AVG_FIVE_MIN    "plugin." PLUGIN_NAME ".loadavg.five"
#define LOAD_AVG_FIFTEEN_MIN "plugin." PLUGIN_NAME ".loadavg.fifteen"

// Process Strings
#define CURRENT_PROCESSES "plugin." PLUGIN_NAME ".current_processes"

// Memory/Swap Strings
#define TOTAL_RAM  "plugin." PLUGIN_NAME ".total_ram"
#define FREE_RAM   "plugin." PLUGIN_NAME ".free_ram"
#define SHARED_RAM "plugin." PLUGIN_NAME ".shared_ram"
#define BUFFER_RAM "plugin." PLUGIN_NAME ".buffer_ram"
#define TOTAL_SWAP "plugin." PLUGIN_NAME ".total_swap"
#define FREE_SWAP  "plugin." PLUGIN_NAME ".free_swap"

// Base net stats name, full name needs to populated
// with NET_STATS.infname.RX/TX.standard_net_stats field
#define NET_STATS "plugin." PLUGIN_NAME ".net."

// Timestamp Strings
#define TIMESTAMP "plugin." PLUGIN_NAME ".timestamp_ms"

#define NET_STATS_DIR  "/sys/class/net"
#define STATISTICS_DIR "statistics"

// Used for matching to slave (old name) and lower (new name) symlinks
// in a bonded interface
// This way we can report things like plugin.net.bond0.slave_dev1.speed
#define SLAVE "slave_"
#define LOWER "lower_"

// Dir name for slave/lower interfaces that are bond members. This dir houses
// port information we may want such as the up/down streams port state
#define BONDING_SLAVE_DIR "bonding_slave"

namespace
{
DbgCtl dbg_ctl{DEBUG_TAG};
}

static int
statAdd(const char *name, TSRecordDataType record_type, TSMutex create_mutex)
{
  int stat_id = -1;

  TSMutexLock(create_mutex);

  if (TS_ERROR == TSStatFindName(name, &stat_id)) {
    stat_id = TSStatCreate(name, record_type, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    if (stat_id == TS_ERROR) {
      Dbg(dbg_ctl, "Error creating stat_name: %s", name);
    } else {
      Dbg(dbg_ctl, "Created stat_name: %s stat_id: %d", name, stat_id);
    }
  }

  TSMutexUnlock(create_mutex);

  return stat_id;
}

static ssize_t
getFile(const char *filename, char *buffer, size_t bufferSize)
{
  TSFile f = TSfopen(filename, "r");
  if (!f) {
    buffer[0] = 0;
    // Return -1 to indicate read err
    return -1;
  }

  ssize_t s = TSfread(f, buffer, bufferSize - 1);
  if (s > 0) {
    buffer[s] = 0;
  } else {
    buffer[0] = 0;
  }

  TSfclose(f);

  return s;
}

static void
statSet(const char *name, long value, TSMutex stat_creation_mutex)
{
  int stat_id = statAdd(name, TS_RECORDDATATYPE_INT, stat_creation_mutex);
  if (stat_id != TS_ERROR) {
    TSStatIntSet(stat_id, value);
  }
}

static void
setNetStat(TSMutex stat_creation_mutex, const char *interface, const char *entry, const char *subdir, bool subdirstatname)
{
  char sysfs_name[PATH_MAX];
  char stat_name[255];
  char data[255];

  memset(&stat_name[0], 0, sizeof(stat_name));
  memset(&sysfs_name[0], 0, sizeof(sysfs_name));
  memset(&data[0], 0, sizeof(data));

  if ((interface == nullptr) || (entry == nullptr)) {
    TSError("%s: nullptr subdir or entry", DEBUG_TAG);
    return;
  }

  // Generate the ATS stats name
  if (subdirstatname) {
    snprintf(&stat_name[0], sizeof(stat_name), "%s%s.%s.%s", NET_STATS, interface, subdir, entry);
  } else {
    snprintf(&stat_name[0], sizeof(stat_name), "%s%s.%s", NET_STATS, interface, entry);
  }

  // Determine if this is a toplevel netdev stat, or one from statistics.
  if (subdir == nullptr) {
    snprintf(&sysfs_name[0], sizeof(sysfs_name), "%s/%s/%s", NET_STATS_DIR, interface, entry);
  } else {
    snprintf(&sysfs_name[0], sizeof(sysfs_name), "%s/%s/%s/%s", NET_STATS_DIR, interface, subdir, entry);
  }

  if (getFile(&sysfs_name[0], &data[0], sizeof(data)) < 0) {
    Dbg(dbg_ctl, "Error reading file %s", sysfs_name);
  } else {
    statSet(stat_name, atol(data), stat_creation_mutex);
  }
}

static void
setBondingStat(TSMutex stat_creation_mutex, const char *interface)
{
  char           infdir[PATH_MAX];
  struct dirent *dent;

  memset(&infdir[0], 0, sizeof(infdir));

  if (interface == nullptr) {
    TSError("%s: nullptr interface", DEBUG_TAG);
    return;
  }

  snprintf(&infdir[0], sizeof(infdir), "%s/%s", NET_STATS_DIR, interface);
  DIR *localdir = opendir(infdir);

  while ((dent = readdir(localdir)) != nullptr) {
    if (((strncmp(SLAVE, dent->d_name, strlen(SLAVE)) == 0) || (strncmp(LOWER, dent->d_name, strlen(LOWER)) == 0)) &&
        (dent->d_type == DT_LNK)) {
      // We have a symlink starting with slave or lower, get its speed
      setNetStat(stat_creation_mutex, interface, "speed", dent->d_name, true);
    }

    if (strncmp(BONDING_SLAVE_DIR, dent->d_name, strlen(BONDING_SLAVE_DIR)) == 0 && (dent->d_type != DT_LNK)) {
      setNetStat(stat_creation_mutex, interface, "ad_actor_oper_port_state", dent->d_name, false);
      setNetStat(stat_creation_mutex, interface, "ad_partner_oper_port_state", dent->d_name, false);
    }
  }

  closedir(localdir);
}

static int
netStatsInfo(TSMutex stat_creation_mutex)
{
  struct dirent *dent;
  DIR           *srcdir = opendir(NET_STATS_DIR);

  if (srcdir == nullptr) {
    return 0;
  }

  while ((dent = readdir(srcdir)) != nullptr) {
    if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0 || (dent->d_type != DT_LNK)) {
      continue;
    }

    setNetStat(stat_creation_mutex, dent->d_name, "speed", nullptr, false);
    setNetStat(stat_creation_mutex, dent->d_name, "collisions", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "multicast", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_bytes", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_compressed", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_crc_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_dropped", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_fifo_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_frame_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_length_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_missed_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_nohandler", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_over_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "rx_packets", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_aborted_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_bytes", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_carrier_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_compressed", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_dropped", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_fifo_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_heartbeat_errors", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_packets", STATISTICS_DIR, false);
    setNetStat(stat_creation_mutex, dent->d_name, "tx_window_errors", STATISTICS_DIR, false);

    setBondingStat(stat_creation_mutex, dent->d_name);
  }

  closedir(srcdir);

  return 0;
}

static void
getStats(TSMutex stat_creation_mutex)
{
#if HAVE_SYSINFO
  struct sysinfo info;

  sysinfo(&info);

  statSet(TIMESTAMP,
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
          stat_creation_mutex);
  statSet(LOAD_AVG_ONE_MIN, info.loads[0], stat_creation_mutex);
  statSet(LOAD_AVG_FIVE_MIN, info.loads[1], stat_creation_mutex);
  statSet(LOAD_AVG_FIFTEEN_MIN, info.loads[2], stat_creation_mutex);
  statSet(CURRENT_PROCESSES, info.procs, stat_creation_mutex);
  statSet(TOTAL_RAM, info.totalram, stat_creation_mutex);
  statSet(FREE_RAM, info.freeram, stat_creation_mutex);
  statSet(SHARED_RAM, info.sharedram, stat_creation_mutex);
  statSet(BUFFER_RAM, info.bufferram, stat_creation_mutex);
  statSet(TOTAL_SWAP, info.totalswap, stat_creation_mutex);
  statSet(FREE_SWAP, info.freeswap, stat_creation_mutex);
#endif // #ifdef HAVE_SYSINFO
  netStatsInfo(stat_creation_mutex);

  return;
}

static int
systemStatsContCB(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSMutex stat_creation_mutex;

  Dbg(dbg_ctl, "entered %s", __FUNCTION__);

  stat_creation_mutex = TSContMutexGet(cont);
  getStats(stat_creation_mutex);

  TSContScheduleOnPool(cont, SYSTEM_STATS_TIMEOUT, TS_THREAD_POOL_TASK);
  Dbg(dbg_ctl, "finished %s", __FUNCTION__);

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont                   stats_cont;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", DEBUG_TAG);
    return;
  } else {
    Dbg(dbg_ctl, "Plugin registration succeeded");
  }

  stats_cont = TSContCreate(systemStatsContCB, TSMutexCreate());
  TSContDataSet(stats_cont, nullptr);

  // We want our first hit immediate to populate the stats,
  // Subsequent schedules done within the function will be for
  // 5 seconds.
  TSContScheduleOnPool(stats_cont, 0, TS_THREAD_POOL_TASK);

  Dbg(dbg_ctl, "Init complete");
}
