#!/bin/sh
#
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

set +x
cd "${WORKSPACE}/src"

# First, make sure there are no trailing WS!!!
git grep -IE ' +$' | \
  fgrep -v '.gold:' | \
  fgrep -v '.test_input'
if [ "1" != "$?" ]; then
    echo "Error: Trailing whitespaces are not allowed!"
    echo "Error: Please run: git grep -IE ' +$'"
    exit 1
fi
echo "Success! No trailing whitespace"

# Make sure there are no DOS shit here.
git grep -IE $'\r$' | \
    fgrep -v 'lib/yamlcpp' | \
    fgrep -v '.test_input'
if [ "1" != "$?" ]; then
    echo "Error: Please make sure to run dos2unix on the above file(s)"
    exit 1
fi
echo "Success! No DOS carriage return"

set -x

autoreconf -if && ./configure
[ "0" != "$?" ] && exit 1

${ATS_MAKE} clang-format
[ "0" != "$?" ] && exit 1

# Older branches didn't have either autopep8 or yapf. Only run these checks on
# branches where the pre-commit hook was updated to check them.
if grep -q autopep8 tools/git/pre-commit; then
    ${ATS_MAKE} autopep8
    [ "0" != "$?" ] && exit 1
fi

if grep -q yapf tools/git/pre-commit; then
    ${ATS_MAKE} yapf
    [ "0" != "$?" ] && exit 1
fi

git diff --exit-code
[ "0" != "$?" ] && exit 1

# Normal exit
exit 0
