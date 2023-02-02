/** @file
 *
 * Fast small foot print histogram support.

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

#include <stdint.h>
#include <array>

namespace ts
{
/** Small fast histogram.
 *
 * This is a stepped logarithmic histogram. Each range is twice the size of the previous range. Each range is divided into equal
 * sized spans, with a bucket for each span. The ranges and spans are defined by the @a R and @a S template parameters. There is
 * an underflow range for values less than @c 2^S. There is a range for each power of 2 from @c 2^S to @c 2^(S+R-1) and overflow
 * bucket for values greater than or equal to @c 2^(R+S-1).
 *
 * This can also been seen as having a range for each bit from @c S to @c S+R-1. The bucket is determined by the most significant
 * bit of the value. If it is past @c S+R-1 then it is put in the overflow bucket. If the MSB is less than @c S then it is put in
 * a bucket in the underflow range, values <tt>0 .. (2^S)-1</tt>. For normal ranges, the range is determined by the bit index and
 * the next @c S bits are used as an index into the buckets for that range. Note this is the same for the underflow range which is
 * used when the MSB is in the first @c S bits.
 *
 * For example, if @a S is 2 then the buckets are (where @c U is an underflow bucket)
 * <tt>0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28, ...</tt> <- Sample value
 * <tt>U U U U 0 0 0 0 1  1  1  1  2  2  2  2  ...</tt> <- Range
 *
 * To keep data relevant there is a decay mechanism will divides all of the bucket counts by 2. If done periodically this creates
 * an exponential decay of sample data, which is less susceptible to timing issues. Instances can be summed so that parallel
 * instances can be kept on different threads without locking and then combined.
 *
 * @tparam R Bits for the overall range of the histogram.
 * @tparam S Bits used for spans inside a range.
 *
 */
template <auto R, auto S> class Histogram
{
  using self_type = Histogram; ///< Self reference type.

public:
  /// Type used for internal calculations.
  using raw_type = uint64_t;
  /// Number of bits to use for the base range.
  static constexpr raw_type N_RANGE_BITS = R;
  /// Number of bits to split each base range in to span buckets.
  static constexpr raw_type N_SPAN_BITS = S;
  /// Number of buckets per span.
  static constexpr raw_type N_SPAN_BUCKETS = 1 << N_SPAN_BITS;
  /// Mask to extract the local bucket index from a sample.
  static constexpr raw_type SPAN_MASK = (1 << N_SPAN_BITS) - 1;
  /// Initial mask to find the MSB in the sample.
  static constexpr raw_type MSB_MASK = 1 << (N_RANGE_BITS + N_SPAN_BITS - 1);
  /// Total number of buckets - 1 for overflow and an extra range for less than @c LOWER_BOUND
  static constexpr raw_type N_BUCKETS = ((N_RANGE_BITS + 1) * N_SPAN_BUCKETS) + 1;
  /// Samples less than this go in the underflow range.
  static constexpr raw_type LOWER_BOUND = 1 << N_SPAN_BITS;
  /// Sample equal or greater than this  go in the overflow bucket.
  static constexpr raw_type UPPER_BOUND = 1 << (N_RANGE_BITS + N_SPAN_BITS + 1);

  /** Add @sample to the histogram.
   *
   * @param sample Value to add.
   * @return @a this
   */
  self_type &operator()(raw_type sample);

  /** Decrease all values by a factor of 2.
   *
   * @return @a this
   */
  self_type &decay();

  /** Get bucket count.
   *
   * @param idx Index of the bucket.
   * @return Count in the bucket.
   */
  raw_type operator[](unsigned idx);

  /** Lower bound for samples in bucket.
   *
   * @param idx Index of the bucket.
   * @return The smallest sample value that will increment the bucket.
   */
  static raw_type lower_bound(unsigned idx);

  /** Add counts from another histogram.
   *
   * @param that Source histogram.
   * @return @a this
   *
   * The buckets are added in parallel.
   */
  self_type &operator+=(self_type const &that);

protected:
  /// The buckets.
  std::array<raw_type, N_BUCKETS> _bucket = {0};
};

/// @cond INTERNAL_DETAIL
template <auto R, auto S>
auto
Histogram<R, S>::operator[](unsigned int idx) -> raw_type
{
  return _bucket[idx];
}

template <auto R, auto S>
auto
Histogram<R, S>::operator+=(self_type const &that) -> self_type &
{
  auto dst = _bucket.data();
  auto src = that._bucket.data();
  for (raw_type idx = 0; idx < N_BUCKETS; ++idx) {
    *dst++ += *src++;
  }
  return *this;
}

template <auto R, auto S>
auto
Histogram<R, S>::operator()(raw_type sample) -> self_type &
{
  int idx = N_BUCKETS - 1; // index of overflow bucket
  if (sample < LOWER_BOUND) {
    idx = sample;                    // sample -> bucket is identity in the underflow range.
  } else if (sample < UPPER_BOUND) { // not overflow bucket.
    idx       -= N_SPAN_BUCKETS;     // bottom bucket in the range.
    auto mask = MSB_MASK;            // Mask to probe for bit set.
    // Shift needed after finding the MSB to put the span bits in the LSBs.
    unsigned normalize_shift_count = N_RANGE_BITS - 1;
    // Walk the mask bit down until the MSB is found. Each span bumps down the bucket index
    // and the shift for the span bits. An MSB will be found because @a sample >= @c LOWER_BOUND
    // The MSB is not before @c MSB_MASK because @a sample < @c UPPER_BOUND
    while (0 == (sample & mask)) {
      mask >>= 1;
      --normalize_shift_count;
      idx -= N_SPAN_BUCKETS;
    }
    idx += (sample >> normalize_shift_count) & SPAN_MASK;
  } // else idx remains the overflow bucket.
  ++_bucket[idx];
  return *this;
}

template <auto R, auto S>
auto
Histogram<R, S>::lower_bound(unsigned idx) -> raw_type
{
  auto range         = idx / N_SPAN_BUCKETS;
  raw_type base      = 0; // minimum value for the range (not span!).
  raw_type span_size = 1; // for @a range 0 or 1
  if (range > 0) {
    base = 1 << (range + N_SPAN_BITS - 1);
    if (range > 1) { // at @a range == 1 this would be 0, which is wrong.
      span_size = base >> N_SPAN_BITS;
    }
  }
  return base + span_size * (idx & SPAN_MASK);
}

template <auto R, auto S>
auto
Histogram<R, S>::decay() -> self_type &
{
  for (auto &v : _bucket) {
    v >>= 1;
  }
  return *this;
}

/// @endcond

} // namespace ts
