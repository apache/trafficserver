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

man_pages = [
    # Add all files in the reference/api directory to the list of manual
    # pages
    ('developer-guide/api/functions/' + filename[:-4], filename.split('.', 1)[0], filename.split('.', 1)[0] + ' API function', None, '3ts') for filename in os.listdir('developer-guide/api/functions/') if filename != 'index.en.rst' and filename.endswith('.rst')] + [

    ('appendices/command-line/traffic_ctl.en', 'traffic_ctl', u'Traffic Server command line tool', None, '8'),
    ('appendices/command-line/traffic_crashlog.en', 'traffic_crashlog', u'Traffic Server crash log helper', None, '8'),
    ('appendices/command-line/traffic_logcat.en', 'traffic_logcat', u'Traffic Server log spooler', None, '8'),
    ('appendices/command-line/traffic_logstats.en', 'traffic_logstats', u'Traffic Server analyzer', None, '8'),
    ('appendices/command-line/traffic_manager.en', 'traffic_manager', u'Traffic Server process manager', None, '8'),
    ('appendices/command-line/traffic_server.en', 'traffic_server', u'Traffic Server', None, '8'),

    ('appendices/command-line/tspush.en', 'tspush', u'Push objects into the Traffic Server cache', None, '1'),
    ('appendices/command-line/traffic_top.en', 'traffic_top', u'Display Traffic Server statistics', None, '1'),
    ('appendices/command-line/tsxs.en', 'tsxs', u'Traffic Server plugin tool', None, '1'),
    ('appendices/command-line/traffic_via.en', 'traffic_via', u'Traffic Server Via header decoder', None, '1'),

    ('admin-guide/files/cache.config.en', 'cache.config', u'Traffic Server cache configuration file', None, '5'),
    ('admin-guide/files/hosting.config.en', 'hosting.config', u'Traffic Server domain hosting configuration file', None, '5'),
    ('admin-guide/files/ip_allow.config.en', 'ip_allow.config', u'Traffic Server IP access control configuration file', None, '5'),
    ('admin-guide/files/log_hosts.config.en', 'log_hosts.config', u'Traffic Server log host configuration file', None, '5'),
    ('admin-guide/files/logging.config.en', 'logging.config', u'Traffic Server logging configuration file', None, '5'),
    ('admin-guide/files/parent.config.en', 'parent.config', u'Traffic Server parent cache configuration file', None, '5'),
    ('admin-guide/files/plugin.config.en', 'plugin.config', u'Traffic Server global plugin configuration file', None, '5'),
    ('admin-guide/files/records.config.en', 'records.config', u'Traffic Server configuration file', None, '5'),
    ('admin-guide/files/remap.config.en', 'remap.config', u'Traffic Server remap rules configuration file', None, '5'),
    ('admin-guide/files/splitdns.config.en', 'splitdns.config', u'Traffic Server split DNS configuration file', None, '5'),
    ('admin-guide/files/ssl_multicert.config.en', 'ssl_multicert.config',
        u'Traffic Server SSL certificate configuration file', None, '5'),
    ('admin-guide/files/storage.config.en', 'storage.config', u'Traffic Server cache storage configuration file', None, '5'),
    ('admin-guide/files/volume.config.en', 'volume.config', u'Traffic Server cache volume configuration file', None, '5'),

]

if __name__ == '__main__':
    # Use optparse instead of argparse because this needs to work on old Python versions.
    import optparse

    parser = optparse.OptionParser(description='Traffic Server Sphinx docs configuration')
    parser.add_option('--section', type=int, default=0, dest='section')

    (options, args) = parser.parse_args()

    # Print the names of the man pages for the requested manual section.
    for page in man_pages:
        if options.section == 0 or options.section == int(page[4][0]):
            print(page[1] + '.' + page[4])
