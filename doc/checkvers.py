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

import sys, os

if __name__ == '__main__':
  # Use optparse instead of argparse because this needs to work on old Python versions.
  import optparse

  parser = optparse.OptionParser(description='Traffic Server Sphinx docs configuration')
  parser.add_option('--check-version', action='store_true', dest='checkvers')

  (options, args) = parser.parse_args()

  # Check whether we have a recent version of sphinx. EPEL and CentOS are completely crazy and I don't understand their
  # packaging at all. The test below works on Ubuntu and places where sphinx is installed sanely AFAICT.
  if options.checkvers:
    print 'checking for sphinx version >= 1.1... ',
    try:
      import sphinx

      version = sphinx.__version__
      print 'found ' + sphinx.__version__

      (major, minor, micro) = version.split('.')
      if (int(major) < 1) or (int(major) == 1 and int(minor) < 1):
          sys.exit(1)

    except Exception as e:
      print e
      sys.exit(1)

    print 'checking for sphinx.writers.manpage... ',
    try:
        from sphinx.writers import manpage
        print 'yes'
    except Exception as e:
      print e
      sys.exit(1)
