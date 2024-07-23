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

# Update the PKGDATE with the new version date when making a new clang-format binary package.
PKGDATE="20200514"

function main() {
  set -e # exit on error
  ROOT=${ROOT:-$(cd $(dirname $0) && git rev-parse --show-toplevel)/.git/fmt/${PKGDATE}}
  # The presence of this file indicates clang-format was successfully installed.
  INSTALLED_SENTINEL=${ROOT}/.clang-format-installed

  # Check for the option to just install clang-format without running it.
  just_install=0
  if [ $# -gt 0 ] && [ $1 = "--install" ] ; then
    just_install=1
    if [ $# -ne 1 ] ; then
      echo "No other arguments should be used with --install."
      exit 2
    fi
  fi
  DIR=${@:-.}
  PACKAGE="clang-format-${PKGDATE}.tar.bz2"
  VERSION="clang-format version 10.0.0 (https://github.com/llvm/llvm-project.git d32170dbd5b0d54436537b6b75beaf44324e0c28)"

  URL=${URL:-https://ci.trafficserver.apache.org/bintray/${PACKAGE}}

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

  # Note that the two spaces between the hash and ${ARCHIVE) is needed
  if [ ! -e ${FORMAT} -o ! -e ${ROOT}/${PACKAGE} ] ; then
    ${CURL} -L --progress-bar -o ${ARCHIVE} ${URL}
    ${TAR} -x -C ${ROOT} -f ${ARCHIVE}
    cat > ${ROOT}/sha1 << EOF
5eec43e5c7f3010d6e6f37639491cabe51de0ab2  ${ARCHIVE}
EOF
    ${SHASUM} -c ${ROOT}/sha1
    chmod +x ${FORMAT}
  fi


  # Make sure we only run this with our exact version
  ver=$(${FORMAT} --version)
  if [ "$ver" != "$VERSION" ]; then
      echo "Wrong version of clang-format!"
      echo "See https://bintray.com/apache/trafficserver/clang-format-tools/view for a newer version,"
      echo "or alternatively, undefine the FORMAT environment variable"
      exit 1
  fi
  touch ${INSTALLED_SENTINEL}
  [ ${just_install} -eq 1 ] && return

  # Efficiently retrieving modification timestamps in a platform
  # independent way is challenging. We use find's -newer argument, which
  # seems to be broadly supported. The following file is created and has a
  # timestamp just before running clang-format. Any file with a timestamp
  # after this we assume was modified by clang-format.
  start_time_file=$(mktemp -t clang-format-start-time.XXXXXXXXXX)
  touch ${start_time_file}

  target_files=$(find $DIR -iname \*.[ch] -o -iname \*.cc -o -iname \*.h.in)
  for file in ${target_files}; do
    # The ink_autoconf.h and ink_autoconf.h.in files are generated files,
    # so they do not need to be re-formatted by clang-format. Doing so
    # results in make rebuilding all our files, so we skip formatting them
    # here.
    base_name=$(basename ${file})
    [ ${base_name} = 'ink_autoconf.h.in' -o ${base_name} = 'ink_autoconf.h' ] && continue

    ${FORMAT} -i $file
  done

  find ${target_files} -newer ${start_time_file}
  rm ${start_time_file}
}

if [[ "$(basename -- "$0")" == 'clang-format.sh' ]]; then
  main "$@"
else
  ROOT=${ROOT:-$(git rev-parse --show-toplevel)/.git/fmt/${PKGDATE}}
fi
