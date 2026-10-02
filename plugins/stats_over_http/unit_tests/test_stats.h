/** @file

  Records for the unit tests of the Prometheus renderer of stats_over_http.

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

#include <cstdint>
#include <string>
#include <vector>

#include "prometheus_render.h"

struct Stat {
  const char      *name;
  TSRecordDataType data_type;
  TSRecordData     datum;
};

inline Stat
counter(const char *name, int64_t value)
{
  Stat stat{name, TS_RECORDDATATYPE_COUNTER, {}};

  stat.datum.rec_counter = value;
  return stat;
}

inline Stat
gauge(const char *name, int64_t value)
{
  Stat stat{name, TS_RECORDDATATYPE_INT, {}};

  stat.datum.rec_int = value;
  return stat;
}

inline Stat
floating(const char *name, float value)
{
  Stat stat{name, TS_RECORDDATATYPE_FLOAT, {}};

  stat.datum.rec_float = value;
  return stat;
}

inline Stat
string(const char *name, const char *value)
{
  Stat stat{name, TS_RECORDDATATYPE_STRING, {}};

  stat.datum.rec_string = const_cast<char *>(value);
  return stat;
}

inline std::string
render(PrometheusRenderer &renderer, const std::vector<Stat> &stats)
{
  renderer.begin();
  for (const Stat &stat : stats) {
    renderer.add(stat.name, stat.data_type, stat.datum);
  }
  return renderer.render();
}
