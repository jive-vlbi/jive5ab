.. _jive5ab-program:

jive5ab program
===============

JOINT INSTITUTE FOR VLBI IN EUROPE
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*OUDE HOOGEVEENSEDIJK 4, DWINGELOO*

02 April 2020

TO: Distribution

FROM: Harro Verkouter

SUBJECT: *jive5ab* command set version 1.11 (*jive5ab* 2.4 and up)

*Telephone: +31 (0) 592 596500*

*Fax: +31 (0) 592 596539*

jive5ab program
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The commands detailed in this memo are implemented by a program named
*jive5ab* and control the DIM functionality of the three types of Mark5
DIM VLBI data acquisition systems designed by MIT Haystack [1]_: the
Mark5A, B and C.

The program offers a limited set of DIM services on Mark5B DOM systems
and hardware agnostic DIM services (including high-speed network packet
capture) on regular computer systems, including Mark6 and FlexBuff.

In addition to this, *jive5ab* offers non-DIM and (real-time) e-VLBI
functionality on all systems. *jive5ab* is fully compatible with 32- and
64-bit operating systems and can be compiled/run on 32- or 64-bit
POSIX/Intel® based systems, e.g. Linux (Debian, RedHat, Ubuntu) or Mac
OSX.

The *jive5ab* binary distribution will run on all systems [2]_

and auto-detects the hardware it is running on. Therefore this command
set document will encompass all of the Mark5A, B, C and generic command
sets. *jive5ab* will choose the appropriate one to support at runtime.

*jive5ab* implements the following DIM Command Set revisions: Mark5A
v2.73, Mark5B v1.12 and Mark5C v1.0.

Note: sometimes *jive5ab* had to deviate from or had to choose one of
multiple interpretations of commands found in the official Command Sets.
All known deviations with respect to MIT Haystack software will be
specifically documented <font color="red"> and clearly marked in red</font>.

.. role:: red
   
   and clearly marked in red.

Commands that are not part of the MIT Haystack Mark5A, 5B, or 5C command
sets are documented like normal commands; highlighting them in red or
any other visually distinctive way would be too distractive, given the
number of them. Some commands were introduced only in later versions
than 2.4.0. Where appropriate, these occurrences and their earliest
appearance will be indicated.

We would like to thank Alan R. Whitney and Chester Ruszczyk from MIT
Haystack observatory for making the original documentation source
documents for the Mark5 command sets available

- a real time- and life saver.  [3]_ :
http://www.haystack.mit.edu/tech/vlbi/mark5/index.html  [4]_: provided
OS and Conduant SDK versions are compatible with where the binary was
compiled on and linked with

.. [1]
   “Packet reordering metrics” - http://www.ietf.org/rfc/rfc4737.txt

.. [2]
   http://www.haystack.mit.edu/tech/vlbi/mark5/mark5_memos/100.pdf

