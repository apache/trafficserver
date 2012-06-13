#include "regex_helper.h"

bool
regexHelper::setRegexMatch(const std::string& s) 
{

    const char* errorComp = NULL;
    const char* errorStudy = NULL;
    int erroffset;

    regexString = s;
    regex = pcre_compile(regexString.c_str(), 0, &errorComp, &erroffset, NULL);

    if (regex == NULL) 
    {
        return false;
    }
    regexExtra = pcre_study(regex, 0, &errorStudy);
    if ((regexExtra == NULL) && (errorStudy != 0)) 
    {
        return false;
    }
    if (pcre_fullinfo(regex, regexExtra, PCRE_INFO_CAPTURECOUNT, &regexCcount) != 0)
        return false;
    return true;
  }

const std::string&
regexHelper::getRegexString() const 
{
    return regexString;
}

int
regexHelper::getRegexCcount() const 
{
    return regexCcount;
}

int
regexHelper::regexMatch(const char* str, int len, int ovector[]) const {
    return pcre_exec(regex,                // the compiled pattern
          regexExtra,          // Extra data from study (maybe)
          str,                  // the subject std::string
          len,                  // the length of the subject
          0,                    // start at offset 0 in the subject
          0,                    // default options
          ovector,              // output vector for substring information
          OVECCOUNT);           // number of elements in the output vector
  };


