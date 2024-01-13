.. _vsi-s-command-query-and-response-syntax-1:

VSI-S Command, Query and Response Syntax
========================================

The following explanation of the VSI-S syntax may be useful in
understanding the structure of commands, queries and their respective
responses. This explanation has been lifted directly from the VSI-S
specification.

.. _command-syntax-1:

Command Syntax
--------------

Commands cause the system to take some action and are of the form

::

   \<keyword\> = \<field 1\> : \<field 2\> : …. ;

where <keyword> is a VSI-S command keyword. The number of fields may
either be fixed or indefinite; fields are separated by colons and
terminated with a semi-colon. A field may be of type decimal integer,
decimal real, integer hex, character, literal ASCII or a VSI-format time
code. White space between tokens in the command line is ignored, however
most character fields disallow embedded white space. For Field System
compatibility, field length is limited to 32 characters except for the
‘scan label’ (see Section 6), which is limited to 64 characters.

.. _command-response-syntax-1:

Command-Response Syntax
-----------------------

Each command elicits a response of the form

::

   !\<keyword\> = \< return code \> [:\<DTS-specific return\> :….] ;

where

<keyword> is the command keyword

<return code> is an ASCII integer as follows:

1. action successfully completed
2. action initiated or enabled, but not completed
3. command not implemented or not relevant to this DTS
4. syntax error
5. error encountered during attempt to execute
6. currently too busy to service request; try again later
7. inconsistent or conflicting request3
8. no such keyword
9. parameter error

<DTS-specific return> one or more optional fields specific to the
particular DTS, following the standard fields defined by VSI-S; fields
may be of any type, but should be informative about the details of the
action or error.

.. _query-and-query-response-syntax-1:

Query and Query-Response Syntax
-------------------------------

Queries return information about the system and are of the form

::

   \<keyword\> ? \<field 1\> : \<field 2\> : …. ; 

with a response of the form

::

   !\<keyword\> ? \<field 1(return code)\> : \<field 2\> : \<field 3\> : …: [\<DTS-specific return\>];

where

<return code> is an ASCII integer as follows:

1.  query successfully completed
2.  action initiated or enabled, but not completed
3.  query not implemented or not relevant to this DTS
4.  syntax error
5.  error encountered during attempt to execute query
6.  currently too busy to service request; try again later
7.  inconsistent or conflicting request [5]_
8.  no such keyword
9.  parameter error
10. indeterminate state

Note: *A ‘blank’ in a returned query field indicates the value of the
parameter is unknown.*

*A ‘?’ in a returned query field indicates that not only is the
parameter unknown, but that some sort of error condition likely exists.*
