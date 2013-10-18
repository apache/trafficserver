/**
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

/**
 * @file InitializableValue.h
 */

#pragma once
#ifndef ATSCPPAPI_INITIALIZABLEVALUE_H_
#define ATSCPPAPI_INITIALIZABLEVALUE_H_

namespace atscppapi {

// cannot be static as InitializableValue is a template and a static
// member of that would be instantiated once for every type the
// template is instantiated
extern bool transaction_data_caching_enabled;

/**
 * @private
 */
template <typename Type> class InitializableValue {
public:
  InitializableValue() : initialized_(false) { }
  explicit InitializableValue(Type value, bool initialized = true) : value_(value), initialized_(initialized) { }

  inline void setValue(const Type &value) {
    value_ = value;
    initialized_ = true;
  }

  inline bool isInitialized() const {
#ifdef DISABLE_TRANSACTION_DATA_CACHING
    return false;
#endif
    return transaction_data_caching_enabled && initialized_;
  }

  inline Type &getValueRef() {
    return value_;
  }

  inline Type getValue() {
    return value_;
  }

  inline const Type &getValueRef() const {
    return value_;
  }

  inline void setInitialized(bool initialized = true) {
    initialized_ = initialized;
  }

  inline operator Type&() {
    return value_;
  }

  inline operator const Type&() const {
    return value_;
  }

  inline InitializableValue<Type> &operator=(const Type& value) {
    setValue(value);
    return *this;
  }

private:
  Type value_;
  bool initialized_;
};

}

#endif /* ATSCPPAPI_INITIALIZABLEVALUE_H_ */
