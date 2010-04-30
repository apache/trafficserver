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

/***************************************/
/****************************************************************************
 *
 *  StatAggregation.cc - Functions for computing node and cluster stat
 *                          aggregation
 *
 *
 ****************************************************************************/

/****************************************************************************
 *    ____
 *   /    \
 *  /      \
 * |  STOP  | This file is depreciated. Please see proxy/mgmt2/stats/...
 *  \      /
 *   \____/
 *
 ****************************************************************************/

#include "ink_config.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
#include "WebMgmtUtils.h"
#include "ink_hrtime.h"
#include "MgmtUtils.h"
#include "MgmtDefs.h"
#include "LocalManager.h"

#define _HEADER
#define _D(x)
#define _FOOTER
#include "DynamicStats.h"

#define WMT_STATS_1
#define QT_STATS_1

const ink_hrtime hrThreshold = 10 * HRTIME_SECOND;

void
AgInt_generic(const char *processVar, const char *nodeVar)
{
  MgmtInt tmp;

  if (varIntFromName(processVar, &tmp)) {
    varSetInt(nodeVar, tmp);
  } else {
    varSetInt(nodeVar, -20);
  }
}

void
AgInt_generic_scale(const char *processVar, const char *nodeVar, double factor)
{
  MgmtInt tmp;

  if (varIntFromName(processVar, &tmp)) {
    tmp = (MgmtInt) (tmp * factor);
    varSetInt(nodeVar, tmp);
  } else {
    varSetInt(nodeVar, -20);
  }
}

void
AgFloat_generic(char *processVar, char *nodeVar)
{
  MgmtFloat tmp;

  if (varFloatFromName(processVar, &tmp)) {
    varSetFloat(nodeVar, tmp);
  } else {
    varSetFloat(nodeVar, -20.0);
  }
}

void
AgFloat_generic_scale_to_int(const char *processVar, const char *nodeVar, double factor)
{
  MgmtFloat tmp;

  if (varFloatFromName(processVar, &tmp)) {
    tmp = tmp * factor;
    tmp = tmp + 0.5;            // round up.
    varSetInt(nodeVar, (int) tmp);
  } else {
    varSetInt(nodeVar, -20);
  }
}




//
// Calculate the free space in the cache(bytes_free & percent_free)
// NOTE: The cache keeps a new stat 'proxy.process.cache.percent_full'
//       from which the 'percent_free' could be calculated
//
void
Ag_cachePercent()
{
  MgmtInt bTotal;
  MgmtInt bUsed;
  MgmtInt bFree;
  MgmtFloat pFree;

  if (varIntFromName("proxy.process.cache.bytes_total", &bTotal) &&
      varIntFromName("proxy.process.cache.bytes_used", &bUsed)) {
    if (bTotal <= 0) {
      pFree = 0.0;
      bFree = 0;
    } else {
      bFree = bTotal - bUsed;
      pFree = (MgmtFloat) ((double) bFree / (double) bTotal);
    }
  } else {
    bFree = -20;
    pFree = -20.0;
  }

  ink_assert(varSetFloat("proxy.node.cache.percent_free", pFree));
  ink_assert(varSetInt("proxy.node.cache.bytes_free", bFree));
}


// HTTP hit stats
// NOTE: no cache hit info. for WMT, QT
static const char *hitCounters[] = {
  "proxy.process.http.cache_hit_fresh", // 0
  "proxy.process.http.cache_hit_revalidated",   // 1
  "proxy.process.http.cache_hit_ims",   // 2
  "proxy.process.http.cache_hit_stale_served",  // 3
  "proxy.process.rni.block_hit_count",  // 4
  NULL
};

// HTTP miss stats
// NOTE: no cache miss info. for WMT, QT
static const char *missCounters[] = {
  "proxy.process.http.cache_miss_cold", // 0
  "proxy.process.http.cache_miss_changed",      // 1
  "proxy.process.http.cache_miss_not_cacheable",        // 2
  "proxy.process.http.cache_miss_client_no_cache",      // 3
  "proxy.process.http.cache_miss_ims",  // 4
  "proxy.process.http.cache_read_error",        // 5
  "proxy.process.rni.block_miss_count", // 6
  NULL
};

//
// Calculate Node cache hits/misses i.e. hit ratio
// Should include HTTP and maybe RNI(?)
//
void
Ag_cacheHits()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds

  MgmtInt hits = 0;
  MgmtInt miss = 0;
  MgmtInt total = 0;
  MgmtInt lookup;
  MgmtFloat hitRate;
  int i = 0;

  // the position in this array is significant, hardcoded, and inter-related
  static StatTwoIntSamples hit_count_table[] = {
    {"proxy.process.http.cache_hit_fresh", 0, 0, 0, 0}, // 0
    {"proxy.process.http.cache_hit_revalidated", 0, 0, 0, 0},   // 1
    {"proxy.process.http.cache_hit_ims", 0, 0, 0, 0},   // 2
    {"proxy.process.http.cache_hit_stale_served", 0, 0, 0, 0},  // 3
    {"proxy.process.rni.block_hit_count", 0, 0, 0, 0},  // 6
    {NULL, -1, -1, -1, -1}
  };

  // the position in this array is significant, hardcoded, and inter-related
  static StatTwoIntSamples miss_count_table[] = {
    {"proxy.process.http.cache_miss_cold", 0, 0, 0, 0}, // 0
    {"proxy.process.http.cache_miss_changed", 0, 0, 0, 0},      // 1 
    {"proxy.process.http.cache_miss_not_cacheable", 0, 0, 0, 0},        // 2
    {"proxy.process.http.cache_miss_client_no_cache", 0, 0, 0, 0},      // 3
    {"proxy.process.http.cache_miss_ims", 0, 0, 0, 0},  // 4
    {"proxy.process.http.cache_read_error", 0, 0, 0, 0},        // 5 
    {"proxy.process.rni.block_miss_count", 0, 0, 0, 0}, // 6
    {NULL, -1, -1, -1, -1}
  };

  // the position in this array is significant and hardcoded
  static const char *hit_counts_names[] = {
    "proxy.node.http.cache_hit_fresh_avg_10s",  // 0
    "proxy.node.http.cache_hit_revalidated_avg_10s",    // 1
    "proxy.node.http.cache_hit_ims_avg_10s",    // 2
    "proxy.node.http.cache_hit_stale_served_avg_10s",   // 3
    "proxy.node.rni.block_hit_count_avg_10s",   // 4
    NULL
  };

  // the position in this array is significant and hardcoded
  static const char *miss_counts_names[] = {
    "proxy.node.http.cache_miss_cold_avg_10s",  // 0
    "proxy.node.http.cache_miss_changed_avg_10s",       // 1 
    "proxy.node.http.cache_miss_not_cacheable_avg_10s", // 2
    "proxy.node.http.cache_miss_client_no_cache_avg_10s",       // 3
    "proxy.node.http.cache_miss_ims_avg_10s",   // 4
    "proxy.node.http.cache_read_error_avg_10s", // 5
    "proxy.node.rni.block_miss_count_avg_10s",  // 6
    NULL
  };

  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ///////////////////////////////////////
    // scroll values for Hit/Miss tables //
    //////////////////////////////////////
    for (i = 0; hit_count_table[i].lm_record_name; i++) {
      hit_count_table[i].previous_time = hit_count_table[i].current_time;
      hit_count_table[i].previous_value = hit_count_table[i].current_value;
    }

    for (i = 0; miss_count_table[i].lm_record_name; i++) {
      miss_count_table[i].previous_time = miss_count_table[i].current_time;
      miss_count_table[i].previous_value = miss_count_table[i].current_value;
    }

    //////////////////////////
    // calculate new values //
    //////////////////////////
    for (i = 0; hit_count_table[i].lm_record_name; i++) {
      int status;
      hit_count_table[i].current_value = -10000;
      hit_count_table[i].current_time = ink_get_hrtime();
      status = varIntFromName(hit_count_table[i].lm_record_name, &(hit_count_table[i].current_value));
    }

    for (i = 0; miss_count_table[i].lm_record_name; i++) {
      int status;
      miss_count_table[i].current_value = -10000;
      miss_count_table[i].current_time = ink_get_hrtime();
      status = varIntFromName(miss_count_table[i].lm_record_name, &(miss_count_table[i].current_value));
    }

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      MgmtInt num_hits = 0;
      MgmtInt num_misses = 0;
      MgmtInt diff = 0;

      // generate time window deltas and sum
      for (i = 0; hit_counts_names[i]; i++) {
        diff = hit_count_table[i].diff_value();
        varSetInt(hit_counts_names[i], diff, true);
        num_hits += diff;
      }
      for (i = 0; miss_counts_names[i]; i++) {
        diff = miss_count_table[i].diff_value();
        varSetInt(miss_counts_names[i], diff, true);
        num_misses += diff;
      }

      total = num_hits + num_misses;
      if (total == 0)
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) ((double) num_hits / (double) total);

      // new stats
      varSetInt("proxy.node.cache_total_hits_avg_10s", num_hits);
      varSetInt("proxy.node.cache_total_misses_avg_10s", num_misses);
      if (num_hits <= total)
        varSetFloat("proxy.node.cache_hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }

  // Lifetime stats
  for (i = 0; hitCounters[i]; i++) {
    if (varIntFromName(hitCounters[i], &lookup)) {
      hits += lookup;
    } else {
      mgmt_log(stderr, "[Ag_cacheHits] Bad Cache Hit Count %s\n", hitCounters[i]);
      hits = 0;
      miss = 0;
      hitRate = 0.0;
      goto SET_VARS;
    }
  }

  for (i = 0; missCounters[i]; i++) {
    if (varIntFromName(missCounters[i], &lookup)) {
      miss += lookup;
    } else {
      mgmt_log(stderr, "[Ag_cacheHits] Bad Cache Miss Count %s\n", missCounters[i]);
      miss = 0;
      hitRate = 0.0;
      goto SET_VARS;
    }
  }

  total = hits + miss;
  if (total == 0) {
    hitRate = 0.00;
  } else {
    hitRate = (MgmtFloat) ((double) hits / (double) total);
  }

SET_VARS:
  // Old stats
  varSetInt("proxy.node.http.cache_total_hits", hits);
  varSetInt("proxy.node.http.cache_total_misses", miss);
  varSetFloat("proxy.node.http.cache_hit_ratio", hitRate);

  // new stats
  varSetInt("proxy.node.cache_total_hits", hits);
  varSetInt("proxy.node.cache_total_misses", miss);
  varSetFloat("proxy.node.cache_hit_ratio", hitRate);

}

//
// Calculate Node HostDB hit ratio
//
void
Ag_HostdbHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples node_hostdb_total_lookups = { "proxy.process.hostdb.total_lookups", 0, 0, 0, 0 };
  static StatTwoIntSamples node_hostdb_hits = { "proxy.process.hostdb.total_hits", 0, 0, 0, 0 };
  static const char *node_hostdb_total_lookups_name = "proxy.node.hostdb.total_lookups_avg_10s";
  static const char *node_hostdb_hits_name = "proxy.node.hostdb.total_hits_avg_10s";
  int status;
  MgmtInt hostdbHits;
  MgmtInt hostdbLookups;
  MgmtFloat floatSum = -20;
  MgmtFloat hitRate;

  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ////////////////////////////////////////
    // scroll values for node DNS //
    ///////////////////////////////////////
    node_hostdb_total_lookups.previous_time = node_hostdb_total_lookups.current_time;
    node_hostdb_total_lookups.previous_value = node_hostdb_total_lookups.current_value;

    node_hostdb_hits.previous_time = node_hostdb_hits.current_time;
    node_hostdb_hits.previous_value = node_hostdb_hits.current_value;

    //////////////////////////
    // calculate new values //
    //////////////////////////
    node_hostdb_total_lookups.current_value = -10000;
    node_hostdb_total_lookups.current_time = ink_get_hrtime();
    status = varIntFromName(node_hostdb_total_lookups.lm_record_name, &(node_hostdb_total_lookups.current_value));

    node_hostdb_hits.current_value = -10000;
    node_hostdb_hits.current_time = ink_get_hrtime();
    status = varIntFromName(node_hostdb_hits.lm_record_name, &(node_hostdb_hits.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      MgmtInt num_total_lookups = 0;
      MgmtInt num_hits = 0;
      MgmtInt diff = 0;

      // generate time window deltas and sum
      diff = node_hostdb_total_lookups.diff_value();
      varSetInt(node_hostdb_total_lookups_name, diff);
      num_total_lookups = diff;

      diff = node_hostdb_hits.diff_value();
      varSetInt(node_hostdb_hits_name, diff);
      num_hits = diff;

      // limit to 100% :)
      if (num_hits > num_total_lookups)
        num_hits = num_total_lookups;

      if (num_total_lookups == 0)
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) ((double) num_hits / (double) num_total_lookups);

      // new stat
      if (num_hits <= num_total_lookups)
        varSetFloat("proxy.node.hostdb.hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }
  // Deal with Lifetime stats
  if (varIntFromName("proxy.process.hostdb.total_hits", &hostdbHits) &&
      varIntFromName("proxy.process.hostdb.total_lookups", &hostdbLookups)) {
    if (hostdbLookups == 0) {
      floatSum = 0.0;
    } else {
      floatSum = (MgmtFloat) ((double) hostdbHits / (double) hostdbLookups);
    }
  }

  varSetFloat("proxy.node.hostdb.hit_ratio", floatSum);
}


//////////////////////////////////////////////////////////////////////////////
//
//      Ag_TransactionPercentsAndMeanTimes()
//
//      This function is big, but the function is simple.  It samples
//      the many transaction_counts and transaction_times statistics,
//      computes the delta over at least hrThreshold time units, and
//      uses the delta to compute transaction disposition percentages
//      and mean times over that region.  Because there are so many
//      stats, all handled identically, a table-driven approach is used.
//
//////////////////////////////////////////////////////////////////////////////

void
Ag_TransactionPercentsAndMeanTimes()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds

  // the position in this array is significant, hardcoded, and inter-related
  static StatTwoIntSamples count_table[] = {
    {"proxy.process.http.transaction_counts.hit_fresh", 0, 0, 0, 0},    // 0
    {"proxy.process.http.transaction_counts.hit_revalidated", 0, 0, 0, 0},      // 1
    {"proxy.process.http.transaction_counts.miss_cold", 0, 0, 0, 0},    // 5
    {"proxy.process.http.transaction_counts.miss_changed", 0, 0, 0, 0}, // 6
    {"proxy.process.http.transaction_counts.miss_client_no_cache", 0, 0, 0, 0}, // 8
    {"proxy.process.http.transaction_counts.miss_not_cacheable", 0, 0, 0, 0},   // 7
    {"proxy.process.http.transaction_counts.errors.connect_failed", 0, 0, 0, 0},        // 11
    {"proxy.process.http.transaction_counts.errors.aborts", 0, 0, 0, 0},        // 12
    {"proxy.process.http.transaction_counts.errors.possible_aborts", 0, 0, 0, 0},       // 13
    {"proxy.process.http.transaction_counts.errors.pre_accept_hangups", 0, 0, 0, 0},    // 14
    {"proxy.process.http.transaction_counts.errors.early_hangups", 0, 0, 0, 0}, // 15
    {"proxy.process.http.transaction_counts.errors.empty_hangups", 0, 0, 0, 0}, // 16
    {"proxy.process.http.transaction_counts.errors.other", 0, 0, 0, 0}, // 17
    {"proxy.process.http.transaction_counts.other.unclassified", 0, 0, 0, 0},   // 18
    {NULL, -1, -1, -1, -1}
  };

  // the position in this array is significant and hardcoded
  static StatTwoFloatSamples times_table[] = {
    {"proxy.process.http.transaction_totaltime.hit_fresh", 0, 0, 0, 0}, // 0
    {"proxy.process.http.transaction_totaltime.hit_revalidated", 0, 0, 0, 0},   // 1
    {"proxy.process.http.transaction_totaltime.miss_cold", 0, 0, 0, 0}, // 4
    {"proxy.process.http.transaction_totaltime.miss_changed", 0, 0, 0, 0},      // 5
    {"proxy.process.http.transaction_totaltime.miss_client_no_cache", 0, 0, 0, 0},      // 7
    {"proxy.process.http.transaction_totaltime.miss_not_cacheable", 0, 0, 0, 0},        // 6
    {"proxy.process.http.transaction_totaltime.errors.connect_failed", 0, 0, 0, 0},     // 10
    {"proxy.process.http.transaction_totaltime.errors.aborts", 0, 0, 0, 0},     // 11
    {"proxy.process.http.transaction_totaltime.errors.possible_aborts", 0, 0, 0, 0},    // 12
    {"proxy.process.http.transaction_totaltime.errors.pre_accept_hangups", 0, 0, 0, 0}, // 13
    {"proxy.process.http.transaction_totaltime.errors.early_hangups", 0, 0, 0, 0},      // 14
    {"proxy.process.http.transaction_totaltime.errors.empty_hangups", 0, 0, 0, 0},      // 15
    {"proxy.process.http.transaction_totaltime.errors.other", 0, 0, 0, 0},      // 16
    {"proxy.process.http.transaction_totaltime.other.unclassified", 0, 0, 0, 0},        // 17
    {NULL, -1, -1, -1, -1}
  };

  // the position in this array is significant and hardcoded
  static const char *counts_names[] = {
    "proxy.node.http.transaction_counts_avg_10s.hit_fresh",     // 0
    "proxy.node.http.transaction_counts_avg_10s.hit_revalidated",       // 1
    "proxy.node.http.transaction_counts_avg_10s.miss_cold",     // 4
    "proxy.node.http.transaction_counts_avg_10s.miss_changed",  // 5
    "proxy.node.http.transaction_counts_avg_10s.miss_client_no_cache",  // 7
    "proxy.node.http.transaction_counts_avg_10s.miss_not_cacheable",    // 6
    "proxy.node.http.transaction_counts_avg_10s.errors.connect_failed", // 10
    "proxy.node.http.transaction_counts_avg_10s.errors.aborts", // 11
    "proxy.node.http.transaction_counts_avg_10s.errors.possible_aborts",        // 12
    "proxy.node.http.transaction_counts_avg_10s.errors.pre_accept_hangups",     // 13
    "proxy.node.http.transaction_counts_avg_10s.errors.early_hangups",  // 14
    "proxy.node.http.transaction_counts_avg_10s.errors.empty_hangups",  // 15
    "proxy.node.http.transaction_counts_avg_10s.errors.other",  // 16
    "proxy.node.http.transaction_counts_avg_10s.other.unclassified",    // 17
    NULL
  };

  // the position in this array is significant and hardcoded
  static const char *frac_names[] = {
    "proxy.node.http.transaction_frac_avg_10s.hit_fresh",       // 0
    "proxy.node.http.transaction_frac_avg_10s.hit_revalidated", // 1
    "proxy.node.http.transaction_frac_avg_10s.miss_cold",       // 4
    "proxy.node.http.transaction_frac_avg_10s.miss_changed",    // 5
    "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache",    // 7
    "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable",      // 6
    "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed",   // 10
    "proxy.node.http.transaction_frac_avg_10s.errors.aborts",   // 11
    "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts",  // 12
    "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups",       // 13
    "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups",    // 14
    "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups",    // 15
    "proxy.node.http.transaction_frac_avg_10s.errors.other",    // 16
    "proxy.node.http.transaction_frac_avg_10s.other.unclassified",      // 17
    NULL
  };

  // the position in this array is significant and hardcoded
  static const char *frac_int_names[] = {
    "proxy.node.http.transaction_frac_avg_10s.hit_fresh_int_pct",       // 0
    "proxy.node.http.transaction_frac_avg_10s.hit_revalidated_int_pct", // 1
    "proxy.node.http.transaction_frac_avg_10s.miss_cold_int_pct",       // 4
    "proxy.node.http.transaction_frac_avg_10s.miss_changed_int_pct",    // 5
    "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache_int_pct",    // 7
    "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable_int_pct",      // 6
    "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed_int_pct",   // 10
    "proxy.node.http.transaction_frac_avg_10s.errors.aborts_int_pct",   // 11
    "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts_int_pct",  // 12
    "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups_int_pct",       // 13
    "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups_int_pct",    // 14
    "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups_int_pct",    // 15
    "proxy.node.http.transaction_frac_avg_10s.errors.other_int_pct",    // 16
    "proxy.node.http.transaction_frac_avg_10s.other.unclassified_int_pct",      // 17
    NULL
  };

  // the position in this array is significant and hardcoded
  static const char *avgtime_names[] = {
    "proxy.node.http.transaction_msec_avg_10s.hit_fresh",       // 0
    "proxy.node.http.transaction_msec_avg_10s.hit_revalidated", // 1
    "proxy.node.http.transaction_msec_avg_10s.miss_cold",       // 4
    "proxy.node.http.transaction_msec_avg_10s.miss_changed",    // 5
    "proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache",    // 7
    "proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable",      // 6
    "proxy.node.http.transaction_msec_avg_10s.errors.connect_failed",   // 10
    "proxy.node.http.transaction_msec_avg_10s.errors.aborts",   // 11
    "proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts",  // 12
    "proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups",       // 13
    "proxy.node.http.transaction_msec_avg_10s.errors.early_hangups",    // 14
    "proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups",    // 15
    "proxy.node.http.transaction_msec_avg_10s.errors.other",    // 16
    "proxy.node.http.transaction_msec_avg_10s.other.unclassified",      // 17
    NULL
  };

  ink_hrtime current_time = ink_get_hrtime();

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////

  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    int i;

    ///////////////////
    // scroll values //
    ///////////////////

    for (i = 0; count_table[i].lm_record_name; i++) {
      count_table[i].previous_time = count_table[i].current_time;
      count_table[i].previous_value = count_table[i].current_value;
    }
    for (i = 0; times_table[i].lm_record_name; i++) {
      times_table[i].previous_time = times_table[i].current_time;
      times_table[i].previous_value = times_table[i].current_value;
    }

    //////////////////////////
    // calculate new values //
    //////////////////////////

    for (i = 0; count_table[i].lm_record_name; i++) {
      int status;
      count_table[i].current_value = -10000;
      count_table[i].current_time = ink_get_hrtime();
      status = varIntFromName(count_table[i].lm_record_name, &(count_table[i].current_value));
    }
    for (i = 0; times_table[i].lm_record_name; i++) {
      int status;
      times_table[i].current_value = -10000;
      times_table[i].current_time = ink_get_hrtime();
      status = varFloatFromName(times_table[i].lm_record_name, &(times_table[i].current_value));
    }

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////

    if ((current_time - last_set_time) > window) {
      MgmtInt num_transactions = 0;

      // generate time window deltas and sum
      for (i = 0; counts_names[i]; i++) {
        MgmtInt diff = count_table[i].diff_value();
        varSetInt(counts_names[i], diff);
        num_transactions += diff;
      }

      // generate node percentages
      for (i = 0; frac_names[i]; i++) {
        float frac = 0;
        if (num_transactions > 0)
          frac = (MgmtFloat) ((double) count_table[i].diff_value() / (double) num_transactions);
        varSetFloat(frac_names[i], frac);
        AgFloat_generic_scale_to_int(frac_names[i], frac_int_names[i], PCT_TO_INTPCT_SCALE);
      }

      // generate mean transaction times
      for (i = 0; avgtime_names[i]; i++) {
        MgmtInt msecs = 0;
        if (count_table[i].diff_value() > 0)
          msecs = (MgmtInt) (1000.0 * ((double) times_table[i].diff_value() / (double) count_table[i].diff_value()));
        varSetInt(avgtime_names[i], msecs);
      }
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }
}                               /* End Ag_TransactionPercentsAndMeanTimes */


void
Ag_Throughput()
{
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples node_http_user_agent_total_response_bytes =
    { "proxy.node.http.user_agent_total_response_bytes", 0, 0, 0, 0 };
  static StatTwoIntSamples node_rni_downstream_total_bytes = { "proxy.node.rni.downstream_total_bytes", 0, 0, 0, 0 };
  static const char *node_http_ua_total_response_bytes_name = "proxy.node.http.user_agent_total_response_bytesavg_10s";
  static const char *node_rni_downstream_total_bytes_name = "proxy.node.rni.downstream_total_bytes_avg_10s";
  static ink_hrtime lastThroughputTime = 0;
  static MgmtInt lastBytesThrough = 0;
  // These aren't used.
  //static MgmtInt lastBytesHttpUAThrough;
  //static MgmtInt lastBytesHttpOSThrough;
  //static MgmtInt lastBytesRNIUAThrough; 
  MgmtInt bytesThrough;
  MgmtInt bytesHttpUAThrough;
  MgmtInt bytesHttpOSThrough;
  MgmtInt bytesRNIUAThrough;
  MgmtInt bytesWMTUAThrough = 0;
  MgmtInt bytesQTUAThrough = 0;
  ink_hrtime nowTime = ink_get_hrtime();
  ink_hrtime diffTime = nowTime - lastThroughputTime;
  double tmp;
  MgmtInt intSum;
  MgmtFloat floatSum = -20.0;

  // Avoid warnings, we might want to clear out some of these variables ... /leif.
  NOWARN_UNUSED(node_http_user_agent_total_response_bytes);
  NOWARN_UNUSED(node_rni_downstream_total_bytes);
  NOWARN_UNUSED(node_http_ua_total_response_bytes_name);
  NOWARN_UNUSED(node_rni_downstream_total_bytes_name);

  if (diffTime > window) {
    if (varIntFromName("proxy.node.http.user_agent_total_response_bytes", &bytesHttpUAThrough)
        && varIntFromName("proxy.node.http.origin_server_total_response_bytes", &bytesHttpOSThrough)
        && varIntFromName("proxy.node.rni.downstream_total_bytes", &bytesRNIUAThrough)
#ifdef WMT_STATS_1
        && varIntFromName("proxy.node.wmt.downstream_total_bytes", &bytesWMTUAThrough)
#endif
#ifdef QT_STATS_1
        && varIntFromName("proxy.node.qt.downstream_total_bytes", &bytesQTUAThrough)
#endif
      ) {
      bytesThrough = bytesHttpUAThrough +
        bytesRNIUAThrough + bytesWMTUAThrough + bytesQTUAThrough;
      if (lastThroughputTime != 0 && bytesThrough != 0) {
        if (lastBytesThrough > (bytesHttpUAThrough +
                                bytesRNIUAThrough + bytesWMTUAThrough + bytesQTUAThrough)) {
          // The proxy must have died so just set the value to zero
          intSum = 0;
        } else {

          tmp = (double) (bytesThrough - lastBytesThrough) / diffTime;
          intSum = (MgmtInt) (tmp * HRTIME_SECOND);
          floatSum = (tmp * HRTIME_SECOND * 8) / 1000000.0;
        }
        // old
        varSetInt("proxy.node.http.throughput", intSum);
        // new stat
        varSetFloat("proxy.node.client_throughput_out", floatSum);
      }
      lastThroughputTime = nowTime;
      lastBytesThrough = bytesThrough;
    } else {
      // old stat
      varSetInt("proxy.node.http.throughput", -20);
      // new stats 
      varSetFloat("proxy.node.client_throughput_out", -20.0);
    }
  }
}

void
Ag_DnsLookupsPerSecond()
{
  static ink_hrtime lastDNSLookupTime = 0;
  static MgmtInt lastDNSLookup = 0;
  ink_hrtime nowTime = ink_get_hrtime();
  ink_hrtime diffTime = nowTime - lastDNSLookupTime;

  MgmtInt totalDNSLookups;
  double tmp;
  MgmtFloat floatSum;

  if (diffTime > hrThreshold) {
    if (varIntFromName("proxy.process.dns.total_dns_lookups", &totalDNSLookups)) {
      if (lastDNSLookupTime != 0 && lastDNSLookup != 0) {
        if (lastDNSLookup > totalDNSLookups) {
          // The proxy must have died so just set the value to zero
          floatSum = 0.0;
        } else {
          tmp = (MgmtFloat) ((double) (totalDNSLookups - lastDNSLookup) / (double) diffTime);
          floatSum = (MgmtFloat) (tmp * HRTIME_SECOND);
        }
        varSetFloat("proxy.node.dns.lookups_per_second", floatSum);
      }
      lastDNSLookupTime = nowTime;
      lastDNSLookup = totalDNSLookups;
    }
  }
}

void
Ag_XactsPerSecond()
{
  static ink_hrtime lastXactLookupTime = 0;
  static MgmtInt lastTotalXactLookup = 0;
  static MgmtInt lastHttpXactLookup = 0;
  static MgmtInt lastRniXactLookup = 0;
  static MgmtInt lastWmtXactLookup = 0;
  static MgmtInt lastQtXactLookup = 0;
  ink_hrtime nowTime = ink_get_hrtime();
  ink_hrtime diffTime = nowTime - lastXactLookupTime;
  MgmtInt totalXacts = 0;
  MgmtInt httpXacts = 0;
  MgmtInt rniXacts = 0;
  MgmtInt wmtXacts = 0;
  MgmtInt qtXacts = 0;
  double tmp;
  MgmtFloat totalfloatSum = 0.0;
  MgmtFloat httpSum = 0.0;
  MgmtFloat rniSum = 0.0;
  MgmtFloat wmtSum = 0.0;
  MgmtFloat qtSum = 0.0;

  if (diffTime > hrThreshold) {
    if (varIntFromName("proxy.process.http.incoming_requests", &httpXacts)
        && varIntFromName("proxy.process.rni.downstream_requests", &rniXacts)
        && varIntFromName("proxy.process.wmt.downstream_requests", &wmtXacts)
        && varIntFromName("proxy.process.qt.downstream_requests", &qtXacts)) {
      totalXacts = httpXacts + rniXacts + wmtXacts + qtXacts;
      if (lastXactLookupTime != 0 && lastTotalXactLookup != 0) {
        if (lastTotalXactLookup > totalXacts) {
          // The proxy must have died so just set the value to zero
          totalfloatSum = 0.0;
          httpSum = 0.0;
          rniSum = 0.0;
          wmtSum = 0.0;
          qtSum = 0.0;
        } else {
          tmp = (MgmtFloat) ((double) (totalXacts - lastTotalXactLookup) / diffTime);
          totalfloatSum = (MgmtFloat) (tmp * HRTIME_SECOND);
          tmp = (MgmtFloat) ((double) (httpXacts - lastHttpXactLookup) / diffTime);
          httpSum = (MgmtFloat) (tmp * HRTIME_SECOND);
          tmp = (MgmtFloat) ((double) (rniXacts - lastRniXactLookup) / diffTime);
          rniSum = (MgmtFloat) (tmp * HRTIME_SECOND);
          tmp = (MgmtFloat) ((double) (wmtXacts - lastWmtXactLookup) / diffTime);
          wmtSum = (MgmtFloat) (tmp * HRTIME_SECOND);
          tmp = (MgmtFloat) ((double) (qtXacts - lastQtXactLookup) / diffTime);
          qtSum = (MgmtFloat) (tmp * HRTIME_SECOND);
        }
        varSetFloat("proxy.node.user_agent_xacts_per_second", totalfloatSum);
        varSetFloat("proxy.node.http.user_agent_xacts_per_second", httpSum);
        varSetFloat("proxy.node.rni.user_agent_xacts_per_second", rniSum);
        varSetFloat("proxy.node.wmt.user_agent_xacts_per_second", wmtSum);
        varSetFloat("proxy.node.qt.user_agent_xacts_per_second", qtSum);
      }
      lastXactLookupTime = nowTime;
      lastTotalXactLookup = totalXacts;
      lastHttpXactLookup = httpXacts;
      lastRniXactLookup = rniXacts;
      lastWmtXactLookup = wmtXacts;
      lastQtXactLookup = qtXacts;
    }
  }
}

//
// Aggregate total documents served for HTTP and RNI(?)
//
void
Ag_TotalDocumentsServed()
{
  MgmtInt http_docs = 0;
  MgmtInt rni_docs = 0;
  MgmtInt wmt_docs = 0;
  MgmtInt qt_docs = 0;
  MgmtInt total_docs = 0;

  if (varIntFromName("proxy.node.http.user_agents_total_documents_served", &http_docs)
      && varIntFromName("proxy.node.rni.user_agents_total_documents_served", &rni_docs)
      && varIntFromName("proxy.node.wmt.user_agents_total_documents_served", &wmt_docs)
      && varIntFromName("proxy.node.qt.user_agents_total_documents_served", &qt_docs)) {
    total_docs = http_docs + rni_docs + wmt_docs + qt_docs;
    varSetInt("proxy.node.user_agents_total_documents_served", total_docs);
  } else {
    varSetInt("proxy.node.user_agents_total_documents_served", -20);
  }

}


//
// Aggregate client/server connections for HTTP and RNI(?)
//
void
Ag_Connections()
{
  MgmtInt http_ua_client_conn = 0;
  MgmtInt http_os_server_conn = 0;
  MgmtInt http_pp_server_conn = 0;
  MgmtInt http_cache_conn = 0;
  MgmtInt rni_client_conn = 0;
  MgmtInt rni_server_conn = 0;
  MgmtInt rni_cache_conn = 0;
  MgmtInt wmt_client_conn = 0;
  MgmtInt wmt_server_conn = 0;
  MgmtInt wmt_cache_conn = 0;
  MgmtInt qt_client_conn = 0;
  MgmtInt qt_server_conn = 0;
  MgmtInt qt_cache_conn = 0;
  MgmtInt cache_conn = 0;
  MgmtInt client_conn = 0;
  MgmtInt server_conn = 0;

  if (varIntFromName("proxy.node.http.user_agent_current_connections_count", &http_ua_client_conn)
      && varIntFromName("proxy.node.http.origin_server_current_connections_count", &http_os_server_conn)
      && varIntFromName("proxy.node.http.current_parent_proxy_connections", &http_pp_server_conn)
      && varIntFromName("proxy.node.http.cache_current_connections_count", &http_cache_conn)
      && varIntFromName("proxy.node.rni.current_cache_connections", &rni_cache_conn)
      && varIntFromName("proxy.node.rni.current_client_connections", &rni_client_conn)
      && varIntFromName("proxy.node.rni.current_server_connections", &rni_server_conn)
      && varIntFromName("proxy.node.wmt.current_client_connections", &wmt_client_conn)
      && varIntFromName("proxy.node.wmt.current_server_connections", &wmt_server_conn)
      && varIntFromName("proxy.node.wmt.current_cache_connections", &wmt_cache_conn)
      && varIntFromName("proxy.node.qt.current_client_connections", &qt_client_conn)
      && varIntFromName("proxy.node.qt.current_server_connections", &qt_server_conn)
      && varIntFromName("proxy.node.qt.current_cache_connections", &qt_cache_conn)

    ) {
    client_conn = http_ua_client_conn +
      rni_client_conn + wmt_client_conn + qt_client_conn;
    server_conn = http_os_server_conn + http_pp_server_conn +
      rni_server_conn + wmt_server_conn + qt_server_conn;
    cache_conn = http_cache_conn + rni_cache_conn + wmt_cache_conn + qt_cache_conn;
    varSetInt("proxy.node.current_client_connections", client_conn);
    varSetInt("proxy.node.current_server_connections", server_conn);
    varSetInt("proxy.node.current_cache_connections", cache_conn);
  } else {
    varSetInt("proxy.node.current_client_connections", -20);
    varSetInt("proxy.node.current_server_connections", -20);
    varSetInt("proxy.node.current_cache_connections", -20);
  }

}

//
// Calculate Node Bandwidth ratio i.e. bandwidth savings
//
// FIXME: Should reflext HTTP and maybe RNI(?)
//        Currently only reflects HTTP
//
// NOTE: 8/21/98 (Bug INKqa03094)
//       A special scenario is considered in this code where during fresh 
//       cache start and no documents are in the cache, the number of bytes sent
//       to the Origin servers can exceed the number of bytes sent to
//       the client where by negative bandwidth savings are reported.
//       Because of this, it has been decided not to report the negative
//       bandwidth savings and just report ZERO instead.
//
void
Ag_Bytes()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples node_ua_total_bytes = { "proxy.node.user_agent_total_bytes", 0, 0, 0, 0 };
  static StatTwoIntSamples node_os_total_bytes = { "proxy.node.origin_server_total_bytes", 0, 0, 0, 0 };
  static const char *node_ua_total_bytes_name = "proxy.node.user_agent_total_bytes_avg_10s";
  static const char *node_os_total_bytes_name = "proxy.node.origin_server_total_bytes_avg_10s";
  int status;
  MgmtFloat hitRate;

  MgmtInt h, b;
  MgmtInt UA_bytes;             // User Agent
  MgmtInt OS_bytes;             // Origin Server
  MgmtInt PP_bytes;             // Parent Proxy(?)
  MgmtFloat bandwidthHitRate;
  MgmtInt cacheOn = 1;          // on by default
  MgmtInt httpCacheOn;
  bool ok = true;

  // See if cache is on
  ink_assert(varIntFromName("proxy.config.http.cache.http", &httpCacheOn));
  cacheOn = httpCacheOn;

  /////////////////////////////////////////////////////////////
  // add up the downstream (client <-> proxy) traffic volume //
  /////////////////////////////////////////////////////////////
  UA_bytes = 0;

  if (varIntFromName("proxy.process.http.user_agent_request_document_total_size", &b) &&
      varIntFromName("proxy.process.http.user_agent_request_header_total_size", &h)) {
    UA_bytes += h + b;
    varSetInt("proxy.node.http.user_agent_total_request_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.http.user_agent_total_request_bytes", -20);
  }

  if (varIntFromName("proxy.process.http.user_agent_response_document_total_size", &b) &&
      varIntFromName("proxy.process.http.user_agent_response_header_total_size", &h)) {
    UA_bytes += h + b;
    varSetInt("proxy.node.http.user_agent_total_response_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.http.user_agent_total_response_bytes", -20);
  }

  // Add RNI User Agent request/response bytes
  if (varIntFromName("proxy.process.rni.downstream.request_bytes", &b) &&
      varIntFromName("proxy.process.rni.downstream.response_bytes", &h)) {
    UA_bytes += h + b;
    varSetInt("proxy.node.rni.downstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.rni.downstream_total_bytes", -20);
  }

#ifdef WMT_STATS_1
  // Add WMT User Agent request/response bytes (proxy<->origin server)
  if (varIntFromName("proxy.process.wmt.downstream.request_bytes", &b) &&
      varIntFromName("proxy.process.wmt.downstream.response_bytes", &h)) {
    UA_bytes += h + b;
    varSetInt("proxy.node.wmt.downstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.wmt.downstream_total_bytes", -20);
  }
#endif

#ifdef QT_STATS_1
  // Add QT User Agent request/response bytes (proxy<->origin server)
  if (varIntFromName("proxy.process.qt.downstream.request_bytes", &b) &&
      varIntFromName("proxy.process.qt.downstream.response_bytes", &h)) {
    UA_bytes += h + b;
    varSetInt("proxy.node.qt.downstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.qt.downstream_total_bytes", -20);
  }
#endif

  //////////////////////////////////////////////////////////////////
  // add up the upstream (proxy <-> server/parent) traffic volume //
  //////////////////////////////////////////////////////////////////
  OS_bytes = 0;

  if (varIntFromName("proxy.process.http.origin_server_request_document_total_size", &b) &&
      varIntFromName("proxy.process.http.origin_server_request_header_total_size", &h)) {
    OS_bytes += h + b;
    varSetInt("proxy.node.http.origin_server_total_request_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.http.origin_server_total_request_bytes", -20);
  }

  if (varIntFromName("proxy.process.http.origin_server_response_document_total_size", &b) &&
      varIntFromName("proxy.process.http.origin_server_response_header_total_size", &h)) {
    OS_bytes += h + b;
    varSetInt("proxy.node.http.origin_server_total_response_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.http.origin_server_total_response_bytes", -20);
  }

  // Add RNI origin server request/response bytes
  if (varIntFromName("proxy.process.rni.upstream.request_bytes", &b) &&
      varIntFromName("proxy.process.rni.upstream.response_bytes", &h)) {
    OS_bytes += h + b;
    varSetInt("proxy.node.rni.upstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.rni.upstream_total_bytes", -20);
  }

#ifdef WMT_STATS_1
  // Add WMT origin server request/response bytes
  if (varIntFromName("proxy.process.wmt.upstream.request_bytes", &b) &&
      varIntFromName("proxy.process.wmt.upstream.response_bytes", &h)) {
    OS_bytes += h + b;
    varSetInt("proxy.node.wmt.upstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.wmt.upstream_total_bytes", -20);
  }
#endif

#ifdef QT_STATS_1
  // Add QT origin server request/response bytes
  if (varIntFromName("proxy.process.qt.upstream.request_bytes", &b) &&
      varIntFromName("proxy.process.qt.upstream.response_bytes", &h)) {
    OS_bytes += h + b;
    varSetInt("proxy.node.qt.upstream_total_bytes", h + b);
  } else {
    ok = false;
    varSetInt("proxy.node.qt.upstream_total_bytes", -20);
  }
#endif

  // Parent Proxy bytes?
  PP_bytes = 0;

  // Set Node UA/OS total bytes
  varSetInt("proxy.node.user_agent_total_bytes", UA_bytes);
  varSetInt("proxy.node.origin_server_total_bytes", OS_bytes);

  ////////////////////////////////////////////////////////////
  // now compute the bandwidth savings:                     //
  //                                                        //
  // savings = (server_bytes / client_bytes)                //
  //         = (client_bytes - server_bytes) / client_bytes //
  //                                                        //
  ////////////////////////////////////////////////////////////
  bool setBW = true;
  if (ok == true) {
    if (UA_bytes > 0 && cacheOn) {
      bandwidthHitRate = ((double) UA_bytes - ((double) OS_bytes + (double) PP_bytes)) / (double) UA_bytes;
      if (bandwidthHitRate < 0.0)
        setBW = false;          // negative bandwidth scenario...
    } else {
      bandwidthHitRate = 0.0;
    }
  } else {
    bandwidthHitRate = -20.0;
  }

  if (setBW) {
    // old stat
    varSetFloat("proxy.node.http.bandwidth_hit_ratio", bandwidthHitRate);

    // new stat
    varSetFloat("proxy.node.bandwidth_hit_ratio", bandwidthHitRate);
  }

  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ////////////////////////////////////////
    // scroll values for node UA/OS bytes //
    ///////////////////////////////////////
    node_ua_total_bytes.previous_time = node_ua_total_bytes.current_time;
    node_ua_total_bytes.previous_value = node_ua_total_bytes.current_value;

    node_os_total_bytes.previous_time = node_os_total_bytes.current_time;
    node_os_total_bytes.previous_value = node_os_total_bytes.current_value;

    //////////////////////////
    // calculate new values //
    //////////////////////////
    node_ua_total_bytes.current_value = -10000;
    node_ua_total_bytes.current_time = ink_get_hrtime();
    status = varIntFromName(node_ua_total_bytes.lm_record_name, &(node_ua_total_bytes.current_value));

    node_os_total_bytes.current_value = -10000;
    node_os_total_bytes.current_time = ink_get_hrtime();
    status = varIntFromName(node_os_total_bytes.lm_record_name, &(node_os_total_bytes.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      MgmtInt num_ua_total = 0;
      MgmtInt num_os_total = 0;
      MgmtInt diff = 0;

      // generate time window deltas and sum
      diff = node_ua_total_bytes.diff_value();
      varSetInt(node_ua_total_bytes_name, diff);
      num_ua_total = diff;

      diff = node_os_total_bytes.diff_value();
      varSetInt(node_os_total_bytes_name, diff);
      num_os_total = diff;

      if (num_ua_total == 0 || (num_ua_total < num_os_total))
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) (((double) num_ua_total - (double) num_os_total) / (double) num_ua_total);

      // new stat
      varSetFloat("proxy.node.bandwidth_hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }
}                               // end Ag_Bytes()

// void aggregateNodeRecords()
//
//  Updates node records from process records.  There is a probably
//    a good argument that this should be table based some how instead
//    of a giant function.  I didn't know how many special case entries
//    we'll need so I made it a long function
//
void
aggregateNodeRecords()
{
  // HTTP
  MgmtInt tmp1;

  /* NOTE: the following two stats are redunant and one of them should be remove */
  AgInt_generic("proxy.process.http.incoming_requests", "proxy.node.http.user_agents_total_documents_served");

  AgInt_generic("proxy.process.http.incoming_requests", "proxy.node.http.user_agents_total_transactions_count");

  AgInt_generic("proxy.process.http.outgoing_requests", "proxy.node.http.origin_server_total_transactions_count");

  AgInt_generic("proxy.process.http.current_cache_connections", "proxy.node.http.cache_current_connections_count");

  AgInt_generic("proxy.process.http.current_client_connections",
                "proxy.node.http.user_agent_current_connections_count");

  if (varIntFromName("proxy.process.http.current_server_connections", &tmp1)) {
    varSetInt("proxy.node.http.origin_server_current_connections_count", tmp1);
  } else {
    varSetInt("proxy.node.http.origin_server_current_connections_count", -20);
  }
  AgInt_generic("proxy.process.http.current_parent_proxy_connections",
                "proxy.node.http.current_parent_proxy_connections");

  // RNI
  AgInt_generic("proxy.process.rni.downstream_requests", "proxy.node.rni.user_agents_total_documents_served");
  AgInt_generic("proxy.process.rni.current_client_connections", "proxy.node.rni.current_client_connections");
  AgInt_generic("proxy.process.rni.current_server_connections", "proxy.node.rni.current_server_connections");
  AgInt_generic("proxy.process.rni.current_cache_connections", "proxy.node.rni.current_cache_connections");

  // WMT
  AgInt_generic("proxy.process.wmt.downstream_requests", "proxy.node.wmt.user_agents_total_documents_served");
  AgInt_generic("proxy.process.wmt.current_client_connections", "proxy.node.wmt.current_client_connections");
  AgInt_generic("proxy.process.wmt.current_server_connections", "proxy.node.wmt.current_server_connections");
  AgInt_generic("proxy.process.wmt.current_cache_connections", "proxy.node.wmt.current_cache_connections");

  // QT
  AgInt_generic("proxy.process.qt.downstream_requests", "proxy.node.qt.user_agents_total_documents_served");
  AgInt_generic("proxy.process.qt.current_client_connections", "proxy.node.qt.current_client_connections");
  AgInt_generic("proxy.process.qt.current_server_connections", "proxy.node.qt.current_server_connections");
  AgInt_generic("proxy.process.qt.current_cache_connections", "proxy.node.qt.current_cache_connections");

  // Cache
  AgInt_generic("proxy.process.cache.bytes_total", "proxy.node.cache.bytes_total");

  // DNS
  AgInt_generic("proxy.process.dns.total_dns_lookups", "proxy.node.dns.total_dns_lookups");

  // HostDB
  AgInt_generic("proxy.process.hostdb.total_lookups", "proxy.node.hostdb.total_lookups");

  AgInt_generic("proxy.process.hostdb.total_hits", "proxy.node.hostdb.total_hits");

  // Cluster
  AgInt_generic("proxy.process.cluster.nodes", "proxy.node.cluster.nodes");

  // Transactions per second
  Ag_XactsPerSecond();

  // Find total docs served to user agents
  Ag_TotalDocumentsServed();

  // Dervive The Number of DNS lookups per second
  Ag_DnsLookupsPerSecond();

  // Dervive the HostDB hit rate
  Ag_HostdbHitRate();

  Ag_Bytes();                   // call before Ag_Throughput()

  // Derive throughput
  Ag_Throughput();

  // Dervive Cache Hit Rate
  Ag_cacheHits();

  // Derive Percent Free
  Ag_cachePercent();

  // Derive Transaction Distribution & Mean Times
  Ag_TransactionPercentsAndMeanTimes();

  // Overall
  Ag_Connections();

  // These are for the benefit of SNMP so that we don't have to use
  // 64 bit integers or floating point encodings.
  AgFloat_generic_scale_to_int("proxy.node.client_throughput_out",
                               "proxy.node.client_throughput_out_kbit", MBIT_TO_KBIT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.http.cache_hit_ratio",
                               "proxy.node.http.cache_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.cache_hit_ratio", "proxy.node.cache_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.http.bandwidth_hit_ratio",
                               "proxy.node.http.bandwidth_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.bandwidth_hit_ratio",
                               "proxy.node.bandwidth_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.bandwidth_hit_ratio",
                               "proxy.cluster.bandwidth_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.cache.percent_free",
                               "proxy.node.cache.percent_free_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.node.hostdb.hit_ratio",
                               "proxy.node.hostdb.hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgInt_generic_scale("proxy.process.cache.bytes_used", "proxy.process.cache.bytes_used_mb", BYTES_TO_MB_SCALE);
  AgInt_generic_scale("proxy.process.cache.bytes_total", "proxy.node.cache.bytes_total_mb", BYTES_TO_MB_SCALE);
  AgInt_generic_scale("proxy.node.cache.bytes_free", "proxy.node.cache.bytes_free_mb", BYTES_TO_MB_SCALE);
  AgInt_generic_scale("proxy.cluster.cache.bytes_free", "proxy.cluster.cache.bytes_free_mb", BYTES_TO_MB_SCALE);
}
