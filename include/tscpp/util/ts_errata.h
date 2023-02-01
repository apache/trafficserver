/** @file Diagnostic definitions and functions.

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

#include "tscpp/util/ts_diag_levels.h"
#include "swoc/TextView.h"
#include "swoc/Errata.h"

static constexpr swoc::Errata::Severity ERRATA_DIAG{DL_Diag};
static constexpr swoc::Errata::Severity ERRATA_DEBUG{DL_Debug};
static constexpr swoc::Errata::Severity ERRATA_STATUS{DL_Status};
static constexpr swoc::Errata::Severity ERRATA_NOTE{DL_Note};
static constexpr swoc::Errata::Severity ERRATA_WARN{DL_Warning};
static constexpr swoc::Errata::Severity ERRATA_ERROR{DL_Error};
static constexpr swoc::Errata::Severity ERRATA_FATAL{DL_Fatal};
static constexpr swoc::Errata::Severity ERRATA_ALERT{DL_Alert};
static constexpr swoc::Errata::Severity ERRATA_EMERGENCY{DL_Emergency};

// This is treated as an array so must numerically match with @c DiagsLevel
static constexpr std::array<swoc::TextView, 9> Severity_Names{
  {"Diag", "Debug", "Status", "Note", "Warn", "Error", "Fatal", "Alert", "Emergency"}
};
