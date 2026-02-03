/** @file

  Udi "King of the Hill" Table - A fixed-size, self-cleaning hash table.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ts
{

/** A fixed-size hash table using the Udi "King of the Hill" algorithm.
 *
 * Instantiations of this table track the keys/entities (IPs, URLs, etc.) with
 * the highest rates of events (e.g. number of requests, number of errors, etc.).
 *
 * This implements the Udi algorithm (US Patent 7533414) for tracking entities
 * (IPs, URLs, etc.) and their events with bounded memory. The algorithm was
 * developed to address abuse detection where a "screening list" identifies
 * potential abuse events.
 *
 * From the patent: "A screening list includes event IDs and associated count values.
 * A pointer cyclically selects entries in the table, advancing as events are received.
 * An incoming event ID is compared with the event IDs in the table. If the incoming
 * event ID matches an event ID in the screening list, the associated count is
 * incremented. Otherwise, the count of a selected table entry is decremented. If the
 * count value of the selected entry falls to zero, it is replaced with the incoming
 * event and the count is reset to one."
 *
 * Thus the table serves as a "screening list" - the "hot" items to investigate. Each slot
 * can be investigated using the templated @a Data to determine which top talkers require
 * action.
 *
 * Key properties:
 * - Fixed memory: N slots = bounded memory, no unbounded growth
 * - Self-cleaning: No cleanup thread needed, table manages itself
 * - Hot tracking: High-score entries naturally stay in the table
 * - Simple locking: Single mutex for all operations
 * - Safe references: Returns shared_ptr so data survives eviction
 *
 * @tparam Key The entity type (e.g., IP address, URL)
 * @tparam Data User's custom data type to associate with each entry
 * @tparam Hash Hash function for keys (defaults to std::hash)
 *
 * The table owns the key and score for each entry. Users provide only their custom
 * Data type which is stored in a shared_ptr for safe access.
 *
 * Thread Safety:
 * All operations are protected by a single mutex and are serialized.
 * Returned shared_ptr<Data> remains valid even after the slot is evicted.
 *
 * Example usage:
 * @code
 * struct MyData {
 *   std::atomic<uint32_t> error_count{0};
 *   std::atomic<uint32_t> success_count{0};
 * };
 *
 * ts::UdiTable<std::string, MyData> table(10000);
 *
 * auto data = table.process_event("some_key", 1);
 * if (data) {
 *   data->error_count.fetch_add(1);
 * }
 * @endcode
 */
template <typename Key, typename Data, typename Hash = std::hash<Key>> class UdiTable
{
public:
  using key_type  = Key;
  using data_type = Data;
  using data_ptr  = std::shared_ptr<Data>;

  using data_format_fn = std::function<std::string(Key const &, uint32_t, data_ptr const &)>;

  // =========================================================================
  // Public API - Declarations
  // =========================================================================

  /**
   * @param[in] num_slots Total number of slots to allocate.
   */
  explicit UdiTable(size_t num_slots);

  // No copying or moving
  UdiTable(UdiTable const &)            = delete;
  UdiTable &operator=(UdiTable const &) = delete;
  UdiTable(UdiTable &&)                 = delete;
  UdiTable &operator=(UdiTable &&)      = delete;

  /** Retrieve the configured data for a given key.
   *
   * @param[in] key The key to look up.
   * @return The data associated with @a key if found, nullptr otherwise.
   *
   * Thread-safe: Uses mutex lock.
   * The returned shared_ptr remains valid even if the slot is later evicted.
   */
  data_ptr                    find(Key const &key);
  std::shared_ptr<Data const> find(Key const &key) const;

  /** Process an event for a key, creating a slot if needed via @a contest.
   *
   * If the key is already tracked, increments its score and returns the Data.
   * If not, a call to @a contest is made to see whether the @a key should evict
   * an entry and take its place.
   *
   * @param[in] key The key for which an event is being processed.
   * @param[in] score_delta Score to add (typically 1 for events).
   * @return The data for @a key if tracked or contest won, nullptr if contest lost.
   *
   * Thread-safe: Uses mutex lock.
   * The returned shared_ptr remains valid even if the slot is later evicted.
   */
  data_ptr process_event(Key const &key, uint32_t score_delta = 1);

  /** Remove a key from the table.
   *
   * @param[in] key The key to remove.
   * @return Whether @a key was found and removed.
   *
   * Note: Existing shared_ptr references to the removed Data remain valid.
   */
  bool remove(Key const &key);

  // Statistics
  size_t   num_slots() const;
  size_t   slots_used() const;
  uint64_t contests() const;
  uint64_t contests_won() const;
  uint64_t evictions() const;

  /** Reset table-level metrics to zero.
   *
   * Does NOT modify any entries in the table.
   */
  void reset_metrics();

  /** Get the timestamp of the last reset (or table creation).
   */
  std::chrono::system_clock::time_point last_reset_time() const;

  /** Get the number of seconds since last reset (or table creation).
   */
  uint64_t seconds_since_reset() const;

  /** Dump all entries to a string.
   *
   * This may be useful to evaluate the behavior of the table for debugging or
   * diagnostic purposes.
   *
   * @param[in] format_data Optional function to format each entry (key, score, data).
   * @return A string representation of all table entries.
   */
  std::string dump(data_format_fn format_data = nullptr) const;

private:
  /** The data used to track the key and score used to determine eviction.
   */
  struct Slot {
    Key      key{};
    uint32_t score{0};
    data_ptr data;

    /** Check whether there is an entity associated with this slot.
     *
     * Note that a score of zero does not necessarily mean that the slot is
     * empty. Upon table initialization, all slots are empty.
     *
     * @return Whether the slot has no entity assigned.
     */
    bool
    is_empty() const
    {
      return !data;
    }

    /** Remove the entity associated with this slot.
     */
    void
    clear()
    {
      key   = Key{};
      score = 0;
      data.reset();
    }
  };

  /** Determine whether the given key should evict a slot.
   *
   * Assumes @a mutex_ already held exclusively.
   *
   * @param[in] key The key trying to enter.
   * @param[in] incoming_score The score of the incoming key.
   * @return The data for @a key if contest won, nullptr if contest lost.
   */
  data_ptr contest(Key const &key, uint32_t incoming_score);

  /// Single global mutex for all operations
  mutable std::mutex mutex_;

  /// Lookup map: key -> slot index
  std::unordered_map<Key, size_t, Hash> lookup_;

  /// The slots representing the table.
  std::vector<Slot> slots_;

  /// The contest pointer - rotates through all slots as @a contest() is called.
  size_t contest_ptr_{0};

  /// Metrics.
  std::atomic<uint64_t> metric_contests_{0};
  std::atomic<uint64_t> metric_contests_won_{0};
  std::atomic<uint64_t> metric_evictions_{0};

  /// Timestamp of last reset (or construction)
  std::chrono::system_clock::time_point last_reset_time_;
};

// ===========================================================================
// Implementation
// ===========================================================================

template <typename Key, typename Data, typename Hash>
UdiTable<Key, Data, Hash>::UdiTable(size_t num_slots) : slots_(num_slots), last_reset_time_(std::chrono::system_clock::now())
{
  slots_.reserve(num_slots);
  lookup_.reserve(num_slots);
}

template <typename Key, typename Data, typename Hash>
std::shared_ptr<Data>
UdiTable<Key, Data, Hash>::find(Key const &key)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = lookup_.find(key);
  if (it != lookup_.end()) {
    return slots_[it->second].data;
  }
  return nullptr;
}

template <typename Key, typename Data, typename Hash>
std::shared_ptr<Data const>
UdiTable<Key, Data, Hash>::find(Key const &key) const
{
  return const_cast<UdiTable *>(this)->find(key);
}

template <typename Key, typename Data, typename Hash>
std::shared_ptr<Data>
UdiTable<Key, Data, Hash>::process_event(Key const &key, uint32_t score_delta)
{
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if already tracked
  auto it = lookup_.find(key);
  if (it != lookup_.end()) {
    Slot &slot  = slots_[it->second];
    slot.score += score_delta;
    return slot.data;
  }

  // Not tracked - contest for a slot
  return contest(key, score_delta);
}

template <typename Key, typename Data, typename Hash>
bool
UdiTable<Key, Data, Hash>::remove(Key const &key)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = lookup_.find(key);
  if (it == lookup_.end()) {
    return false;
  }

  slots_[it->second].clear();
  lookup_.erase(it);
  return true;
}

template <typename Key, typename Data, typename Hash>
size_t
UdiTable<Key, Data, Hash>::num_slots() const
{
  return slots_.size();
}

template <typename Key, typename Data, typename Hash>
size_t
UdiTable<Key, Data, Hash>::slots_used() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return lookup_.size();
}

template <typename Key, typename Data, typename Hash>
uint64_t
UdiTable<Key, Data, Hash>::contests() const
{
  return metric_contests_.load(std::memory_order_relaxed);
}

template <typename Key, typename Data, typename Hash>
uint64_t
UdiTable<Key, Data, Hash>::contests_won() const
{
  return metric_contests_won_.load(std::memory_order_relaxed);
}

template <typename Key, typename Data, typename Hash>
uint64_t
UdiTable<Key, Data, Hash>::evictions() const
{
  return metric_evictions_.load(std::memory_order_relaxed);
}

template <typename Key, typename Data, typename Hash>
void
UdiTable<Key, Data, Hash>::reset_metrics()
{
  std::lock_guard<std::mutex> lock(mutex_);
  metric_contests_.store(0, std::memory_order_relaxed);
  metric_contests_won_.store(0, std::memory_order_relaxed);
  metric_evictions_.store(0, std::memory_order_relaxed);
  last_reset_time_ = std::chrono::system_clock::now();
}

template <typename Key, typename Data, typename Hash>
std::chrono::system_clock::time_point
UdiTable<Key, Data, Hash>::last_reset_time() const
{
  return last_reset_time_;
}

template <typename Key, typename Data, typename Hash>
uint64_t
UdiTable<Key, Data, Hash>::seconds_since_reset() const
{
  auto now     = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_reset_time_);
  return static_cast<uint64_t>(elapsed.count());
}

template <typename Key, typename Data, typename Hash>
std::string
UdiTable<Key, Data, Hash>::dump(data_format_fn format_data) const
{
  std::string result;
  result.reserve(slots_.size() * 64);

  std::lock_guard<std::mutex> lock(mutex_);

  for (auto const &[key, slot_idx] : lookup_) {
    Slot const &slot = slots_[slot_idx];
    if (format_data) {
      result += format_data(slot.key, slot.score, slot.data);
    } else {
      result += "slot[" + std::to_string(slot_idx) + "] score=" + std::to_string(slot.score) + "\n";
    }
  }
  return result;
}

template <typename Key, typename Data, typename Hash>
std::shared_ptr<Data>
UdiTable<Key, Data, Hash>::contest(Key const &key, uint32_t incoming_score)
{
  // Called with mutex_ already held exclusively

  metric_contests_.fetch_add(1, std::memory_order_relaxed);

  // This should never happen, but just in case
  if (slots_.empty()) {
    return nullptr;
  }

  // Get next contest slot and advance pointer (rotating through all slots)
  size_t slot_idx = contest_ptr_;
  contest_ptr_    = (contest_ptr_ + 1) % slots_.size();

  Slot    &slot          = slots_[slot_idx];
  uint32_t current_score = slot.score;

  if (slot.is_empty() || incoming_score > current_score) {
    // New key wins - takes the slot
    if (!slot.is_empty()) {
      // Evict the old key from the lookup
      lookup_.erase(slot.key);
      metric_evictions_.fetch_add(1, std::memory_order_relaxed);
    }

    // Initialize the slot with new key and fresh Data
    slot.key     = key;
    slot.score   = incoming_score;
    slot.data    = std::make_shared<Data>();
    lookup_[key] = slot_idx;

    metric_contests_won_.fetch_add(1, std::memory_order_relaxed);
    return slot.data;
  } else {
    // New key loses - existing slot survives but is weakened
    if (current_score > 0) {
      --slot.score;
    }
    return nullptr;
  }
}

} // namespace ts
