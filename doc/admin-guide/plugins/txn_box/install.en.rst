.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: txnbox_common.defs

.. highlight:: text
.. default-domain:: txb

.. _installing:

***************
Installing
***************

|TxB| can be used as a global or remap plugin, or both.

Global
******

The |TxB| library, "txn_box.so", must be added to the "plugin.config" file to load as a global
plugin. It takes two types of arguments, a *key* or a *file*. Key arguments are preceeded by "--key"
and specify the key in the configuration file to use as the root key for the configuration. This is
by default "txn_box", which means the configuration file is expected to have an object at the top
level which contains the key "txn_box". Other keys at that level are ignored. The character "." can
be used to separate keys, each of which is expected to be a key in an object that is the value of a
previous object. As a special case, a key "." (a single period) indicates the entire file should be
considered as the YAML configuration.

The file argument is treated as a path to a file. These can be `file globs
<https://en.wikipedia.org/wiki/Glob_(programming)#Syntax>`__ in which case every file specified is
loaded if it has not already been loaded. |TxB| tracks which files have been loaded by absolute path
and if the same file is specified again it is not loaded. If the path is relative, it is treated as
relative to the |TS| configuratoin directory. The key used for each file is the most recent key
argument.

Although basic usage is simple - simply list the set of configurations to load, use of the arguments
can be a bit subtle. Here are some examples to clarify that usage.

Load the file "txn_box/global.yaml" from the |TS| configuration directory using the default root
key "txn_box" ::

   txn_box.so txn_box/global.yaml

Load the files "global.yaml", "security.yaml", and "ycpi.yaml" from the |TS| configuration directory
using the default root key "txn_box" ::

   txn_box.so txn_box/global.yaml txn_box/security.yaml txn_box/ycpi.yaml

Load the file "txn_box/global.yaml" from the |TS| configuration directory, using the root key
"global" as the root of the configuration. ::

   txn_box.so --key global txn_box/global.yaml

Load the file "txn_box/global.yaml" from the |TS| configuration directory, using the top level key
"txn_box". The ``--key`` argument is useless because no file arguments follow it. ::

   txn_box.so txn_box/global.yaml --key global

Load all the ".yaml" files in the "txn_box" directory under the |TS| configuration directory. The
default key "txn_box" is used as the root for each file. ::

   txn_box.so txn_box/*.yaml

As the previous, except use "txb" as the root key for all of the files. ::

   txn_box.so --key txb txn_box/*.yaml

Load all the ".yaml" files with a prefix of "ycpi" from the "/home/y/conf/ts" directory. This ignores
the |TS| configuration directory because it is an absolute path. ::

   txn_box.so /home/y/conf/ts/ycpi.*.yaml

All of these can be combined. If the goal is to load from "/home/y/conf/ts" and the "txn_box" directory
under the |TS| configuration directory, using the "ycpi" key for the former, and loading those first,
it would be ::

   txn_box.so --key ycpi /home/y/conf/ts/ycpi.*.yaml --key txn_box txn_box/*.yaml

The explicit reversion to the "txn_box" root key is needed because of the preceeding "--key ycpi". If
the loading order where the other way it would be ::

   txn_box.so txn_box/*.yaml --key global /home/y/conf/ts/ycpi.*.yaml

Suppose that was wanted, except the "/home/y/conf/ycpi.security.yaml" file should be
loaded first. Then ::

   txn_box.so --key "global" /home/y/conf/ts/ycpi.security.yaml \
              --key "txn_box" txn_box/*.yaml \
              --key "global" /home/y/conf/ts/ycpi.*.yaml

The file "ycpi.security.yaml" will only be loaded once. When it is specified again by the third
configuration argument, it will be noted as having already been loaded and not reloaded. Note: this
checking is by absolute path so it can be defeated by symlinks.

Remap
*****

|TxB| can also be used as a remap plugin. The transaction context is shared between remap and global
configurations so that, for instance, variables set in one can be accessed or changed in the
other. This allows capturing transaction state in an earlier hook using the global plugin for use in
the remap hook. Directives can be scheduled for later hooks using :drtv:`when` from a remap
configuration.

Configuration for remap posits the existence of a notional ``when: remap`` directive at the top
level. This is not something that can be explicitly done, it is entirely implicit in the
configuration being loaded from a remap rule. For this reason, in contrast to a global configuration
the YAML can be only a directive or list of directives without a base key or :drtv:`when`. This
applies even if the different root key is used.

The plugin parameters are the same as arguments to the global plugin, except the default key is
".", or the root of the YAML file.

In general elements in the remap hook are the same as in the user agent request hook.

Reloading
=========

Configurations can be reloaded without restarting |TS|. For remap configuration this is done by
reloading "remap.config" using the command ``traffic_ctl config reload`` after modifying the
"remap.config" file in the same way as other remap based plugins.

For global plugins, |TxB| provides a plugin message mechanism to reload the configuration. The
command for this ``traffic_ctl plugin msg txn_box.reload Delain``. Unfortunately, due to issues
with ``traffic_ctl`` the last argument is required, even though it is ignored. Even empty quotes
will cause a command error, some text is required.

When the global plugin configuration is reloaded, the original arguments specified in the
"plugin.config" file are used. The files are reloaded form disk which will bring in any changed
content. A key point is any file patterns are expanded during reload. This means different files may
be loaded even though the arguments remain the same. If the reload fails, this is logged and the
configuration is not changed.
