//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __MATCHER_H__
#define __MATCHER_H__ 1


#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__matcher_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>
#include <ts/ts.h>
#include "regex_helper.h"
#include <iostream> // For debugging

#include "lulu.h"


// Possible operators that we support (at least partially)
enum MatcherOps {
  MATCH_EQUAL,
  MATCH_LESS_THEN,
  MATCH_GREATER_THEN,
  MATCH_REGULAR_EXPRESSION
};


///////////////////////////////////////////////////////////////////////////////
// Base class for all Matchers (this is also the interface)
//
class Matcher
{
public:
  explicit Matcher(const MatcherOps op)
    : _pdata(NULL), _op(op)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Matcher");
  }

  virtual ~Matcher() {
    TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Matcher");
    free_pdata();
  }

  void set_pdata(void* pdata) { _pdata = pdata; }
  void* get_pdata() const { return _pdata; }
  virtual void free_pdata() { TSfree(_pdata); _pdata = NULL; }

protected:
  void* _pdata;
  const MatcherOps _op;

private:
  DISALLOW_COPY_AND_ASSIGN(Matcher);
};

// Template class to match on various types of data
template<class T>
class Matchers : public Matcher
{
public:
  explicit Matchers<T>(const MatcherOps op)
    : Matcher(op)
  { }

  // Getters / setters
  const T get() const { return _data; };
 
 void setRegex(const std::string data)
 {
     if(!helper.setRegexMatch(_data))
     {
         std::cout<<"Invalid regex:failed to precompile"<<std::endl;
         abort();
     }
     TSDebug(PLUGIN_NAME,"Regex precompiled successfully");

 }

 void setRegex(const unsigned int t)
 {
     return;
 }

 void setRegex(const TSHttpStatus t)
 {
     return;
 }

  void set (const T d)
  { 
      _data = d;
      if(_op == MATCH_REGULAR_EXPRESSION)
        setRegex(d);
  }

  // Evaluate this matcher
  bool
  test(const T t) const {
    switch (_op) {
    case MATCH_EQUAL:
      return test_eq(t);
      break;
    case MATCH_LESS_THEN:
      return test_lt(t);
      break;
    case MATCH_GREATER_THEN:
      return test_gt(t);
      break;
    case MATCH_REGULAR_EXPRESSION:
      return test_reg(t);
      break;
    default:
      // ToDo: error
      break;
    }
    return false;
  }

private:
  // For basic types
  bool test_eq(const T t) const {
    // std::cout << "Testing: " << t << " == " << _data << std::endl;
    return t == _data;
  }
  bool test_lt(const T t) const {
    // std::cout << "Testing: " << t << " < " << _data << std::endl;
    return t < _data;
  }
  bool test_gt(const T t) const {
    // std::cout << "Testing: " << t << " > " << _data << std::endl;
    return t > _data;
  }

 bool test_reg(const unsigned int t) const {
    // Not support
    return false;
  }

  bool test_reg(const TSHttpStatus t) const {
    // Not support
    return false;
  }
  
  bool test_reg(const std::string t) const {
      TSDebug(PLUGIN_NAME, "Test regular expression %s : %s", _data.c_str(), t.c_str());
          int ovector[OVECCOUNT];
          if (helper.regexMatch(t.c_str(), t.length(), ovector) > 0) {
              TSDebug(PLUGIN_NAME, "Successfully found regular expression match");
              return true;
    }
      return false;
  }
  T _data;
  regexHelper helper;
};

#endif // __MATCHER_H
