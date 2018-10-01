#!/bin/sh
set -e -x -u
exec openssl dgst -sign $KEY $@;
