.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. _developer-doc-adding-domains:

Creating New Domains
********************

In the event a new type of object or reference needs to be documented, and none
of the existing markup options or domains are appropriate, it is possible to
extend |RST| and Sphinx by adding custom domains.

Each domain may be designed to accept any number of required and optional
arguments, as well as any collection of domain options, and each option may be
designed to support arbitrary values, restricted (enumerated) values, or to
simply act as flags.

All custom domain definitions should be located in ``doc/ext/traffic-server.py``
and consist of, at a bare minimum, a domain class definition and a domain
reference class definition. Sphinx domains are implemented in Python.

For this section, we will use the contrived example of creating a domain which
permits us to define and reference a set of variables which are constrained by
the following characteristics:

#. Each variable in the domain must be one of known list of data types, which
   we will limit here to the possibilities of :literal:`integeer`,
   :literal:`float`, :literal:`string`.

#. Where the data type is not specified, we can assume it is :literal:`string`.

#. Variables which are numeric in their type may have a range of permissible
   values.

#. Variables in the domain may still be present and supported in the system, but
   are planned to be removed in some future release.

#. Every variable is associated with a single URI protocol, though there is no
   validation performed on the value used to represent the protocol name.

As stated, this example is fairly contrived and would not match any particularly
likely real-world needs, but it will allow us to demonstrate the full extent of
custom domain definition without needless complexity, reader's suspension of
disbelief permitting.

For this chapter's purpose, we will call this domain simply *Variables*, and we
will construct classes which allow us to document variables thusly::

    .. ts:variable:: http_enabled http integer
       :deprecated:

       Enables (any postive, non-zero value) or disables (any zero or negative
       value) processing of HTTP requests.

And referencing of those variables defined with this domain via::

    :ts:variable:`http_enabled`

Defining the Domain
===================

Each domain is defined by a class which inherits from ``std.Target``. Several
class attributes are expected, which determine how domain object definitions are
processed. |TS| convention is to name each domain's class in camel case,
beginning with :literal:`TS` to prevent any class name collisions with builtin
Sphinx classes.

.. code::

    class TSVariable(std.Target):

We have named the domain's defining class as *TSVariable* and inherited from the
:literal:`std.Target` class. Given the earlier stated requirements, we need a
domain which supports at least two required attributes (a name, of course, and
a URI protocol with which it is associated) and a commonly defined, though
optional, third attribute (a data type). We'll deal with the value ranges and
deprecation status later.

.. code::

    class TSVariable(std.Target):
        required_arguments = 2
        optional_arguments = 1
        final_argument_whitespace = False
        has_content = True

We've now specified the appropriate number of required and optional arguments,
though not what each one happens to be or in what order the required arguments
need be written. Additionally, we've declared that definitions using this
domain do not permit whitespace in the final argument, but definitions can have
a block of text content which follows them and should be associated with the
item being defined.

.. note::

   Permitting whitespace in the final argument causes the final value of a valid
   definition to *slurp* the remaining content of the definition. Normally, each
   argument is separated by whitespace, thus ``foo bar baz`` would only be a
   valid definition if the domain's required and optional argument counts added
   up to exactly three. If the domain defined only two arguments as expected,
   but sets ``final_argument_whitespace`` to *True*, then the definition would
   be valid and the second argument in this case would be ``bar baz``.

Our requirements also state support for optional value ranges, and a flag to
indicate whether the variable is being deprecated. These can easily be
supported through the ``option_spec``, which allows for options to be tagged
on to a domain item, on the lines immediately following its definition.

.. code::

    class TSVariable(std.Target):
        ...
        option_spec = {
            'deprecated' : rst.directives.flag,
            'range' : rst.directives.unchanged
        }

For our example, ``deprecated`` is simply a boolean flag, and ``range`` will be
an arbitrary string on which we will perform no particular transformation or
validation (good behavior will be left up to those documenting their variables
with this domain). The ``rst.directives`` module may be consulted for a wider
range of predefined option types, including the ability to define your own
types which can perform any complexity of validation you may desire to
implement.

It would be good form to also include a docstring for the class explaining the
expected arguments in brief. With that included, our class now looks like:

.. code::

    class TSVariable(std.Target):
        """
        Description of a Traffic Server protocol variable.

        Required arguments, in order, are:

            URI Protocol
            Variable name

        Optional argument is the data type of the variable, with "string" the
        the default. Possible values are: "string", "integer", and "float".

        Options supported are:

            :deprecated: - A simple flag option indicating whether the variable
            is slated for removal in future releases.

            :range: - A string describing the permissible range of values the
            variable may contain.
        """

        option_spec = {
            'deprecated' : rst.directives.flag,
            'range' : rst.directives.unchanged
        }

        required_arguments = 2
        optional_arguments = 1
        final_argument_whitespace = False
        has_content = True

Every domain class must also provide a ``run`` method, which is called every
time an item definition using the domain is encountered. This method is where
all argument and option validations are performed, and where transformation of
the definition into the documentation's rendered output occurs.

The core responsibilities of the ``run`` method in a domain class are to
populate the domain's data dictionary, for use by references, as well as to
transform the item's definition into a document structure suitable for
rendering. The default title to be used for references will be constructed in
this method, and all arguments and options will be processed.

Our variables domain might have the following ``run`` method:

.. code::

    def run(self):
        var_name, var_proto = self.arguments[0:2]
        var_type = 'string'

        if (len(self.arguments) > 2):
            var_type = self.arguments[2]

        # Create a documentation node to use as the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'variable'

        # Add the signature child node for permalinks.
        title = sphinx.addnodes.desc_signature(var_name, '')
        title['ids'].append(nodes.make_id('variable-'+var_name))
        title['names'].append(var_name)
        title['first'] = False
        title['objtype'] = node['objtype']
        self.add_name(title)
        title.set_class('ts-variable-title')

        title += sphinx.addnodes.desc_name(var_name, var_name)
        node.append(title)

        env.domaindata['ts']['variable'][var_name] = env.docname

        # Create table detailing all provided domain options
        fl = nodes.field_list()

        if ('deprecated' in self.options):
            fl.append(self.make_field('Deprecated', 'Yes'))

        if ('range' in self.options):
            fl.append(self.make_field('Value range:', self.options['range']))

        # Parse any associated block content for the item's description
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so Sphinx will list this variable and its
        # references in the index section.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(
            ('single', _('%s') % var_name, nodes.make_id(var_name), '')
        )

        return [ indexnode, node, fl, nn ]

Defining the Domain Reference
=============================

Domain reference definitions are quite simple in comparison to the full domain
definition. As with the domain itself, they are defined by a single class, but
inherit from ``XRefRole`` instead. There are no attributes necessary, and only
a single method, ``process_link`` need be defined.

For our variables domain references, the class definition is a very short one.
|TS| convention is to name the reference class the same as the domain class, but
with :literal:`Ref` appended to the name. Thus, the domain class ``TSVariable``
is accompanied by a ``TSVariableRef`` reference class.

.. code::

    class TSVariableRef(XRefRole):
        def process_link(self, env, ref_node, explicit_title_p, title, target):
            return title, target

The ``process_link`` method will receive several arguments, as described below,
and should return two values: a string containing the title of the reference,
and a hyperlink target to be used for the rendered documentation.

The ``process_link`` method receives the following arguments:

``self``
    The reference instance object, as per Python method conventions.

``env``
    A dictionary object containing the environment of the documentation
    processor in its state at the time of the reference encounter.

``ref_node``
    The node object of the reference as encountered in the documentation source.

``explicit_title_p``
    Contains the text content of the reference's explicit title overriding, if
    present in the reference markup.

``title``
    The processed form of the reference title, which may be the result of domain
    class transformations or an overriding of the reference title within the
    reference itself.

``target``
    The computed target of the reference, suitable for use by Sphinx to
    construct hyperlinks to the location of the item's definition, wherever it
    may reside in the final rendered form of the documentation.

In our reference class, we have simply returned the processed title (allowing
the documentation to override the variable's name if desired, or defaulting to
the domain class's representation of the variable name in all other cases) and
the parser's computed target.

It is recommended to leave the ``target`` untouched, however you may choose to
perform any transformations you wish on the value of the ``title``, bearing in
mind that whatever string is returned will appear verbatim in the rendered
documentation everywhere references for this domain are used.

Exporting the Domain
====================

With both the domain itself and references to it now defined, the final step is
to register those classes as domain and reference handlers in a namespace. This
is done for |TS| (in its ``:ts:`` namespace) quite easily by modifying the
``TrafficServerDomain`` class, also located in ``doc/ext/traffic-server.py``.

The following dictionaries defined by that class should be updated to include
the new domain and reference. In each case, the key used when adding to the
dictionary should be the string you wish to use in documentation markup for
your new domain. In our example's case, we will choose ``variable`` since it
aligns with the Python classes we've created above, and their contrived purpose.

object_types
    Used to define the actual markup string

directives
    Defines which class is used to implement a given domain.

roles
    Defines the class used to implement references to a domain.

initial_data
    Used to initialized the dictionary which tracks all encountered instances of
    each domain. This should always be set to an empty dictionary for each
    domain.

dangling_warnings
    May be used to provide a default warning if a reference is attempted to a
    non-existent item for a domain.

