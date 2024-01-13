
.. _scan-names-scan-labels-and-linux-filenames-1:

Scan names, Scan Labels and Linux filenames
===========================================

Mark5 defines a ‘scan’ as a continuously recorded set of data. Each scan
is identified by a scan name, experiment name and station code, which
are normally derived from the information in the `associated VEX file
used in the scheduling of the experiment (see
http://lupus.gsfc.nasa.gov/vex/ vex.html). An attempt to record a scan
with a duplicate scan name on the same disk module
will <http://lupus.gsfc.nasa.gov/vex/vex.html>`__ cause a trailing
alphabetical character (‘a-z’, then ‘A-Z’) to be automatically appended
to the scan name. If there are more than 52 scans with same
user-specified name, the suffix sequence will repeat. Information about
the experiment name, station code, bit-stream mask, and sample rate are
stored in the associated directory entry.

A scan label is defined as the character string

::

   \<exp name\>_\<stn code\>_\<scan name\>

where

<exp name> is the name of the experiment (e.g. ‘grf103’); maximum 8
characters, but by convention corresponds to a standardized 6-character
experiment name. If null, will be replaced with ‘EXP’.

<stn code> is the station code (e.g. ‘ef’); maximum 8 characters, but by
convention corresponds to standardized 2-character codes. If null, will
be replaced with ‘STN’

<scan name> is the identifier for the scan (e.g. ‘254-1056’), usually
assigned by the observation-scheduling program; max 31 characters,
though may be augmented to 32 characters by automatically generated
duplicate-breaking suffix character.

Maximum scan-label length, including embedded underscores and possible
scan-name suffix character, is 50 characters. <experiment name>,
<station code> and <scan name> may contain only standard alpha-numeric
characters, except ‘+’, ‘-‘ and ‘.’ characters may also be used in

<scan name>. All fields are case sensitive. No white space is allowed in
any of these subfields. Lower-case characters in all subfields are
preferred. An example scan label is:

::

   grf103_ef_scan001

When a Mark5B scan (or portion of a scan) is copied to a Linux file with
*disk2file*, a Linux filename compatible with the internationally agreed
e-VLBI filenaming convention (reference
http://www.haystack.edu/tech/vlbi/evlbi/memo.html memo #49) is assigned
as

::

   ‘\<scan label\>_bm=\<bit-stream mask\>.m5b’ (example: ‘grf103_ef_scan001_bm=0x0000ffff.m5b’)

Linux files to be transferred to a Mark5B disk via the ’\ *file2disk’*
should have filenames corresponding to the standardized format described
above so that the associated Mark5B directory entries can be properly
filled.

Note: The <scan name> is equivalent to what is called <scan_ID> in VEX
files, except the set of legal characters in <scan name> is more
restricted and must be observed.
