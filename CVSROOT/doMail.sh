#!/bin/sh 
(echo ""; echo -n `date "+[%y/%b/%d %H:%M:%S]"`; echo; echo; cat) | mail -s "*5* has been verupdated"  verkouter@jive.nl eldering@jive.nl kettenis@jive.nl
