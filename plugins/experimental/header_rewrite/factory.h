//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Implement the classes for the various types of hash keys we support.
//
#ifndef __FACTORY_H__
#define __FACTORY_H__ 1

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__factory_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>

#include "operator.h"
#include "condition.h"

Operator* operator_factory(const std::string& op);
Condition* condition_factory(const std::string& cond);


#endif
