//////////////////////////////////////////////////////////////////////////////////////////////
// condition.cc: Implementation of the condition base class
//
//

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__condition_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <ts/ts.h>
#include <string>

#include "condition.h"


static MatcherOps
parse_matcher_op(std::string& arg)
{
  switch (arg[0]) {
  case '=':
    arg.erase(0,1);
    return MATCH_EQUAL;
    break;
  case '<':
    arg.erase(0,1);
    return MATCH_LESS_THEN;
    break;
  case '>':
    arg.erase(0,1);
    return MATCH_GREATER_THEN;
    break;
  case '/':
    arg.erase(0,1);
    arg.erase(arg.length() -1 , arg.length());
    return MATCH_REGULAR_EXPRESSION;
    break;
  default:
    return MATCH_EQUAL;
    break;
  }
}


void
Condition::initialize(Parser& p)
{
  Statement::initialize(p);

  if (p.mod_exist("OR")) {
    if (p.mod_exist("AND")) {
      TSError("header_rewrite: Can't have both AND and OR in mods");
    } else {
      _mods = static_cast<CondModifiers>(_mods | COND_OR);
    }
  } else if (p.mod_exist("AND")) {
    _mods = static_cast<CondModifiers>(_mods | COND_AND);
  }

  if (p.mod_exist("NOT")) {
    _mods = static_cast<CondModifiers>(_mods | COND_NOT);
  }

  if (p.mod_exist("L")) {
    _mods = static_cast<CondModifiers>(_mods | COND_LAST);
  }

  _cond_op = parse_matcher_op(p.get_arg());
}
