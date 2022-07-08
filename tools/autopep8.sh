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

  # Only run autopep8 on tracked files. This saves time and possibly avoids
  # formatting files the user doesn't want formatted.
  tmp_dir=$(mktemp -d -t tracked-git-files.XXXXXXXXXX)
  files=${tmp_dir}/git_files.txt
  files_filtered=${tmp_dir}/git_files_filtered.txt
  git ls-tree -r HEAD --name-only ${DIR} | grep -vE "lib/yamlcpp" > ${files}
  # Add to the above any newly added staged files.
  git diff --cached --name-only --diff-filter=A >> ${files}
  # Keep this list of Python extensions the same with the list of
  # extensions searched for in the tools/git/pre-commit hook.
  grep -E '\.py$|\.cli.ext$|\.test.ext$' ${files} > ${files_filtered}
  # Prepend the filenames with "./" to make the modified file output consistent
  # with the clang-format target output.
  sed -i'.bak' 's:^:\./:' ${files_filtered}
  rm -f ${files_filtered}.bak

  # Efficiently retrieving modification timestamps in a platform
  # independent way is challenging. We use find's -newer argument, which
  # seems to be broadly supported. The following file is created and has a
  # timestamp just before running clang-format. Any file with a timestamp
  # after this we assume was modified by clang-format.
  start_time_file=${tmp_dir}/format_start.$$
  touch ${start_time_file}
  autopep8 \
      --ignore-local-config \
      -i \
      -j 0 \
      --exclude "${DIR}/lib/yamlcpp" \
      --max-line-length 132 \
      --aggressive \
      --aggressive \
      $(cat ${files_filtered})
  find $(cat ${files_filtered}) -newer ${start_time_file}

  # The above will not catch the Python files in the metalink tests because
  # they do not have extensions.
  metalink_dir=${DIR}/plugins/experimental/metalink/test
  autopep8 \
      --ignore-local-config \
      -i \
      -j 0 \
      --exclude "${DIR}/lib/yamlcpp" \
      --max-line-length 132 \
      --aggressive \
      --aggressive \
      --recursive \
      ${metalink_dir}
  find ${metalink_dir} -newer ${start_time_file}
  rm -rf ${tmp_dir}
  deactivate
}

if [[ "$(basename -- "$0")" == 'autopep8.sh' ]]; then
  main "$@"
else
  AUTOPEP8_VENV=${AUTOPEP8_VENV:-$(git rev-parse --show-toplevel)/.git/fmt/autopep8_${AUTOPEP8_VERSION}_venv}
fi
