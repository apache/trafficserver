#!/bin/sh
set -e -x -u
exec openssl dgst -verify $KEY -signature $SIG $@;
