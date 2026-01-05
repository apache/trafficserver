#!/bin/bash
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
#
# HRW4U Test Runner
# Runs hrw_confcmp on pairs of .input.txt and .output.txt files

set -euo pipefail

# Try to find hrw_confcmp binary
# 1. Look in current directory (if running from build dir)
# 2. Look relative to script location (if running from source dir)
if [[ -x "./tools/hrw_confcmp/hrw_confcmp" ]]; then
  CONFCMP="$(pwd)/tools/hrw_confcmp/hrw_confcmp"
elif [[ -x "hrw_confcmp" ]]; then
  CONFCMP="$(pwd)/hrw_confcmp"
else
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  CONFCMP="${SCRIPT_DIR}/hrw_confcmp"
fi

if [[ ! -x "$CONFCMP" ]]; then
  echo "Error: hrw_confcmp not found or not executable"
  echo "Tried:"
  echo "  - ./tools/hrw_confcmp/hrw_confcmp (build directory)"
  echo "  - ./hrw_confcmp (current directory)"
  echo "  - ${SCRIPT_DIR}/hrw_confcmp (script directory)"
  echo ""
  echo "Please build it first with: cmake --build build --target hrw_confcmp"
  exit 1
fi

USE_BATCH=false
SHOW_HELP=false
TEST_DIRS=()

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch)
      USE_BATCH=true
      shift
      ;;
    --help|-h)
      SHOW_HELP=true
      shift
      ;;
    *)
      TEST_DIRS+=("$1")
      shift
      ;;
  esac
done

if [[ "$SHOW_HELP" == true ]] || [[ ${#TEST_DIRS[@]} -eq 0 ]]; then
  echo "Usage: $0 [--batch] <test_directory> [test_directory...]"
  echo ""
  echo "Options:"
  echo "  --batch  Use batch mode for faster execution (single process)"
  echo ""
  echo "Example:"
  echo "  $0 tools/hrw4u/tests/data/vars"
  echo "  $0 --batch tools/hrw4u/tests/data/{hooks,conds,ops,vars}"
  exit 1
fi

# Get time in milliseconds (portable across macOS/Linux)
get_time_ms() {
  if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: use perl for millisecond precision
    perl -MTime::HiRes=time -e 'printf "%.0f\n", time * 1000'
  else
    # Linux: use date with nanoseconds
    echo $(($(date +%s%N) / 1000000))
  fi
}

# Check if a test should be skipped based on exceptions.txt
is_test_excepted() {
  local test_dir="$1"
  local test_name="$2"

  # Only except examples/all-nonsense
  if [[ "$test_dir" =~ /examples$ ]] && [[ "$test_name" == "all-nonsense" ]]; then
    return 0  # Test is excepted
  fi

  return 1  # Test is not excepted
}

# Collect all test pairs from directories
collect_test_pairs() {
  for test_dir in "${TEST_DIRS[@]}"; do
    if [[ ! -d "$test_dir" ]]; then
      echo "Warning: Directory not found: $test_dir" >&2
      continue
    fi

    local abs_test_dir="$(cd "$test_dir" && pwd)"

    while IFS= read -r input_file; do
      local base_name="${input_file%.input.txt}"
      local test_name="${base_name##*/}"
      local output_file="${base_name}.output.txt"

      # Skip excepted tests
      if is_test_excepted "$test_dir" "$test_name"; then
        echo "  Skipping excepted test: ${test_name}" >&2
        continue
      fi

      if [[ -f "$output_file" ]]; then
        local abs_output="$(cd "$(dirname "$output_file")" && pwd)/$(basename "$output_file")"
        local abs_input="$(cd "$(dirname "$input_file")" && pwd)/$(basename "$input_file")"
        echo "$abs_output $abs_input"
      fi
    done < <(find "$test_dir" -maxdepth 1 -name "*.input.txt" | sort)
  done
}

# Run in batch mode (single process, much faster)
run_batch_mode() {
  echo ""
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "Running tests in BATCH mode (single process)"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

  local start_ms=$(get_time_ms)

  # Collect pairs and feed to batch mode
  local pairs
  pairs=$(collect_test_pairs)
  local total_tests=$(echo "$pairs" | grep -c . || echo 0)

  if [[ $total_tests -eq 0 ]]; then
    echo "  No test pairs found"
    return 0
  fi

  echo "  Found $total_tests test pairs"
  echo ""

  # Run in batch mode (without --quiet so we can count results)
  local result
  set +e
  result=$(echo "$pairs" | "$CONFCMP" --batch 2>&1)
  local exit_code=$?
  set -e

  local end_ms=$(get_time_ms)
  local elapsed=$((end_ms - start_ms))

  # Parse batch output - use the summary line which is always present
  local summary_line
  summary_line=$(echo "$result" | grep "^Batch Summary:" || echo "")
  local passed=0
  local failed=0

  if [[ -n "$summary_line" ]]; then
    # Extract numbers: "Batch Summary: 47 total, 47 passed, 0 failed, 0 errors"
    passed=$(echo "$summary_line" | awk -F', ' '{for(i=1;i<=NF;i++) if($i ~ /passed/) {gsub(/[^0-9]/,"",$i); print $i}}')
    failed=$(echo "$summary_line" | awk -F', ' '{for(i=1;i<=NF;i++) if($i ~ /failed/) {gsub(/[^0-9]/,"",$i); print $i}}')
  fi
  passed=${passed:-0}
  failed=${failed:-0}

  # Show failures if any
  if [[ $failed -gt 0 ]]; then
    echo "Failed tests:"
    echo "$result" | grep "^FAIL:" | while read -r line; do
      echo "  ✗ $line"
    done
    echo ""
  fi

  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "Overall Summary (batch mode)"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "  Total tests:  $total_tests"
  echo "  Passed:       $passed"
  echo "  Failed:       $failed"
  echo ""
  echo "  Total time:   ${elapsed} ms"
  if [[ $total_tests -gt 0 ]]; then
    local avg=$((elapsed / total_tests))
    echo "  Avg per test: ${avg} ms"
  fi

  if [[ $exit_code -eq 0 ]]; then
    echo ""
    echo "✓ All tests passed!"
    return 0
  else
    echo ""
    echo "✗ Some tests failed"
    return 1
  fi
}

# Run in serial mode (one process per test, slower but more detailed output)
run_serial_mode() {
  local total_tests=0
  local total_passed=0
  local total_failed=0
  local failed_tests=()
  local total_time_ms=0

  process_directory() {
    local test_dir="$1"
    local dir_name="${test_dir##*/}"

    if [[ ! -d "$test_dir" ]]; then
      echo "Warning: Directory not found: $test_dir"
      return
    fi

    local abs_test_dir="$(cd "$test_dir" && pwd)"

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Testing directory: $dir_name"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    local dir_tests=0
    local dir_passed=0
    local dir_failed=0

    while IFS= read -r input_file; do
      local base_name="${input_file%.input.txt}"
      local test_name="${base_name##*/}"
      local output_file="${base_name}.output.txt"

      if [[ ! -f "$output_file" ]]; then
        continue
      fi

      # Skip excepted tests
      if is_test_excepted "$test_dir" "$test_name"; then
        printf "  ⊘ %-30s (excepted)\n" "${dir_name}/${test_name}:"
        continue
      fi

      dir_tests=$((dir_tests + 1))

      local abs_output="$(cd "$(dirname "$output_file")" && pwd)/$(basename "$output_file")"
      local abs_input="$(cd "$(dirname "$input_file")" && pwd)/$(basename "$input_file")"

      local start_ms=$(get_time_ms)
      if "$CONFCMP" "$abs_output" "$abs_input" >/dev/null 2>&1; then
        local end_ms=$(get_time_ms)
        local elapsed=$((end_ms - start_ms))
        total_time_ms=$((total_time_ms + elapsed))
        printf "  ✓ %-30s %4d ms\n" "${dir_name}/${test_name}:" "$elapsed"
        dir_passed=$((dir_passed + 1))
      else
        local end_ms=$(get_time_ms)
        local elapsed=$((end_ms - start_ms))
        total_time_ms=$((total_time_ms + elapsed))
        printf "  ✗ %-30s %4d ms (FAILED)\n" "${dir_name}/${test_name}:" "$elapsed"
        dir_failed=$((dir_failed + 1))
        failed_tests+=("${dir_name}/${test_name}|$CONFCMP \"$abs_output\" \"$abs_input\"")
      fi
    done < <(find "$test_dir" -maxdepth 1 -name "*.input.txt" | sort)

    if [[ $dir_tests -eq 0 ]]; then
      echo "  No test pairs found (*.input.txt + *.output.txt)"
    else
      echo ""
      echo "  Directory Summary: $dir_passed passed, $dir_failed failed (total: $dir_tests)"
    fi

    total_tests=$((total_tests + dir_tests))
    total_passed=$((total_passed + dir_passed))
    total_failed=$((total_failed + dir_failed))
  }

  for test_dir in "${TEST_DIRS[@]}"; do
    process_directory "$test_dir"
  done

  echo ""
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "Overall Summary"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "  Total tests:  $total_tests"
  echo "  Passed:       $total_passed"
  echo "  Failed:       $total_failed"
  echo ""
  if [[ $total_tests -gt 0 ]]; then
    avg_time=$((total_time_ms / total_tests))
    echo "  Total time:   ${total_time_ms} ms"
    echo "  Avg per test: ${avg_time} ms"
  fi

  if [[ $total_failed -gt 0 ]]; then
    echo ""
    echo "Failed tests:"
    for failed in "${failed_tests[@]}"; do
      IFS='|' read -r test_name cmd <<< "$failed"
      echo ""
      echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
      echo "  Failed Test: $test_name"
      echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

      # Extract file paths from command
      output_file=$(echo "$cmd" | grep -o '"[^"]*\.output\.txt"' | tr -d '"')
      input_file=$(echo "$cmd" | grep -o '"[^"]*\.input\.txt"' | tr -d '"')

      echo ""
      echo "To re-run this test:"
      echo "  $cmd"
      echo ""

      echo "Expected Output (.output.txt):"
      echo "────────────────────────────────────────────────────────────"
      cat "$output_file" 2>/dev/null || echo "Error: Could not read $output_file"
      echo ""

      echo "HRW4U Input (.input.txt):"
      echo "────────────────────────────────────────────────────────────"
      cat "$input_file" 2>/dev/null || echo "Error: Could not read $input_file"
      echo ""

      echo "Comparison Details (--debug):"
      echo "────────────────────────────────────────────────────────────"
      $CONFCMP --debug "$output_file" "$input_file" 2>&1
      echo ""
    done
    echo ""
    exit 1
  else
    echo ""
    echo "✓ All tests passed!"
    exit 0
  fi
}

# Main execution
if [[ "$USE_BATCH" == true ]]; then
  run_batch_mode
else
  run_serial_mode
fi
