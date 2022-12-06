#!/bin/bash
# vim: sw=4:ts=4:softtabstop=4:ai:et

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

fail()
{
    echo $1
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
ROOT=${ROOT:-${SCRIPT_DIR}}
[ -d "${ROOT}" ] || fail "ROOT: \"${ROOT}\" is not a directory."
[ -d "${ROOT}/tests" ] || fail "\"${ROOT}/tests\" is not a directory."

PV_VERSION_FILE="${ROOT}/tests/proxy-verifier-version.txt"
[ -r "${PV_VERSION_FILE}" ] || fail "\"${PV_VERSION_FILE}\" does not exist."

pv_name="proxy-verifier"
pv_version=`cat ${ROOT}/tests/proxy-verifier-version.txt`
pv_top_dir="${ROOT}/tests/proxy-verifier"
pv_unpack_dir="${pv_top_dir}/unpack/${pv_version}"
bin_dir="${pv_unpack_dir}/bin"
pv_dir="${pv_name}-${pv_version}"
pv_tar_filename="${pv_dir}.tar.gz"
pv_tar="${pv_top_dir}/${pv_tar_filename}"
pv_tar_url="https://ci.trafficserver.apache.org/bintray/${pv_tar_filename}"
expected_sha1="d939629949bafe6df8821e5d441762066cc6d556"
pv_client="${bin_dir}/verifier-client"
pv_server="${bin_dir}/verifier-server"
TAR=${TAR:-tar}
CURL=${CURL:-curl}
# Check to see whether Proxy Verifier has already been unpacked.
if ! [ -x ${pv_client} -a -x ${pv_server} ]
then
    # 1. Get the tar file if it has not been retrieved already.
    # Note that the two spaces between the hash and ${ARCHIVE) is needed
    if [ ! -e ${pv_tar} ]
    then
        # default to using native sha1sum command when available
        if [ $(which sha1sum) ]
        then
            SHASUM=${SHASUM:-sha1sum}
        else
            SHASUM=${SHASUM:-shasum}
        fi
        mkdir -p ${pv_top_dir}
        ${CURL} -L --progress-bar -o ${pv_tar} ${pv_tar_url} || \
            fail "Failed to download ${pv_tar_url}."
        cat > ${pv_top_dir}/sha1 << EOF
${expected_sha1}  ${pv_tar}
EOF
        ${SHASUM} -c ${pv_top_dir}/sha1 || fail "SHA1 mismatch for downloaded ${pv_tar_filename}."
    fi

    # 2. Untar the Proxy Verifier binaries.
    mkdir -p ${pv_unpack_dir}
    ${TAR} -x -C ${pv_unpack_dir} -f ${pv_tar}

    # 3. Determine the target OS.
    pv_os_dir=""
    case $(uname -s) in
    Darwin)
        pv_os_dir="${pv_unpack_dir}/${pv_dir}/mac-os"
        ;;
    Linux)
        pv_os_dir="${pv_unpack_dir}/${pv_dir}/linux"
        ;;
    *)
        fail "We need to build proxy-verifier for $(uname -s)"
    esac

    # 4. Link the OS-specific binaries to the bin directory.
    mkdir -p ${bin_dir}
    ln -s ${pv_os_dir}/verifier-client ${bin_dir}
    ln -s ${pv_os_dir}/verifier-server ${bin_dir}
    chmod +x ${pv_client}
    chmod +x ${pv_server}
fi
