# -*- coding: utf-8 -*-
#
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

"""
    TS Sphinx Directives
    ~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx Docs directives for Apache Traffic Server

    :copyright: Copyright 2013 by the Apache Software Foundation
    :license: Apache
"""

from docutils import nodes, utils
from docutils.parsers.rst.roles import set_classes
from docutils.parsers import rst
from docutils.parsers.rst import directives
from sphinx.domains import Domain, ObjType, std
from sphinx.roles import XRefRole
from sphinx.locale import l_, _
import sphinx

# Autolink for Trafficserver issues
# Moved from doc/conf.py, now integrated into TS domain
#
# Two types of issues supported:
# * Jira: for archive purpose only (after the Jira to Github move)
# * Github: for new issues (after the Jira to Github move)
#
# Syntax:
# :ts:jira:`XXXX` where XXXX is the issue number
# :ts:github:`XXXX` where XXXX is the issue number
#
# Output
# * Prefix issue number with 'TS-'
# * Render HTML link:
#   - either to https://issues.apache.org/jira/browse/TS-XXXX
#   - or to https://github.com/apache/trafficserver/issues/XXXX

def ts_jira_role(name, rawtext,  issue_num, lineno, inliner, options={}, content=[]):
  """Link to a Trafficserver Jira issue.

  Returns 2 part tuple containing list of nodes to insert into the
  document and a list of system messages.  Both are allowed to be
  empty.

  :param name: The role name used in the document.
  :param rawtext: The entire markup snippet, with role.
  :param issue_num: The issue number marked with the role.
  :param lineno: The line number where rawtext appears in the input.
  :param inliner: The inliner instance that called us.
  :param options: Directive options for customization.
  :param content: The directive content for customization.
  """
  app = inliner.document.settings.env.app
  try:
    base_url = app.config.trafficserver_jira_url
    if not base_url:
      raise AttributeError
  except AttributeError, err:
    raise ValueError('trafficserver_jira_url configuration values not set (%s)' % str(err))
  #
  issue_prefix = 'TS-'
  node = make_link_node(rawtext, app, base_url, issue_prefix, issue_num, options)
  return [node], []

def ts_github_role(name, rawtext,  issue_num, lineno, inliner, options={}, content=[]):
  """Link to a Trafficserver Github issue.

  Returns 2 part tuple containing list of nodes to insert into the
  document and a list of system messages.  Both are allowed to be
  empty.

  :param name: The role name used in the document.
  :param rawtext: The entire markup snippet, with role.
  :param text: The text marked with the role.
  :param lineno: The line number where rawtext appears in the input.
  :param inliner: The inliner instance that called us.
  :param options: Directive options for customization.
  :param content: The directive content for customization.
  """
  app = inliner.document.settings.env.app
  try:
    base_url = app.config.trafficserver_github_url
    if not base_url:
      raise AttributeError
  except AttributeError, err:
    raise ValueError('trafficserver_github_url configuration values not set (%s)' % str(err))
  #
  issue_prefix = ''
  node = make_link_node(rawtext, app, base_url, issue_prefix, issue_num, options)
  return [node], []

def make_link_node(rawtext, app, base_url, issue_prefix, issue_num, options):
  """Create a link to a Apache Jira resource.

  :param rawtext: Text being replaced with link node.
  :param app: Sphinx application context
  :param type: Link type ('jira' or 'github')
  :param slug: ID of the thing to link to
  :param options: Options dictionary passed to role func.
  """
  #
  try:
      issue_num_int = int(issue_num)
      if issue_num_int <= 0:
          raise ValueError
  except ValueError:
    raise ValueError('Trafficserver issue number must be a number greater than or equal to 1; '
                     '"%s" is invalid.' % text, line=lineno)
  #
  base_url = base_url + issue_prefix + '{0}'
  ref = base_url.format(issue_num)
  set_classes(options)
  node = nodes.reference(rawtext, issue_prefix + issue_num, refuri=ref,
                         **options)
  return node

class TSConfVar(std.Target):
    """
    Description of a traffic server configuration variable.

    Argument is the variable as defined in records.config.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    option_spec = {
        'class' : rst.directives.class_option,
        'reloadable' : rst.directives.flag,
        'deprecated' : rst.directives.flag,
        'overridable' : rst.directives.flag,
        'units' : rst.directives.unchanged,
    }
    required_arguments = 3
    optional_arguments = 1 # default is optional, special case if omitted
    final_argument_whitespace = True
    has_content = True

    def make_field(self, tag, value):
        field = nodes.field();
        field.append(nodes.field_name(text=tag))
        body = nodes.field_body()
        if (isinstance(value, basestring)):
            body.append(sphinx.addnodes.compact_paragraph(text=value))
        else:
            body.append(value)
        field.append(body)
        return field

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        cv_default = None
        cv_scope, cv_name, cv_type = self.arguments[0:3]
        if (len(self.arguments) > 3):
            cv_default = self.arguments[3]

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'cv'

        # Next, make a signature node. This creates a permalink and a
        # highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(cv_name, '')
        title['ids'].append(nodes.make_id(cv_name))
        title['ids'].append(cv_name)
        title['names'].append(cv_name)
        title['first'] = False
        title['objtype'] = 'cv'
        self.add_name(title)
        title.set_class('ts-cv-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(cv_name, cv_name)

        node.append(title)

        if ('class' in self.options):
            title.set_class(self.options.get('class'))
        # This has to be a distinct node before the title. if nested then
        # the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[cv_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['ts']['cv'][cv_name] = env.docname

        fl = nodes.field_list()
        fl.append(self.make_field('Scope', cv_scope))
        fl.append(self.make_field('Type', cv_type))
        if (cv_default):
            fl.append(self.make_field('Default', cv_default))
        else:
            fl.append(self.make_field('Default', sphinx.addnodes.literal_emphasis(text='*NONE*')))
        if ('units' in self.options):
            fl.append(self.make_field('Units', self.options['units']))
        if ('reloadable' in self.options):
            fl.append(self.make_field('Reloadable', 'Yes'))
        if ('overridable' in self.options):
            fl.append(self.make_field('Overridable', 'Yes'))
        if ('deprecated' in self.options):
            fl.append(self.make_field('Deprecated', 'Yes'))

        # Get any contained content
        nn = nodes.compound();
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this config variable to the
        # index. nodes.make_id() specifies the link anchor name that is
        # implicitly generated by the anchor node above.
        indexnode = sphinx.addnodes.index(entries=[])
        if sphinx.version_info >= (1, 4) :
            indexnode['entries'].append(
                ('single', _('%s') % cv_name, nodes.make_id(cv_name), '', '')
            )
        else :
            indexnode['entries'].append(
                ('single', _('%s') % cv_name, nodes.make_id(cv_name), '')
            )

        return [ indexnode, node, fl, nn ]


class TSConfVarRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target

def metrictypes(typename):
    return directives.choice(typename.lower(), ('counter','gauge','derivative','flag','text'))

def metricunits(unitname):
    return directives.choice(unitname.lower(), ('ratio','percent','kbits','mbits','bytes','kbytes','mbytes','nanoseconds','microseconds','milliseconds','seconds'))

class TSStat(std.Target):
    """
    Description of a traffic server statistic.

    Argument is the JSON stat group ("global", etc.) in which the statistic is
    returned, then the statistic name as used by traffic_ctl/stats_over_http,
    followed by the value type of the statistic ('string', 'integer'), and
    finally an example value.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered
    equivalent to the Doxygen short and long description.
    """

    option_spec = {
        'type': metrictypes,
        'unit': metricunits,
        'introduced' : rst.directives.unchanged,
        'deprecated' : rst.directives.unchanged,
        'ungathered' : rst.directives.flag
    }
    required_arguments = 3
    optional_arguments = 1 # example value is optional
    final_argument_whitespace = True
    has_content = True

    def make_field(self, tag, value):
        field = nodes.field();
        field.append(nodes.field_name(text=tag))
        body = nodes.field_body()
        if (isinstance(value, basestring)):
            body.append(sphinx.addnodes.compact_paragraph(text=value))
        else:
            body.append(value)
        field.append(body)
        return field

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        stat_example = None
        stat_group, stat_name, stat_type = self.arguments[0:3]
        if (len(self.arguments) > 3):
            stat_example = self.arguments[3]

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'stat'

        # Next, make a signature node. This creates a permalink and a
        # highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(stat_name, '')
        title['ids'].append(nodes.make_id('stat-'+stat_name))
        title['names'].append(stat_name)
        title['first'] = False
        title['objtype'] = 'stat'
        self.add_name(title)
        title.set_class('ts-stat-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(stat_name, stat_name)

        node.append(title)

        # This has to be a distinct node before the title. if nested then
        # the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[stat_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['ts']['stat'][stat_name] = env.docname

        fl = nodes.field_list()
        fl.append(self.make_field('Collection', stat_group))
        if ('type' in self.options):
            fl.append(self.make_field('Type', self.options['type']))
        if ('unit' in self.options):
            fl.append(self.make_field('Units', self.options['unit']))
        fl.append(self.make_field('Datatype', stat_type))
        if ('introduced' in self.options and len(self.options['introduced']) > 0):
            fl.append(self.make_field('Introduced', self.options['introduced']))
        if ('deprecated' in self.options):
            if (len(self.options['deprecated']) > 0):
                fl.append(self.make_field('Deprecated', self.options['deprecated']))
            else:
                fl.append(self.make_field('Deprecated', 'Yes'))
        if ('ungathered' in self.options):
            fl.append(self.make_field('Gathered', 'No'))
        if (stat_example):
            fl.append(self.make_field('Example', stat_example))

        # Get any contained content
        nn = nodes.compound();
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this statistic to the
        # index. nodes.make_id() specifies the link anchor name that is
        # implicitly generated by the anchor node above.
        indexnode = sphinx.addnodes.index(entries=[])

        if sphinx.version_info >= (1, 4) :
            indexnode['entries'].append(
                ('single', _('%s') % stat_name, nodes.make_id(stat_name), '', '')
            )
        else :
            indexnode['entries'].append(
                ('single', _('%s') % stat_name, nodes.make_id(stat_name), '')
            )

        return [ indexnode, node, fl, nn ]

class TSStatRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target

class TSIssueRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return 'TS-' + title, 'https://issues.apache.org/jira/browse/TS-' + target

class TrafficServerDomain(Domain):
    """
    Apache Traffic Server Documentation.
    """

    name = 'ts'
    label = 'Traffic Server'
    data_version = 2

    object_types = {
        'cv': ObjType(l_('configuration variable'), 'cv'),
        'stat': ObjType(l_('statistic'), 'stat'),
        'jira': ObjType(l_('jira'), 'jira')
    }

    directives = {
        'cv' : TSConfVar,
        'stat' : TSStat
    }

    roles = {
        'cv' : TSConfVarRef(),
        'stat' : TSStatRef(),
        'jira' : ts_jira_role,
        'github' : ts_github_role
    }

    initial_data = {
        'cv' : {}, # full name -> docname
        'stat' : {},
        'issue' : {}
    }

    dangling_warnings = {
        'cv' : "No definition found for configuration variable '%(target)s'",
        'stat' : "No definition found for statistic '%(target)s'",
        'issue' : "No definition found for issue '%(target)s'"
    }

    def clear_doc(self, docname):
        cv_list = self.data['cv']
        for var, doc in cv_list.items():
            if doc == docname:
                del cv_list[var]
        stat_list = self.data['stat']
        for var, doc in stat_list.items():
            if doc == docname:
                del stat_list[var]
        issue_list = self.data['issue']
        for var, doc in issue_list.items():
            if doc == docname:
                del issue_list[var]

    def find_doc(self, key, obj_type):
        zret = None

        if obj_type == 'cv' :
            obj_list = self.data['cv']
        elif obj_type == 'stat' :
            obj_list = self.data['stat']
        elif obj_type == 'issue' :
            obj_list = self.data['issue']
        else:
            obj_list = None

        if obj_list and key in obj_list:
            zret = obj_list[key]

        return zret

    def resolve_xref(self, env, src_doc, builder, obj_type, target, node, cont_node):
        if obj_type == 'issue':
            return sphinx.util.nodes.make_refnode(builder, src_doc, src_doc, nodes.make_id(target), cont_node, None)
        else:
            dst_doc = self.find_doc(target, obj_type)
            if (dst_doc):
                return sphinx.util.nodes.make_refnode(builder, src_doc, dst_doc, nodes.make_id(target), cont_node, 'records.config')

    def get_objects(self):
        for var, doc in self.data['cv'].iteritems():
            yield var, var, 'cv', doc, var, 1
        for var, doc in self.data['stat'].iteritems():
            yield var, var, 'stat', doc, var, 1
        for var, doc in self.data['issue'].iteritems():
            yield var, var, 'issue', doc, var, 1

# These types are ignored as missing references for the C++ domain.
# We really need to do better with this. Editing this file for each of
# these is already getting silly.
EXTERNAL_TYPES = set((
    'bool', 'int', 'uint',
    'uint8_t', 'uint16_t', 'uint24_t', 'uint32_t', 'uint64_t',
    'int8_t', 'int16_t', 'int24_t', 'int32_t', 'int64_t',
    'unsigned', 'unsigned int',
    'off_t', 'size_t', 'time_t',
    'Event', 'INK_MD5', 'DLL<EvacuationBlock>',
    'sockaddr'
    ))

# Clean up specific references that we know will never be defined but are implicitly used by
# other domain directives. Hand convert them to literals.
def xref_cleanup(app, env, node, contnode):
    rdomain = node['refdomain']
    rtype = node['reftype']
    rtarget = node['reftarget']
    if ('cpp' == rdomain) or ('c' == rdomain):
        if 'type' == rtype:
            # one of the predefined type, or a pointer or reference to it.
            if (rtarget in EXTERNAL_TYPES) or (('*' == rtarget[-1] or '&' == rtarget[-1]) and rtarget[:-1] in EXTERNAL_TYPES):
                node = nodes.literal()
                node += contnode
                return node
    return;

def setup(app):
    app.add_crossref_type('configfile', 'file',
                        objname='Configuration file',
                        indextemplate='pair: %s; Configuration files')
    app.add_crossref_type('logfile', 'file',
                        objname='Log file',
                        indextemplate='pair: %s; Log files')

    rst.roles.register_generic_role('arg', nodes.emphasis)
    rst.roles.register_generic_role('const', nodes.literal)

    app.add_domain(TrafficServerDomain)
    app.add_config_value('trafficserver_jira_url', None, 'env')
    app.add_config_value('trafficserver_github_url', None, 'env')

    # Types that we want the C domain to consider built in
    for word in EXTERNAL_TYPES:
        sphinx.domains.c.CObject.stopwords.add(word)

    app.connect('missing-reference', xref_cleanup)
