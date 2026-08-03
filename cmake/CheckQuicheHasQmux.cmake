#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

# quiche.h always declares quiche_config_enable_qmux(), regardless of whether quiche was built
# with its `qmux` Rust feature -- only the compiled library conditionally exports the symbol. So
# this must be a full compile-and-link check against the actual quiche library, not a header-only
# check, or it would report qmux support as available even when the linked quiche lacks it.
function(CHECK_QUICHE_HAS_QMUX OUT_VAR)
  set(CHECK_PROGRAM
      "
        #include <quiche.h>

        int main() {
            quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
            quiche_config_enable_qmux(config, true);
            return 0;
        }
        "
  )
  set(CMAKE_REQUIRED_LIBRARIES quiche::quiche)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("${CHECK_PROGRAM}" ${OUT_VAR})
  set(${OUT_VAR}
      ${${OUT_VAR}}
      PARENT_SCOPE
  )
endfunction()
