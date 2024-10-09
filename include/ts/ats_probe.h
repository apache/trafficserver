/** @file

    Expose ATS SystemTap probes.

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

// To enable ATS SystemTap probes, pass -DENABLE_PROBES=ON to cmake.
#ifdef ENABLE_SYSTEMTAP_PROBES

#include <sys/sdt.h>

#define ATS_PROBE(probe)                                  DTRACE_PROBE(trafficserver, probe)
#define ATS_PROBE1(probe, param1)                         DTRACE_PROBE1(trafficserver, probe, param1)
#define ATS_PROBE2(probe, param1, param2)                 DTRACE_PROBE2(trafficserver, probe, param1, param2)
#define ATS_PROBE3(probe, param1, param2, param3)         DTRACE_PROBE3(trafficserver, probe, param1, param2, param3)
#define ATS_PROBE4(probe, param1, param2, param3, param4) DTRACE_PROBE4(trafficserver, probe, param1, param2, param3, param4)
#define ATS_PROBE5(probe, param1, param2, param3, param4, param5) \
  DTRACE_PROBE5(trafficserver, probe, param1, param2, param3, param4, param5)
#define ATS_PROBE6(probe, param1, param2, param3, param4, param5, param6) \
  DTRACE_PROBE6(trafficserver, probe, param1, param2, param3, param4, param5, param6)
#define ATS_PROBE7(probe, param1, param2, param3, param4, param5, param6, param7) \
  DTRACE_PROBE7(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7)
#define ATS_PROBE8(probe, param1, param2, param3, param4, param5, param6, param7, param8) \
  DTRACE_PROBE8(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7, param8)
#define ATS_PROBE9(probe, param1, param2, param3, param4, param5, param6, param7, param8, param9) \
  DTRACE_PROBE9(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7, param8, param9)
#define ATS_PROBE10(probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10) \
  DTRACE_PROBE10(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10)
#define ATS_PROBE11(probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10, param11) \
  DTRACE_PROBE11(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10, param11)
#define ATS_PROBE12(probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10, param11, param12)    \
  DTRACE_PROBE12(trafficserver, probe, param1, param2, param3, param4, param5, param6, param7, param8, param9, param10, param11, \
                 param12)

#else

#define ATS_PROBE(...)
#define ATS_PROBE1(...)
#define ATS_PROBE2(...)
#define ATS_PROBE3(...)
#define ATS_PROBE4(...)
#define ATS_PROBE5(...)
#define ATS_PROBE6(...)
#define ATS_PROBE7(...)
#define ATS_PROBE8(...)
#define ATS_PROBE9(...)
#define ATS_PROBE10(...)
#define ATS_PROBE11(...)
#define ATS_PROBE12(...)

#endif
