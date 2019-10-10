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

#pragma once

#ifdef HAVE_SYSTEMTAP

#include <sys/sdt.h>

#define ATS_PROBE(probe) DTRACE_PROBE(trafficserver, probe)
#define ATS_PROBE1(probe, param1) DTRACE_PROBE1(trafficserver, probe, param1)
#define ATS_PROBE2(probe, param1, param2) DTRACE_PROBE2(trafficserver, probe, param1, param2)

#else

#define ATS_PROBE(...)
#define ATS_PROBE1(...)
#define ATS_PROBE2(...)

#endif
