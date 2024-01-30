#!/bin/bash

if [ -z "$3" ] ; then
  echo "Usage: $0 major minor point"
  exit 1
fi

# Header
sed -i doc/conf.py --expr "s/^release = .*\$/release = \"$1.$2.$3\"/"
sed -i doc/conf.py --expr "s/^version = .*\$/version = \"$1.$2\"/"
sed -i doc/Doxyfile --expr "s/\(PROJECT_NUMBER *= *\).*\$/\\1\"$1.$2.$3\"/"

sed -i plugin/txn_box.part --expr "s/PartVersion(\"[0-9.]*\")/PartVersion(\"$1.$2.$3\")/"
