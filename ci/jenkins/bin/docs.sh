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

# These shenanigans are here to allow it to run both manually, and via Jenkins
test -z "${ATS_MAKE}" && ATS_MAKE="make"
test ! -z "${WORKSPACE}" && cd "${WORKSPACE}/src"

# Skip if nothing in doc has changed
INCLUDE_FILES=$(for i in $(git grep literalinclude doc/ | awk '{print $3}'); do basename $i; done | sort -u | paste -sd\|)
echo $INCLUDE_FILES
if [ ! -z "$ghprbActualCommit" ]; then
    git diff ${ghprbActualCommit}^...${ghprbActualCommit} --name-only | egrep -E "(^doc/|$INCLUDE_FILES)" > /dev/null
    if [ $? = 1 ]; then
        echo "No relevant files changed, skipping run"
        exit 0
    fi
fi

# Run configure on the docs builds each time in case there have been updates
autoreconf -fi && ./configure --enable-docs || exit 1

cd doc

echo "Building English version"
rm -rf docbuild/html
${ATS_MAKE} -e SPHINXOPTS="-D language='en'" html
[ $? != 0 ] && exit 1

# Only continue with the rsync and JA build if we're on the official docs updates
[ -w /home/docs ] || exit 0

/usr/bin/rsync --delete -av docbuild/html/ /home/docs/en/${ATS_BRANCH}

echo "Building JA version"
rm -rf docbuild/html
${ATS_MAKE} -e SPHINXOPTS="-D language='ja'" html
[ $? != 0 ] && exit 1
/usr/bin/rsync --delete -av docbuild/html/ /home/docs/ja/${ATS_BRANCH}
