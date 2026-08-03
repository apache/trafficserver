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

/// Hosts Stripe::Directory::raw_dir in POSIX shared memory so the next start can attach it instead of rebuilding from
/// disk. Purely an optimization: anything wrong drops shm and rebuilds. See the shm-fast-restart developer guide.
class CacheShm
{
public:
  static constexpr std::string_view tag{"ATS-SHM-V1"};

  enum class Mode {
    Disabled,       ///< shm.enabled=0; behave like today.
    AttachExisting, ///< A valid prior control segment exists; stripes attach by identity or create fresh.
    CreateFresh,    ///< No/invalid prior control - create everything new (cold path).
  };

  /// Must run after the store is read but before any Stripe is built.
  static void initialize(const Store &store);

  static Mode
  mode()
  {
    return _mode;
  }

  /// Attaches this stripe's prior segment when one of matching size exists, else creates fresh. nullptr means the caller
  /// must fall back to the heap path, which is always the case in Disabled.
  static char *attach_or_create_stripe(const char *stripe_key, std::size_t directory_size);

  /// Reclaims segments left by stripes no longer in the cache, e.g. a dropped disk. Call once after all stripes init.
  /// Idempotent; no-ops when no stripe came up this run, since that cannot be told from an aborted init.
  static void finalize_attach();

  /// Whether a pointer was returned from attach_or_create_stripe (munmap vs ats_free).
  static bool is_shm_pointer(char *raw_dir);

  /// Called after sync_cache_dir_on_shutdown; a crash instead leaves the flag clear, which drops the segment next start.
  static void mark_clean_shutdown();

  /// Marks this stripe's control-segment entry so the next start recreates it rather than attaching. Never writes through
  /// raw_dir, which is also the source buffer for the on-disk directory write. No-op for a non-shm pointer.
  static void invalidate_stripe_directory(char *raw_dir);

  /// Never shm_unlink: the segment must survive for the next start. No-op for a non-shm pointer.
  static void detach_stripe(char *raw_dir);

  /// A writer/reader mismatch forces a drop and rebuild. Exposed for unit testing.
  static uint64_t abi_hash();

  /// Informational only, not a trust gate: a storage change keeps the segment and each stripe attaches by its own identity.
  static uint64_t storage_signature(const Store &store);

  /// Backs the concurrent-attach owner-liveness guard. Exposed for unit testing.
  static bool process_is_alive(int pid);

private:
  static Mode _mode;
};
