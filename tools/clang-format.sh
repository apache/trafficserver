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
PKGDATE="20230424"

function main() {
  set -e # exit on error
  ROOT=${ROOT:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/.git/fmt/${PKGDATE}}
  # The presence of this file indicates clang-format was successfully installed.
  INSTALLED_SENTINEL=${ROOT}/.clang-format-installed

  # Check for the option to just install clang-format without running it.
  just_install=0
  if [ "$1" = "--install" ] ; then
    just_install=1
    if [ $# -ne 1 ] ; then
      echo "No other arguments should be used with --install."
      exit 2
    fi
  fi
  DIR="${*:-.}"
  PACKAGE="clang-format-${PKGDATE}.tar.bz2"
  VERSION="clang-format version 16.0.2 (https://github.com/llvm/llvm-project.git 18ddebe1a1a9bde349441631365f0472e9693520)"

  URL=${URL:-https://ci.trafficserver.apache.org/bintray/${PACKAGE}}

  TAR=${TAR:-tar}
  CURL=${CURL:-curl}

  # Default to sha256sum, but honor the env variable just in case
  if [ -n "$(which sha256sum)" ] ; then
    SHASUM=${SHASUM:-sha256sum}
  else
    SHASUM=${SHASUM:-shasum -a 256}
  fi

  ARCHIVE="$ROOT/$(basename "${URL}")"

  case $(uname -s) in
  Darwin)
    FORMAT=${FORMAT:-${ROOT}/clang-format/clang-format.macos.$(uname -m)}
    ;;
  Linux)
    FORMAT=${FORMAT:-${ROOT}/clang-format/clang-format.linux.$(uname -m)}
    ;;
  *)
    echo "Leif needs to build a clang-format for $(uname -s)"
    exit 2
  esac

  mkdir -p "${ROOT}"

  # Note that the two spaces between the hash and ${ARCHIVE) is needed
  if [ ! -e "${FORMAT}" ] || [ ! -e "${ROOT}/${PACKAGE}" ] ; then
    ${CURL} -L --progress-bar -o "${ARCHIVE}" "${URL}"
    ${TAR} -x -C "${ROOT}" -f "${ARCHIVE}"
    cat > "${ROOT}/sha256" << EOF
e6530f9f4ddc61d8de9b6f980ec01656a2c998a83bb9b29323c04ba2232e8f25  ${ARCHIVE}
EOF
    ${SHASUM} -c "${ROOT}/sha256"
    chmod +x "${FORMAT}"
  fi


  # Make sure we only run this with our exact version
  ver=$(${FORMAT} --version)
  if [ "$ver" != "$VERSION" ]; then
      echo "Wrong version of clang-format!"
      echo "Contact the ATS community for help and details about clang-format versions."
      exit 1
  fi
  touch "${INSTALLED_SENTINEL}"
  [ ${just_install} -eq 1 ] && return

  # Efficiently retrieving modification timestamps in a platform
  # independent way is challenging. We use find's -newer argument, which
  # seems to be broadly supported. The following file is created and has a
  # timestamp just before running clang-format. Any file with a timestamp
  # after this we assume was modified by clang-format.
  start_time_file=$(mktemp -t clang-format-start-time.XXXXXXXXXX)
  touch "${start_time_file}"

  # shellcheck disable=SC2086
  target_files=$(find $DIR -iname '*.[ch]' -o -iname '*.cc' -o -iname '*.h.in')
  for file in ${target_files}; do
    # The ink_autoconf.h and ink_autoconf.h.in files are generated files,
    # so they do not need to be re-formatted by clang-format. Doing so
    # results in make rebuilding all our files, so we skip formatting them
    # here.
    case $(basename "${file}") in
    ink_autoconf.h.in|ink_autoconf.h)
        ;;
    *)
        ${FORMAT} -i "$file"
        ;;
    esac

  done

  # shellcheck disable=SC2086
  find ${target_files} -newer "${start_time_file}"
  rm "${start_time_file}"
}

if [[ "$(basename -- "$0")" == 'clang-format.sh' ]]; then
  main "$@"
else
  ROOT=${ROOT:-$(git rev-parse --show-toplevel)/.git/fmt/${PKGDATE}}
fi
