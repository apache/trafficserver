#!/bin/env python

import sys
import logging

sys.path = ['../framework'] + sys.path
import atstf

from twisted_test import ExampleTwistedTest
from httplib_test import ExampleHttpClientTest

test_cases = [ExampleTwistedTest, ExampleHttpClientTest]

if __name__ == '__main__':
    # Spawn ATS and origin processes

    tm = atstf.ProcessManager(root_path="../..", config_path="config.json", log_level=logging.INFO, max_start_sec=5)

    try:
        tm.start()

        # Run the test suite and generate a JUnit XML report

        if atstf.run_tests(test_cases=test_cases, name='example', report='report.xml'):
            sys.exit(0)
        else:
            sys.exit(1)
    finally:
        tm.stop()
