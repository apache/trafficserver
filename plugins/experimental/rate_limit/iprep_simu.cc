
/** @file

  Simulator application, for testing the behavior of the SieveLRU. This does
  not build as part of the system, but put here for future testing etc.

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
#include <getopt.h>

#include <vector>
#include <fstream>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstdint>

// Yeh well, sue me, boost is useful here, and this is not part of the actual core code
#include <boost/algorithm/string.hpp>

#include "ip_reputation.h"

// Convenience class declarations
using IpMap  = std::unordered_map<IpReputation::KeyClass, std::tuple<int, bool>>; //  count / false = good, true = bad
using IpList = std::vector<IpMap::iterator>;

// Holds all command line options
struct CmdConfigs {
  uint32_t start_buckets, end_buckets, incr_buckets;
  uint32_t start_size, end_size, incr_size;
  uint32_t start_threshold, end_threshold, incr_threshold;
  uint32_t start_permablock, end_permablock, incr_permablock;
};

///////////////////////////////////////////////////////////////////////////////
// Command line options / parsing, returns the parsed and populate CmdConfig
// structure (from above).
//
std::tuple<int32_t, int32_t, int32_t>
splitArg(std::string str)
{
  int32_t start = 0, end = 0, incr = 1;
  std::vector<std::string> results;

  boost::split(results, str, [](char c) { return c == '-' || c == '/'; });

  if (results.size() > 0) {
    start = std::stoi(results[0]);
    if (results.size() > 1) {
      end = std::stoi(results[1]);
      if (results.size() > 2) {
        incr = std::stoi(results[2]);
      }
    } else {
      end = start;
    }
  } else {
    std::cerr << "Malformed argument: " << str << std::endl;
  }

  return {start, end, incr};
}

CmdConfigs
parseArgs(int argc, char **argv)
{
  CmdConfigs options;
  int c;
  constexpr struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},       {"buckets", required_argument, NULL, 'b'},   {"perma", required_argument, NULL, 'p'},
    {"size", required_argument, NULL, 's'}, {"threshold", required_argument, NULL, 't'}, {NULL, 0, NULL, 0}};

  // Make sure the optional values have been set

  options.start_permablock = 0;
  options.end_permablock   = 0;
  options.incr_permablock  = 1;

  while (1) {
    int ix = 0;

    c = getopt_long(argc, argv, "b:f:p:s:t:h?", long_options, &ix);
    if (c == -1)
      break;

    switch (c) {
    case 'h':
    case '?':
      std::cerr << "usage: iprep_simu -b|--buckets <size>[-<end bucket range>[/<increment>]]" << std::endl;
      std::cerr << "                  -s|--size <bucket size>[-<end bucket size range>[/<increment>]]" << std::endl;
      std::cerr << "                  -t|--threshold <bucket num>[-<end bucket num range>[/<increment>]]" << std::endl;
      std::cerr << "                  [-p|--perma <permablock>[-<end permablock range>[/<increment>]]]" << std::endl;
      std::cerr << "                 [-h|--help" << std::endl;
      exit(0);
      break;
    case 'b':
      std::tie(options.start_buckets, options.end_buckets, options.incr_buckets) = splitArg(optarg);
      break;
    case 's':
      std::tie(options.start_size, options.end_size, options.incr_size) = splitArg(optarg);
      break;
    case 'p':
      std::tie(options.start_permablock, options.end_permablock, options.incr_permablock) = splitArg(optarg);
      break;
    case 't':
      std::tie(options.start_threshold, options.end_threshold, options.incr_threshold) = splitArg(optarg);
      break;
    default:
      fprintf(stderr, "getopt returned weird stuff: 0%o\n", c);
      exit(-1);
      break;
    }
  }

  return options; // RVO
}

///////////////////////////////////////////////////////////////////////////////
// Load a configuration file, and populate the two structures with the
// list of IPs (and their status) as well as the full sequence of requests.
//
// Returns a tuple with the number of good requests and bad requests, respectively.
//
std::tuple<uint32_t, uint32_t>
loadFile(std::string fname, IpMap &all_ips, IpList &ips)
{
  std::ifstream infile(fname);

  float timestamp; // The timestamp from the request(relative)
  std::string ip;  // The IP
  bool status;     // Bad (false) or Good (true) request?

  uint32_t good_ips      = 0;
  uint32_t bad_ips       = 0;
  uint32_t good_requests = 0;
  uint32_t bad_requests  = 0;

  // Load in the entire file, and fill the request vector as well as the IP lookup table (state)
  while (infile >> timestamp >> ip >> status) {
    auto ip_hash = IpReputation::SieveLru::hasher(ip, ip.find(':') != std::string::npos ? AF_INET6 : AF_INET);
    auto it      = all_ips.find(ip_hash);

    if (!status) {
      ++good_requests;
    } else {
      ++bad_requests;
    }

    if (all_ips.end() != it) {
      auto &[key, data]       = *it;
      auto &[count, d_status] = data;

      ++count;
      ips.push_back(it);
    } else {
      all_ips[ip_hash] = {0, status};
      ips.push_back(all_ips.find(ip_hash));
      if (!status) {
        ++good_ips;
      } else {
        ++bad_ips;
      }
    }
  }

  std::cout << std::setprecision(3);
  std::cout << "Total number of requests: " << ips.size() << std::endl;
  std::cout << "\tGood requests: " << good_requests << " (" << 100.0 * good_requests / ips.size() << "%)" << std::endl;
  std::cout << "\tBad requests: " << bad_requests << " (" << 100.0 * bad_requests / ips.size() << "%)" << std::endl;
  std::cout << "Unique IPs in set: " << all_ips.size() << std::endl;
  std::cout << "\tGood IPs: " << good_ips << " (" << 100.0 * good_ips / all_ips.size() << "%)" << std::endl;
  std::cout << "\tBad IPs: " << bad_ips << " (" << 100.0 * bad_ips / all_ips.size() << "%)" << std::endl;
  std::cout << std::endl;

  return {good_requests, bad_requests};
}

int
main(int argc, char *argv[])
{
  auto options = parseArgs(argc, argv);

  // All remaining arguments should be files, so lets process them one by one
  for (int file_num = optind; file_num < argc; ++file_num) {
    IpMap all_ips;
    IpList ips;

    // Load the data from file
    auto [good_requests, bad_requests] = loadFile(argv[file_num], all_ips, ips);

    // Here starts the actual simulation, loop through variations
    for (uint32_t size = options.start_size; size <= options.end_size; size += options.incr_size) {
      for (uint32_t buckets = options.start_buckets; buckets <= options.end_buckets; buckets += options.incr_buckets) {
        for (uint32_t threshold = options.start_threshold; threshold <= options.end_threshold;
             threshold += options.incr_threshold) {
          for (uint32_t permablock = options.start_permablock; permablock <= options.end_permablock;
               permablock += options.incr_permablock) {
            // Setup the buckets and metrics for this loop
            IpReputation::SieveLru ipt(buckets, size);

            auto start = std::chrono::system_clock::now();

            // Some metrics
            uint32_t good_blocked      = 0;
            uint32_t good_allowed      = 0;
            uint32_t bad_blocked       = 0;
            uint32_t bad_allowed       = 0;
            uint32_t good_perm_blocked = 0;
            uint32_t bad_perm_blocked  = 0;

            for (auto iter : ips) {
              auto &[ip, data]       = *iter;
              auto &[count, status]  = data;
              auto [bucket, cur_cnt] = ipt.increment(ip);

              // Currently we only allow perma-blocking on items in bucket 1, so check for that first.
              if (cur_cnt > permablock && bucket == ipt.lastBucket()) {
                bucket = ipt.block(ip);
              }

              if (bucket == ipt.blockBucket()) {
                if (!status) {
                  ++good_perm_blocked;
                } else {
                  ++bad_perm_blocked;
                }
              } else if (bucket <= threshold) {
                if (!status) {
                  ++good_blocked;
                } else {
                  ++bad_blocked;
                }
              } else {
                if (!status) {
                  ++good_allowed;
                } else {
                  ++bad_allowed;
                }
              }
            }

            auto end = std::chrono::system_clock::now();

            uint32_t total_blocked      = bad_blocked + good_blocked;
            uint32_t total_perm_blocked = bad_perm_blocked + good_perm_blocked;
            uint32_t total_allowed      = bad_allowed + good_allowed;

            // ipt.dump();

            std::chrono::duration<double> elapsed_seconds = end - start;

            std::cout << "Running with size=" << size << ", buckets=" << buckets << ", threshold=" << threshold
                      << ", permablock=" << permablock << std::endl;
            std::cout << "Processing time: " << elapsed_seconds.count() << std::endl;
            std::cout << "Denied requests: " << total_blocked + total_perm_blocked << std::endl;
            std::cout << "\tGood requests denied: " << good_blocked + good_perm_blocked << " ("
                      << 100.0 * (good_blocked + good_perm_blocked) / good_requests << "%)" << std::endl;
            std::cout << "\tBad requests denied: " << bad_blocked + bad_perm_blocked << " ("
                      << 100.0 * (bad_blocked + bad_perm_blocked) / bad_requests << "%)" << std::endl;
            std::cout << "Allowed requests: " << total_allowed << std::endl;
            std::cout << "\tGood requests allowed: " << good_allowed << " (" << 100.0 * good_allowed / good_requests << "%)"
                      << std::endl;
            std::cout << "\tBad requests allowed: " << bad_allowed << " (" << 100.0 * bad_allowed / bad_requests << "%)"
                      << std::endl;
            if (permablock) {
              std::cout << "Permanently blocked IPs: " << ipt.bucketSize(ipt.blockBucket()) << std::endl;
              std::cout << "\tGood requests permanently denied: " << good_perm_blocked << " ("
                        << 100.0 * good_perm_blocked / good_requests << "%)" << std::endl;
              std::cout << "\tBad requests permanently denied: " << bad_perm_blocked << " ("
                        << 100.0 * bad_perm_blocked / bad_requests << "%)" << std::endl;
            }
            std::cout << "Estimated score (lower is better): "
                      << 100.0 * ((100.0 * good_blocked / good_requests + 100.0 * bad_allowed / bad_requests) /
                                  (100.0 * good_allowed / good_requests + 100.0 * bad_blocked / bad_requests))
                      << std::endl;
            std::cout << "Memory used for IP Reputation data: " << ipt.memoryUsed() / (1024.0 * 1024.0) << "MB" << std::endl
                      << std::endl;
          }
        }
      }
    }
  }
}
