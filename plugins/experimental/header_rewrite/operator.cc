//////////////////////////////////////////////////////////////////////////////////////////////
// operator.cc: Implementation of the operator base class
//
//

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__operator_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <ts/ts.h>
#include "operator.h"

const OperModifiers
Operator::get_oper_modifiers() const {
  if (_next)
    return static_cast<OperModifiers>(_mods | static_cast<Operator*>(_next)->get_oper_modifiers());

  return _mods;
}

void
Operator::initialize(Parser& p) {
  Statement::initialize(p);

  if (p.mod_exist("L")) {
    _mods = static_cast<OperModifiers>(_mods | OPER_LAST);
  }

  if (p.mod_exist("QSA")) {
    _mods = static_cast<OperModifiers>(_mods | OPER_QSA);
  }

}
