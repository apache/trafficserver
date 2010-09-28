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
 Stat types

 ***************************************************************************/

#ifndef STAT_API_TYPES_H
#define STAT_API_TYPES_H

// TODO: This needs to be supported with non-V2 APIs as well.
#if TS_HAS_V2STATS
#include <string> // TODO: Get rid of string here, why do we need it ?
#include <vector> // TODO: Is the vector really necessary ? 
#include "inktomi++.h"

class HistogramStats
{
public:
  // TODO: Eliminate STL strings ?
    HistogramStats() { }
    HistogramStats(const std::string &stat_prefix, long long max_stat) { init(stat_prefix, max_stat); }
    void init(const std::string &stat_prefix, long long max_stat);
    void inc(long long stat_val);
    long long get_bucket(long long theNumber);
private:
    std::vector<uint32_t> buckets; // TODO: Do we need a vector here?
};
#endif

#endif // STAT_API_TYPES_H

