#!/bin/sh
LOGFILE=/tmp/commitlog.$$
touch ${LOGFILE} 
DATE=`date "+%m/%d/%y [%H:%M:%S]"`

for file in $*
do
    echo "${DATE}: Post-commit notification for ${file}" >> ${LOGFILE}
done

# Eat extra input
cat >> ${LOGFILE}
