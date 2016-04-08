#! /usr/bin/env bash
#
#  Simple wrapper to run clang-format on a bunch of files
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

set -e # exit on error

DIR=${1:-.}
ROOT=${ROOT:-$(git rev-parse --show-toplevel)/.git/fmt}
URL=${URL:-https://bintray.com/artifact/download/apache/trafficserver/clang-format-20150331.tar.bz2}

TAR=${TAR:-tar}
CURL=${CURL:-curl}

# default to using native sha1sum command when available
if [ $(which sha1sum) ] ; then
  SHASUM=${SHASUM:-sha1sum}
else
  SHASUM=${SHASUM:-shasum}
fi

ARCHIVE=$ROOT/$(basename ${URL})

case $(uname -s) in
Darwin)
  FORMAT=${FORMAT:-${ROOT}/clang-format/clang-format.osx}
  ;;
Linux)
  FORMAT=${FORMAT:-${ROOT}/clang-format/clang-format.linux}
  ;;
*)
  echo "Leif needs to build a clang-format for $(uname -s)"
  exit 2
esac

mkdir -p ${ROOT}

if [ ! -e ${FORMAT} ] ; then
  ${CURL} -L --progress-bar -o ${ARCHIVE} ${URL}
  ${TAR} -x -C ${ROOT} -f ${ARCHIVE}
  cat > ${ROOT}/sha1 << EOF
7117c5bed99da43be733427970b4239f4bd8063d  ${ARCHIVE}
EOF
  ${SHASUM} -a 1 -c ${ROOT}/sha1
fi

for file in $(find $DIR -iname \*.[ch] -o -iname \*.cc); do
    echo $file
    ${FORMAT} -i $file
done
