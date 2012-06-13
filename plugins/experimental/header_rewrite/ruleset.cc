//////////////////////////////////////////////////////////////////////////////////////////////
// ruleset.cc: implementation of the ruleset class
//
//
#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__ruleset_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>

#include "ruleset.h"
#include "factory.h"


///////////////////////////////////////////////////////////////////////////////
// Class implementation (no reason to have these inline)
//
void
RuleSet::append(RuleSet* rule) {
  RuleSet* tmp = this;

  TSReleaseAssert(rule->next == NULL);

  while (tmp->next)
    tmp = tmp->next;
  tmp->next = rule;
}


void
RuleSet::add_condition(Parser& p) {
  Condition* c = condition_factory(p.get_op());

  if (NULL != c) {
    TSDebug(PLUGIN_NAME, "Adding condition: %%{%s} with arg: %s\n", p.get_op().c_str(), p.get_arg().c_str());
    c->initialize(p);
    if (!c->set_hook(_hook)) {
      TSError("header_rewrite: can't use this condition in this hook");
      return;
    }
    if (NULL == _cond) {
      _cond = c;
    } else {
      _cond->append(c);
    }

    // Update some ruleset state based on this new condition
    _last |= c->last();
    _ids = static_cast<ResourceIDs>(_ids | _cond->get_resource_ids());
  }
}


void
RuleSet::add_operator(Parser& p) {
  Operator* o = operator_factory(p.get_op());

  if (NULL != o) {
    // TODO: This should be extended to show both the "argument" and the "value" (if both are used)
    TSDebug(PLUGIN_NAME, "Adding operator: %s(%s)\n", p.get_op().c_str(), p.get_arg().c_str());
    o->initialize(p);
    if (!o->set_hook(_hook)) {
      TSError("header_rewrite: can't use this operator in this hook");
      return;
    }
    if (NULL == _oper) {
      _oper = o;
    } else {
      _oper->append(o);
    }

    // Update some ruleset state based on this new operator
    _opermods = static_cast<OperModifiers>(_opermods | _oper->get_oper_modifiers());
    _ids = static_cast<ResourceIDs>(_ids | _oper->get_resource_ids());
  }
}
