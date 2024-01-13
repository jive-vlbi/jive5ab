.. _channel-dropping-and-corner-turning-1:

channel dropping and corner turning
===================================

*jive5ab* adds two generic data processing steps that can be inserted
into practically any data transfer: channel dropping and corner turning.

Both can operate under real-time constraints provided enough processing
resources (CPU, memory, network bandwidth) are available.

.. _channel-dropping-1:

Channel dropping
----------------

Channel dropping is the technique by which a VLBI data stream can be
compressed by selectively throwing away bits. E.g. all the bits carrying
the data from one (or more) channel(s) could be marked as unwanted. The
unwanted bits are replaced by bits from other channels such that the
size of the total data frame is reduced to a data rate fitting over the
network link. Channel dropping was developed for real-time e-VLBI
observations but can apply to other transfers as well, though better
ways exist (see corner turning).

Channel dropping technology allows *jive5ab* to send 31 out of the 32
channels of a 1024 Mbps observation (i.e. 990Mbps) over a 1Gbps ethernet
link (=1000Mbps) rather than having to step down to 512Mbps observing
mode and thus having close to 96% of the 1024 Mbps sensitivity rather
than 70%.

Channel dropping is indicated to the system using the “trackmask=“
command. Setting the track mask to a non-zero value indicates that
channel dropping is to be inserted in the data processing chain. The
argument is a 64-bit hexadecimal value. A ‘1’ in a bit position means
that that bit is to be kept. Bits having ‘0’ will be discarded. Disable
channel dropping by setting the track mask to 0 (zero).

As an example this feature can be used to make 1-bit data from 2-bit
data by throwing away alternate bits, effectively halving the data,
resulting in ‘only’ a 29% signal-to-noise degradation. For best results
the magnitude bits should be thrown away. Depending on your actual setup
this will either be:

::

   trackmask = 0x5555555555555555 or

   trackmask = 0xaaaaaaaaaaaaaaaa

Note the 64 bit hexadecimal values. If less than 64 bits of track mask
are provided, the lower bits of the 64 bits track mask will be set to
this value and the higher order bits will be zeroed.

If two systems are doing a compressed network transfer (e.g. in2net +
net2out) the track mask, data format and network parameters *must* be
configured on both systems and, for maximum success, be set to identical
values!

Those transfers honouring the channel dropping processing automatically
know wether to insert a compression or decompression step.

The “trackmask=” command always returns ‘1’ (“initiated but not
completed yet”) because in the background the (de)compression algorithms
are being computed and their C-code implementation generated, compiled
and dynamically loaded into the binary. The “trackmask?” query will
return ‘1’ as long as this process is running. When “trackmask?” returns
‘0’ it is safe to use the channel dropping transfer.

At this moment, a channel dropping processing step is automatically
inserted in certain transfers if the track mask not equal  0. To wit, a
compression step is inserted in the following transfers:

::

   disk2net, file2net, fill2net, disk2file, fill2file, in2net, in2mem, in2file, in2fork, in2memfork, mem2net, disk2net, file2net, fill2net

and a decompression step is automatically inserted in those:

::

   net2out, net2disk, net2fork, net2file, net2mem, net2check, net2sfxc, file2check,

.. _corner-turning-1:

Corner turning
--------------

Corner turning, sometimes called de-channelizing, is the processing by
which the samples from the VLBI data frames are re-arranged into
separate buffers such that each buffer contains a time series of one
individual channel.

This has to do with the traditional recording format of VLBI data:
typically the bits are arranged in sampling order. The samples for all
channels for a particular time, followed by the samples for the next
sample time. This implies that the next sample from a channel is found
at a different memory address. After corner turning the samples of a
single channel are found in a time series, adjacent to each other. This
is the preferred format for correlators.

After corner turning the channel buffers are translated into legacy VDIF
format. Each channel will be carried in its own VDIF data thread.
*jive5ab* allows each data thread to be sent to a different network
location or file on disk (including discarding it).

Corner turning/de-channelizing can be used for multiple purposes:
bringing down the real-time data rate, distribute one incoming data
stream over multiple destinations or convert any of the supported data
formats into VDIF format.

In order to make the most use of corner turning an intimate knowledge of
the actual layout of the bits in the data format is necessary. Please
refer to the table in Section 6 for the supported data format
definitions.

*jive5ab* contains hand-crafted assembly code (32- and 64-bit versions)
designed to run on Intel®/ AMD® processors supporting the SSE2 (or
higher) instruction set to fully exploit the CPU’s potential. SSE2
offers instructions which process 128 bits at a time.

Due to limitations in the CPU registers (mostly the amount of them, in
32-bit mode) the corner turning routines that are most efficient are
those who de-channelize a maximum of 8 channels of data.

Fortunately, looking at the data format memos mentioned in section 6,
typically, 16 and 32 channel observations are, as seen from a bit-stream
layout pattern, nothing more than an 8- channel layout repeated two or
four times. As such, a 16 channel de-channelizer routine could first
split the data into 2x 16-bit ‘chunks’, each containing 8 channels of
data. Each of the two halves can subsequently be efficiently
de-channelized using the 8-channel de-channelizer.

*jive5ab* supports daisy-chaining of splitting operations to allow the
user to build efficient de- channelizer / chunker chains.

There is also the possibility of not splitting at all and thus the data
frames will be just re-labelled with a legacy VDIF header. For none of
the data formats this yields *valid* VDIF data though!

Even for Mark5B data in standard astro mode conversion to VDIF is not
this simple because the order of the sign- and magnitude bits is
reversed between the two formats. Within *jive5ab*\ ’s built- in
de-channelizing routines exists an efficient sign-magnitude
swapper [12]_ which can be inserted into the de-channelizing chain.

The basic operation of the corner turner follows this pattern:

-  receive a data frame, tagged with stream label *X* from a previous
   step

-  break the frame up into a number of pieces, say *N*, each *1/N*\ ’th
   in size of the input frame and relabel them as stream *X*N* through
   *X*N+(N-1).* This is done to be able to keep track of all the
   individual channels as they flow through the chain. This step may do
   complex bit-level movements to group as many bits of each channel
   together as is possible before appending the samples into that
   channel’s specific buffer

-  accumulate these frames for a (configurable) amount of input frames

-  output the *N* new frames to the next step and start a new
   accumulation

   The first step in the chain just reads data frames from the selected
   medium and tags them with stream 0. These frames are handed off to
   the first step in the corner turning chain. The last but one step
   will divide the output up into VDIF frames of the configured size,
   set the VDIF *thread- id* to *X*. The final step will write these
   VDIF frames to either the network, a file or discard them altogether.

   The output VDIF streams will be labelled *0 .. n.* This is important
   because the corner turning routines do not know anything about the
   observation; they strictly deal with bits and bytes and leave all
   interpretation and logic to the user.

The spill2\*, spin2\*, spif2\*, spid2\*, splet2\* functions drive the
corner turning capabilities [13]_. The most important parameters to
these commands are the corner turning chain and what to do with the
output(s) of the corner turning engine.

In general the commands look like this:

::

   sp\*2\* = … : \<corner turning chain\> : \<outputX\> = \<dstY\> [ : \<outputZ\> = \<dstA\>]

Depending on the actual data source, the … may contain a connect or a
file name or other specifics and will not be discussed here.

In section **8.2.1** the <corner turning chain> will be described whilst
the <outputX>

= <dstY> will be covered in **8.2.2**.

.. _the-corner-turning-chain-1:

The <corner turning chain>
~~~~~~~~~~~~~~~~~~~~~~~~~~

With powerful things comes daunting syntax. This is a single field which
follows a certain grammar to allow one to configure the goriest details
of the corner turning engine. For clarity it is probably best to
represent the syntax in an Extended Backus-Naur Form (EBNF) [14]_ like
format. Double quotes indicate string literals, entries in curly braces
(“{ … }”) mean they may be repeated any number of times (including zero
times).

+------------+---------------------------------------------------------+
| chain      | = step , {“+” step}                                     |
+============+=========================================================+
| step       | = built_in_step \| dynamic_step , {“\*” , n_accumulate  |
|            | }                                                       |
+------------+---------------------------------------------------------+
| bui        | = “8bitx4” \| “16bitx2” \| “16bitx4” \| “32bitx2” \|    |
| lt_in_step | “swap_sign_mag” \| “2Ch2bit1to2” \| “4Ch2bit1to2” \|    |
|            | “8Ch2bit1to2_hv” \| “8Ch2bit_hv” \| “16Ch2bit1to2_hv”   |
+------------+---------------------------------------------------------+
| dy         | = n_inputbit , “>” , channels                           |
| namic_step |                                                         |
+------------+---------------------------------------------------------+
| channels   | = channel_def , { channel_def }                         |
+------------+---------------------------------------------------------+
| ch         | = bit_index , { “,” , bit_index }                       |
| annel_bits |                                                         |
+------------+---------------------------------------------------------+
| n_a        | = integer                                               |
| ccumulate, |                                                         |
| bit_index, |                                                         |
| n_inputbit |                                                         |
+------------+---------------------------------------------------------+

In words this sais, sort of:

A corner turning setup is formed by at least one corner turning step.
Multiple steps can be added together (“+”) to form a chain. Each step
can either be a built-in step or a dynamically-defined step. Optionally,
by “multiplying” a step by an integer it is possible to configure how
many input frames the current step must accumulate [15]_. A built-in
step is just named by a literal string (see above). The more advanced
dynamic-step has a sub- format. The code must know the input data word
width and a description of which bits to take and where to place them to
generate output channels. An ouput channel is defined using a
comma-separated list of source bit index/indices inside square brackets
(“[ … ]”). The amount of bracketed bit-lists is the number of channels
output by this dynamic splitter.

Note that the dynamic-step is not real-time guaranteed. What it does is,
based upon your bit- extraction specification, generate C-source code
for an extraction function. This code will be compiled and dynamically
loaded back into the program. Some optimization takes place but it
remains C-code in stead of Assembler/SSE2 instructions. This corner
turner’s throughput will vary upon data rate and system performance,
obviously.

A few examples of corner turning chains may be illustrative:

**Q:** Corner turn 1024 Mbps Mark4 data into individual channels.

**A:** This mode has 16 channels at 2 bit per sample: each channel
consisting of a sign and a magnitude bit, so 32 bit streams. Typically
these are recorded over 64 bit streams for 16 Mbps/ track, thus fanout
1:2. There exists a built-in corner turner for 8 channels, 2bit fanout 1
to 2 (“8Ch2bit1to2_hv” - see above). This one processes 32 bit streams
into 8 channels of output.

It turns out (see MarkIV Memo 230.1) that the 16 channels are recorded
as 2x an 8 channel setup. So for corner turning the 16 channel data, we
can first split the data frame in 2x 32 bits (also a built-in splitter!)
and then use the built-in 8 channel splitter. So we end up with a corner
turning chain like this:

::

   sp\*2\* = … : 32bitx2 + 8Ch2bit1to2_hv : ….

**Q:** Break 4 Gbps Mark5B data up into 4x 1 Gbps streams

**A:** This is an easy one! Mark5B format is very simple! The sign and
magnitude bits are situated next to each other in pairs. There is no
fan-in or fan-out. 4 Gbps Mark5B data is generated as 32 channels of
2bit sampled data, for a total of 64 bit streams.

We can just take 16 bits at a time because that groups together 8
channels of data. There exists a built-in 16bit splitter which we’ll
use.

There is one catch with this. The sign- and magnitude bits in Mark5B
data are reversed in order compared to what VDIF (which is the format
we’ll output) dictates. There is a highly efficient built-in
sign/magnitude swapper which does not split but just swaps all the bits
in its input.

We end up with the following corner turning chain:

::

   sp\*2\* = … : swap_sign_mag + 16bitx4 : … or 
   sp\*2\* = … : 16bitx4 + swap_sign_mag : …

because it does not matter when we swap the sign/magnitude bits
(logically). For the CPU the first might be more efficient than the
latter but that test is left as an excercise to the reader.

**Q:** I only want to extract some specific bits from my data

**A:** That will be the dynamic splitter then. Let’s assume 16 bit
streams are recorded. Each time sample is therefore 16 bits wide. Also
assume that it contains data from 8 channels, two bits for each channel.
So every 16 bits contain one 2-bit sample of each channel [16]_. For
example two things are wanted: make 1-bit data as well as extract only
two channels from the eight. Assume Mark5B layout: sample bits are
recorded pairwise next to each other. Then the following would extract
one bit each from channel 3 and 5 and produce two VDIF threads
(channels) as output:

::

   sp\*2\* = … : 16 \> [6] [10] : …

Now VDIF thread#0 contains bits 6, 22, 38, …; thread#1 will contain bits
10, 26, 42 etc (each 16th bit, starting from 6 and 10). It is also
possible to duplicate, reorder or rearrange bits at will:

::

   sp\*2\* = … : 16 \> [6,6] [1,0] [3,8] : …

.. _configuration-of-corner-turner-output-outputx-dsty-1:

Configuration of corner turner output: <outputX> = <dstY>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The output of the corner turner is *N* threads of legacy VDIF (since
*jive5ab* 3.0.0 normal VDIF, EDV version 0) numbered with thread-id (or
‘tag’) *0* .. *N-1*, where *N* is obviously defined by the actual corner
turning chain setup (see previous section, **8.2.1**).

Using the <outputX> = <dstY> syntax a (sub)set of VDIF threads can be
sent to destination Y. OutputX may be a single thread-id, a
comma-separated list of individual VDIF thread-ids or a range of
thread-ids. DstY depends on the actual transfer type - either a
HOST[@PORT] for the \*2net transfers or a filename for the \*2file
transfers.

Note that the [@PORT] part of a network destination is optional; in case
it is left out, the data port value set using net_protocol is
used [17]_. The ‘@‘ symbol was used to separate host and port because
the ‘:’ is already in use as the VSI/S parameter-separation character.

It is perfectly legal to over- or under specify the outputs; it is
absolutely not necessary to mention exactly all output thread-ids.

If the output section sees a VDIF thread-id that has no associated
destination, it will be silently ignored. This can usefully be exploited
for extracting only channels with useful data or limiting data rate.

If the outputs are over-specified - more VDIF thread-ids have
destinations than actual outputs generated by the corner turner, that’s
harmless. The non-produced VDIF thread- ids will not end up in the
output on the premise that, in fact, these thread-ids are not produced.

There may be any number of <outputX> = <dstY> configured.

.. _examples-1:

Examples:
~~~~~~~~~

Ensure all streams go to the same network destination:

::

   sp\*2net = … : 0-1024 = sfxc.jive.nl@46227

Write even thread-ids in one file, the odd ones in another:

::

   sp\*2file = … : 0,2,4,6,8 = /path/to/even.vdif

   : 1,3,5,7,9 = /path/to/odd.vdif

For your convenience, <dstY> may be a name ‘template’. This happens when
<dstY> contains the special string “{tag}”. Each occurrence of this this
pattern in <dstY> will be replaced by the number of the VDIF thread-id
that will be written to that <dstY>:

::

   sp\*2file = … : 0,2,4,6,8 = /path/to/thread{tag}.vdif

would open file “/path/to/thread0.vdif” and only store VDIF frames of
thread #0 in there. Likewise for threads 2, 4, 6 and 8.
