#!/bin/sh -x
(echo ""; id; echo `date "+%y/%m/%d %H:%M:%s"`; echo $*;echo;cat) | mail -s "*5* has been verupdated"  verkouter@jive.nl
