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

pkg set-publisher -g http://pkg.omniti.com/omniti-ms/ ms.omniti.com

pkg refresh

# Base ATS build dependencies.
pkg install \
  developer/gcc46 \
  developer/object-file \
  developer/linker \
  developer/library/lint \
  system/header \
  system/library/math/header-math \
  developer/lexer/flex \
  developer/parser/bison \
  developer/build/libtool \
  developer/versioning/git \
  omniti/runtime/tcl-8 \
  developer/build/automake-111 \
  developer/build/autoconf \
  developer/build/gnu-make || true

pkg update || true
