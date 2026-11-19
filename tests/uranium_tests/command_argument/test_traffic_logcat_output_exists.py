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

import re

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Create the runroot whose environment locates traffic_logcat.

    Each test names an input file that does not exist, so traffic_logcat never waits on stdin.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    return ats_factory.create("ts")


def test_traffic_logcat_keeps_existing_output(ats_factory: ATSFactory) -> None:
    """Without -w, traffic_logcat refuses an output path that already exists and leaves it alone.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    output = _ats.run_directory / "existing.log"
    output.write_text("keep\n")
    result = _ats.run("traffic_logcat", "-o", output, _ats.run_directory / "missing.blog")
    assert result.returncode != 0, result.output
    assert re.search(r"output file .* already exists", result.stderr), result.output
    assert output.read_text() == "keep\n"


def test_traffic_logcat_refuses_output_symlink(ats_factory: ATSFactory) -> None:
    """Without -w, traffic_logcat does not follow a dangling symbolic link at the output path.

    The output is created exclusively, which fails on any existing name, the link included, so the
    file the link names is never created.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    target = _ats.run_directory / "link-target.log"
    output = _ats.run_directory / "dangling.log"
    output.symlink_to(target)
    result = _ats.run("traffic_logcat", "-o", output, _ats.run_directory / "missing.blog")
    assert result.returncode != 0, result.output
    assert re.search(r"output file .* already exists", result.stderr), result.output
    assert not target.exists()


def test_traffic_logcat_overwrites_output(ats_factory: ATSFactory) -> None:
    """With -w, traffic_logcat truncates an existing output file instead of refusing it.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    output = _ats.run_directory / "existing.log"
    output.write_text("replace\n")
    result = _ats.run("traffic_logcat", "-w", "-o", output, _ats.run_directory / "missing.blog")
    assert output.read_text() == "", result.output
