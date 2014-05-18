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
from docutils.parsers.rst import Directive
from docutils.statemachine import ViewList
from os import path
from sphinx.domains import Domain
from sphinx.domains.c import CObject

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

def lookup_group(name):
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

      # Markup any incomplete paragraph before the element
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

      # Markup any incomplete paragraph before the element
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

      # Markup any incomplete paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the element
      sub_paragraphs = []
      text = markup_tree(sub_paragraphs, '', elem)

      # Markup any incomplete paragraph inside the element
      if text:
        sub_paragraphs.append(markup_text(text))

      # Indent paragraphs inside the element and append a note
      # admonition to our paragraphs list
      paragraphs.append('.. note::\n\n' + re.compile('^.', re.M).sub('   \g<0>', '\n\n'.join(sub_paragraphs)))

      # Gather text content after the element
      text = elem.tail or ''

    # Markup e.g. @deprecated with a generic admonition.  The Sphinx
    # deprecated directive demands a version number, but there's a
    # precedent for a "Deprecated" generic admonition in the
    # [reStructuredText reference].
    #
    #    [reStructuredText reference]
    #                  http://docutils.sourceforge.net/docs/ref/rst/directives.txt
    elif elem.tag == 'xrefsect':

      # Markup any incomplete paragraph before the element
      if text:
        paragraphs.append(markup_text(text))

      # Gather text content inside the xrefdescription child element
      sub_paragraphs = []
      text = markup_tree(sub_paragraphs, '', elem.find('xrefdescription'))

      # Markup any incomplete paragraph inside the xrefdescription
      if text:
        sub_paragraphs.append(markup_text(text))

      # Indent paragraphs inside the xrefdescription and append a
      # generic admonition to our paragraphs list
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
  inside Sphinx.
  """

  has_content = True

  def run(self):
    if etree and path.isfile('source/doxygen_xml_api/index.xml'):

      # The construct whose documentation strings to grab.  It can be
      # specified directly to this directive, e.g.
      #
      #    .. doxygen:description:: TSPluginRegistrationInfo
      #
      # or it can be inherited from a preceding directive, e.g.
      #
      #    .. doxygen:function:: TSPluginRegister
      #
      #       .. doxygen:description::
      if self.content:
        name, = self.content

        # Lookup the construct in the Doxygen index or, failing that,
        # iterate over all Doxygen files in search of a matching group
        try:
          compound, = index.xpath('descendant::compound[descendant::name[text() = $name]][1]', name=name)

        except ValueError:

          memberdef = lookup_group(name)
          if memberdef is None:
            return []

        else:

          filename = compound.get('refid') + '.xml'
          if filename not in cache:
            cache[filename] = etree.parse('source/doxygen_xml_api/' + filename)

          memberdef, = cache[filename].xpath('descendant::compounddef[compoundname[text() = $name]]', name=name) or cache[filename].xpath('descendant::memberdef[name[text() = $name]]', name=name)

      else:

        env = self.state.document.settings.env
        memberdef = env.doxygen_memberdef

      # Grab the documentation strings and translate them into
      # reStructuredText
      paragraphs = []

      directive = self.name.split(':', 1)[-1]
      if directive == 'description':

        # Doxygen sections have only a singular description.
        # Otherwise concatenate the brief and detailed descriptions.
        description = memberdef.find('description')
        if description is None:
          text = markup_tree(paragraphs, markup_tree(paragraphs, '', memberdef.find('briefdescription')), memberdef.find('detaileddescription'))

        else:
          text = markup_tree(paragraphs, '', description)

      else:

        # Brief or detailed description.  Doxygen sections have only a
        # singular description.
        elem, = memberdef.xpath(directive + ' | description')
        text = markup_tree(paragraphs, '', elem)

      # Markup any incomplete paragraph inside the element
      if text:
        paragraphs.append(markup_text(text))

      block = ViewList('\n\n'.join(paragraphs).split('\n'))

      node = self.state.parent
      self.state.nested_parse(block, 0, node)

    return []

def signature(memberdef):
  """
  Reconstruct an object's signature from a Doxygen memberdef.  The
  object might be a define, enum, function, struct, typedef, etc.
  """

  # A define has no argsstring
  argsstring = memberdef.xpath('string(argsstring)')
  if not argsstring:

    argsstring = ', '.join(param.xpath('string()') for param in memberdef.findall('param'))
    if argsstring:
      argsstring = '(' + argsstring + ')'

  # A define, enum, or struct has no definition
  return (memberdef.xpath('string(definition)') or memberdef.xpath('string(compoundname | name)')) + argsstring

class DoxygenObject(CObject):
  """
  Directive to use objects' signatures, or a group of objects'
  signatures, that have been extracted from source files by Doxygen.
  """

  if etree and path.isfile('source/doxygen_xml_api/index.xml'):
    def get_signatures(self):
      if self.objtype == 'group':
        name, = CObject.get_signatures(self)

        # Lookup the group in the Doxygen index or, failing that,
        # iterate over all Doxygen files in search of a matching group
        try:
          compound, = index.xpath('descendant::compound[attribute::kind = "group" and descendant::name[text() = $name]][1]', name=name)

        except ValueError:

          compounddef = lookup_group(name)
          if compounddef is None:
            return

        else:

          filename = compound.get('refid') + '.xml'
          if filename in cache:
            compounddef = cache[filename]

          else:
            compounddef = cache[filename] = etree.parse('source/doxygen_xml_api/' + filename)

        # DoxygenDescription can inherit the construct whose
        # documentation strings to grab from this directive
        self.env.doxygen_memberdef = compounddef

        # Return the signatures of the objects in the group
        for memberdef in compounddef.xpath('descendant::memberdef'):
          yield signature(memberdef)

      else:
        for name in CObject.get_signatures(self):

          # Lookup the object in the Doxygen index.  (Don't iterate over
          # all Doxygen files since this is definitely not a group.)
          compound, = index.xpath('descendant::compound[descendant::name[text() = $name]][1]', name=name)

          filename = compound.get('refid') + '.xml'
          if filename not in cache:
            cache[filename] = etree.parse('source/doxygen_xml_api/' + filename)

          # DoxygenDescription can inherit the construct whose
          # documentation strings to grab from this directive, e.g.
          #
          #    .. doxygen:function:: TSPluginRegister
          #
          #       .. doxygen:description::
          self.env.doxygen_memberdef, = memberdef, = cache[filename].xpath('descendant::compounddef[compoundname[text() = $name]]', name=name) or cache[filename].xpath('descendant::memberdef[name[text() = $name]]', name=name)

          # Return the signatures of the objects
          yield signature(memberdef)

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

def setup(app):
  if not etree:
    app.warn('\n'.join((
      'Python lxml library not found.',
      '  We can\'t grab documentation strings that have been extracted from',
      '  source files by Doxygen without it.',
      '  Depending on your system, try installing the python-lxml package.')))

  if not path.isfile('source/doxygen_xml_api/index.xml'):
    app.warn('\n'.join((
      'Doxygen files not found: source/doxygen_xml_api/index.xml',
      '  Run "$ make doxygen" to generate these XML files.')))

  app.add_domain(DoxygenDomain)
