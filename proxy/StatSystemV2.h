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

/***************************************************************************
 Stats

 ***************************************************************************/

#ifndef STAT_SYSTEM_V2_H
#define STAT_SYSTEM_V2_H

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include "api/ts/ts.h"

#include "I_EventSystem.h"

class Event;

class StatSystemV2
{
public:
    static bool increment(uint32_t stat_num, int64 stat_val = 1);
    static bool increment(const char *stat_name, int64 stat_val = 1);
    static bool get(uint32_t stat_num, int64 *stat_val);
    static bool get(const char *stat_name, int64 *stat_val);
    static bool get_current(uint32_t stat_num, int64 *stat_val);
    static bool get_current(const char *stat_name, int64 *stat_val);
    
    static bool registerStat(const char *stat_name, uint32_t *stat_num);
    static void setMaxStatsAllowed(uint32_t max_stats_allowed);
    static void setNumStatsEstimate(uint32_t num_stats_estimate);
    static void init();
    
private:
    // These must be called after acquiring a lock
    // Since these are private, only methods in StatCollectorContinuation can call them
    static void incrementGlobal(uint32_t stat_num, int64 stat_val = 1);
    static void clear();
    static void collect();

    static bool getStatNum(const char *stat_name, uint32_t &stat_num);
    static std::map<std::string, uint32_t> stat_name_to_num;
    static std::vector< std::pair<std::string, int64> > global_stats;
    static uint32_t MAX_STATS_ALLOWED;
    static uint32_t NUM_STATS_ESTIMATE;

    friend class StatCollectorContinuation;
};

class StatCollectorContinuation : public Continuation
{
public:
    StatCollectorContinuation();
    static void setStatCommandPort(int port);
    static void setReadTimeout(int secs = 1, int usecs = 0);
    
private:
    int mainEvent(int event, Event *e);
    static int doWrite(int fd, const char* buf, size_t towrite);
    static void print_stats(std::stringstream &printbuf);
    static void print_stat(const char *stat_name, std::stringstream &printbuf, bool current = false);
    static void print_stats(const std::vector<std::string> &stat_names, std::stringstream &printbuf, bool current = false);
    static void get_stats_with_prefix(const std::string &stat_prefix, std::vector<std::string> &stat_names);
    static void *commandLoop(void *data);
    static void *commandListen(void *data);
    static int getCommand(int fd, char *buf, int buf_size);

    // member variables
    static int _statCommandPort;
    static time_t _startTime;
    static int _readTimeout;
    static int _readTimeoutUSecs;
};

#endif // STAT_SYSTEM_V2

