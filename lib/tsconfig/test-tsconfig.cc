# include "tsconfig/TsValue.h"
# include <stdio.h>
# include <iostream>

using ts::config::Configuration;
using ts::config::Value;

inline std::ostream& operator << ( std::ostream& s, ts::ConstBuffer const& b ) {
  if (b._ptr) s.write(b._ptr, b._size);
  else s << b._size;
  return s;
}

int main(int argc, char ** argv) {
  printf("Testing TsConfig\n");
  ts::Rv<Configuration> cv = Configuration::loadFromPath("test-1.tsconfig");
  if (cv.isOK()) {
    Value v = cv.result().find("thing-1.name");
    if (v) {
      std::cout << "thing-1.name = " << v.getText() << std::endl;
    } else {
      std::cout << "Failed to find 'name' in 'thing-1'" << std::endl;
    }
  } else {
    std::cout << "Load failed" << std::endl
              << cv.errata()
      ;
  }
}
