/** @file

  Facilities to help with marshaling and unmarshaling integral values "embedable" in nul-terminated strings.

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

#ifndef MARSHAL_INTEGRAL_H
#define MARSHAL_INTEGRAL_H

#include <type_traits>

// Template parameters named 'IT' must be integral types.  Template parameters named 'UT' must be unsigned integral types.

namespace LogUtils
{
namespace MarshalIntegralImpl
{
  // Do not use anything in this namespace directly.

  template <typename IT, bool IsSigned, bool IsIntegral> class Marshal;

  // Marshal an unsigned value into a sequence of bytes.  Each byte in the sequence contains 7 bits of the value, from least to most
  // significant.  The bit masked by 1 << 7 is set in all bytes of the sequence except for the last one.  The sequence has only the
  // length needed to hold the most significant 1 bit in the value.
  //
  template <typename UT> class Marshal<UT, false, true>
  {
  public:
    Marshal(UT val) : _val(val) {}

    unsigned char
    next()
    {
      unsigned char ret = _val % (1 << 7);

      _val >>= 7;

      if (_val) {
        ret |= 1 << 7;
      }

      return ret;
    }

    char
    cNext()
    {
      return char(next());
    }

    bool
    done() const
    {
      return _val == 0;
    }

  private:
    UT _val;
  };

  // Marshal signed integral types by mapping values onto unsigned.
  //
  template <typename IT> class Marshal<IT, true, true> : public Marshal<typename std::make_unsigned<IT>::type, false, true>
  {
  private:
    using UT   = typename std::make_unsigned<IT>::type;
    using Base = Marshal<UT, false, true>;

    // Map negative to odd, non-negative to even.
    //
    static UT
    _cvt(IT val)
    {
      if (val < 0) {
        return UT((2 * -val) - 1);
      }

      return UT(2 * val);
    }

  public:
    Marshal(IT val) : Base(_cvt(val)) {}
  };

  template <typename IT, bool IsSigned, bool IsIntegral> class Unmarshal;

  // Unmarshal back to unsigned value.
  //
  template <typename UT> class Unmarshal<UT, false, true>
  {
  public:
    Unmarshal() : _val(0), _shift(0) {}

    bool
    next(unsigned char b)
    {
      _val |= UT(b bitand ((1 << 7) - 1)) << _shift;
      _shift += 7;

      return (b bitand (1 << 7)) != 0;
    }

    bool
    cNext(char b)
    {
      return next(static_cast<unsigned char>(b));
    }

    UT
    result() const
    {
      return _val;
    }

  private:
    UT _val;
    int _shift;
  };

  // Unmarshal signed integral values.
  //
  template <typename IT> class Unmarshal<IT, true, true> : public Unmarshal<typename std::make_unsigned<IT>::type, false, true>
  {
  private:
    using UT   = typename std::make_unsigned<IT>::type;
    using Base = Unmarshal<UT, false, true>;

    // Map odd to negative, even to non-negative.
    //
    static IT
    _cvt(UT val)
    {
      if (val bitand 1) {
        return -IT((val + 1) / 2);
      }

      return IT(val / 2);
    }

  public:
    IT
    result() const
    {
      return _cvt(Base::result());
    }
  };

  // Try to make the errors less cryptic if someone uses a non-integral type by mistake.

  struct for_marshal_unmarshal_integral_only_use_integral_type_for_IT_template_parameter {
    for_marshal_unmarshal_integral_only_use_integral_type_for_IT_template_parameter();
  };

  template <typename IT, bool IsSigned>
  class Marshal<IT, IsSigned, false> : public for_marshal_unmarshal_integral_only_use_integral_type_for_IT_template_parameter
  {
  };

  template <typename IT, bool IsSigned>
  class Unmarshal<IT, IsSigned, false> : public for_marshal_unmarshal_integral_only_use_integral_type_for_IT_template_parameter
  {
  };

} // end namespace MarshalIntegralImpl

// Marshal an integral value into a sequence of bytes.  'IT' is the integral type holding the value.  No byte in the sequence will
// be zero.  (The value 0 translates to a sequence of zero length.)
//
// Public Members:
// MarshalIntegral<IT>(IT) - construct with value to be converted to a byte sequence.
// unsigned char next() - returns the next byte in the sequence.
// char cNext() - same as next(), except the return value is converted to type char.
// bool done() - returns true when the sequence is complete (no more calls to next()/cNext() are needed).  Calls to next()/cNext()
//   when done() is true will return 0.
//
template <typename IT>
using MarshalIntegral = MarshalIntegralImpl::Marshal<IT, std::is_signed<IT>::value, std::is_integral<IT>::value>;

// Unmarshal from a byte sequence back to an integral value.  This will work even if marshaling and unmarshaling are done on
// architectures of different endiance.  If the type IT has enough precision to hold the value, it doesn't matter if it's not the
// same type passed to the MarshalIntegral template.  But the signed-ness of the types must match when marshaling and then
// unmarshaling, even if the value is not negative.  Unmarshaling a 1-length sequence containing a zero byte will result in a
// value of zero.
//
// Public members:
// UnmarshalIntegral<IT>() - parameterless constructor.
// bool next(unsigned char) - pass in the next byte of the sequence.  Returns true unless the byte was the last byte in the
//   sequence.
// bool cNext(char) - same as next() except the parameter is of type char.
// IT result() - returns the unmarshaled value after all bytes in the sequence have been passed to next()/cNext().
//
template <typename IT>
using UnmarshalIntegral = MarshalIntegralImpl::Unmarshal<IT, std::is_signed<IT>::value, std::is_integral<IT>::value>;

// Marshal bytes into 'in', which has a << insertion operator.  This will insert a nul char if val == 0.
//
template <class Insertable, typename IT>
void
marshalInsert(Insertable &in, IT val)
{
  MarshalIntegral<IT> m(val);

  do {
    in << m.cNext();

  } while (!m.done());
}

template <typename IT>
IT
unmarshalFromArr(const char *p)
{
  UnmarshalIntegral<IT> u;

  while (u.cNext(*(p++)))
    ;

  return (u.result());
}

template <typename IT>
void
unmarshalFromArr(const char *p, IT &result)
{
  result = unmarshalFromArr<IT>(p);
}

} // end namespace LogUtils

#endif // Include once.
