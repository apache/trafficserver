/** @file
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

#ifndef TS_CPP11_HEADERS_H_
#define TS_CPP11_HEADERS_H_
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace ats {
namespace api {
namespace headers {

class Header;
typedef typename std::vector<Header> HeaderVector;

class Header {
private:
  std::string name_;
  std::vector<std::string> field_values_;
public:
  Header() {
  }
  Header(const std::string &name, const std::string &value) {
    name_ = name;
    addNewValue(value);
  }

  Header(std::string name, const std::vector<std::string> & values) {
    name_ = name;
    field_values_ = values;
  }

  void assignName(const std::string & name) {
    name_ = name;
  }

  void assignName(const char *buf, size_t len) {
    name_.assign(buf, len);
  }

  void addNewValue(const char *buf, size_t len) {
    std::string newVal(buf, len);
    field_values_.push_back(newVal);
  }

  void addNewValue(std::string value) {
    field_values_.push_back(value);
  }

  int getNumValues() const {
    return field_values_.size();
  }

  std::string getValue(int valueindx) const{
    return field_values_[valueindx];
  }

  std::string getName() const {
    return name_;
  }

  std::string getJoinedValues() const {
    std::string ret;

    for (std::vector<std::string>::size_type i = 0; i < field_values_.size();
        ++i) {
      if (i > 0)
        ret.append(",");
      ret.append(field_values_[i]);
    }
    return ret;
  }

  std::vector<std::string> getValues() const {
    return field_values_;
  }
};



/*
 *  predicates
 */
class HeaderName: public std::unary_function<Header, bool> { // could probably be replaced with mem_ptr_fun()..
private:
  std::string name_;
public:
  HeaderName(std::string name) :
      name_(name) {
  }

  bool operator()(const Header &field) const {
    return (field.getName() == name_);
  }
};

}
}
}

#endif /* TS_CPP11_HEADERS_H_ */
