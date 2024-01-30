# -*- coding: utf-8 -*-
#
# Copyright 2019, Oath Inc.
# SPDX-License-Identifier: Apache-2.0
"""
    Transaction Box Sphinx Directives
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx Docs directives for Transaction Box plugin.
"""

from docutils import nodes
from docutils.parsers import rst
from docutils.parsers.rst import directives
from sphinx.domains import Domain, ObjType, std
from sphinx.roles import XRefRole
from sphinx.locale import _
import sphinx

import re


def txb_value_type_options(argument):
    return directives.choice(argument, ('NULL', 'string', 'integer', 'boolean', 'ip-address', 'duration'))


def txb_make_field(self, tag, value):
    field = nodes.field()
    field.append(nodes.field_name(text=tag))
    body = nodes.field_body()
    if isinstance(value, str):
        body.append(sphinx.addnodes.compact_paragraph(text=value))
    else:
        body.append(value)
    field.append(body)
    return field


class TxbDirective(std.Target):
    """
    Transaction Box Directive.
    """

    make_field = txb_make_field

    required_arguments = 1  # name of directive
    optional_arguments = 0
    final_argument_whitespace = False
    has_content = True

    option_spec = {
        'class': rst.directives.class_option,
        'arg': rst.directives.unchanged,
        'value': rst.directives.unchanged,
        'keys': rst.directives.unchanged,
        'hooks': rst.directives.unchanged
    }

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0]
        txb_id = nodes.make_id(txb_name)

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'directive'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_name)
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'directive'
        self.add_name(title)
        title.set_class('directive-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)
        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['directive'][txb_name] = env.docname

        fl = nodes.field_list()
        if ('keys' in self.options):
            key_field = nodes.field()
            key_field.append(nodes.field_name(text='Secondary Keys'))
            key_value = nodes.field_list()
            key_body = nodes.field_body()
            key_body.append(key_value)
            key_field.append(key_body)
            key_list = self.options['keys'].split('|')
            for key in key_list:
                tag = key
                descr = ''
                if ':' in key:
                    (tag, descr) = key.split(':')
                tag = tag.strip()
                descr = descr.strip()
                key_value.append(self.make_field(tag, descr))
            fl.append(key_field)
        if ('arg' in self.options):
            fl.append(self.make_field('Argument', self.options['arg']))
        if ('value' in self.options):
            fl.append(self.make_field('Value', self.options['value']))

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, fl, nn]


#  Extractor support
class TxbExtractor(std.Target):
    """
    Extractor description.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True
    has_content = True

    make_field = txb_make_field

    option_spec = {
        'class': rst.directives.class_option,
        'arg': rst.directives.unchanged,
        #        'result': txb_value_type_options
        'result': rst.directives.unchanged
    }

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0]
        txb_id = nodes.make_id(txb_name)

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'extractor'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'extractor'
        self.add_name(title)
        title.set_class('directive-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)

        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['extractor'][txb_name] = env.docname

        fl = nodes.field_list()
        if ('result' in self.options):
            fl.append(self.make_field('Result', sphinx.addnodes.literal_emphasis(text=self.options['result'])))


#            fl.append(self.make_field('Result', self.options['result']))
        if ('arg' in self.options):
            fl.append(self.make_field('Argument', self.options['arg']))

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, fl, nn]


class TxbDirectiveRef(XRefRole):

    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target


class TxbExtractorRef(XRefRole):

    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target


class TxbComparison(std.Target):
    """
    Comparison description.
    """
    make_field = txb_make_field

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    has_content = True

    option_spec = {
        'class': rst.directives.class_option,
        'arg': rst.directives.unchanged,
        'type': rst.directives.unchanged,
        'groups': rst.directives.unchanged,
        'tuple': rst.directives.flag
    }

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0]
        txb_id = nodes.make_id(txb_name)

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'comparison'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'comparison'
        self.add_name(title)
        title.set_class('comparison-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)
        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        fl = nodes.field_list()
        if ('arg' in self.options):
            fl.append(self.make_field('Argument', self.options['arg']))
        if ('type' in self.options):
            fl.append(self.make_field('Value Types', self.options['type']))
        if ('tuple' in self.options):
            fl.append(self.make_field('List matching', sphinx.addnodes.literal_emphasis(text='enabled')))
        if ('groups' in self.options):
            fl.append(self.make_field('Groups', self.options['groups']))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['comparison'][txb_name] = env.docname

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, fl, nn]


class TxbRef(XRefRole):

    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target


class TxbModifier(std.Target):
    """
    Transaction Box Modifier.
    """

    make_field = txb_make_field

    required_arguments = 1  # name of modifier
    optional_arguments = 0
    final_argument_whitespace = False
    has_content = True
    obj_type = 'modifier'

    option_spec = {
        'class': rst.directives.class_option,
        'arg': rst.directives.unchanged,
        'expr': rst.directives.unchanged,
        'value': rst.directives.unchanged,
        'result': rst.directives.unchanged
    }

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0]
        txb_id = nodes.make_id(txb_name)

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = self.obj_type

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_name)
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = self.obj_type
        self.add_name(title)
        title.set_class('modifier-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)
        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb'][self.obj_type][txb_name] = env.docname

        fl = nodes.field_list()
        if ('keys' in self.options):
            fl.append(self.make_field('Secondary Keys', self.options['keys']))
        if ('arg' in self.options):
            fl.append(self.make_field('Argument', self.options['arg']))
        if ('value' in self.options):
            fl.append(self.make_field('Value', self.options['value']))

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, fl, nn]


class TxnBoxDomain(Domain):
    """
    Transaction Box Documentation.
    """

    name = 'txb'
    label = 'Transaction Box'
    data_version = 2

    object_types = {
        'directive': ObjType(_('Directive'), 'directive'),
        'extractor': ObjType(_('Extractor'), 'extractor'),
        'comparison': ObjType(_('Comparison'), 'comparison'),
        'modifier': ObjType(_('Modifier'), 'modifier')
    }

    directives = {'directive': TxbDirective, 'extractor': TxbExtractor, 'comparison': TxbComparison, 'modifier': TxbModifier}

    roles = {'drtv': TxbDirectiveRef(), 'ex': TxbExtractorRef(), 'cmp': TxbRef(), 'mod': TxbRef()}

    initial_data = {
        'directive': {},  # full name -> docname
        'extractor': {},
        'comparison': {},
        'modifier': {}
    }

    dangling_warnings = {
        'directive': "No definition found for directive '%(target)s'",
        'extractor': "No definition found for extractor '%(target)s'",
        'comparison': "No definition found for comparison '%(target)s'",
        'modifier': "No definition found for modifier '%(targets)s'"
    }

    def clear_doc(self, docname):
        tmp_list = self.data['directive']
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]
        tmp_list = self.data['extractor']
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]
        tmp_list = self.data['comparison']
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]
        tmp_list = self.data['modifier']
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]

    def find_doc(self, key, obj_type):
        zret = None

        if obj_type == 'drtv':
            obj_list = self.data['directive']
        elif obj_type == 'extractor':
            obj_list = self.data['extractor']
        elif obj_type == 'cmp':
            obj_list = self.data['comparison']
        elif obj_type == 'ex':
            obj_list = self.data['extractor']
        elif obj_type == 'mod':
            obj_list = self.data['modifier']
        else:
            obj_list = None

        if obj_list and key in obj_list:
            zret = obj_list[key]

        return zret

    def resolve_xref(self, env, src_doc, builder, obj_type, target, node, cont_node):
        dst_doc = self.find_doc(target, obj_type)
        if (dst_doc):
            return sphinx.util.nodes.make_refnode(builder, src_doc, dst_doc, nodes.make_id(target), cont_node, target)

    def get_objects(self):
        for var, doc in self.data['directive'].items():
            yield var, var, 'directive', doc, var, 1
        for var, doc in self.data['extractor'].items():
            yield var, var, 'extractor', doc, var, 1
        for var, doc in self.data['comparison'].items():
            yield var, var, 'comparison', doc, var, 1
        for var, doc in self.data['modifier'].items():
            yield var, var, 'modifier', doc, var, 1


def setup(app):
    rst.roles.register_generic_role('arg', nodes.emphasis)
    rst.roles.register_generic_role('const', nodes.literal)

    app.add_domain(TxnBoxDomain)
