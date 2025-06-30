/** @file
 *
 *  Basic implementation of Snowflake Id.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/types.h>

/** A utility class for the various SnowflakeID classes.
 *
 * Because each SnowflakeID has its own uint64_t structure, composition is
 * preferable over inheritance. This class provides the base functionality
 * used across the various SnowflakeID classes.
 */
class SnowflakeIDUtils
{
public:
  /**
   * @param[in] id The snowflake id value.
   */
  SnowflakeIDUtils(uint64_t id);
  ~SnowflakeIDUtils() = default;

  // Copy and move are default.
  SnowflakeIDUtils(SnowflakeIDUtils const &) = default;
  SnowflakeIDUtils(SnowflakeIDUtils &&)      = default;

  /** Set the machine id for this ATS host.
   * @note This must be called before any SnowflakeId instances are created.
   * @param machine_id The machine ID to set.
   */
  static void set_machine_id(uint64_t machine_id);

  /** Retrieve the machine ID.
   * @return The machine ID that was set.
   */
  static uint64_t
  get_machine_id()
  {
    return global_machine_id;
  }

  /** Convert the snowflake value to a string.
   * @return An encoded string representation of the snowflake ID.
   */
  std::string_view get_string() const;

public:
  /** The epoch for our snowflake IDs. Midnight January 1, 2025 */
  static constexpr uint64_t EPOCH = 1735689600000ULL; // 2025-01-01T00:00:00Z

private:
  /** The host identifier.
   *
   * This is the value that makes snowflake IDs unique across different
   * machines. Within an organization.
   */
  static std::atomic<uint64_t> global_machine_id;

  /** The snowflake value.
   *
   * This is the 64-bit integer that represents the snowflake ID.
   */
  uint64_t const m_snowflake_value = 0;

  /** Cached string representation of the ID.
   * This is lazily computed when get_string() is called and cached for future
   * calls.
   */
  mutable std::string m_id_string;
};

/** An implementation of Snowflake ID.
 *
 * UUID (Universally Unique Identifier) is a 128 bit integer designed to be
 * unique across space and time anywhere. Snowflake ID is a 64 bit value that is
 * designed to be unique within a certain environment. It accomplishes this via
 * a millisecond time component, a machine identifier component, and a sequence
 * counter for snowflakes created on the machine in the same millisecond. Its
 * scope is smaller than UUID, but it is more efficient in terms of storage and
 * performance.
 *
 * Limitations:
 * The underlying 64 bit integer has advantages in size, performance, and
 * representation, but comes with its own limitations for uniqueness. The
 * default Snowflake ID below has 41 bits for the timestamp, 12 bits for the
 * machine ID, and 10 bits for the sequence number. This means that:
 *
 * - The timestamp can represent up to 2^41 milliseconds, which is about 69
 *   years. After that point, snowflake IDs will start to repeat.
 *
 * - The machine ID, used to keep snowflake IDs unique across different
 *   machines, can represent up to 2^12 (4096) different machines. If you have
 *   more than 4096 machines, unique snowflake IDs will not be guaranteed.
 *
 * - The sequence number can represent up to 2^10 (1024) different snowflakes
 *   generated in the same millisecond on the same machine. If you generate more
 *   than 1024 snowflakes in the same millisecond on the same machine, unique
 *   snowflake IDs will not be possible.
 *
 * API Expectations:
 * @a set_machine_id must be called before any SnowflakeID instances are
 * created.
 *
 */
class SnowflakeID
{
public:
  /** Create a unique snowflake ID using the current time, machine id, and a
   * sequence counter.
   */
  SnowflakeID();
  ~SnowflakeID() = default;

  /** A convience function to create a snowflake and immediately return its ID.
   * @note This function is thread-safe and will return a unique ID each time it
   * is called.
   * @return A 64-bit unsigned integer representing the next Snowflake ID.
   */
  static uint64_t
  get_next_value()
  {
    return SnowflakeID().get_value();
  }

  /** Return the snowflake value.
   * @return The snowflake ID as a 64-bit unsigned integer.
   */
  uint64_t
  get_value() const
  {
    return m_snowflake.value;
  }

  /** Return a readable string representing the snowflake id.
   * @note This string is lazily computed when this function is called and
   * cached for future calls.
   * @return An encoded string representation of the snowflake ID.
   */
  std::string_view get_string() const;

private:
  /** A generator singleton encapsulating the state across the generation of snowflake ids. */
  class SnowflakeIDGenerator
  {
  public:
    /** Get the singleton instance of the SnowflakeIDGenerator.
     * @return A reference to the singleton instance.
     */
    static SnowflakeIDGenerator &instance();

    /** Generate a new snowflake ID.
     * @return The next Snowflake ID.
     */
    uint64_t get_next_id();

  private:
    /** The timestamp of the last created snowflake ID. */
    uint64_t m_last_timestamp = 0;

    /** The sequence value of the last created snowflake ID. */
    uint64_t m_last_sequence = 0;

    /** A mutex used to make snowflake ID created thread safe. */
    std::mutex m_mutex;
  };

  /** Generate the next snowflake value. */
  static uint64_t generate_next_snowflake_value();

private:
  union snowflake_t {
    uint64_t value = 0;
    struct {
// Layout the bytes according to endianness such that subsequent snowflake IDs
// are always increasing in value.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      uint64_t sequence    : 10; /// Increments from 0 when the timestamp is the same.
      uint64_t machine_id  : 12; /// Masked with @a global_machine_id.
      uint64_t timestamp   : 41; /// Milliseconds since epoch.
      uint64_t always_zero : 1;  /// Reserved always 0. For signedness compatibility.
#else                            // big-endian
      uint64_t always_zero : 1;  /// Reserved always 0. For signedness compatibility.
      uint64_t timestamp   : 41; /// Milliseconds since epoch.
      uint64_t machine_id  : 12; /// Masked with @a global_machine_id.
      uint64_t sequence    : 10; /// Increments from 0 when the timestamp is the same.
#endif                           // __BYTE_ORDER__
    } pieces;
  } m_snowflake;

  /** The common utility functions used across SnowflakeID flavors. */
  SnowflakeIDUtils m_utils;
};

/** A modified snowflake ID without bits assigned to a sequence number.
 *
 * This type of snowflake is useful for organizationally unique IDs that are
 * created once per ATS instance and therefore don't need a sequence number.
 */
class SnowflakeIdNoSequence
{
public:
  SnowflakeIdNoSequence();
  ~SnowflakeIdNoSequence() = default;

  /** Return the snowflake value.
   * @return The snowflake ID as a 64-bit unsigned integer.
   */
  uint64_t
  get_value() const
  {
    return m_snowflake.value;
  }

  /** Return a readable string representing the snowflake id.
   * @note This string is lazily computed when this function is called and
   * cached for future calls.
   * @return An encoded string representation of the snowflake ID.
   */
  std::string_view get_string() const;

private:
  /** Generate a new SnoflakeIdNoSequence value. */
  static uint64_t generate_next_snowflake_value();

private:
  union snowflake_t {
    uint64_t value = 0;
    struct {
// Layout the bytes according to endianness such that subsequent snowflake IDs
// are always increasing in value.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      uint64_t machine_id  : 22; /// Masked with @a global_machine_id.
      uint64_t timestamp   : 41; /// Milliseconds since epoch
      uint64_t always_zero : 1;  /// Reserved always 0. For signedness compatibility.
#else                            // big-endian
      uint64_t always_zero : 1;  /// Reserved always 0. For signedness compatibility.
      uint64_t timestamp   : 41; /// Milliseconds since epoch
      uint64_t machine_id  : 22; /// Masked with @a global_machine_id.
#endif                           // __BYTE_ORDER__

    } pieces;
  } m_snowflake;

  /** The common utility functions used across SnowflakeID flavors. */
  SnowflakeIDUtils m_utils;
};
