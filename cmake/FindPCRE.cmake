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


find_path(PCRE_INCLUDE_DIR NAMES pcre.h PATH_SUFFIXES pcre)
find_library(PCRE_LIBRARY NAMES pcre)

message(PCRE_INCLUDE_DIR=${PCRE_INCLUDE_DIR})
message(PCRE_LIBRARY=${PCRE_LIBRARY})

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PCRE DEFAULT_MSG PCRE_LIBRARY PCRE_INCLUDE_DIR)

if(PCRE_FOUND)
    SET(PCRE_LIBRARIES ${PCRE_LIBRARY})
    SET(PCRE_INCLUDE_DIRS ${PCRE_INCLUDE_DIR})
else(PCRE_FOUND)
    SET(PCRE_LIBRARIES)
    SET(PCRE_INCLUDE_DIRS)
endif(PCRE_FOUND)
