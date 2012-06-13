#ifndef REGEX_HELPER_H
#define REGEX_HELPER_H

#include <pcre.h>

#include <string>


const int OVECCOUNT = 30; // We support $1 - $9 only, and this needs to be 3x that

class regexHelper{
public:
    regexHelper():
        regex(NULL),regexExtra(NULL),regexCcount(0)
    {
    
    }

  ~regexHelper() 
  {
      if (regex)
          pcre_free(regex);

      if (regexExtra)
          pcre_free(regexExtra);
  }



bool setRegexMatch(const std::string &s);
const std::string& getRegexString() const;
int getRegexCcount() const;
int regexMatch(const char*,int,int ovector[]) const;

private:
  pcre* regex;
  pcre_extra* regexExtra;
  std::string regexString;
  int regexCcount;

};


#endif
