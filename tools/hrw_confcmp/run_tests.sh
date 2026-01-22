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
DEBUG_MODE=false
TEST_DIRS=()

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch)
      USE_BATCH=true
      shift
      ;;
    --debug)
      DEBUG_MODE=true
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
  echo "Usage: $0 [--batch] [--debug] <test_directory> [test_directory...]"
  echo ""
  echo "Options:"
  echo "  --batch  Use batch mode for faster execution (single process)"
  echo "  --debug  Stop after first failure and show detailed comparison"
  echo ""
  echo "The script will automatically test:"
  echo "  1. Original config files (.conf, .config, .hrw) vs their .hrw4u conversions"
  echo "  2. Standard test pairs (*.input.txt + *.output.txt)"
  echo ""
  echo "Examples:"
  echo "  $0 tools/hrw4u/tests/data/vars"
  echo "  $0 --batch tools/hrw4u/tests/data/{hooks,conds,ops,vars}"
  echo "  $0 --debug tests/gold_tests/pluginTest/header_rewrite/rules"
  exit 1
fi

# Debug mode forces serial mode (can't stop on first failure in batch mode)
if [[ "$DEBUG_MODE" == true ]] && [[ "$USE_BATCH" == true ]]; then
  echo "Note: --debug mode forces serial execution (incompatible with --batch)"
  USE_BATCH=false
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

# Check if a test should be skipped based on exceptions
is_test_excepted() {
  local test_dir="$1"
  local test_name="$2"

  # examples/all-nonsense: Intentionally invalid syntax for testing error handling
  if [[ "$test_dir" =~ /examples$ ]] && [[ "$test_name" == "all-nonsense" ]]; then
    return 0  # Test is excepted
  fi

  # ops/skip-remap: Uses READ_REQUEST_PRE_REMAP_HOOK which cannot be used in remap rules
  if [[ "$test_dir" =~ /ops$ ]] && [[ "$test_name" == "skip-remap" ]]; then
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

    # First, find original config files (.conf, .config, .hrw) with matching .hrw4u files
    while IFS= read -r orig_file; do
      local base_name="${orig_file%.*}"
      local hrw4u_file="${base_name}.hrw4u"

      if [[ -f "$hrw4u_file" ]]; then
        local abs_orig="$(cd "$(dirname "$orig_file")" && pwd)/$(basename "$orig_file")"
        local abs_hrw4u="$(cd "$(dirname "$hrw4u_file")" && pwd)/$(basename "$hrw4u_file")"
        # Output: expected (original) then input (hrw4u)
        echo "$abs_orig $abs_hrw4u"
      fi
    done < <(find "$test_dir" -maxdepth 1 \( -name "*.conf" -o -name "*.config" -o -name "*.hrw" \) | sort)

    # Then, find .input.txt/.output.txt pairs
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

    # First, process original config files (.conf, .config, .hrw) with matching .hrw4u files
    while IFS= read -r orig_file; do
      local base_name="${orig_file%.*}"
      local test_name="${base_name##*/}"
      local hrw4u_file="${base_name}.hrw4u"

      if [[ ! -f "$hrw4u_file" ]]; then
        continue
      fi

      dir_tests=$((dir_tests + 1))

      local abs_orig="$(cd "$(dirname "$orig_file")" && pwd)/$(basename "$orig_file")"
      local abs_hrw4u="$(cd "$(dirname "$hrw4u_file")" && pwd)/$(basename "$hrw4u_file")"

      local start_ms=$(get_time_ms)
      if "$CONFCMP" "$abs_orig" "$abs_hrw4u" >/dev/null 2>&1; then
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
        failed_tests+=("${dir_name}/${test_name}|$CONFCMP \"$abs_orig\" \"$abs_hrw4u\"")

        # In debug mode, stop immediately and show details
        if [[ "$DEBUG_MODE" == true ]]; then
          echo ""
          echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
          echo "  FIRST FAILURE (debug mode): ${dir_name}/${test_name}"
          echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
          echo ""
          echo "To re-run this test:"
          echo "  $CONFCMP \"$abs_orig\" \"$abs_hrw4u\""
          echo ""
          echo "Original Config File (authority):"
          echo "────────────────────────────────────────────────────────────"
          cat "$abs_orig" 2>/dev/null || echo "Error: Could not read $abs_orig"
          echo ""
          echo "Generated HRW4U File:"
          echo "────────────────────────────────────────────────────────────"
          cat "$abs_hrw4u" 2>/dev/null || echo "Error: Could not read $abs_hrw4u"
          echo ""
          echo "Comparison Details (--debug):"
          echo "────────────────────────────────────────────────────────────"
          $CONFCMP --debug "$abs_orig" "$abs_hrw4u" 2>&1
          echo ""
          exit 1
        fi
      fi
    done < <(find "$test_dir" -maxdepth 1 \( -name "*.conf" -o -name "*.config" -o -name "*.hrw" \) | sort)

    # Then, process .input.txt/.output.txt pairs
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

        # In debug mode, stop immediately and show details
        if [[ "$DEBUG_MODE" == true ]]; then
          echo ""
          echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
          echo "  FIRST FAILURE (debug mode): ${dir_name}/${test_name}"
          echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
          echo ""
          echo "To re-run this test:"
          echo "  $CONFCMP \"$abs_output\" \"$abs_input\""
          echo ""
          echo "Expected Output (.output.txt):"
          echo "────────────────────────────────────────────────────────────"
          cat "$abs_output" 2>/dev/null || echo "Error: Could not read $abs_output"
          echo ""
          echo "HRW4U Input (.input.txt):"
          echo "────────────────────────────────────────────────────────────"
          cat "$abs_input" 2>/dev/null || echo "Error: Could not read $abs_input"
          echo ""
          echo "Comparison Details (--debug):"
          echo "────────────────────────────────────────────────────────────"
          $CONFCMP --debug "$abs_output" "$abs_input" 2>&1
          echo ""
          exit 1
        fi
      fi
    done < <(find "$test_dir" -maxdepth 1 -name "*.input.txt" | sort)

    if [[ $dir_tests -eq 0 ]]; then
      echo "  No test pairs found"
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

      echo ""
      echo "To re-run this test:"
      echo "  $cmd"
      echo ""

      # Extract file paths from command - try both types
      local first_file=$(echo "$cmd" | grep -o '"[^"]*"' | head -1 | tr -d '"')
      local second_file=$(echo "$cmd" | grep -o '"[^"]*"' | tail -1 | tr -d '"')

      # Determine test type based on file extensions
      if [[ "$first_file" =~ \.(conf|config|hrw)$ ]]; then
        # Config file test: original vs .hrw4u
        echo "Original Config File (authority):"
        echo "────────────────────────────────────────────────────────────"
        cat "$first_file" 2>/dev/null || echo "Error: Could not read $first_file"
        echo ""

        echo "Generated HRW4U File:"
        echo "────────────────────────────────────────────────────────────"
        cat "$second_file" 2>/dev/null || echo "Error: Could not read $second_file"
        echo ""
      else
        # Standard test: .output.txt vs .input.txt
        echo "Expected Output (.output.txt):"
        echo "────────────────────────────────────────────────────────────"
        cat "$first_file" 2>/dev/null || echo "Error: Could not read $first_file"
        echo ""

        echo "HRW4U Input (.input.txt):"
        echo "────────────────────────────────────────────────────────────"
        cat "$second_file" 2>/dev/null || echo "Error: Could not read $second_file"
        echo ""
      fi

      echo "Comparison Details (--debug):"
      echo "────────────────────────────────────────────────────────────"
      $CONFCMP --debug "$first_file" "$second_file" 2>&1
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
