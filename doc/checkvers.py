# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys
import os

if __name__ == '__main__':
    # Use optparse instead of argparse because this needs to work on old Python versions.
    import optparse

    parser = optparse.OptionParser(description='Traffic Server Sphinx docs configuration')
    parser.add_option('--check-version', action='store_true', dest='checkvers')

    (options, args) = parser.parse_args()

    # Check whether we have a recent version of sphinx. EPEL and CentOS are completely crazy and I don't understand their
    # packaging at all. The test below works on Ubuntu and places where sphinx is installed sanely AFAICT.
    if options.checkvers:
        print('checking for sphinx version >= 1.5.1... '),
        # Need at least 1.5.1 to use svg
        # version >= 1.2 guarantees sphinx.version_info is available.
        try:
            import sphinx

            if 'version_info' in dir(sphinx):
                print('Found Sphinx version {0}'.format(sphinx.version_info))
            else:
                version = sphinx.__version__
                print('Found Sphinx version (old) {0}'.format(sphinx.__version__))
                sphinx.version_info = version.split('.')

            if sphinx.version_info < (1, 5, 1):
                print('sphinx version is older than 1.5.1')
                sys.exit(1)

        except Exception as e:
            print(e)
            sys.exit(1)

        print('checking for sphinx.writers.manpage... '),
        try:
            from sphinx.writers import manpage
            print('yes')
        except Exception as e:
            print(e)
            sys.exit(1)

        print('checking for sphinxcontrib.plantuml...'),
        try:
            import sphinxcontrib.plantuml
            print('yes')
        except Exception as e:
            print(e);
            sys.exit(1)
