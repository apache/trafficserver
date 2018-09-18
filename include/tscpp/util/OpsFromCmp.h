/** @file

   Convenient definition of comparison operators for user-defined types.

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

// Defines all comparison operators for const references to TYPE.  For op1, op2 instances of TYPE, cmp(op1, op2) must
// be a valid expression that returns negative if op1 < op2, zero if op1 == op2, positive if op1 > op2.
//
#define TS_DEFINE_CMP_OPS(TYPE) TS_DEFINE_CMP_OPS_2T_INORDER(TYPE, TYPE)

// Defines all comparison operators between const references of TYPE1 and TYPE2.  For op1 an instance of TYPE1 and op2
// an instance of TYPE2, cmp(op1, op2) must be a valid expression that returns negative if op1 < op2, zero if op1 == op2,
// positive if op1 > op2.
//
#define TS_DEFINE_CMP_OPS_2T(TYPE1, TYPE2)   \
  TS_DEFINE_CMP_OPS_2T_INORDER(TYPE1, TYPE2) \
  TS_DEFINE_CMP_OPS_2T_REVERSE(TYPE1, TYPE2)

// Defines all comparison operators where the first operand is a const reference to TYPE1, and the second operand is a
// const reference to TYPE2.  cmp(op1, op2) must be a valid expression that returns negative if op1 < op2, zero if
// op1 == op2, positive if op1 > op2.
//
#define TS_DEFINE_CMP_OPS_2T_INORDER(TYPE1, TYPE2)                                          \
                                                                                            \
  inline bool operator==(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) == 0; } \
                                                                                            \
  inline bool operator!=(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) != 0; } \
                                                                                            \
  inline bool operator>(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) > 0; }   \
                                                                                            \
  inline bool operator>=(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) >= 0; } \
                                                                                            \
  inline bool operator<(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) < 0; }   \
                                                                                            \
  inline bool operator<=(TYPE1 const &op1, TYPE2 const &op2) { return cmp(op1, op2) <= 0; }

// Defines all comparison operators where the first operand is a const reference to TYPE2, and the second operand is a
// const reference to TYPE2.  cmp(op2, op1) must be a valid expression that returns negative if op2 < op1, zero if
// op2 == op1, positive if op2 > op1.
//
#define TS_DEFINE_CMP_OPS_2T_REVERSE(TYPE1, TYPE2)                                          \
                                                                                            \
  inline bool operator==(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) == 0; } \
                                                                                            \
  inline bool operator!=(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) != 0; } \
                                                                                            \
  inline bool operator>(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) < 0; }   \
                                                                                            \
  inline bool operator>=(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) <= 0; } \
                                                                                            \
  inline bool operator<(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) > 0; }   \
                                                                                            \
  inline bool operator<=(TYPE2 const &op1, TYPE1 const &op2) { return cmp(op2, op1) >= 0; }

// NOTE: See src/tscpp/util/unit_tests/test_OpsFromCmp.cc for examples of use.
