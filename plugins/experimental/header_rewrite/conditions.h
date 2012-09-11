//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Declarations for all conditionals / conditional values we support.
//
#ifndef __CONDITIONS_H__
#define __CONDITIONS_H__ 1

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__conditions_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>
#include <ts/ts.h>
#include <boost/lexical_cast.hpp>

#include "condition.h"
#include "matcher.h"
#include "value.h"
#include "lulu.h"
//#include <mdbm.h>


///////////////////////////////////////////////////////////////////////////////
// Condition declarations.
//

// Always true
class ConditionTrue : public Condition
{
public:
  ConditionTrue()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTrue");
  }

  void append_value(std::string& s, const Resources& res) { s += "TRUE";  }

protected:
  bool eval(const Resources& res) {
    TSDebug(PLUGIN_NAME, "Evaluating TRUE()");
    return true;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionTrue);
};


// Always false
class ConditionFalse : public Condition
{
public:
  ConditionFalse()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionFalse");
  }
  void append_value(std::string& s, const Resources& res) { s += "FALSE"; }

protected:
  bool eval(const Resources& res) {
    TSDebug(PLUGIN_NAME, "Evaluating FALSE()");
    return false;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionFalse);
};


// Check the HTTP return status
class ConditionStatus : public Condition
{
public:
  ConditionStatus()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionStatus");
  }
  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);
  void initialize_hooks(); // Return status only valid in certain hooks

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionStatus);
};


// Random 0 to (N-1)
class ConditionRandom : public Condition
{
public:
  ConditionRandom()
    : _seed(0), _max(0)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionRandom");
  }
  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionRandom);

  unsigned int _seed;
  unsigned int _max;
};


// access(file)
class ConditionAccess : public Condition
{
public:
  ConditionAccess()
    : _next(0), _last(false)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionAccess");
  }
  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionAccess);

  time_t _next;
  bool _last;
};


// header
class ConditionHeader : public Condition
{
public:
  explicit ConditionHeader(bool client = false)
    : _client(client)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionHeader, client %d", client);
  };

  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionHeader);

  bool _client;
};

// path 
class ConditionPath : public Condition
{
public:
  explicit ConditionPath()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionPath");
  };

  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionPath);
};















class ConditionQuery : public Condition
{
public:
  explicit ConditionQuery()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionQuery");
  };

  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionQuery);

};


// url
class ConditionUrl : public Condition
{
public:
  explicit ConditionUrl(bool client = false)
    : _url_qual(URL_QUAL_NONE), _client(client)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionUrl");
  };

  void initialize(Parser& p);
  void set_qualifier(const std::string& q);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionUrl);

  UrlQualifiers _url_qual;
  bool _client;
};


// DBM lookups
class ConditionDBM : public Condition
{
public:
  ConditionDBM()
    : 
    //_dbm(NULL),
      _file("")
  {
    _mutex = TSMutexCreate();
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionDBM");
  }

  ~ConditionDBM() {
    // if (_dbm) {
    //   mdbm_close(_dbm);
    //   _dbm = NULL;
    // }
  }

  void initialize(Parser& p);
  void append_value(std::string& s, const Resources& res);

protected:
  bool eval(const Resources& res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionDBM);

  //MDBM* _dbm;
  std::string _file;
  Value _key;
  TSMutex _mutex;
};

#endif // __CONDITIONS_H
