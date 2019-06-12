#! /usr/bin/env bash
#
#  Fetch the plantuml JAR file from bintray.
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

# Version tag.
PKGDATE="1.2018.1"
PKG_EXT="tar.bz2"
TMPDIR=${TMPDIR:-/tmp}

function main() {
  set -e # exit on error
  PACKAGE="plantuml-${PKGDATE}"
  JAR="plantuml.jar"
  ROOT=${ROOT:-$(cd $(dirname $0) && git rev-parse --show-toplevel)/.git/doc-tools}

  URL=${URL:-https://ci.trafficserver.apache.org/bintray/${PACKAGE}.${PKG_EXT}}
  TAR=${TAR:-tar}
  CURL=${CURL:-curl}

  mkdir -p ${ROOT}
  cd ${ROOT}

  if [ ! -e ${PACKAGE} ] ; then
    # default to using native sha1sum command when available
    if [ $(which sha1sum) ] ; then
      SHASUM=${SHASUM:-sha1sum}
    else
      SHASUM=${SHASUM:-shasum}
    fi

    DL="${TMPDIR}/${PACKAGE}.${PKG_EXT}"
    rm -rf ${DL}
    ${CURL} -L --progress-bar -o ${DL} ${URL}
    cat > ${TMPDIR}/plantuml-sha-checksum << EOF
4dbf218641a777007f9bc72ca8017a41a23e1081  ${DL}
EOF
    ${SHASUM} -c ${TMPDIR}/plantuml-sha-checksum
    ${TAR} xf ${DL}
    cd ${PACKAGE}
  fi
  echo ${ROOT}/${PACKAGE}/${JAR}
}

main "$@"
