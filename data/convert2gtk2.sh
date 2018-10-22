#!/bin/bash

bindir=$HOME/Source/glade

for f_xml in `ls *.glade`
do
  echo "[${f_xml}]"
  ${bindir}/gladeconvert2 ${f_xml}
done

