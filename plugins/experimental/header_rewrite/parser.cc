//////////////////////////////////////////////////////////////////////////////////////////////
// parser.cc: implementation of the config parser
//
//
#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__parser_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <utility>
#include <boost/tokenizer.hpp>

#include <ts/ts.h>

#include "parser.h"

// Tokenizer separators
static boost::escaped_list_separator<char> token_sep('\\', ' ', '\"');
static boost::char_separator<char> comma_sep(",");


// This is the core "parser", parsing rule sets
void
Parser::preprocess(std::vector<std::string>& tokens)
{
  // Special case for "conditional" values
  if (tokens[0].substr(0, 2) == "%{") {
    _cond = true;
  } else if (tokens[0] == "cond") {
    _cond = true;
    tokens.erase(tokens.begin());
  }

  // Is it a condition or operator?
  if (_cond) {
    if ((tokens[0].substr(0, 2) == "%{") && (tokens[0][tokens[0].size() - 1] == '}')) {
      std::string s = tokens[0].substr(2, tokens[0].size() - 3);

      _op = s;
      if (tokens.size() > 1)
        _arg = tokens[1];
      else
        _arg = "";
    } else {
      TSError("header_rewrite: conditions must be embraced in %%{}");
      return;
    }
  } else {
    // Operator has no qualifiers, but could take an optional second argumetn
    _op = tokens[0];
    if (tokens.size() > 1) {
      _arg = tokens[1];
      if (tokens.size() > 2)
        _val = tokens[2];
      else
        _val = "";
    } else {
      _arg = "";
      _val = "";
    }
  }

  // The last token might be the "flags" section
  if (tokens.size() > 0) {
    std::string m  = tokens[tokens.size() - 1];

    if (!m.empty() && (m[0] == '[')) {
      if (m[m.size() - 1] == ']') {
        m = m.substr(1, m.size() - 2);
        if (m.find_first_of(',') != std::string::npos) {
          boost::tokenizer<boost::char_separator<char> > mods(m, comma_sep);
          std::copy(mods.begin(), mods.end(), std::back_inserter(_mods));
        } else {
          _mods.push_back(m);
        }
      } else {
        // Syntax error
        TSError("header_rewrite: mods have to be embraced in []");
        return;
      }
    }
  }
}


Parser::Parser(const std::string& line) :
  _cond(false), _empty(false)
{
  TSDebug("header_rewrite_dbg", "Calling CTOR for Parser");

  if (line[0] == '#') {
    _empty = true;
  } else {
    boost::tokenizer<boost::escaped_list_separator<char> > elem(line, token_sep);
    std::vector<std::string> tokens;

    for (boost::tokenizer<boost::escaped_list_separator<char> >::iterator it = elem.begin(); it != elem.end(); it++) {
      // Skip "empty" tokens (tokenizer is a little brain dead IMO)
      if ((*it) != "")
        tokens.push_back(*it);
    }

    if (tokens.empty()) {
      _empty = true;
    } else {
      preprocess(tokens);
    }
  }
}
