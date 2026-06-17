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
expected_sha1="8e9adc6e0b31251ca8e6fc2064ae54e5d752bb72"
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
        if command -v sha1sum >/dev/null 2>&1
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

    # 2. Determine the target OS.
    pv_os_archive_dir=""
    case $(uname -s) in
    Darwin)
        case $(uname -m) in
        x86_64)
            pv_os_archive_dir="${pv_dir}/darwin-amd64"
            ;;
        arm64)
            pv_os_archive_dir="${pv_dir}/darwin-arm64"
            ;;
        *)
            fail "Unrecognized Mac architecture: $(uname -m)"
            ;;
        esac
        ;;
    Linux)
        case $(uname -m) in
        x86_64)
            pv_os_archive_dir="${pv_dir}/linux-amd64"
            ;;
        aarch64)
            pv_os_archive_dir="${pv_dir}/linux-arm64"
            ;;
        *)
            fail "Unrecognized Linux architecture: $(uname -m)"
            ;;
        esac
        ;;
    *)
        fail "We need to build proxy-verifier for $(uname -s)"
    esac

    # 3. Extract the OS-specific binaries to the bin directory.
    mkdir -p ${bin_dir}
    rm -f "${pv_client}" "${pv_server}" "${pv_client}.tmp" "${pv_server}.tmp"
    ${TAR} -x -O -f "${pv_tar}" "${pv_os_archive_dir}/verifier-client" > "${pv_client}.tmp" || \
        fail "Failed to extract ${pv_os_archive_dir}/verifier-client from ${pv_tar_filename}."
    ${TAR} -x -O -f "${pv_tar}" "${pv_os_archive_dir}/verifier-server" > "${pv_server}.tmp" || \
        fail "Failed to extract ${pv_os_archive_dir}/verifier-server from ${pv_tar_filename}."
    mv "${pv_client}.tmp" "${pv_client}"
    mv "${pv_server}.tmp" "${pv_server}"
    chmod +x "${pv_client}" "${pv_server}"
fi
