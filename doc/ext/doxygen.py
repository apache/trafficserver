# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed
# with this work for additional information regarding copyright
# ownership.  The ASF licenses this file to you under the Apache
# License, Version 2.0 (the "License"); you may not use this file
# except in compliance with the License.  You may obtain a copy of the
# License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.  See the License for the specific language governing
# permissions and limitations under the License.

import codecs, os, re, subprocess
from docutils import nodes
from docutils.parsers.rst import Directive
from docutils.statemachine import ViewList
from docutils.transforms import Transform
from lxml import etree
from os import path
from sphinx.domains import Domain
from sphinx.domains.c import CObject

def lookup_group(cache, name):
  """
  Doxygen doesn't index group or section titles, so iterate over all
  Doxygen files in search of a matching group.  This is slow, so try
  to be clever about it.
  """

  # Start with files that have already been parsed.  Checking these
  # should be less expensive than parsing new files, and there's a
  # good chance that sections at least will belong to the same file as
  # other sections or functions in which we're interested.
  for doxygen in cache.itervalues():
    try:
      compounddef, = doxygen.xpath('(descendant::compounddef[title[text() = $name]] | descendant::sectiondef[header[text() = $name]])[1]', name=name)

    except ValueError:
      continue

    return compounddef

  # Iterate over the remaining files
  for filename in os.listdir('source/doxygen_xml_api'):
    if filename != 'index.xml' and filename not in cache:

      # Check first if it contains the string we're after.  This
      # should be less expensive than parsing every file.
      if name in codecs.open('source/doxygen_xml_api/' + filename, encoding='utf-8').read():

        doxygen = cache[filename] = etree.parse('source/doxygen_xml_api/' + filename)
        try:
          compounddef, = doxygen.xpath('(descendant::compounddef[title[text() = $name]] | descendant::sectiondef[header[text() = $name]])[1]', name=name)

        except ValueError:
          continue

        return compounddef

def markup_text(text):
  """Add markup to text with the help of regular expressions."""

  # Admonitions
  text = re.compile('(?<=\.)\s+(important|note):', re.I).sub('\n\n.. \\1::\n\n   ', text)
  text = re.compile('^(important|note):', re.I).sub('.. \\1::\n\n   ', text)

  # Constants, functions, and types.  Markup function calls (non-empty
  # argument lists) with the literal role, which Sphinx prescribes for
  # code.
  text = re.compile('\\b[A-Z]+(?:_[A-Z]+)+\\b').sub(':c:data:`\g<0>`', text)
  text = re.compile('\\b[A-Z](?:[A-Z]+[a-z]+|[a-z]+[A-Z]+)[A-Za-z]*\\b(\(.*?\))?').sub(lambda m: ':c:func:`' + m.group(0) + '`' if m.group(1) == '()' else '``' + m.group(0) + '``' if m.group(1) else ':c:type:`' + m.group(0) + '`', text)

  return text

def markup_tree(paragraphs, text, elem):
  """
  Walk a Doxygen tree gathering text content along the way, similarly
  to Element.itertext().  elem is the input and paragraphs and text
  are state.  Complete, marked up paragraphs are appended to the
  paragraphs list while text is the current incomplete paragraph (or
  empty string).  The paragraphs list is mutable while text is a
  string and therefore immutable.  Alternative implementations might
  use a class or a closure to keep track of state, instead of
  arguments and the return value.
  """

  # Add text content inside the element to the current incomplete
  # paragraph
  if elem.text:
    text += elem.text

  # Process child elements
  for elem in elem:

    # Split up paragraphs
    if elem.tag == 'para':

      # Markup the paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the element
      text = markup_tree(paragraphs, '', elem)

      # Markup any incomplete paragraph inside the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content after the element
      text = elem.tail or ''

    # Exclude parameter and return value descriptions
    elif elem.tag == 'parameterlist' or elem.tag == 'simplesect' and elem.get('kind') == 'return':

      # Gather text content after the element
      if elem.tail:
        text += elem.tail

    # Markup code blocks as literal blocks, as prescribed by Sphinx
    elif elem.tag == 'programlisting':

      # Markup the paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the element
      sub_paragraphs = []
      text = markup_tree(sub_paragraphs, '', elem)

      # Do *not* add markup inside a code block
      if text:
        sub_paragraphs.append(text)

      # Indent paragraphs inside the element and append a literal
      # block to our paragraphs list
      paragraphs.append('::\n\n' + re.compile('^.', re.M).sub('   \g<0>', '\n\n'.join(sub_paragraphs)))

      # Gather text content after the element
      text = elem.tail or ''

    # Markup notes
    elif elem.tag == 'simplesect' and elem.get('kind') == 'note':

      # Markup the paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the element
      sub_paragraphs = []
      text = markup_tree(sub_paragraphs, '', elem)

      # Markup any incomplete paragraph inside the element
      if text:
        sub_paragraphs.append(markup_text(text))

      # Indent paragraphs inside the element and append a note
      # directive to our paragraphs list
      paragraphs.append('.. note::\n\n' + re.compile('^.', re.M).sub('   \g<0>', '\n\n'.join(sub_paragraphs)))

      # Gather text content after the element
      text = elem.tail or ''

    # Markup e.g. @deprecated with a generic admonition directive.
    # The Sphinx deprecated directive demands a version number, but
    # there's a precedent for a "Deprecated" generic admonition in the
    # [reStructuredText reference].
    #
    #    [reStructuredText reference]
    #                  http://docutils.sourceforge.net/docs/ref/rst/directives.txt
    elif elem.tag == 'xrefsect':

      # Markup the paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the xrefdescription child element
      sub_paragraphs = []

      xrefdescription, = elem.xpath('xrefdescription')
      text = markup_tree(sub_paragraphs, '', xrefdescription)

      # Markup any incomplete paragraph inside the xrefdescription
      if text:
        sub_paragraphs.append(markup_text(text))

      # Indent paragraphs inside the xrefdescription and append a
      # generic admonition directive to our paragraphs list
      paragraphs.append('.. admonition:: ' + elem.xpath('string(xreftitle)') + '\n\n' + re.compile('^.', re.M).sub('   \g<0>', '\n\n'.join(sub_paragraphs)))

      # Gather text content after the element
      text = elem.tail or ''

    # Add all other text content to the current incomplete paragraph
    else:
      text = markup_tree(paragraphs, text, elem)

      # Add text content between child elements
      if elem.tail:
        text += elem.tail

  return text

class DoxygenDescription(Directive):
  """
  Directive to use descriptions extracted from source files.  Grab
  documentation strings that have been extracted from source files by
  Doxygen, translate them into reStructuredText, and then use them
  inside Sphinx documents.
  """

  has_content = True

  def run(self):
    if not path.isfile('source/doxygen_xml_api/index.xml'):
      return []

    env = self.state.document.settings.env

    # The construct whose documentation strings to grab.  It can be
    # specified directly to this directive, e.g.
    #
    #    .. doxygen:description:: TSPluginRegistrationInfo
    #
    # or it can be inherited from a parent directive, e.g.
    #
    # .. doxygen:function:: TSPluginRegister
    #
    #    .. doxygen:description::
    if self.content:
      name, = self.content

    else:
      name = env.doxygen_name

    # Lookup the construct in the Doxygen index or, failing that,
    # iterate over all Doxygen files in search of a matching group
    try:
      compound, = env.doxygenindex.xpath('descendant::compound[descendant::name[text() = $name]][1]', name=name)

    except ValueError:

      memberdef = lookup_group(env.doxygen, name)
      if memberdef is None:
        return []

    else:

      filename = compound.get('refid') + '.xml'
      if filename not in env.doxygen:
        env.doxygen[filename] = etree.parse('source/doxygen_xml_api/' + filename)

      memberdef, = env.doxygen[filename].xpath('descendant::compounddef[compoundname[text() = $name]] | descendant::memberdef[name[text() = $name]]', name=name)

    # Grab the documentation strings and translate them into
    # reStructuredText
    paragraphs = []

    directive = self.name.split(':', 1)[-1]
    if directive == 'description':

      # Doxygen sections have only a single description.  Otherwise
      # concatenate the brief and detailed descriptions.
      try:
        description, = memberdef.xpath('description')

      except ValueError:

        briefdescription, = memberdef.xpath('briefdescription')
        detaileddescription, = memberdef.xpath('detaileddescription')

        text = markup_tree(paragraphs, markup_tree(paragraphs, '', briefdescription), detaileddescription)

      else:
        text = markup_tree(paragraphs, '', description)

    else:

      # Brief or detailed description.  Doxygen sections have only a
      # single description.
      elem, = memberdef.xpath(directive + ' | description')
      text = markup_tree(paragraphs, '', elem)

    if text:
      paragraphs.append(markup_text(text))

    block = ViewList('\n\n'.join(paragraphs).split('\n'))

    node = self.state.parent
    self.state.nested_parse(block, 0, node)

    return []

class DoxygenObject(CObject):
  """
  Directive to use objects' signatures, or a group of objects'
  signatures, that have been extracted from source files by Doxygen.
  """

  def get_signatures(self):
    if not path.isfile('source/doxygen_xml_api/index.xml'):
      return

    if self.objtype == 'group':

      # DoxygenDescription can inherit the construct whose
      # documentation strings to grab from this directive
      self.env.doxygen_name, = name, = CObject.get_signatures(self)

      # Lookup the group in the Doxygen index or, failing that,
      # iterate over all Doxygen files in search of a matching group
      try:
        compound, = self.env.doxygenindex.xpath('descendant::compound[attribute::kind = "group" and descendant::name[text() = $name]][1]', name=name)

      except ValueError:

        compounddef = lookup_group(self.env.doxygen, name)
        if compounddef is None:
          return

      else:

        filename = compound.get('refid') + '.xml'
        if filename in self.env.doxygen:
          compounddef = self.env.doxygen[filename]

        else:
          compounddef = self.env.doxygen[filename] = etree.parse('source/doxygen_xml_api/' + filename)

      # Return the signatures of the objects in the group
      for memberdef in compounddef.xpath('descendant::memberdef'):
        yield memberdef.xpath('concat(definition, argsstring)')

    else:
      for name in CObject.get_signatures(self):

        # DoxygenDescription can inherit the construct whose
        # documentation strings to grab from this directive, e.g.
        #
        # .. doxygen:function:: TSPluginRegister
        #
        #    .. doxygen:description::
        self.env.doxygen_name = name

        # Lookup the object in the Doxygen index.  (Don't iterate over
        # all Doxygen files since this is definitely not a group.)
        compound, = self.env.doxygenindex.xpath('descendant::compound[descendant::name[text() = $name]][1]', name=name)

        filename = compound.get('refid') + '.xml'
        if filename not in self.env.doxygen:
          self.env.doxygen[filename] = etree.parse('source/doxygen_xml_api/' + filename)

        memberdef, = self.env.doxygen[filename].xpath('descendant::compounddef[compoundname[text() = $name]] | descendant::memberdef[name[text() = $name]]', name=name)

        # Return the signatures of the objects
        yield memberdef.xpath('concat(definition, argsstring)')

class DoxygenDomain(Domain):

  directives = {
    'briefdescription': DoxygenDescription,
    'detaileddescription': DoxygenDescription,
    'description': DoxygenDescription,
    'group': DoxygenObject,
    'function': DoxygenObject,
    'macro': DoxygenObject,
    'type': DoxygenObject }

  name = 'doxygen'

  def __init__(self, env):
    Domain.__init__(self, env)

    # Run Doxygen on Read the Docs to generate XML files
    if os.environ.get('READTHEDOCS'):
      subprocess.call('doxygen')

    if not path.isfile('source/doxygen_xml_api/index.xml'):
      return

    # Doxygen files that have already been parsed
    env.doxygen = {}

    # Doxygen index
    env.doxygenindex = etree.parse('source/doxygen_xml_api/index.xml')

def setup(app):
  app.add_domain(DoxygenDomain)
