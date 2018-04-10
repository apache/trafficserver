#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

Clang_Tidy_Options = -fix -fix-errors -header-filter=.*

# Sort the filenames to remove duplicates, then filter to retain
# just the C and C++ sources so we don't pick up lex and yacc files
# for example.

Clang_Tidy_CC_Files = $(filter %.c, $(sort $(1)))
Clang_Tidy_CXX_Files = $(filter %.cc, $(sort $(1)))

#clang-tidy rules. We expect these to be actions with something like
#$(DIST_SOURCES) as the dependencies.rules. Note that $DIST_SOURCES
#is not an automake API, it is an implementation detail, but it ought
#to be stable enough.
#
#All this clearly requires GNU make.

CXX_Clang_Tidy = $(CLANG_TIDY) $(Clang_Tidy_Options) $(call Clang_Tidy_CXX_Files,$^) -- $(CXXCOMPILE) -x c++
CC_Clang_Tidy = $(CLANG_TIDY) $(Clang_Tidy_Options) $(call Clang_Tidy_CC_Files,$^) -- $(COMPILE) -x c
