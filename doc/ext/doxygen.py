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

import os, subprocess
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

if etree and path.isfile('source/doxygen_xml_api/index.xml'):

  # Doxygen files that have already been parsed
  cache = {}

  # Doxygen index
  index = etree.parse('source/doxygen_xml_api/index.xml')

def doctree_resolved(app, doctree, docname):
  """
  Add links from an API description to the code for that object.
  Doxygen knows where in the code objects are located.  Based on the
  sphinx.ext.viewcode and sphinx.ext.linkcode extensions.
  """

  traverse = doctree.traverse(addnodes.desc_signature)
  if traverse:
    for signode in traverse:

      # Get the name of the object
      for child in signode:
        if isinstance(child, addnodes.desc_name):
          name = child.astext()

          break

      # Lookup the object in the Doxygen index
      try:
        compound, = index.xpath('descendant::compound[descendant::name[text() = $name]][1]', name=name)

      except ValueError:
        continue

      filename = compound.get('refid') + '.xml'
      if filename not in cache:
        cache[filename] = etree.parse('source/doxygen_xml_api/' + filename)

      # An enumvalue has no location
      memberdef, = cache[filename].xpath('descendant::compounddef[compoundname[text() = $name]]', name=name) or cache[filename].xpath('descendant::memberdef[name[text() = $name] | enumvalue[name[text() = $name]]]', name=name)

      # Append the link after the object's signature.  Get the source
      # file and line number from Doxygen and use them to construct the
      # link.
      location = memberdef.find('location')

      emphasis = nodes.emphasis('', ' ' + path.basename(location.get('file')) + ' line ' + location.get('line'))

      # Use a relative link if the output is HTML, otherwise fall back
      # on an absolute link to Read the Docs
      refuri = 'api/' + compound.get('refid') + '_source.html#l' + location.get('line').rjust(5, '0')
      if app.builder.name == 'html':
        refuri = osutil.relative_uri(app.builder.get_target_uri(docname), refuri)

      else:
        refuri = 'http://docs.trafficserver.apache.org/en/latest/' + refuri

      reference = nodes.reference('', '', emphasis, classes=['viewcode-link'], reftitle='Code', refuri=refuri)
      signode += reference

    # Style the links
    raw = nodes.raw('', '<style> .rst-content dl dt .headerlink { display: inline-block } .rst-content dl dt .headerlink:after { visibility: hidden } .rst-content dl dt .viewcode-link { color: #2980b9; float: right; font-size: inherit; font-weight: normal } .rst-content dl dt:hover .headerlink:after { visibility: visible } </style>', format='html')
    doctree.insert(0, raw)

def setup(app):
  if etree and path.isfile('source/doxygen_xml_api/index.xml'):
    app.connect('doctree-resolved', doctree_resolved)

  else:
    if not etree:
      app.warn('''Python lxml library not found
  The library is used to add links from an API description to the code
  for that object.
  Depending on your system, try installing the python-lxml package.''')

    if not path.isfile('source/doxygen_xml_api/index.xml'):
      app.warn('''Doxygen files not found: source/doxygen_xml_api/index.xml
  The files are used to add links from an API description to the code
  for that object.
  Run "$ make doxygen" to generate these XML files.''')
