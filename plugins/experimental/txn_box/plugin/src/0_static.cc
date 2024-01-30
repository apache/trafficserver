/** @file Static data.
 *
 * Copyright 20120 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
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
