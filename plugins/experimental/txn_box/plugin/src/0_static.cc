/**
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

  Copyright 2019, Oath Inc.
*/

#include <array>

#include "swoc/MemSpan.h"
#include "swoc/TextView.h"
#include "swoc/Errata.h"

#include "txn_box/Modifier.h"
#include "txn_box/Config.h"

/// Directive definition.
Config::Factory Config::_factory;

/// Defined extractors.
Extractor::Table Extractor::_ex_table;

/// Static mapping from modifier to factory.
Modifier::Factory Modifier::_factory;

static std::array<swoc::TextView, 5> S_NAMES = {"Success", "Debug", "Info", "Warning", "Error"};

const bool TXN_BOX_LIB_INIT = []() -> bool {
  swoc::Errata::SEVERITY_NAMES   = swoc::MemSpan<swoc::TextView const>{S_NAMES.data(), S_NAMES.size()};
  swoc::Errata::DEFAULT_SEVERITY = S_ERROR;
  swoc::Errata::FAILURE_SEVERITY = S_ERROR;
  return true;
}();
