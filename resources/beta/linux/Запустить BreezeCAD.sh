#!/bin/sh
# BreezeCAD for beta testers: starts the program from the "program" folder.
here=$(dirname "$(readlink -f "$0")")
exec "$here/program/breezecad" "$@"
