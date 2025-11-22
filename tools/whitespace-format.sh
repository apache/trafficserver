#!/usr/bin/env bash
#
#  Script to fix whitespace issues: trailing whitespace and DOS carriage returns.
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

set -e

# Get the repository root
REPO_ROOT=$(cd $(dirname $0) && git rev-parse --show-toplevel)

EXCLUDE_PATTERNS=(
    "lib/yamlcpp"
    "lib/Catch2"
    "lib/systemtap"
    ".gold:"
    ".test_input"
)

GREP_EXCLUDE_ARGS=""
for pattern in "${EXCLUDE_PATTERNS[@]}"; do
    GREP_EXCLUDE_ARGS="${GREP_EXCLUDE_ARGS} | grep -F -v '${pattern}'"
done

echo "Fixing whitespace issues in ${REPO_ROOT}"

# Should the file be excluded?
should_exclude() {
    local file="$1"
    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        if echo "$file" | grep -F -q "$pattern"; then
            return 0
        fi
    done
    return 1
}

if [[ "$OSTYPE" == "darwin"* ]]; then
    SED_INPLACE=(sed -i '')
else
    SED_INPLACE=(sed -i)
fi

FILES_MODIFIED=0

# Fix trailing whitespace.
echo "Checking for trailing whitespace..."
TRAILING_WS_FILES=$(git grep -IE ' +$' | eval "grep -F -v 'lib/yamlcpp' | grep -F -v 'lib/Catch2' | grep -F -v 'lib/systemtap' | grep -F -v '.gold:' | grep -F -v '.test_input'" | cut -d: -f1 | sort -u || true)

if [ -n "$TRAILING_WS_FILES" ]; then
    while IFS= read -r file; do
        if [ -f "$file" ]; then
            echo "  Fixing trailing whitespace in: $file"
            "${SED_INPLACE[@]}" 's/[[:space:]]*$//' "$file"
            FILES_MODIFIED=$((FILES_MODIFIED + 1))
        fi
    done <<< "$TRAILING_WS_FILES"
fi

# Fix DOS carriage returns.
echo "Checking for DOS carriage returns..."
DOS_FILES=$(git grep -IE $'\r$' | eval "grep -F -v 'lib/yamlcpp' | grep -F -v 'lib/Catch2' | grep -F -v 'lib/systemtap' | grep -F -v '.test_input'" | cut -d: -f1 | sort -u || true)

if [ -n "$DOS_FILES" ]; then
    while IFS= read -r file; do
        if [ -f "$file" ]; then
            echo "  Removing DOS carriage returns from: $file"
            "${SED_INPLACE[@]}" $'s/\r$//' "$file"
            FILES_MODIFIED=$((FILES_MODIFIED + 1))
        fi
    done <<< "$DOS_FILES"
fi

if [ $FILES_MODIFIED -eq 0 ]; then
    echo "Success! No whitespace issues found."
else
    echo "Fixed whitespace issues in $FILES_MODIFIED file(s)."
fi

exit 0

