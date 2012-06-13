//////////////////////////////////////////////////////////////////////////////////////////////
// 
// Interface for the config line parser
//
#ifndef __PARSER_H__
#define __PARSER_H__ 1


#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__parser_h[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <string>
#include <vector>
#include <algorithm>

#include "lulu.h"


///////////////////////////////////////////////////////////////////////////////
//
class Parser
{
public:
  explicit Parser(const std::string& line);

  bool empty() const { return _empty; }
  bool is_cond() const { return _cond; }

  bool cond_op_is(const std::string s) const { return _cond && (_op == s); }
  bool oper_op_is(const std::string s) const { return !_cond && (_op == s); }

  const std::string& get_op() const { return _op; }
  std::string& get_arg() { return _arg; }
  const std::string& get_value() const { return _val; }

  bool mod_exist(const std::string m) const {
    return (std::find(_mods.begin(), _mods.end(), m) != _mods.end());
  }

private:
  void preprocess(std::vector<std::string>& tokens);
  DISALLOW_COPY_AND_ASSIGN(Parser);

  bool _cond;
  bool _empty;
  std::vector<std::string> _mods;
  std::string _op;
  std::string _arg;
  std::string _val;
};


#endif // __PARSER_H
