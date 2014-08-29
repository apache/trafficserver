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

man_pages = [
  # Add all files in the reference/api directory to the list of manual
  # pages
  ('reference/api/' + filename[:-4], filename.split('.', 1)[0], '', None, '3ts') for filename in os.listdir('reference/api') if filename != 'index.en.rst' and filename.endswith('.rst')] + [

  ('reference/commands/traffic_cop.en', 'traffic_cop', u'Traffic Server watchdog', None, '8'),
  ('reference/commands/traffic_line.en', 'traffic_line', u'Traffic Server command line', None, '8'),
  ('reference/commands/traffic_logcat.en', 'traffic_logcat', u'Traffic Server log spooler', None, '8'),
  ('reference/commands/traffic_logstats.en', 'traffic_logstats', u'Traffic Server analyzer', None, '8'),
  ('reference/commands/traffic_manager.en', 'traffic_manager', u'Traffic Server process manager', None, '8'),
  ('reference/commands/traffic_server.en', 'traffic_server', u'Traffic Server', None, '8'),

  ('reference/commands/tspush.en', 'tspush', u'Push objects into the Traffic Server cache', None, '1'),
  ('reference/commands/traffic_top.en','traffic_top', u'Display Traffic Server statistics', None, '1'),
  ('reference/commands/tsxs.en', 'tsxs', u'Traffic Server plugin tool', None, '1'),

  ('reference/configuration/cache.config.en', 'cache.config', u'Traffic Server cache configuration file', None, '5'),
  ('reference/configuration/congestion.config.en', 'congestion.config', u'Traffic Server congestion control configuration file', None, '5'),
  ('reference/configuration/hosting.config.en', 'hosting.config', u'Traffic Server domain hosting configuration file', None, '5'),
  ('reference/configuration/icp.config.en', 'icp.config', u'Traffic Server ICP configuration file', None, '5'),
  ('reference/configuration/ip_allow.config.en', 'ip_allow.config', u'Traffic Server IP access control configuration file', None, '5'),
  ('reference/configuration/log_hosts.config.en', 'log_hosts.config', u'Traffic Server log host configuration file', None, '5'),
  ('reference/configuration/logs_xml.config.en', 'logs_xml.config', u'Traffic Server log format configuration file', None, '5'),
  ('reference/configuration/parent.config.en', 'parent.config', u'Traffic Server parent cache configuration file', None, '5'),
  ('reference/configuration/plugin.config.en', 'plugin.config', u'Traffic Server global plugin configuration file', None, '5'),
  ('reference/configuration/records.config.en', 'records.config', u'Traffic Server configuration file', None, '5'),
  ('reference/configuration/remap.config.en', 'remap.config', u'Traffic Server remap rules configuration file', None, '5'),
  ('reference/configuration/splitdns.config.en', 'splitdns.config', u'Traffic Server split DNS configuration file', None, '5'),
  ('reference/configuration/ssl_multicert.config.en', 'ssl_multicert.config', u'Traffic Server SSL certificate configuration file', None, '5'),
  ('reference/configuration/storage.config.en', 'storage.config', u'Traffic Server cache storage configuration file', None, '5'),
  ('reference/configuration/update.config.en', 'update.config', u'Traffic Server automated update configuration file', None, '5'),
  ('reference/configuration/volume.config.en', 'volume.config', u'Traffic Server cache volume configuration file', None, '5'),

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
      print page[1] + '.' + page[4]
