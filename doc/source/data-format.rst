.. _data-format-support-across-the-range-of-systems-1:

Data format support across the range of systems
===============================================

The Mark5 series of data recorders all have a ‘native’ data format. The
following table lists

*jive5ab* support and according to which reference the support for it
has been coded.

+-------+----------+---------------------------------------------------+
| Rec   | Format   | Comments                                          |
| order |          |                                                   |
+=======+==========+===================================================+
| M     | Mark4,   | Mark4 Memo 230 (rev 1.21)                         |
| ark5A | VLBA     |                                                   |
+-------+----------+---------------------------------------------------+
| M     | Mark5B   | Mark5 Memo 019 +0xABADDEED syncword +\ *jive5ab*  |
| ark5B |          | 4 Gbps extension                                  |
+-------+----------+---------------------------------------------------+
| M     | VDIF, if | http://www.vlbi.org/vdif/ (version 1.0)           |
| ark5C | any      |                                                   |
+-------+----------+---------------------------------------------------+

Traditionally, on the Mark5A and B the supported data format is strictly
tied to the firmware on the MIT Haystack I/O board installed. Mark5C
does not do formatting but doesn’t “support” all formats either. In the
official DIM implementation, it is impossible to configure a non-Mark5B
data format and in order to support Mark5B data on a Mark5A system an
updated I/O board firmware needs to be installed (Mark5A+). *jive5ab*
supports the Mark5B format up to 4 Gbps. Neither of the systems support
VDIF (bar 5C) natively.

*jive5ab* supports all of the listed formats on all of the systems on
the basis that for data transfers not involving the I/O board firmware,
it should not matter what the format is.

Then there is the fact that for some transfers and/or processing the
data format *must* be known whilst for others it can be totally opaque.
For the latter cases the special format ‘*none*’ has been introduced.

In general it is good practice to always configure the data format if it
is known which data format is to be expected, be it from the formatter,
the network or elsewhere.

Under the following circumstances the software can certainly not
successfully operate without knowing the exact details of the incoming
data format:

1. transfers which make use of corner turning; to be able to correctly
   decode the incoming data frames into the individual channels
2. printing the time stamps or verifying the incoming data format [8]_
3. if the remaining time on a recording medium is to be correctly
   computed

The specification of the data format allows *jive5ab* to size its
buffers and network packets (for UDP based transfers) appropriately and
set up correct time stamp decoding. On the other hand, for a blind
transfer of data from machine A to machine B [9]_ using a reliable
protocol (e.g. TCP) the buffer- and packet sizes are completely
irrelevant, and by extension the data format itself.

.. _configuring-data-formats-1:

Configuring data formats
------------------------

To allow configuring non-native formats the “mode=” command on the
various systems has been enriched. When configuring a data format the
MIT Haystack I/O board should not support the hardware is left untouched
and only *jive5ab*\ ’s internal idea of the current format is updated.

On Mark5B/DOM, Mark5C and generic systems both Mark5A and Mark5B
‘flavours’ of setting the data format and data rate are supported –
mode+play_rate (for MarkIV and VLBA data formats) as well as
mode+clock_set for Mark5B formats.

In Walter Brisken’s mark5access [10]_, currently part of the DiFX [11]_
source code tree, a canonical data format designation is used to
describe the important properties of the data. *jive5ab* supports this
format as well. With a single argument to the “mode=“ command, the data
format and track bit rate are set. This so-called ‘magic mode’
configuration persists until a hardware style “mode=“, “play_rate=“ or
“clock_set=“ command is issued to the system.

The canonical format is a single, case-insensitive, ASCII string,
formatted as follows:

where

::

   \<format\>[_\<frame size\>]-\<rate\>-\<channels\>-\<bits\>[/\<decimation\>]

<format> is the data format, with supported values:

-  Mark5B

-  VDIF

-  VDIFL (VDIF w/ legacy headers)

-  VLBA\ *n*\ \_\ *m* with *n,m* integers

-  MKIV\ *n*\ \_\ *m* ,,

   Note: the *n*\ \_\ *m* for VLBA and MKIV formats are the fan mode.
   1_4 means 1:4 fan-out and 2_1 means 2:1 fan-in. In practice, fan-in
   is not supported but the values of *n* and *m* are always used to
   compute the *actual* number of tracks used in the recording, in order
   to compute the track bit rate, which is necessary for correctly de-
   or encoding time stamps.

   \_<frame size> is the VDIF *payload* size (the Data Array size in the
   VDIF standard), thus excluding the header. This parameter **must** be
   supplied for VDIF and VDIFL and **may not** be supplied for any of
   the other formats.

   <rate> is the total data rate in Mbps, excluding framing

   <channels> is the number of baseband channels, typically 2n

   <bits> is the number of bits per sample

   <decimation> (optional) the decimation that was applied. Note that
   *jive5ab* does support specifying it but ignores its value
   completely.

Examples:

MKIV1_4-512-8-2 for 512 Mbps, 16 channels of 2 bit/sample MarkIV data

VDIFL_8192-4096-32-2 for 4 Gbps legacy VDIF with 32 channels of 2
bit/sample, sent in UDP packets of 8192 bytes data (excluding 16 bytes
of legacy VDIF header)
