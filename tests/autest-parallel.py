#!/usr/bin/env python3
'''
Parallel autest runner for Apache Traffic Server.

This script runs autest tests in parallel by spawning multiple autest processes,
each with a different port offset to avoid port conflicts.

Usage:
    ./autest-parallel.py -j 4 --sandbox /tmp/autest-parallel
    ./autest-parallel.py -j 8 --filter "cache-*" --sandbox /tmp/sb
    ./autest-parallel.py --list  # Just list tests without running
'''
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

import argparse
import fnmatch
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional


@dataclass
class TestResult:
    """Result from running a single autest process."""
    worker_id: int
    tests: List[str]
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    warnings: int = 0
    exceptions: int = 0
    unknown: int = 0
    duration: float = 0.0
    failed_tests: List[str] = field(default_factory=list)
    output: str = ""
    return_code: int = 0


def discover_tests(test_dir: Path, filter_patterns: Optional[List[str]] = None) -> List[str]:
    """
    Discover all .test.py files in the test directory.

    Args:
        test_dir: Path to gold_tests directory
        filter_patterns: Optional list of glob patterns to filter tests

    Returns:
        List of test names (without .test.py extension)
    """
    tests = []
    for test_file in test_dir.rglob("*.test.py"):
        # Extract test name (filename without .test.py)
        test_name = test_file.stem.replace('.test', '')

        # Apply filters if provided
        if filter_patterns:
            if any(fnmatch.fnmatch(test_name, pattern) for pattern in filter_patterns):
                tests.append(test_name)
        else:
            tests.append(test_name)

    return sorted(tests)


def partition_tests(tests: List[str], num_jobs: int) -> List[List[str]]:
    """
    Partition tests into roughly equal groups for parallel execution.

    Args:
        tests: List of test names
        num_jobs: Number of parallel workers

    Returns:
        List of test lists, one per worker
    """
    if num_jobs <= 0:
        num_jobs = 1

    partitions = [[] for _ in range(min(num_jobs, len(tests)))]
    for i, test in enumerate(tests):
        partitions[i % len(partitions)].append(test)

    return [p for p in partitions if p]  # Remove empty partitions


def strip_ansi(text: str) -> str:
    """Remove ANSI escape codes from text."""
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)


def parse_autest_output(output: str) -> dict:
    """
    Parse autest output to extract pass/fail counts.

    Args:
        output: Raw autest output string

    Returns:
        Dictionary with counts for passed, failed, skipped, etc.
    """
    result = {
        'passed': 0,
        'failed': 0,
        'skipped': 0,
        'warnings': 0,
        'exceptions': 0,
        'unknown': 0,
        'failed_tests': []
    }

    # Strip ANSI codes for easier parsing
    clean_output = strip_ansi(output)

    # Parse the summary section
    # Format: "  Passed: 2" or "  Failed: 0"
    for line in clean_output.split('\n'):
        line = line.strip()
        if 'Passed:' in line:
            try:
                result['passed'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass
        elif 'Failed:' in line and 'test' not in line.lower():
            try:
                result['failed'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass
        elif 'Skipped:' in line:
            try:
                result['skipped'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass
        elif 'Warning:' in line:
            try:
                result['warnings'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass
        elif 'Exception:' in line:
            try:
                result['exceptions'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass
        elif 'Unknown:' in line:
            try:
                result['unknown'] = int(line.split(':')[-1].strip())
            except ValueError:
                pass

    # Extract failed test names
    # Look for lines like "Test: test_name: Failed"
    failed_pattern = re.compile(r'Test:\s+(\S+):\s+Failed', re.IGNORECASE)
    for match in failed_pattern.finditer(clean_output):
        result['failed_tests'].append(match.group(1))

    return result


def run_worker(
    worker_id: int,
    tests: List[str],
    script_dir: Path,
    sandbox_base: Path,
    ats_bin: str,
    extra_args: List[str],
    port_offset_step: int = 1000,
    verbose: bool = False
) -> TestResult:
    """
    Run autest on a subset of tests with isolated sandbox and port range.

    Args:
        worker_id: Worker identifier (0, 1, 2, ...)
        tests: List of test names to run
        script_dir: Directory containing autest.sh
        sandbox_base: Base sandbox directory
        ats_bin: Path to ATS bin directory
        extra_args: Additional arguments to pass to autest
        port_offset_step: Port offset between workers
        verbose: Whether to print verbose output

    Returns:
        TestResult with pass/fail counts
    """
    start_time = time.time()
    result = TestResult(worker_id=worker_id, tests=tests)

    # Create worker-specific sandbox
    sandbox = sandbox_base / f"worker-{worker_id}"
    sandbox.mkdir(parents=True, exist_ok=True)

    # Calculate port offset for this worker
    port_offset = worker_id * port_offset_step

    # Build autest command
    # Use 'uv run autest' directly for better compatibility
    cmd = [
        'uv', 'run', 'autest', 'run',
        '--directory', 'gold_tests',
        '--ats-bin', ats_bin,
        '--sandbox', str(sandbox),
    ]

    # Add test filters
    cmd.append('--filters')
    cmd.extend(tests)

    # Add any extra arguments
    cmd.extend(extra_args)

    # Set up environment with port offset
    # We set this as an actual OS environment variable so ports.py can read it
    env = os.environ.copy()
    env['AUTEST_PORT_OFFSET'] = str(port_offset)

    if verbose:
        print(f"[Worker {worker_id}] Running {len(tests)} tests with port offset {port_offset}")
        print(f"[Worker {worker_id}] Command: {' '.join(cmd)}")

    try:
        proc = subprocess.run(
            cmd,
            cwd=script_dir,
            capture_output=True,
            text=True,
            env=env,
            timeout=3600  # 1 hour timeout per worker
        )
        result.output = proc.stdout + proc.stderr
        result.return_code = proc.returncode

        # Parse results
        parsed = parse_autest_output(result.output)
        result.passed = parsed['passed']
        result.failed = parsed['failed']
        result.skipped = parsed['skipped']
        result.warnings = parsed['warnings']
        result.exceptions = parsed['exceptions']
        result.unknown = parsed['unknown']
        result.failed_tests = parsed['failed_tests']

    except subprocess.TimeoutExpired:
        result.output = "TIMEOUT: Worker exceeded 1 hour timeout"
        result.return_code = -1
        result.failed = len(tests)
    except Exception as e:
        result.output = f"ERROR: {str(e)}"
        result.return_code = -1
        result.failed = len(tests)

    result.duration = time.time() - start_time
    return result


def print_summary(results: List[TestResult], total_duration: float):
    """Print aggregated results from all workers."""
    total_passed = sum(r.passed for r in results)
    total_failed = sum(r.failed for r in results)
    total_skipped = sum(r.skipped for r in results)
    total_warnings = sum(r.warnings for r in results)
    total_exceptions = sum(r.exceptions for r in results)
    total_unknown = sum(r.unknown for r in results)
    total_tests = total_passed + total_failed + total_skipped + total_warnings + total_exceptions + total_unknown

    all_failed_tests = []
    for r in results:
        all_failed_tests.extend(r.failed_tests)

    print("\n" + "=" * 70)
    print("PARALLEL AUTEST SUMMARY")
    print("=" * 70)
    print(f"Workers:     {len(results)}")
    print(f"Total tests: {total_tests}")
    print(f"Duration:    {total_duration:.1f}s")
    print("-" * 70)
    print(f"  Passed:     {total_passed}")
    print(f"  Failed:     {total_failed}")
    print(f"  Skipped:    {total_skipped}")
    print(f"  Warnings:   {total_warnings}")
    print(f"  Exceptions: {total_exceptions}")
    print(f"  Unknown:    {total_unknown}")

    if all_failed_tests:
        print("-" * 70)
        print("FAILED TESTS:")
        for test in sorted(all_failed_tests):
            print(f"  - {test}")

    print("=" * 70)

    # Per-worker summary
    print("\nPer-worker breakdown:")
    for r in results:
        status = "OK" if r.failed == 0 and r.exceptions == 0 else "FAIL"
        print(f"  Worker {r.worker_id}: {r.passed} passed, {r.failed} failed, "
              f"{r.skipped} skipped ({r.duration:.1f}s) [{status}]")


def main():
    parser = argparse.ArgumentParser(
        description='Run autest tests in parallel',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
    # Run all tests with 4 parallel workers
    %(prog)s -j 4 --ats-bin /opt/ats/bin --sandbox /tmp/autest

    # Run specific tests
    %(prog)s -j 2 --filter "cache-*" --filter "tls-*" --ats-bin /opt/ats/bin --sandbox /tmp/autest

    # List tests without running
    %(prog)s --list --ats-bin /opt/ats/bin
'''
    )

    parser.add_argument(
        '-j', '--jobs',
        type=int,
        default=os.cpu_count() or 4,
        help='Number of parallel workers (default: CPU count)'
    )
    parser.add_argument(
        '--ats-bin',
        required=True,
        help='Path to ATS bin directory'
    )
    parser.add_argument(
        '--sandbox',
        default='/tmp/autest-parallel',
        help='Base sandbox directory (default: /tmp/autest-parallel)'
    )
    parser.add_argument(
        '-f', '--filter',
        action='append',
        dest='filters',
        help='Filter tests by glob pattern (can be specified multiple times)'
    )
    parser.add_argument(
        '--list',
        action='store_true',
        help='List tests without running'
    )
    parser.add_argument(
        '--port-offset-step',
        type=int,
        default=1000,
        help='Port offset between workers (default: 1000)'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Verbose output'
    )
    parser.add_argument(
        '--test-dir',
        default='gold_tests',
        help='Test directory relative to script location (default: gold_tests)'
    )
    parser.add_argument(
        'extra_args',
        nargs='*',
        help='Additional arguments to pass to autest'
    )

    args = parser.parse_args()

    # Determine paths
    script_dir = Path(__file__).parent.resolve()
    test_dir = script_dir / args.test_dir

    if not test_dir.exists():
        print(f"Error: Test directory not found: {test_dir}", file=sys.stderr)
        sys.exit(1)

    # Discover tests
    tests = discover_tests(test_dir, args.filters)

    if not tests:
        print("No tests found matching the specified filters.", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(tests)} tests")

    if args.list:
        print("\nTests:")
        for test in tests:
            print(f"  {test}")
        sys.exit(0)

    # Partition tests
    num_jobs = min(args.jobs, len(tests))
    partitions = partition_tests(tests, num_jobs)

    print(f"Running with {len(partitions)} parallel workers")
    print(f"Port offset step: {args.port_offset_step}")
    print(f"Sandbox: {args.sandbox}")

    # Create sandbox base directory
    sandbox_base = Path(args.sandbox)
    sandbox_base.mkdir(parents=True, exist_ok=True)

    # Run workers in parallel
    start_time = time.time()
    results: List[TestResult] = []

    with ProcessPoolExecutor(max_workers=len(partitions)) as executor:
        futures = {}
        for worker_id, worker_tests in enumerate(partitions):
            future = executor.submit(
                run_worker,
                worker_id=worker_id,
                tests=worker_tests,
                script_dir=script_dir,
                sandbox_base=sandbox_base,
                ats_bin=args.ats_bin,
                extra_args=args.extra_args or [],
                port_offset_step=args.port_offset_step,
                verbose=args.verbose
            )
            futures[future] = worker_id

        # Collect results as they complete
        for future in as_completed(futures):
            worker_id = futures[future]
            try:
                result = future.result()
                results.append(result)
                status = "PASS" if result.failed == 0 else "FAIL"
                print(f"[Worker {worker_id}] Completed: {result.passed} passed, "
                      f"{result.failed} failed ({result.duration:.1f}s) [{status}]")
            except Exception as e:
                print(f"[Worker {worker_id}] Error: {e}", file=sys.stderr)
                results.append(TestResult(
                    worker_id=worker_id,
                    tests=partitions[worker_id],
                    failed=len(partitions[worker_id]),
                    output=str(e)
                ))

    total_duration = time.time() - start_time

    # Sort results by worker_id for consistent output
    results.sort(key=lambda r: r.worker_id)

    # Print summary
    print_summary(results, total_duration)

    # Exit with non-zero if any tests failed
    total_failed = sum(r.failed + r.exceptions for r in results)
    sys.exit(1 if total_failed > 0 else 0)


if __name__ == '__main__':
    main()
