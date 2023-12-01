# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version
# 2.0 (the "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.  See the License for the specific language governing
# permissions and limitations under the License.

import os
import subprocess
from docutils import nodes
from os import path
from sphinx import addnodes
from sphinx.util import osutil

try:
    from lxml import etree

except ImportError:
    etree = None

# Run Doxygen on Read the Docs to generate XML files
if os.environ.get('READTHEDOCS'):
    subprocess.call('doxygen')

if etree and path.isfile('xml/index.xml'):

    # Doxygen files that have already been parsed
    cache = {}

    # Doxygen index
    index = etree.parse('xml/index.xml')


def escape(name):
    """
    Partial reimplementation in Python of Doxygen escapeCharsInString()
    """

    return name.replace('_', '__').replace(':', '_1').replace('/', '_2').replace('<', '_3').replace('>', '_4').replace('*', '_5').replace('&', '_6').replace('|', '_7').replace('.', '_8').replace('!', '_9').replace(',', '_00').replace(' ', '_01').replace('{', '_02').replace('}', '_03').replace('?', '_04').replace('^', '_05').replace('%', '_06').replace('(', '_07').replace(')', '_08').replace('+', '_09').replace('=', '_0A').replace('$', '_0B').replace('\\', '_0C')  # yapf: disable


class doctree_resolved:
    """
    Add links from an API description to the source code for that object.
    Doxygen knows where in the source code it's located.
    Based on the sphinx.ext.viewcode and sphinx.ext.linkcode extensions.
    """

    has_link = None

    def __init__(self, app, doctree, docname):

        self.app = app
        self.docname = docname

        self.traverse(doctree, None)
        if self.has_link:

            # Style the links
            raw = nodes.raw(
                '',
                '<style> .rst-content dl dt .headerlink { display: inline-block } .rst-content dl dt .headerlink:after { visibility: hidden } .rst-content dl dt .viewcode-link { color: #2980b9; float: right; font-size: inherit; font-weight: normal } .rst-content dl dt:hover .headerlink:after { visibility: visible } </style>',
                format='html')
            doctree.insert(0, raw)

    def traverse(self, node, owner):
        """
        If an API description is nested in another description,
        lookup the child in the context of the parent
        """

        # nodes.Text iterates over characters, not children
        for child in node.children:
            if isinstance(child, addnodes.desc):
                for desc_child in child.children:
                    if isinstance(desc_child, addnodes.desc_signature):

                        # Get the name of the object.  An owner in the signature
                        # overrides an owner from a parent description.
                        signature_owner = None
                        for child in desc_child.children:
                            if isinstance(child, addnodes.desc_addname):

                                # An owner in the signature ends with ::
                                signature_owner = child.astext()[:-2]

                            elif isinstance(child, addnodes.desc_name):
                                name = child.astext()

                                break

                        # Lookup the object in the Doxygen index
                        try:
                            compound, = index.xpath(
                                'descendant::compound[(not($owner) or name[text() = $owner]) and descendant::name[text() = $name]][1]',
                                owner=signature_owner or owner,
                                name=name)

                        except ValueError:
                            continue

                        filename = compound.get('refid') + '.xml'
                        if filename not in cache:
                            cache[filename] = etree.parse('xml/' + filename)

                        # An enumvalue has no location
                        memberdef, = cache[filename].xpath(
                            'descendant::compounddef[compoundname[text() = $name]]', name=name) or cache[filename].xpath(
                                'descendant::memberdef[name[text() = $name] | enumvalue[name[text() = $name]]]', name=name)

                        # Append the link after the object's signature.
                        # Get the source file and line number from Doxygen and use
                        # them to construct the link.
                        location = memberdef.find('location')
                        filename = path.basename(location.get('file'))

                        # Declarations have no bodystart
                        line = location.get('bodystart') or location.get('line')

                        emphasis = nodes.emphasis('', ' ' + filename + ' line ' + line)

                        # Use a relative link if the output is HTML, otherwise fall
                        # back on an absolute link to Read the Docs.  I haven't
                        # figured out how to get the page name for e.g. a struct
                        # from the XML files so ape Doxygen escapeCharsInString()
                        # instead.
                        refuri = 'api/' + escape(filename) + '_source.html#l' + line.rjust(5, '0')
                        if self.app.builder.name == 'html':
                            refuri = osutil.relative_uri(self.app.builder.get_target_uri(self.docname), refuri)

                        else:
                            refuri = 'http://docs.trafficserver.apache.org/en/latest/' + refuri

                        reference = nodes.reference(
                            '', '', emphasis, classes=['viewcode-link'], reftitle='Source code', refuri=refuri)
                        desc_child += reference

                        # Style the links
                        self.has_link = True

                    else:
                        self.traverse(desc_child, name)

            else:
                self.traverse(child, owner)


def setup(app):
    if etree and path.isfile('xml/index.xml'):

        # The doctree-read event hasn't got the docname argument
        app.connect('doctree-resolved', doctree_resolved)

    else:
        if not etree:
            app.warn(
                '''Python lxml library not found
  The library is used to add links from an API description to the source
  code for that object.
  Depending on your system, try installing the python-lxml package.''')

        if not path.isfile('xml/index.xml'):
            app.warn(
                '''Doxygen files not found: xml/index.xml
  The files are used to add links from an API description to the source
  code for that object.
  Run "$ make doxygen" to generate these XML files.''')
