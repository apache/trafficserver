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

from docutils import nodes
from docutils.parsers import rst
from docutils.parsers.rst import directives
from sphinx.domains import Domain, ObjType, std
from sphinx.roles import XRefRole
from sphinx.locale import _
import sphinx

import os
import subprocess
import re

# 2/3 compat logic
try:
    basestring

    def is_string_type(s):
        return isinstance(s, basestring)
except NameError:

    def is_string_type(s):
        return isinstance(s, str)


class TSConfVar(std.Target):
    """
    Description of a traffic server configuration variable.

    Argument is the variable as defined in records.config.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    option_spec = {
        'class': rst.directives.class_option,
        'reloadable': rst.directives.flag,
        'deprecated': rst.directives.flag,
        'overridable': rst.directives.flag,
        'units': rst.directives.unchanged,
    }
    required_arguments = 3
    optional_arguments = 1  # default is optional, special case if omitted
    final_argument_whitespace = True
    has_content = True

    def make_field(self, tag, value):
        field = nodes.field()
        field.append(nodes.field_name(text=tag))
        body = nodes.field_body()
        if is_string_type(value):
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
        title['classes'].append('ts-cv-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(cv_name, cv_name)

        node.append(title)

        if ('class' in self.options):
            title['classes'].append(self.options.get('class'))
        # This has to be a distinct node before the title. if nested then
        # the browser will scroll forward to just past the title.
        nodes.target('', '', names=[cv_name])
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
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this config variable to the
        # index. nodes.make_id() specifies the link anchor name that is
        # implicitly generated by the anchor node above.
        indexnode = sphinx.addnodes.index(entries=[])
        if sphinx.version_info >= (1, 4):
            indexnode['entries'].append(
                ('single', _('%s') % cv_name, nodes.make_id(cv_name), '', '')
            )
        else:
            indexnode['entries'].append(
                ('single', _('%s') % cv_name, nodes.make_id(cv_name), '')
            )

        return [indexnode, node, fl, nn]


class TSConfVarRef(XRefRole):

    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target


def metrictypes(typename):
    return directives.choice(typename.lower(), ('counter', 'gauge', 'derivative', 'flag', 'text'))


def metricunits(unitname):
    return directives.choice(
        unitname.lower(), (
            'ratio', 'percent', 'kbits', 'mbits', 'bytes', 'kbytes', 'mbytes', 'nanoseconds', 'microseconds', 'milliseconds',
            'seconds'))


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
        'units': metricunits,
        'introduced': rst.directives.unchanged,
        'deprecated': rst.directives.unchanged,
        'ungathered': rst.directives.flag
    }
    required_arguments = 3
    optional_arguments = 1  # example value is optional
    final_argument_whitespace = True
    has_content = True

    def make_field(self, tag, value):
        field = nodes.field()
        field.append(nodes.field_name(text=tag))
        body = nodes.field_body()
        if is_string_type(value):
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
        title['ids'].append(nodes.make_id('stat-' + stat_name))
        title['names'].append(stat_name)
        title['first'] = False
        title['objtype'] = 'stat'
        self.add_name(title)
        title['classes'].append('ts-stat-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(stat_name, stat_name)

        node.append(title)

        # This has to be a distinct node before the title. if nested then
        # the browser will scroll forward to just past the title.
        nodes.target('', '', names=[stat_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['ts']['stat'][stat_name] = env.docname

        fl = nodes.field_list()
        fl.append(self.make_field('Collection', stat_group))
        if ('type' in self.options):
            fl.append(self.make_field('Type', self.options['type']))
        if ('units' in self.options):
            fl.append(self.make_field('Units', self.options['units']))
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
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this statistic to the
        # index. nodes.make_id() specifies the link anchor name that is
        # implicitly generated by the anchor node above.
        indexnode = sphinx.addnodes.index(entries=[])

        if sphinx.version_info >= (1, 4):
            indexnode['entries'].append(
                ('single', _('%s') % stat_name, nodes.make_id(stat_name), '', '')
            )
        else:
            indexnode['entries'].append(
                ('single', _('%s') % stat_name, nodes.make_id(stat_name), '')
            )

        return [indexnode, node, fl, nn]


class TSStatRef(XRefRole):

    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target


class TrafficServerDomain(Domain):
    """
    Apache Traffic Server Documentation.
    """

    name = 'ts'
    label = 'Traffic Server'
    data_version = 2

    object_types = {
        'cv': ObjType(_('configuration variable'), 'cv'),
        'stat': ObjType(_('statistic'), 'stat')
    }

    directives = {'cv': TSConfVar, 'stat': TSStat}

    roles = {'cv': TSConfVarRef(), 'stat': TSStatRef()}

    initial_data = {
        'cv': {},  # full name -> docname
        'stat': {}
    }

    dangling_warnings = {
        'cv': "No definition found for configuration variable '%(target)s'",
        'stat': "No definition found for statistic '%(target)s'"
    }

    def clear_doc(self, docname):
        cv_list = self.data['cv']
        for var, doc in list(cv_list.items()):
            if doc == docname:
                del cv_list[var]
        stat_list = self.data['stat']
        for var, doc in list(stat_list.items()):
            if doc == docname:
                del stat_list[var]

    def find_doc(self, key, obj_type):
        zret = None

        if obj_type == 'cv':
            obj_list = self.data['cv']
        elif obj_type == 'stat':
            obj_list = self.data['stat']
        else:
            obj_list = None

        if obj_list and key in obj_list:
            zret = obj_list[key]

        return zret

    def resolve_xref(self, env, src_doc, builder, obj_type, target, node, cont_node):
        dst_doc = self.find_doc(target, obj_type)
        if (dst_doc):
            return sphinx.util.nodes.make_refnode(builder, src_doc, dst_doc, nodes.make_id(target), cont_node, 'records.config')

    # Python 2/3 compat - iteritems is 2, items is 3
    # Although perhaps the lists are small enough items could be used in Python 2.
    try:
        {}.iteritems()

        def get_objects(self):
            for var, doc in self.data['cv'].iteritems():
                yield var, var, 'cv', doc, var, 1
            for var, doc in self.data['stat'].iteritems():
                yield var, var, 'stat', doc, var, 1
    except AttributeError:

        def get_objects(self):
            for var, doc in self.data['cv'].items():
                yield var, var, 'cv', doc, var, 1
            for var, doc in self.data['stat'].items():
                yield var, var, 'stat', doc, var, 1


# get the branch this documentation is building for in X.X.x form
REPO_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.environ['DOCUTILSCONFIG'])))
CONFIGURE_AC = os.path.join(REPO_ROOT, 'configure.ac')
with open(CONFIGURE_AC, 'r') as f:
    contents = f.read()
    match = re.compile(r'm4_define\(\[TS_VERSION_S],\[(.*?)]\)').search(contents)
    autoconf_version = '.'.join(match.group(1).split('.', 2)[:2] + ['x'])

# get the current branch the local repository is on
REPO_GIT_DIR = os.path.join(REPO_ROOT, ".git")
git_branch = subprocess.check_output(['git', '--git-dir', REPO_GIT_DIR, 'rev-parse', '--abbrev-ref', 'HEAD'])


def make_github_link(name, rawtext, text, lineno, inliner, options=None, content=None):
    """
    This docutils role lets us link to source code via the handy :ts:git: markup.
    Link references are rooted at the top level source directory. All links resolve
    to GitHub.

    Examples:

        To link to proxy/Main.cc:

            Hi, here is a link to the proxy entry point: :ts:git:`proxy/Main.cc`.

        To link to CONTRIBUTING.md:

            If you want to contribute, take a look at :ts:git:`CONTRIBUTING.md`.
    """
    if options is None:
        options = {}
    if content is None:
        content = []
    url = 'https://github.com/apache/trafficserver/blob/{}/{}'
    ref = autoconf_version if autoconf_version == git_branch else 'master'
    node = nodes.reference(rawtext, text, refuri=url.format(ref, text), **options)
    return [node], []


def setup(app):
    app.add_crossref_type('configfile', 'file', objname='Configuration file', indextemplate='pair: %s; Configuration files')

    # Very ugly, but as of Sphinx 1.8 it must be done. There is an `override` option to add_crossref_type
    # but it only applies to the directive, not the role (`file` in this case). If this isn't cleared
    # explicitly the build will fail out due to the conflict. In this case, since the role action is the
    # same in all cases, the output is correct. This does assume the config file names and log files
    # names are disjoint sets.
    del app.registry.domain_roles['std']['file']

    app.add_crossref_type('logfile', 'file', objname='Log file', indextemplate='pair: %s; Log files')

    rst.roles.register_generic_role('arg', nodes.emphasis)
    rst.roles.register_generic_role('const', nodes.literal)

    app.add_domain(TrafficServerDomain)

    # this lets us do :ts:git:`<file_path>` and link to the file on github
    app.add_role_to_domain('ts', 'git', make_github_link)
