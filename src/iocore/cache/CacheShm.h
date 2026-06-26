/** @file

  Shared-memory-backed cache directory for fast restart.

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

#include <cstddef>
#include <cstdint>
#include <string_view>

struct Store;

/// Hosts Stripe::Directory::raw_dir in POSIX shared memory so the next process
/// start can attach the existing directory in milliseconds rather than rebuilding
/// it from disk. Purely an optimization over the rebuild path: anything wrong →
/// drop shm, rebuild from disk. See the cache-shm fast-restart design doc.
class CacheShm
{
public:
  static constexpr std::string_view tag{"ATS-SHM-V1"};

  enum class Mode {
    Disabled,       ///< shm.enabled=0; behave like today.
    AttachExisting, ///< A valid prior control segment exists; stripes attach by identity or create fresh.
    CreateFresh,    ///< No/invalid prior control - create everything new (cold path).
  };

  /// Initialize the control segment and decide Mode. Must be called from
  /// CacheProcessor::start after the store is read but before any Stripe is built.
  static void initialize(const Store &store);

  static Mode
  mode()
  {
    return _mode;
  }

  /// Allocate raw_dir for one stripe, keyed by its identity (`stripe_key`).
  /// Attaches the stripe's prior segment of matching size when one exists, else
  /// creates fresh. Returns the mapped pointer, or nullptr to fall back to the
  /// heap path (always in Disabled).
  static char *attach_or_create_stripe(const char *stripe_key, std::size_t directory_size);

  /// Reclaim segments left by stripes no longer in the cache (e.g. a dropped disk).
  /// Call once after all stripes init, from CacheProcessor::cacheInitialized.
  /// No-ops when no stripe came up this run. Idempotent.
  static void finalize_attach();

  /// Whether a pointer was returned from attach_or_create_stripe (munmap vs ats_free).
  static bool is_shm_pointer(char *raw_dir);

  /// Mark control->clean_shutdown = 1. Called after sync_cache_dir_on_shutdown.
  static void mark_clean_shutdown();

  /// Invalidate one stripe's shm directory (zero its header magic) so the next
  /// start recovers it from disk instead of fast-attaching. Called when a stripe's
  /// shutdown flush failed. No-op if raw_dir is not a shm segment.
  static void invalidate_stripe_directory(char *raw_dir);

  /// Detach (munmap) one stripe's shm directory and forget the pointer; never
  /// shm_unlink (the segment must survive for the next start). No-op if raw_dir
  /// is not a shm segment. Called from ~Stripe so the dtor frees the right way.
  static void detach_stripe(char *raw_dir);

  /// Compile-time ABI fingerprint of the shm-resident layout; a writer/reader
  /// mismatch forces a drop + rebuild. Exposed for unit testing.
  static uint64_t abi_hash();

  /// Fingerprint of the storage topology. Not a trust gate (see initialize()):
  /// informational, drives the "storage changed" log wording.
  static uint64_t storage_signature(const Store &store);

  /// True if `pid` names a live process (pid <= 0 is not). Backs the
  /// concurrent-attach owner-liveness backstop. Exposed for unit testing.
  static bool process_is_alive(int pid);

private:
  static Mode _mode;
};
