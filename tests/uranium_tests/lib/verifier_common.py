'''
Common utilities for the Proxy Verifier extensions.
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
import os
import tempfile
from string import Template


def create_address_argument(ports):
    """
    >>> create_address_argument([8080, 8081])
    '"127.0.0.1:8080,127.0.0.1:8081"'
    """
    is_first = True
    argument = '"'
    for port in ports:
        if is_first:
            is_first = False
        else:
            argument += ','
        argument += "127.0.0.1:{}".format(port)
    argument += '"'
    return argument


def substitute_context_in_replay_file(process, replay_path, context):
    '''
    Perform substitution base on the passed context dict.
    This function will return the new replay_path file
    '''
    # Only files for now
    if os.path.isdir(replay_path):
        raise ValueError(f"Mapping substitution not supported for directories.")

    with open(os.path.join(process.TestDirectory, replay_path), 'r') as replay_file:
        replay_template = Template(replay_file.read())
        replay_content = replay_template.substitute(context)
        tf = tempfile.NamedTemporaryFile(delete=False, dir=process.RunDirectory, suffix=f"_{os.path.basename(replay_path)}")
        replay_path = tf.name
        with open(replay_path, "w") as new_replay_file:
            new_replay_file.write(replay_content)

    # use this as replay_path
    return replay_path
