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
"""Verify hidden-metric matching through the JSON-RPC server."""

from tools.uranium.services import ATS, ATSFactory


class MetricMatchIncludeHiddenScenario:
    """Exercise normal and hidden-inclusive metric matching."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the ATS instance used by the scenario.

        :param ats_factory: Factory for the test-owned ATS process.
        """

        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Create the ATS process that serves JSON-RPC requests.

        :param ats_factory: Factory for the test-owned ATS process.
        """

        return ats_factory.create("ts")

    def validate_metric_match(self, include_hidden: bool) -> None:
        """Verify one metric match request succeeds through JSON-RPC.

        :param include_hidden: Whether to request hidden metrics too.
        """

        arguments = ["metric", "match", "reconfigure_time"]
        if include_hidden:
            arguments.append("--include-hidden")
        result = self._ats.traffic_ctl(*arguments)

        assert result.returncode == 0, result.output
        assert "proxy.process.proxy.reconfigure_time" in result.output
        assert "INVALID_INCOMING_DATA" not in result.output

    def run(self) -> None:
        """Run hidden-inclusive and ordinary metric queries."""

        self._ats.start()
        self.validate_metric_match(include_hidden=True)
        self.validate_metric_match(include_hidden=False)


def test_metric_match_include_hidden(ats_factory: ATSFactory) -> None:
    """The JSON-RPC decoder accepts hidden-inclusive metric requests.

    :param ats_factory: Factory for the test-owned ATS process.
    """

    MetricMatchIncludeHiddenScenario(ats_factory).run()
