# -*- coding: utf-8 -*-
"""
    TS Sphinx Directives
    ~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx Docs directives for Apache Traffic Server

    :copyright: Copyright 2013 by the Apache Software Foundation
    :license: Apache
"""

from docutils import nodes
from docutils.parsers import rst
from sphinx import addnodes
from sphinx.locale import l_, _

class TSConfVar(rst.Directive):
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
    }
    required_arguments = 3
    optional_arguments = 1
    final_argument_whitespace = True
    has_content = True

    def make_field(self, tag, value):
        field = nodes.field();
        field.append(nodes.field_name(text=tag))
        body = nodes.field_body()
        if (isinstance(value, basestring)):
            body.append(addnodes.compact_paragraph(text=value))
        else:
            body.append(value)
        field.append(body)
        return field

    # External entry point
    def run(self):
        cv_default = None
        cv_scope, cv_name, cv_type = self.arguments[0:3]
        if (len(self.arguments) > 3):
            cv_default = self.arguments[3]
        title = addnodes.desc_name(text=cv_name)
        title.set_class('ts-confvar-title')
        if ('class' in self.options):
            title.set_class(self.options.get('class'))
        # Must have empty strings or it assumes it is attached to another node.
        target = nodes.target('', '', names=[cv_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(target);

        # Do the property fields
        fl = nodes.field_list();
        fl.append(self.make_field('Scope', cv_scope))
        fl.append(self.make_field('Type', cv_type))
        if (cv_default):
            fl.append(self.make_field('Default', cv_default))
        else:
            fl.append(self.make_field('Default', addnodes.literal_emphasis(text='*NONE*')))
        if ('reloadable' in self.options):
            fl.append(self.make_field('Reloadable', 'Yes'))
        if ('deprecated' in self.options):
            fl.append(self.make_field('Deprecated', 'Yes'))

        # Get any contained content
        nn = nodes.compound();
        self.state.nested_parse(self.content, self.content_offset, nn)

        return [ target, title, fl, nn ]

def setup(app):
    app.add_directive('ts:confvar', TSConfVar)
