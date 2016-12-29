/** @file

  Scaled integral values.

  In many situations it is desirable to define scaling factors or base units (a "metric"). This template
  enables this to be done in a type and scaling safe manner where the defined factors carry their scaling
  information as part of the type.

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

#if !defined(TS_METRIC_H)
#define TS_METRIC_H

#include <cstdint>
#include <ratio>

namespace ApacheTrafficServer
{
  /** A class to hold scaled values.

      Instances of this class have a @a count and a @a scale. The "value" of the instance is @a
      count * @a scale.  The scale is stored in the compiler in the class symbol table and so only
      the count is a run time value. This prevents passing an incorrectly scaled value. Conversions
      between scales are explicit using @c metric_round_up and @c metric_round_down. Because the
      scales are not the same these conversions can be lossy and the two conversions determine
      whether, in such a case, the result should be rounded up or down to the nearest scale value.

      @note This is modeled somewhat on @c std::chrono and serves a similar function for different
      and simpler cases (where the ratio is always an integer, never a fraction).

      @see metric_round_up
      @see metric_round_down
   */
  template < intmax_t N, typename COUNT_TYPE = int >
  class Metric
  {
    typedef Metric self; ///< Self reference type.
    
  public:
    /// Scaling factor for instances.
    constexpr static intmax_t SCALE = N;
    typedef COUNT_TYPE CountType; ///< Type used to hold the count.

    Metric(); ///< Default contructor.
    Metric(CountType n); ///< Contruct from unscaled integer.

    /// The count in terms of the local @c SCALE.
    CountType count() const;
    /// The absolute count, unscaled.
    CountType units() const;

    /// Convert the count of a differently scaled @c Metric @a src by rounding down if needed.
    /// @internal This is intended for internal use but may be handy for other clients.
    template < intmax_t S, typename I > static intmax_t round_down(Metric<S,I> const& src);
    
  protected:
    CountType _n; ///< Number of scale units.
  };

  template < intmax_t N, typename C >
  inline Metric<N,C>::Metric() : _n() {}
  template < intmax_t N, typename C >
  inline Metric<N,C>::Metric(CountType n) : _n(n) {}
  template < intmax_t N, typename C >
  inline auto Metric<N,C>::count() const -> CountType { return _n; }
  template < intmax_t N, typename C >
  inline auto Metric<N,C>::units() const -> CountType { return _n * SCALE; }

  template < intmax_t N, typename C >
    template < intmax_t S, typename I >
    intmax_t Metric<N,C>::round_down(Metric<S,I> const& src)
    {
      auto n = src.count();
      // Yes, a bit odd, but this minimizes the risk of integer overflow.
      // I need to validate that under -O2 the compiler will only do 1 division to ge
      // both the quotient and remainder for (n/N) and (n%N). In cases where N,S are
      // powers of 2 I have verified recent GNU compilers will optimize to bit operations.
      return (n / N) * S + (( n % N ) * S) / N;
    }

  template < typename M, intmax_t N, typename C >
  M metric_round_up(Metric<N,C> const& src)
  {
    if (1 == M::SCALE) {
      return M(src.units());
    } else {
      typedef std::ratio<M::SCALE, N> R; // R::num == M::SCALE / GCD(M::SCALE, N) == GCF(M::SCALE, N)
      auto n = src.count();
      // Round down and add 1 unless @a n is an even multiple of the GCF of the two scales.
      return M(M::round_down(src) + ((n % R::num) != 0));
    }
  }
      
  template < typename M, intmax_t N, typename C >
  M metric_round_down(Metric<N,C> const& src)
  {
    return M(1 == M::SCALE ? src.units() : M::round_down(src));
  }
}

#endif // TS_METRIC_H
