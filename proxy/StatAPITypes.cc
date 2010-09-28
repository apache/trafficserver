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

// TODO: This needs to be supported with non-V2 APIs as well.
#include "ink_config.h"
#if TS_HAS_V2STATS

#include "api/ts/ts.h"
#include "StatAPITypes.h"
#include "StatSystemV2.h"

long long HistogramStats::get_bucket(long long theNumber)
{
    // theNumber == 0 : bucket = 0
    // theNumber == 1 : bucket = 1
    // theNumber >= 2 && theNumber < 4 : bucket = 2
    // theNumber >= 4 && theNumber < 8: bucket = 3
    // and so on

    int PowerOf2Counter = 0;

    if (theNumber == 0) {
        return 0;
    }
    if(theNumber == 1) {
        return 1;
    }

    // count the number in question down as we count up a power of 2
    while (theNumber > 1) {
        PowerOf2Counter++;
        
        // reduce number by a factor of 2 two
        theNumber >>= 1;
    }

    return PowerOf2Counter + 1;
}

void HistogramStats::init(const std::string &stat_prefix, long long max_stat)
{
    long long max_bucket = get_bucket(max_stat);

    buckets.resize(max_bucket + 2);
    StatSystemV2::registerStat((stat_prefix + "." + "0").c_str(), &buckets[0]);
    for(long long bucket = 0; bucket <= max_bucket; bucket++) {
        std::ostringstream stat_name;
        stat_name << stat_prefix << "." << (1<<bucket);
        StatSystemV2::registerStat(stat_name.str().c_str(), &buckets[bucket+1]);
    }
}

void HistogramStats::inc(long long stat_val)
{
    long long num_buckets = buckets.size();
    if(!num_buckets) {
        return;
    }
    
    long long bucket = get_bucket(stat_val);
   
    if(bucket >= num_buckets) {
        StatSystemV2::increment(buckets[num_buckets-1]);
    }
    else {
        StatSystemV2::increment(buckets[bucket]);
    }
}

#endif
