/** @file
 * Comparison acceleration support.
 *
 * Copyright 2020 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
 */

#include <txn_box/Accelerator.h>

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

// --- //

std::array<Accelerator::Builder, Accelerator::N_ACCELERATORS> Accelerator::_factory;

// --- //

// --- //

namespace
{
[[maybe_unused]] bool INITIALIZED = []() -> bool { return true; }();

// --- //

} // namespace
