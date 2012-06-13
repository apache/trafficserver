//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __RULESET_H__
#define __RULESET_H__ 1


#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__ruleset_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>

#include "matcher.h"
#include "factory.h"
#include "resources.h"
#include "parser.h"


///////////////////////////////////////////////////////////////////////////////
// Class holding one ruleset. A ruleset is one (or more) pre-conditions, and
// one (or more) operators.
//
class RuleSet
{
public:
  RuleSet()
    : next(NULL), _cond(NULL), _oper(NULL), _hook(TS_HTTP_READ_RESPONSE_HDR_HOOK), _ids(RSRC_NONE),
      _opermods(OPER_NONE), _last(false)
  { };

  // No reason to inline these
  void append(RuleSet* rule);

  void add_condition(Parser& p);
  void add_operator(Parser& p);
  bool has_operator() const { return NULL != _oper; }
  bool has_condition() const { return NULL != _cond; }

  void set_hook(TSHttpHookID hook) { _hook = hook; }
  const TSHttpHookID get_hook() const { return _hook; }

  // Inline
  const ResourceIDs get_all_resource_ids() const {
    return _ids;
  }

  bool eval(const Resources& res) const {
    if (NULL == _cond) {
      return true;
    } else {
      return _cond->do_eval(res);
    }
  }

  bool last() const {
    return _last;
  }

  OperModifiers exec(const Resources& res) const {
    _oper->do_exec(res);
    return _opermods;
  }

  RuleSet* next; // Linked list

private:
  DISALLOW_COPY_AND_ASSIGN(RuleSet);

  Condition* _cond; // First pre-condition (linked list)
  Operator* _oper; // First operator (linked list)
  TSHttpHookID _hook; // Which hook is this rule for

  // State values (updated when conds / operators are added)
  ResourceIDs _ids;
  OperModifiers _opermods;
  bool _last;
};


#endif // __RULESET_H
