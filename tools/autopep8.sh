#! /usr/bin/env bash
#
#  Simple wrapper to run autopep8 on a directory.
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

# Update the PKGVERSION with the new desired autopep8 tag when a new autopep8
# version is desired.
# See:
# https://github.com/hhatto/autopep8/tags
AUTOPEP8_VERSION="1.5.3"

VERSION="autopep8 1.5.3 (pycodestyle: 2.6.0)"

# Tie this to exactly the pycodestyle version that shows up in the setup.py of
# autopep8 so we know we run with the same version each time.
# See:
# https://github.com/hhatto/autopep8/blob/master/setup.py
PYCODESTYLE_TAG="2.6.0"

function main() {
  set -e # exit on error

  if ! type virtualenv >/dev/null 2>/dev/null
  then
    pip install -q virtualenv
  fi

  AUTOPEP8_VENV=${AUTOPEP8_VENV:-$(cd $(dirname $0) && git rev-parse --show-toplevel)/.git/fmt/autopep8_${AUTOPEP8_VERSION}_venv}
  if [ ! -e ${AUTOPEP8_VENV} ]
  then
    virtualenv ${AUTOPEP8_VENV}
  fi
  source ${AUTOPEP8_VENV}/bin/activate

  pip install -q "pycodestyle==${PYCODESTYLE_TAG}"
  pip install -q "autopep8==${AUTOPEP8_VERSION}"

  ver=$(autopep8 --version 2>&1)
  if [ "$ver" != "$VERSION" ]
  then
      echo "Wrong version of autopep8!"
      echo "Expected: \"${VERSION}\", got: \"${ver}\""
      exit 1
  fi

  DIR=${@:-.}
  autopep8 \
      --ignore-local-config \
      -i \
      -j 0 \
      --exclude "${DIR}/lib/yamlcpp" \
      --max-line-length 132 \
      --aggressive \
      --aggressive \
      --verbose \
      -r ${DIR}
  deactivate
}

if [[ "$(basename -- "$0")" == 'autopep8.sh' ]]; then
  main "$@"
else
  AUTOPEP8_VENV=${AUTOPEP8_VENV:-$(git rev-parse --show-toplevel)/.git/fmt/autopep8_${AUTOPEP8_VERSION}_venv}
fi
