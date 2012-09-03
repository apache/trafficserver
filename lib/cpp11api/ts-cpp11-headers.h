/*
 * ts-cpp11-headers.h
 *
 *  Created on: Sep 2, 2012
 *      Author: bgeffon
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
typedef std::vector<Header> HeaderVector;

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

  int getNumValues() {
    return field_values_.size();
  }

  std::string getValue(int valueindx) {
    return field_values_[valueindx];
  }

  std::string getName() {
    return name_;
  }

  std::string getJoinedValues() {
    std::string ret;

    for (std::vector<std::string>::size_type i = 0; i < field_values_.size();
        ++i) {
      if (i > 0)
        ret.append(",");
      ret.append(field_values_[i]);
    }
    return ret;
  }

  std::vector<std::string> getValues() {
    return field_values_;
  }
};


}
}
}

#endif /* TS_CPP11_HEADERS_H_ */
