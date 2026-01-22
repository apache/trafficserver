/*
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

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>

#include "ts/ts.h"
#include "tscore/Layout.h"
#include "iocore/eventsystem/RecProcess.h"

class RulesConfig;
class RuleSet;
enum ResourceIDs : unsigned;

#include "comparator.h"

static std::once_flag init_flag;

static void
initialize_hrw_subsystems()
{
  Layout::create();
  RecProcessInit();
}

// Factory functions implemented in header_rewrite.cc
extern "C" {
RulesConfig *create_rules_config(int timezone, int inboundIpSource);
void         destroy_rules_config(RulesConfig *conf);
bool     rules_config_parse(RulesConfig *conf, const char *fname, int default_hook, char *from_url, char *to_url, bool force_hrw4u);
RuleSet *rules_config_get_rule(RulesConfig *conf, int hook);
}

static ConfigComparison::ParseStats
collect_all_stats(RulesConfig *config, ConfigComparison::ConfigComparator &comparator, bool is_hrw4u = false)
{
  ConfigComparison::ParseStats stats;
  stats.is_hrw4u = is_hrw4u;

  for (int hook = TS_HTTP_READ_REQUEST_HDR_HOOK; hook <= TS_HTTP_LAST_HOOK; ++hook) {
    RuleSet *rs = rules_config_get_rule(config, hook);

    if (rs) {
      ConfigComparison::ParseStats hook_stats = comparator.collect_stats(rs);

      stats.rulesets   += hook_stats.rulesets;
      stats.conditions += hook_stats.conditions;
      stats.operators  += hook_stats.operators;
      stats.hooks.insert(hook_stats.hooks.begin(), hook_stats.hooks.end());
    }
  }

  return stats;
}

// Result codes for compare_pair
enum CompareResult { COMPARE_MATCH = 0, COMPARE_DIFFER = 1, COMPARE_ERROR = 2 };

static CompareResult
compare_pair(const char *hrw_file, const char *hrw4u_file, bool debug, bool quiet, bool profile)
{
  using Clock  = std::chrono::high_resolution_clock;
  auto t_start = Clock::now();

  if (!quiet) {
    std::cout << "Header Rewrite Configuration Comparison Tool\n";
    std::cout << "============================================\n\n";
    std::cout << "Parsing hrw config: " << hrw_file << " (using text parser)\n";
  }

  auto         t_hrw_start = Clock::now();
  RulesConfig *hrw_config  = create_rules_config(0, 0);

  if (!rules_config_parse(hrw_config, hrw_file, TS_HTTP_LAST_HOOK, nullptr, nullptr, false)) {
    std::cerr << "ERROR: Failed to parse hrw config file: " << hrw_file << "\n";
    destroy_rules_config(hrw_config);

    return COMPARE_ERROR;
  }

  auto         t_hrw_end     = Clock::now();
  auto         t_hrw4u_start = Clock::now();
  RulesConfig *hrw4u_config  = create_rules_config(0, 0);

  if (!quiet) {
    std::cout << "Parsing hrw4u config: " << hrw4u_file << " (using native hrw4u parser)\n";
  }

  if (!rules_config_parse(hrw4u_config, hrw4u_file, TS_HTTP_LAST_HOOK, nullptr, nullptr, true)) {
    std::cerr << "ERROR: Failed to parse hrw4u config file: " << hrw4u_file << "\n";
    destroy_rules_config(hrw_config);
    destroy_rules_config(hrw4u_config);
    return COMPARE_ERROR;
  }

  auto t_hrw4u_end = Clock::now();

  if (!quiet) {
    std::cout << "\n";
  }

  auto                               t_compare_start = Clock::now();
  ConfigComparison::ConfigComparator comparator;
  bool                               all_hooks_match = true;
  int                                hooks_compared  = 0;

  comparator.set_debug(debug);

  if (debug) {
    std::cout << "DEBUG: Scanning all hooks for rules...\n";
  }

  for (int hook = TS_HTTP_READ_REQUEST_HDR_HOOK; hook <= TS_HTTP_LAST_HOOK; ++hook) {
    auto        hook_id = static_cast<TSHttpHookID>(hook);
    RuleSet    *rs1     = rules_config_get_rule(hrw_config, hook);
    RuleSet    *rs2     = rules_config_get_rule(hrw4u_config, hook);
    const char *name    = TSHttpHookNameLookup(hook_id);

    if (debug) {
      if (rs1 || rs2) {
        std::cout << "DEBUG: Hook " << name << " (" << hook << "): hrw=" << (rs1 ? "HAS_RULES" : "empty")
                  << ", hrw4u=" << (rs2 ? "HAS_RULES" : "empty") << "\n";
      }
    }

    if (!rs1 && !rs2) {
      continue;
    }

    hooks_compared++;

    if (!quiet) {
      std::cout << "Comparing hook: " << name << "\n";
    }

    if (!comparator.compare_rulesets_for_hook(rs1, rs2, hook_id)) {
      all_hooks_match = false;
    } else if (!quiet) {
      std::cout << "  ✓ PASSED\n";
    }
  }

  auto t_compare_end = Clock::now();

  if (!quiet) {
    std::cout << "\n";
    std::cout << "Collecting parse statistics...\n";

    ConfigComparison::ParseStats hrw_stats   = collect_all_stats(hrw_config, comparator, false);
    ConfigComparison::ParseStats hrw4u_stats = collect_all_stats(hrw4u_config, comparator, true);

    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "Comparison Summary\n";
    std::cout << "============================================\n";
    std::cout << "Files compared:\n";
    std::cout << "  hrw (legacy):  " << hrw_file << "\n";
    std::cout << "  hrw4u (new):   " << hrw4u_file << "\n";
    std::cout << "\n";
    std::cout << "Parse Statistics:\n";
    std::cout << "  hrw config:\n";
    std::cout << "    Rulesets: " << hrw_stats.rulesets << "\n";
    std::cout << "    Total conditions: " << hrw_stats.conditions << " (includes nested)\n";
    std::cout << "    Total operators: " << hrw_stats.operators << "\n";
    std::cout << "    Hooks: " << hrw_stats.format_hooks() << "\n";
    std::cout << "  hrw4u config:\n";
    std::cout << "    Rulesets: " << hrw4u_stats.rulesets << "\n";
    std::cout << "    Total conditions: " << hrw4u_stats.conditions << " (includes nested)\n";
    std::cout << "    Total operators: " << hrw4u_stats.operators << "\n";
    std::cout << "    Sections: " << hrw4u_stats.format_hooks() << "\n";
    std::cout << "\n";
    std::cout << "Hooks compared: " << hooks_compared << "\n";

    const auto &result = comparator.get_result();

    if (all_hooks_match) {
      std::cout << "\n✓ SUCCESS: Configurations are equivalent\n";
    } else {
      std::cout << "\n✗ FAILURE: Configurations differ\n";
      std::cout << "\nTotal differences: " << result.differences.size() << "\n";
    }
  }

  destroy_rules_config(hrw_config);
  destroy_rules_config(hrw4u_config);

  auto t_end = Clock::now();

  if (profile) {
    auto hrw_us     = std::chrono::duration_cast<std::chrono::microseconds>(t_hrw_end - t_hrw_start).count();
    auto hrw4u_us   = std::chrono::duration_cast<std::chrono::microseconds>(t_hrw4u_end - t_hrw4u_start).count();
    auto compare_us = std::chrono::duration_cast<std::chrono::microseconds>(t_compare_end - t_compare_start).count();
    auto total_us   = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

    std::cerr << "PROFILE: hrw_parse=" << hrw_us << "us hrw4u_parse=" << hrw4u_us << "us compare=" << compare_us
              << "us total=" << total_us << "us\n";
  }

  return all_hooks_match ? COMPARE_MATCH : COMPARE_DIFFER;
}

static void
usage(const char *progname)
{
  std::cerr << "Usage: " << progname << " [--debug] [--quiet] [--profile] <hrw_config_file> <hrw4u_config_file>\n";
  std::cerr << "       " << progname << " --batch [--quiet] [--profile] < pairs.txt\n";
  std::cerr << "\n";
  std::cerr << "Compare header_rewrite configurations in hrw (.config) and hrw4u (.hrw4u) formats.\n";
  std::cerr << "Both files should produce equivalent runtime behavior.\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  --debug    Show detailed parsing and comparison information\n";
  std::cerr << "  --quiet    Minimal output (for batch mode), only show failures\n";
  std::cerr << "  --batch    Read file pairs from stdin (one pair per line: hrw_file hrw4u_file)\n";
  std::cerr << "  --profile  Show timing breakdown for each comparison\n";
  std::cerr << "\n";
  std::cerr << "Exit codes:\n";
  std::cerr << "  0 - Configurations are equivalent (all pairs in batch mode)\n";
  std::cerr << "  1 - Configurations differ (any pair in batch mode)\n";
  std::cerr << "  2 - Error (parse failure, file not found, etc.)\n";
}

int
main(int argc, char *argv[])
{
  std::call_once(init_flag, initialize_hrw_subsystems);

  bool                      debug   = false;
  bool                      quiet   = false;
  bool                      batch   = false;
  bool                      profile = false;
  std::vector<const char *> files;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--debug") == 0) {
      debug = true;
    } else if (strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
    } else if (strcmp(argv[i], "--batch") == 0) {
      batch = true;
    } else if (strcmp(argv[i], "--profile") == 0) {
      profile = true;
    } else if (argv[i][0] == '-') {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      usage(argv[0]);
      return 2;
    } else {
      files.push_back(argv[i]);
    }
  }

  if (batch) {
    std::string line;
    int         total = 0, passed = 0, failed = 0, errors = 0;

    while (std::getline(std::cin, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }

      std::istringstream iss(line);
      std::string        hrw_file, hrw4u_file;

      if (!(iss >> hrw_file >> hrw4u_file)) {
        std::cerr << "ERROR: Invalid line format (expected: hrw_file hrw4u_file): " << line << "\n";
        errors++;
        continue;
      }
      total++;

      CompareResult result = compare_pair(hrw_file.c_str(), hrw4u_file.c_str(), debug, quiet, profile);

      switch (result) {
      case COMPARE_MATCH:
        passed++;
        if (!quiet) {
          std::cout << "PASS: " << hrw_file << " <-> " << hrw4u_file << "\n";
        }
        break;
      case COMPARE_DIFFER:
        failed++;
        std::cout << "FAIL: " << hrw_file << " <-> " << hrw4u_file << "\n";
        break;
      case COMPARE_ERROR:
        errors++;
        break;
      }
    }

    if (!quiet || failed > 0 || errors > 0) {
      std::cout << "\nBatch Summary: " << total << " total, " << passed << " passed, " << failed << " failed, " << errors
                << " errors\n";
    }

    if (errors > 0) {
      return 2;
    }
    return (failed > 0) ? 1 : 0;
  }

  if (files.size() != 2) {
    usage(argv[0]);
    return 2;
  }

  return compare_pair(files[0], files[1], debug, quiet, profile);
}
