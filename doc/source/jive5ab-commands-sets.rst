jive5ab command set
########################

1pps_source – Select source of 1pps synchronization tick
========================================================

Command syntax: 1pps_source = <1pps source> ; Command response: !
1pps_source = <return code> ;

Query syntax: 1pps_source? ;

Query response: ! 1pps_source ? <return code> : <1pps source> ;

Purpose: Select source of 1pps which will be used to synchronize the
Mark 5B. Settable parameters:

+---+---+----+---+-------------------------------------------------------+
| * | * | *  | * | **Comments**                                          |
| * | * | *A | * |                                                       |
| P | T | ll | D |                                                       |
| a | y | ow | e |                                                       |
| r | p | ed | f |                                                       |
| a | e | va | a |                                                       |
| m | * | lu | u |                                                       |
| e | * | es | l |                                                       |
| t |   | ** | t |                                                       |
| e |   |    | * |                                                       |
| r |   |    | * |                                                       |
| * |   |    |   |                                                       |
| * |   |    |   |                                                       |
+===+===+====+===+=======================================================+
| < | c | al | v | ‘altA’ = AltA1PPS (LVDS signal on 14-pin VSI-H        |
| 1 | h | tA | s | connector; rising edge) ‘altB’ = AltB1PPS (LVTTL      |
| p | a | \| | i | signal on SMA connector on back panel; rising edge)   |
| p | r | al |   | ‘vsi’ = VSI (LVDS signal on 80-pin VSI-H connector;   |
| s |   | tB |   | rising edge)                                          |
| _ |   | \| |   |                                                       |
| s |   | v  |   |                                                       |
| o |   | si |   |                                                       |
| u |   |    |   |                                                       |
| r |   |    |   |                                                       |
| c |   |    |   |                                                       |
| e |   |    |   |                                                       |
| > |   |    |   |                                                       |
+---+---+----+---+-------------------------------------------------------+

Monitor-only parameters:

============== ======== =================== ============
**Parameter**  **Type** **Values**          **Comments**
============== ======== =================== ============
<1pps_source > char     altA \| altB \| vsi 
============== ======== =================== ============

Notes:

1. –

ack – Set UDP backtraffic acknowledge period (jive5ab > 2.7.3, jive5ab-2.7.1-ack-udfix)
=======================================================================================

Command syntax: ack = <ACK period> ; Command response: !ack = <return
code> ;

Query syntax: ack? ;

Query response: !ack ? <return code> : <ACK period> ;

Purpose: Set acknowledgement period for UDP readers; every <ACK
period>’th received packet will generate back traffic

Settable parameters:

+------+---+--------+-----+------------------------------------------+
| *    | * | **A    | **  | **Comments**                             |
| *Par | * | llowed | Def |                                          |
| amet | T | va     | aul |                                          |
| er** | y | lues** | t** |                                          |
|      | p |        |     |                                          |
|      | e |        |     |                                          |
|      | * |        |     |                                          |
|      | * |        |     |                                          |
+======+===+========+=====+==========================================+
| <ACK | i | See    | 10  | Acknowledgement period in                |
| per  | n | Note   |     | number-of-received-packets-between       |
| iod> | t | 2.     |     | acknowledgements. See Note 1.            |
+------+---+--------+-----+------------------------------------------+

Monitor-only parameters:

============= ======== ==============================
**Parameter** **Type** **Comments**
============= ======== ==============================
<ACK period>  int      Current acknowledgement period
============= ======== ==============================

Notes:

1. Network equipment such as routers and switches have no truck with
   uni-directional network traffic: they learn the network port a
   destination is reachable on dynamically, in order to only forward
   traffic to that physical network port. This learning is done by
   broadcasting traffic to all ports in case the addressee is unknown
   and wait for any form of traffic originating from it, back to the
   sender. With the TCP/IP and UDT protocols this is already guaranteed.
   In order to prevent the unidirectional UDP based e-VLBI traffic
   ‘blowing up’ the network - the equipment continues to broadcast until
   learnt – backtraffic has to be generated at least once: as soon as
   the UDP reader receives a packet for the first time. This is,
   however, not enough. Most equipment remembers the learnt
   addressee/physical port combination for a few minutes at best. Some
   equipment even flushes their entire memory in case one of its ports
   is reset, requiring the device to re-learn all mappings. To this
   effect *jive5ab* > 2.7.3 features the “ack” command/query. *jive5ab*
   will generate backtraffic every

   <ACK period>’th received packet if any of the udp-based protocols
   (bar UDT) is used (see “net_protocol”). The compiled-in default is
   every 10th packet. This value was experimentally determined in May
   2016 to be both prevent significant packet loss whilst not causing
   too much effect on return bandwidth as well as not causing too much
   extra overhead at the receiving end.

2. This feature was backported into the 2.7.1 stable series for internal
   use at JIVE. The public release of the “user directory fix” bug fix
   release of jive5ab-2.7.1-ack-udfix therefore includes this feature as
   well.

3. The ACK period is a signed integer. The result of setting a value is
   as follows:

======= ======================================================
ack < 0 Acknowledge every received packet; 1-for-1 backtraffic
======= ======================================================
ack = 0 Reset the ‘ACK period’ to the compiled-in default
ack > 0 Generate backtraffic every ack received packets
======= ======================================================

bank_info – Get bank information (query only)
=============================================

Query syntax: bank_info? ;

Query response: !bank_info ? <return code> : <selected bank> : <#bytes
remaining> : <other bank> : <#bytes remaining> ;

Purpose: Returns information on both selected and unselected banks,
including remaining space available.

Monitor-only parameters:

+-------+---+----+------------------------------------------------------+
| **P   | * | ** | **Comments**                                         |
| arame | * | Va |                                                      |
| ter** | T | lu |                                                      |
|       | y | es |                                                      |
|       | p | ** |                                                      |
|       | e |    |                                                      |
|       | * |    |                                                      |
|       | * |    |                                                      |
+=======+===+====+======================================================+
| <sel  | c | A  | Currently selected bank, or ‘nb’ if operating in     |
| ected | h | \| | non-bank mode. See Note 3. ’-‘ if disk module is     |
| bank> | a | B  | faulty; see Note 1                                   |
|       | r | \| |                                                      |
|       |   | nb |                                                      |
+-------+---+----+------------------------------------------------------+
| <#    | i |    | Approximate #bytes remaining to be recorded on       |
| bytes | n |    | active module or on a non-bank-mode module pair. =0  |
| remai | t |    | if no module selected or faulty module.              |
| ning> |   |    |                                                      |
+-------+---+----+------------------------------------------------------+
| <     | c |    | Bank mode: Unselected bank, if module in unselected  |
| other | h |    | bank is mounted and ready; if no module or faulty    |
| bank> | a |    | module, ‘-‘ is returned. ‘nb’ mode: returned null    |
|       | r |    |                                                      |
+-------+---+----+------------------------------------------------------+
| <#    | i |    | Bank mode: Approximate #bytes remaining to be        |
| bytes | n |    | recorded on inactive module; =0 if no module active, |
| remai | t |    | faulty module. ‘nb’ mode: returned null              |
| ning> |   |    |                                                      |
+-------+---+----+------------------------------------------------------+

Notes:

1. If no modules are inserted, an error code 6 is returned.
2. The estimate of <#bytes remaining> is made without taking into
   account any slow or bad disks. When recording is not in progress, an
   ‘rtime?’ query gives a more precise estimate of the available space
   for the selected bank.
3. \ *jive5ab* < 2.8 returns error code 6 “not in bank mode” if this
   query is executed whilst running in non-bank mode. *jive5ab* >= 2.8
   implements the bank_info? query according to this specification.

bank_set – Select active bank for recording or readback
=======================================================

Command syntax: bank_set = <bank> ;

Command response: ! bank_set = <return code> ;

Query syntax: bank_set? ;

Query response: ! bank_set ? <return code> : <active bank> : <active
VSN> : <inactive bank> : <inactive VSN> ;

Purpose: When in bank mode, the selected bank becomes the ‘active’ bank
for all Mark 5 activities.

Settable parameters:

+---+---+-----+---+----------------------------------------------------+
| * | * | **A | * | **Comments**                                       |
| * | * | llo | * |                                                    |
| P | T | wed | D |                                                    |
| a | y | va  | e |                                                    |
| r | p | lue | f |                                                    |
| a | e | s** | a |                                                    |
| m | * |     | u |                                                    |
| e | * |     | l |                                                    |
| t |   |     | t |                                                    |
| e |   |     | * |                                                    |
| r |   |     | * |                                                    |
| * |   |     |   |                                                    |
| * |   |     |   |                                                    |
+===+===+=====+===+====================================================+
| < | c | A   | A | ‘inc’ increments to next bank in cyclical fashion  |
| b | h | \|  |   | around available bank; see Note 1. ‘bank_set       |
| a | a | B   |   | command will generate an error when operating in   |
| n | r | \|  |   | ‘nb’ mode; see Note 2.                             |
| k |   | inc |   |                                                    |
| > |   |     |   |                                                    |
+---+---+-----+---+----------------------------------------------------+

Monitor-only parameters:

+-------+---+-----+---------------------------------------------------+
| **P   | * | *   | **Comments**                                      |
| arame | * | *Va |                                                   |
| ter** | T | lue |                                                   |
|       | y | s** |                                                   |
|       | p |     |                                                   |
|       | e |     |                                                   |
|       | * |     |                                                   |
|       | * |     |                                                   |
+=======+===+=====+===================================================+
| <a    | c | A   | ‘A’ or ‘B’ if there is an active bank; ‘-‘ if no  |
| ctive | h | \|  | active bank; ‘nb’ if operating in ‘non-bank       |
| bank> | a | B   | mode’. See Note 5!                                |
|       | r | \|  |                                                   |
|       |   | nb  |                                                   |
+-------+---+-----+---------------------------------------------------+
| <a    | c |     | VSN of active module, if any; if operating in     |
| ctive | h |     | ‘nb’ mode: VSN of module \ **that should be**\    |
| VSN>  | a |     | in Bank A.                                        |
|       | r |     |                                                   |
+-------+---+-----+---------------------------------------------------+
| <ina  | c | B   | ‘B’ or ‘A’ if inactive bank is ready; ‘-‘ if      |
| ctive | h | \|  | module not ready; ‘nb’ if operating in ‘non-bank  |
| bank> | a | A   | mode’                                             |
|       | r | \|  |                                                   |
|       |   | -   |                                                   |
+-------+---+-----+---------------------------------------------------+
| <ina  | c |     | VSN of inactive module, if any; if operating in   |
| ctive | h |     | ‘nb’ mode: VSN of module \ **that should be**\    |
| VSN>  | a |     | in Bank B                                         |
|       | r |     |                                                   |
+-------+---+-----+---------------------------------------------------+

Notes:

1. If the requested bank is not the bank already selected, a completion
   code of ‘1’ (delayed completion) is returned. Bank switching takes a
   variable amount of time up to about 3 seconds. While bank switching
   is in progress, many commands and queries will return a code of 5
   (busy, try later) or 6 (conflicting request; in effect, neither bank
   is selected during this transition). If an attempt to switch the bank
   fails (e.g. if there is no ‘ready’ disk module in the other bank), a
   ‘status?’ or “error?’ query will return error 1006, “Bank change
   failed.” A ‘bank_set?’ query will indicate whether the bank has
   changed. Switching banks can also generate other errors if there are
   problems with the target bank. If no active banks are present in the
   system a completion code of ‘6’ (inconsistent or conflicting request)
   will be returned.
2. When operating in ‘nb’ (i.e. non-bank) mode, a ‘bank_set’ command is
   illegal and will generate an error; a ‘bank_set?’ query is allowed to
   gather information. [STRIKEOUT:The system will switch automatically
   to ‘nb’ mode if (and only if) both ‘nb’ modules are properly mounted
   and ready.]
3. The ‘bank_set’ command may not be issued during recording or readback
   (will return an error).
4. When operating in bank mode, a ‘bank_set?’ query always returns the
   currently active module.
5. \ *jive5ab* >= 2.8 properly implements this query in non-bank mode.
   Versions prior to that will return error code 6, “not in bank mode”

[STRIKEOUT:# bank_switch – Enable/disable automatic bank switching
(NYI)]

[STRIKEOUT:Command syntax: bank_switch = <auto-switch on/off> ;]

[STRIKEOUT:Command response: !bank_switch = <return code> ;]

[STRIKEOUT:Query syntax: bank_switch? ;]

[STRIKEOUT:Query response: !bank_switch ? <return code> : <auto-switch
on/off> ;]

~~Purpose: Enable/disable automatic bank-switching for both record and
readback. ~~ [STRIKEOUT:Settable parameters:]

+----------+----+---------+-----+-------------------------------------+
| **Par    | ** | **      | **  | **Comments**                        |
| ameter** | Ty | Allowed | Def |                                     |
|          | pe | v       | aul |                                     |
|          | ** | alues** | t** |                                     |
+==========+====+=========+=====+=====================================+
| <aut     | ch | off \|  | off | If ‘on’, enables automatic          |
| o-switch | ar | on      |     | bank-switching; always ‘off’ in     |
| mode>    |    |         |     | non-bank mode..                     |
+----------+----+---------+-----+-------------------------------------+

[STRIKEOUT:Notes:]

[STRIKEOUT:1. ‘bank_switch’ command will return error if system is
operating in ‘nb’ (non-bank) mode.] [STRIKEOUT:2. When automatic
bank-switching is enabled, the following actions are triggered when
recording hits end-of-media (say, on Bank A):] [STRIKEOUT:1. Bank A
stops recording and updates its directory.] [STRIKEOUT:2. Bank B is
selected as the ‘active’ bank (assumes Bank B is ready).] [STRIKEOUT:3.
Recording starts on Bank B and continues until a ‘record=off’ command is
issued.] [STRIKEOUT:3. During the bank-switching action, up to one
second of data may be lost.] [STRIKEOUT:4. In the example above, if Bank
B is not empty, the data on Bank B will be extended in the usual manner
(i.e. no existing data on Bank B will be lost). In this case, automatic
bank switching on readback will not work properly.] [STRIKEOUT:5. If the
alternate Bank is not ready at the time switching is initiated, the
recording or readback will stop.] [STRIKEOUT:6. The ‘continuation
segment’ of the scan on the alternate disk module maintains the same
scan label as the originating segment, except that the ‘initial’ and
‘continuation’ segments are identified by a trailing or preceding
(respectively) ‘+’ character added to the scan name subfield of the scan
label when a ‘scan_set?’ query is executed.] 7. None of the Mark5A,
DIMino or drs programs implement this command as it cannot, effectively,
be used at all. *jive5ab* has implemented it but its correction
functioning should not be relied upon.

clock_set – Specify CLOCK parameters
====================================

Command syntax: clock_set = <clock frequency> : <clock_source> :
[<clock-generator freq>] ;

Command response: !clock_set = <return code> ;

Query syntax: clock_set? ;

Query response: !clock_set ? <return code> : <clock frequency> : <clock
source> : <clock-generator freq> ;

Purpose: Specify the frequency and source of the CLOCK driving the DIM

Settable parameters:

+----+---+------+-------+-----------------------------------------------+
| *  | * | *    | *     | **Comments**                                  |
| *P | * | *All | *Defa |                                               |
| ar | T | owed | ult** |                                               |
| am | y | valu |       |                                               |
| et | p | es** |       |                                               |
| er | e |      |       |                                               |
| ** | * |      |       |                                               |
|    | * |      |       |                                               |
+====+===+======+=======+===============================================+
| <c | i | 2 \| | -     | DOT clock will advance according to specified |
| lo | n | 4 \| |       | frequency; see Note 1. Certain restrictions   |
| ck | t | 8 \| |       | apply when <clock frequency> = 64; see Note   |
| fr |   | 16   |       | 2. Must be specified before a ‘DOT_set’       |
| eq |   | \|   |       | command is issued – see Note 3.               |
| ue |   | 32   |       |                                               |
| nc |   | \|   |       |                                               |
| y> |   | 64   |       |                                               |
+----+---+------+-------+-----------------------------------------------+
| <c | i | ext  | ext   | ext – VSI clock int – Use clock from internal |
| lo | n | \|   |       | clock-frequency generator                     |
| ck | t | int  |       |                                               |
| _s |   |      |       |                                               |
| ou |   |      |       |                                               |
| rc |   |      |       |                                               |
| e> |   |      |       |                                               |
+----+---+------+-------+-----------------------------------------------+
| <c | r | 0 –  | <     | Frequency to which internal clock generator   |
| lo | e | 40   | clock | is set. See Note 1. Note that clock generator |
| ck | a | (    | frequ | can be set a maximum of 40MHz.                |
| -g | l | MHz) | ency> |                                               |
| en |   |      | (but  |                                               |
| er |   |      | max   |                                               |
| at |   |      | 4     |                                               |
| or |   |      | 0MHz) |                                               |
| f  |   |      |       |                                               |
| re |   |      |       |                                               |
| q> |   |      |       |                                               |
+----+---+------+-------+-----------------------------------------------+

Monitor-only parameters:

================= ======== ========== ============
**Parameter**     **Type** **Values** **Comments**
================= ======== ========== ============
<clock frequency> int                 
================= ======== ========== ============

Notes:

1. The DOT clock timekeeping will advance by counting clock cycles
   according to the specified value of <clock frequency>, regardless of
   the actual clock frequency. For example, if <clock frequency> is
   specified as 32 MHz, but the actual value is 16 MHz, the length of a
   DOT second with be 32,000,000 clock cycles, occupying 2 wall-clock
   seconds. Occasionally, such a mis-setting may be deliberate, mostly
   for testing purposes; for example, setting <clock frequency> to 64MHz
   when the actual frequency of the clock is 32MHz would allow testing
   64MHz functions of the DIM at half-speed. Likewise, if the clock
   source is chosen to be the internal clock generator, the specified
   <clock frequency> need not necessarily correspond; the ratio of the
   <clock-generator freq> to <clock frequency> will determine the DOT
   clock rate relative to actual wall clock rate (e.g. OS clock rate).
2. If the <clock_frequency> is 64 MHz, the combination of bit-stream
   mask and decimation must be set so as not to exceed 1024Mbps
   aggregate bit rate, except for Mark 5B+.
3. [STRIKEOUT:A ‘clock_set’ command issue after a ‘DOT_set’ command will
   cause the value of the DOT clock to become
   indeterminant.]\ \ *jive5ab* ties the execution of ‘DOT_set’ (and
   thus the DOT clock) to the 1PPS tick and thus suffers no
   indeterminancy. However, the 1PPS only occurs after a ‘clock_set’ has
   been issued.
4. A ‘DOT_set’ command issued before a ‘Clock_set’ and ‘1pps_source’
   will cause an error.
5. [STRIKEOUT:‘clock_frq’ may be used as a synonym for ‘clock_set’ for
   VSI compatibility.]\ \ *jive5ab* does not have the ‘clock_frq’
   command.
6. Specifying the clock-generator frequency whilst setting the clock
   source to ‘ext’ will effectively re-program the internal clock chip,
   even though this has no effect. A warning is issued and in future
   releases the behaviour may be changed.

data_check – Check data starting at position of start-scan pointer (query only)
===============================================================================

Query syntax: data_check? ;

Query response: !data_check ? <return code> : <data source> : <start
time> : <date code> : <frame#> :

<frame header period> : <total recording rate> : <byte offset> :
<#missing bytes>;

Purpose: Reads a small amount of data starting at the start-scan pointer
position and attempts to determine the details of the data, including
mode and data time. For most purposes, the ‘scan_check’ command is more
useful.

Monitor-only parameters:

+---+---+-----------+----------------------------------------------------+
| * | * | *         | **Comments**                                       |
| * | * | *Values** |                                                    |
| P | T |           |                                                    |
| a | y |           |                                                    |
| r | p |           |                                                    |
| a | e |           |                                                    |
| m | * |           |                                                    |
| e | * |           |                                                    |
| t |   |           |                                                    |
| e |   |           |                                                    |
| r |   |           |                                                    |
| * |   |           |                                                    |
| * |   |           |                                                    |
+===+===+===========+====================================================+
| < | c | ext \|    | ext – data from Mark 5B DIM input port tvg – data  |
| d | h | tvg \| ?  | from internal tvg (as indicated by bit in disk     |
| a | a | vdif \|   | frame header) ? – [STRIKEOUT:data not in Mark 5B   |
| t | r | legacy    | format (might be Mark5A, for example); all         |
| a |   | vdif \|   | subsequent return fields will be null] See note 6. |
| s |   | Mark5B [  |                                                    |
| o |   | st : ]    |                                                    |
| u |   | vlba \|   |                                                    |
| r |   | mark4     |                                                    |
| c |   |           |                                                    |
| e |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | t |           | Time tag at first disk frame header. See Note 4    |
| s | i |           | below. See note 6.                                 |
| t | m |           |                                                    |
| a | e |           |                                                    |
| r |   |           |                                                    |
| t |   |           |                                                    |
| t |   |           |                                                    |
| i |   |           |                                                    |
| m |   |           |                                                    |
| e |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | i |           | [STRIKEOUT:3-digit date code written in first disk |
| d | n |           | frame header (module 1000 value of Modified Julian |
| a | t |           | Day).] See note 6.                                 |
| t |   |           |                                                    |
| e |   |           |                                                    |
| c |   |           |                                                    |
| o |   |           |                                                    |
| d |   |           |                                                    |
| e |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | i |           | Extracted from first disk frame header; frame# is  |
| f | n |           | always zero on second tick. See note 6.            |
| r | t |           |                                                    |
| a |   |           |                                                    |
| m |   |           |                                                    |
| e |   |           |                                                    |
| # |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | t |           | Each disk frames contains a 16-byte header         |
| f | i |           | followed by 10000 bytes of data                    |
| r | m |           |                                                    |
| a | e |           |                                                    |
| m |   |           |                                                    |
| e |   |           |                                                    |
| h |   |           |                                                    |
| e |   |           |                                                    |
| a |   |           |                                                    |
| d |   |           |                                                    |
| e |   |           |                                                    |
| r |   |           |                                                    |
| p |   |           |                                                    |
| e |   |           |                                                    |
| r |   |           |                                                    |
| i |   |           |                                                    |
| o |   |           |                                                    |
| d |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | r | (Mbps)    |                                                    |
| t | e |           |                                                    |
| o | a |           |                                                    |
| t | l |           |                                                    |
| a |   |           |                                                    |
| l |   |           |                                                    |
| r |   |           |                                                    |
| e |   |           |                                                    |
| c |   |           |                                                    |
| o |   |           |                                                    |
| r |   |           |                                                    |
| d |   |           |                                                    |
| i |   |           |                                                    |
| n |   |           |                                                    |
| g |   |           |                                                    |
| r |   |           |                                                    |
| a |   |           |                                                    |
| t |   |           |                                                    |
| e |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | i |           | Byte offset from start-scan pointer to first disk  |
| b | n |           | frame header.                                      |
| y | t |           |                                                    |
| t |   |           |                                                    |
| e |   |           |                                                    |
| o |   |           |                                                    |
| f |   |           |                                                    |
| f |   |           |                                                    |
| s |   |           |                                                    |
| e |   |           |                                                    |
| t |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+
| < | i | bytes     | Number of missing bytes between last and current   |
| # | n |           | ‘data_check’; Should be =0 if immediately previous |
| m | t |           | ‘data_check’ was within same scan Meaningless if   |
| i |   |           | immediately previous ‘data_check’ was in a         |
| s |   |           | different scan, or if data are not formatted VLBI  |
| s |   |           | data. Null if <#missing bytes> cannot be           |
| i |   |           | calculated; see Note 5.                            |
| n |   |           |                                                    |
| g |   |           |                                                    |
| b |   |           |                                                    |
| y |   |           |                                                    |
| t |   |           |                                                    |
| e |   |           |                                                    |
| s |   |           |                                                    |
| > |   |           |                                                    |
+---+---+-----------+----------------------------------------------------+

Notes:

1. Starting at the start-scan pointer position, the ‘data_check’ query
   searches to find the first valid disk frame header.
2. The ‘data_check’ query will be honored only if record is off.
3. The ‘data_check’ query does not affect the start-scan pointer.
4. Regarding the <start time> value returned by the ‘data_check?’ and,
   ‘scan_check?’ queries: The year and DOY reported in <start time>
   represent the most recent date consistent with the 3-digit <date
   code> in the frame header time tag (modulo 1000 value of Modified
   Julian Day as defined in VLBA tape-format header); this algorithm
   reports the proper year and DOY provided the data were taken no more
   than 1000 days ago.
5. The <#missing bytes> parameter is calculated as the difference the
   expected number of bytes between two samples of recorded data based
   on embedded time tags and the actual observed number of bytes between
   the same time tags. The reported number is the *total* number of
   bytes missing (or added) between the two sample points.
6. \ *jive5ab* extends data_check? to recognize all data formats, not
   just the recorder’s native data type (see Section 7). data_check?’s
   output may be subtly different depending on wether it is issued on a
   Mark5B/DIM, Mark6/FlexBuff or any of the other Mark5’s. Specifically
   for VDIF the number of threads will also be returned or for other
   formats some fields may be omitted (e.g. <frame #>).

datastream – Manage automatic storing of VDIF frames in separate recordings (*jive5ab* >= 3.0.0)
===================================================================================================

Command syntax: datastream = add : <name> : <spec> [ : <spec> ]\*;

datastream = remove : <name> ; datastream = reset ;

datastream = clear ; Command response: !datastream = <return code> ;

Query syntax: datastream? [ <name> ];

Query response: !datastream? <return code> [ : <name \| spec> ]\* ;

Purpose: Manage *jive5ab*\ ’s ability to filter incoming VDIF frames by
VDIF identifiers and record separately. See Section 12.3.

Settable parameters:

+------+------+--------------------------------------------------------+
| *    | **Ty | **Comments**                                           |
| *Par | pe** |                                                        |
| amet |      |                                                        |
| er** |      |                                                        |
+======+======+========================================================+
| add  | lit  | ‘add’ – add a new datastream definition to the         |
| re   | eral | currently defined set of datastreams ‘remove’ –        |
| move | A    | similar, only now remove it                            |
|      | SCII |                                                        |
+------+------+--------------------------------------------------------+
| r    |      | ‘reset’ – keep definitions of datastreams but clear    |
| eset |      | cache of matched/expanded streams. See Note 3.         |
+------+------+--------------------------------------------------------+
| c    |      | ‘clear’ – remove all defined datastreams, turn feature |
| lear |      | off                                                    |
+------+------+--------------------------------------------------------+
| <n   | char | The name of the datastream to manage. See Note 1.      |
| ame> |      |                                                        |
+------+------+--------------------------------------------------------+
| <s   | char | A VDIF match specification, describing frames to       |
| pec> |      | match. See Note 2.                                     |
+------+------+--------------------------------------------------------+

Monitor-only parameters:

+----+---+---+--------------------------------------------------------+
| *  | * | * | **Comments**                                           |
| *P | * | * |                                                        |
| ar | T | D |                                                        |
| am | y | e |                                                        |
| et | p | f |                                                        |
| er | e | a |                                                        |
| ** | * | u |                                                        |
|    | * | l |                                                        |
|    |   | t |                                                        |
|    |   | * |                                                        |
|    |   | * |                                                        |
+====+===+===+========================================================+
| <n | c | n | If no <name> specified, return all defined data stream |
| am | h | o | <name>. Otherwise return all the <spec>’s defined for  |
| e> | a | n | the indicated datastream <name>                        |
|    | r | e |                                                        |
+----+---+---+--------------------------------------------------------+
| <s | c |   | A VDIF match specifications, specifying the VDIF       |
| pe | h |   | frames that would be collected in this stream          |
| c> | a |   |                                                        |
|    | r |   |                                                        |
+----+---+---+--------------------------------------------------------+

Notes:

1. A datastream is identified by its <name>. The name will also be the
   suffix to the recording that will hold frames matching the <spec>’s
   defined for this datastream. After record=on:<scan>, and frame(s)
   matching any of the <spec>’s for a datastream are received, a
   recording by the name of <scan>\_<name> is created. The datastream
   <name> may contain any/all of the replacement fields

   {station} and/or {thread}, which will dynamically expand to the value
   of the corresponding field in a matched VDIF frame.

2. A <spec> is a matching specification of the form
   [\**[<ip|host>][@<port>]/][station.]thread(s)*\*. Brackets, as usual,
   indicate optionality. Unspecified match fields count as “match
   everything”. The shortest <spec> is \*, i.e. match all threads from
   all senders and ports and all VDIF station ids. See Section 12.3 for
   details and examples.

3. Because <name> can contain replacement fields, *jive5ab* keeps an
   internal cache of actual recording suffixes. It is important to clear
   this cache between recordings. By using ‘reset’ all currently defined
   datastreams remain intact and only this cache is cleared, meaning the
   datastream configuration needs only be specified once, e.g. at the
   start of an experiment.

dir_info – Get directory information (query only)
=================================================

Query syntax: dir_info? ;

Query response: !dir_info ? <return code> : <number of scans> : <total
bytes recorded> : <total bytes available> ;

Purpose: Returns information from the data directory, including number
of scans, total bytes recorded and remaining bytes available.

Monitor-only parameters:

+---------------+-----+------+-----------------------------------------+
| **Parameter** | **  | **   | **Comments**                            |
|               | Typ | Valu |                                         |
|               | e** | es** |                                         |
+===============+=====+======+=========================================+
| <number of    | int |      | Returns number of scans currently in    |
| scans>        |     |      | the data directory. See Note 3!         |
+---------------+-----+------+-----------------------------------------+
| <total bytes  | int |      | Sum over all recorded scans             |
| recorded>     |     |      |                                         |
+---------------+-----+------+-----------------------------------------+
| <total bytes  | int |      | Sum of total available disk space       |
| available>    |     |      | (unrecorded plus recorded)              |
+---------------+-----+------+-----------------------------------------+

Notes:

1. The scan directory is automatically stored each time data are
   recorded to the disks.

2. On Mark6/FlexBuff the dir_info? returns the sum of the recorded area
   and free space over all disks currently returned from the
   set_disks?query. The number of scans is unknown and ‘?’ will be
   returned.

3. \ *jive5ab* < 2.8 returns error code 6 “not in bank mode” when this
   query is executed whilst running in non-bank mode. *jive5ab* >= 2.8
   implements this query according to this specification in both bank
   and non-bank mode.

disk_model – Get disk model numbers (query only)
================================================

Query syntax: disk_model? ;

Query response: !disk_model ? <return code> : <disk model#> : <disk
model#> : ……; Purpose: Returns a list of model numbers in currently
selected disk module.

Monitor-only parameters:

+----+---+---+---------------------------------------------------------+
| *  | * | * | **Comments**                                            |
| *P | * | * |                                                         |
| ar | T | V |                                                         |
| am | y | a |                                                         |
| et | p | l |                                                         |
| er | e | u |                                                         |
| ** | * | e |                                                         |
|    | * | s |                                                         |
|    |   | * |                                                         |
|    |   | * |                                                         |
+====+===+===+=========================================================+
| <  | l |   | Returned in order of drive number (0=0M, 1=0S, 2=1M,    |
| di | i |   | 3=1S, etc); a blank field is returned for an empty      |
| sk | t |   | slot. When operating in ‘nb’ mode, disks in banks A and |
| m  | e |   | B are treated as a single module.                       |
| od | r |   |                                                         |
| el | a |   |                                                         |
| #> | l |   |                                                         |
|    | A |   |                                                         |
|    | S |   |                                                         |
|    | C |   |                                                         |
|    | I |   |                                                         |
|    | I |   |                                                         |
+----+---+---+---------------------------------------------------------+

disk_serial – Get disk serial numbers (query only)
==================================================

Query syntax: disk_serial? ;

Query response: !disk_serial ? <return code> : <disk serial#> : <disk
seriall#> : ……;; Purpose: Returns a list of serial numbers in currently
selected disk module.

Monitor-only parameters:

+----+---+---+---------------------------------------------------------+
| *  | * | * | **Comments**                                            |
| *P | * | * |                                                         |
| ar | T | V |                                                         |
| am | y | a |                                                         |
| et | p | l |                                                         |
| er | e | u |                                                         |
| ** | * | e |                                                         |
|    | * | s |                                                         |
|    |   | * |                                                         |
|    |   | * |                                                         |
+====+===+===+=========================================================+
| <  | l |   | Returned in order of drive number (0=0M, 1=0S, 2=1M,    |
| di | i |   | 3=1S, etc); A blank field is returned for an empty      |
| sk | t |   | slot. When operating in ‘nb’ mode, disks in banks A and |
| se | e |   | B are treated as a single module.                       |
| ri | r |   |                                                         |
| al | a |   |                                                         |
| #> | l |   |                                                         |
|    | A |   |                                                         |
|    | S |   |                                                         |
|    | C |   |                                                         |
|    | I |   |                                                         |
|    | I |   |                                                         |
+----+---+---+---------------------------------------------------------+

disk_size – Get disk sizes (query only)
=======================================

Query syntax: disk_size? ;

Query response: !disk_size ? <return code> : <disk size> : <disk size> :
……; Purpose: Returns individual capacities of currently selected module.

Monitor-only parameters:

+---+---+---+-----------------------------------------------------------+
| * | * | * | **Comments**                                              |
| * | * | * |                                                           |
| P | T | V |                                                           |
| a | y | a |                                                           |
| r | p | l |                                                           |
| a | e | u |                                                           |
| m | * | e |                                                           |
| e | * | s |                                                           |
| t |   | * |                                                           |
| e |   | * |                                                           |
| r |   |   |                                                           |
| * |   |   |                                                           |
| * |   |   |                                                           |
+===+===+===+===========================================================+
| < | i | b | Returned in order of drive number (0=0M, 1=0S, 2=1M,      |
| d | n | y | 3=1S, etc); A blank field is returned for an empty slot.  |
| i | t | t | When operating in ‘nb’ mode, disks in banks A and B are   |
| s |   | e | treated as a single module.                               |
| k |   | s |                                                           |
| s |   |   |                                                           |
| i |   |   |                                                           |
| z |   |   |                                                           |
| e |   |   |                                                           |
| > |   |   |                                                           |
+---+---+---+-----------------------------------------------------------+

disk_state –Set/get Disk Module Status (DMS): last significant disk operation
=============================================================================

Command syntax: disk_state = <DMS> ;

Command response: !disk_state = <return code> : <DMS> ;

Query syntax: disk_state? ;

Query response: !disk_state? <return code> : <active bank> :
<active-bank DMS> : <inactive bank> : <inactive-bank DMS>

;

Purpose: Set/get Disk Module Status (DMS), which logs the last
significant operation that happened on the disk module. Settable
parameters:

+------+---+-----------+---+---------------------------------------------+
| *    | * | **Allowed | * | **Comments**                                |
| *Par | * | values**  | * |                                             |
| amet | T |           | D |                                             |
| er** | y |           | e |                                             |
|      | p |           | f |                                             |
|      | e |           | a |                                             |
|      | * |           | u |                                             |
|      | * |           | l |                                             |
|      |   |           | t |                                             |
|      |   |           | * |                                             |
|      |   |           | * |                                             |
+======+===+===========+===+=============================================+
| <    | c | recorded  | n | To be used only if automatically-set DMS    |
| DMS> | h | \| played | o | parameter is to be overwritten. Requires a  |
| (    | a | \| erased | n | preceding ‘protect=off’ and affects only    |
| disk | r | \|        | e | the active module. Current value of         |
| Mo   |   | unknown   |   | ‘disk_state_mask’ is ignored.               |
| dule |   | \| error  |   |                                             |
| Sta  |   |           |   |                                             |
| tus) |   |           |   |                                             |
+------+---+-----------+---+---------------------------------------------+

Monitor-only parameters:

+---+---+----+-------------------------------------------------------------+
| * | * | ** | **Comments**                                                |
| * | * | Va |                                                             |
| P | T | lu |                                                             |
| a | y | es |                                                             |
| r | p | ** |                                                             |
| a | e |    |                                                             |
| m | * |    |                                                             |
| e | * |    |                                                             |
| t |   |    |                                                             |
| e |   |    |                                                             |
| r |   |    |                                                             |
| * |   |    |                                                             |
| * |   |    |                                                             |
+===+===+====+=============================================================+
| < | c | A  | Currently selected bank; ‘-‘ if disk module is judged       |
| a | h | \| | faulty; ‘nb’ if operating in non-bank mode (>=2.8)          |
| c | a | B  |                                                             |
| t | r |    |                                                             |
| i |   |    |                                                             |
| v |   |    |                                                             |
| e |   |    |                                                             |
| b |   |    |                                                             |
| a |   |    |                                                             |
| n |   |    |                                                             |
| k |   |    |                                                             |
| > |   |    |                                                             |
+---+---+----+-------------------------------------------------------------+
| < | c | re | recorded – last significant operation was record or a       |
| a | h | co | record-like function (net2disk or file2disk). played – last |
| c | a | rd | significant operation was playback; disk2net and disk2file  |
| t | r | ed | do not affect DMS. erased – last significant operation was  |
| i |   | \| | erase or conditioning, either from ‘reset=erase’ or         |
| v |   | pl | SSErase. unknown – last significant operation was performed |
| e |   | ay | with version of *dimino* or *SSErase* prior to              |
| - |   | ed | implementation of the DMS function. error – error occurred; |
| b |   | \| | for example, an interrupted conditioning attempt or a       |
| a |   | er | failure during one of the significant operations above      |
| n |   | as |                                                             |
| k |   | ed |                                                             |
| D |   | \| |                                                             |
| M |   | u  |                                                             |
| S |   | nk |                                                             |
| > |   | no |                                                             |
|   |   | wn |                                                             |
|   |   | \| |                                                             |
|   |   | e  |                                                             |
|   |   | rr |                                                             |
|   |   | or |                                                             |
+---+---+----+-------------------------------------------------------------+
| < | c | B  | Unselected bank, if module is mounted and ready; if no      |
| i | h | \| | module or faulty module, ’-‘ is returned.                   |
| n | a | A  |                                                             |
| a | r | \| |                                                             |
| c |   | -  |                                                             |
| t |   |    |                                                             |
| i |   |    |                                                             |
| v |   |    |                                                             |
| e |   |    |                                                             |
| b |   |    |                                                             |
| a |   |    |                                                             |
| n |   |    |                                                             |
| k |   |    |                                                             |
| > |   |    |                                                             |
+---+---+----+-------------------------------------------------------------+
| < | c | re | See above.                                                  |
| i | h | co |                                                             |
| n | a | rd |                                                             |
| a | r | ed |                                                             |
| c |   | \| |                                                             |
| t |   | pl |                                                             |
| i |   | ay |                                                             |
| v |   | ed |                                                             |
| e |   | \| |                                                             |
| - |   | er |                                                             |
| b |   | as |                                                             |
| a |   | ed |                                                             |
| n |   | \| |                                                             |
| k |   | u  |                                                             |
| D |   | nk |                                                             |
| M |   | no |                                                             |
| S |   | wn |                                                             |
| > |   | \| |                                                             |
|   |   | e  |                                                             |
|   |   | rr |                                                             |
|   |   | or |                                                             |
+---+---+----+-------------------------------------------------------------+

Notes:

1. Normally, the setting of the DMS parameter happens automatically
   whenever a record, play or erase command is issued. However, the

   <disk_state=…> command is provided to manually overwrite the current
   DMS parameter. This command requires a preceding ‘protect=off’ and
   affects only the active module. A ‘disk_state=…” command ignores the
   current value of the disk_state_mask (see ‘disk_state_mask’ command).

2. The DMS logs the last significant operation that occurred on a disk
   module. It is designed to distinguish between disk modules waiting to
   be correlated, have been correlated, or have no data (erased) and
   ready to be recorded. The DMS is saved on the disk module in the same
   area as the permanent VSN so that the DMS from both active and
   inactive disk banks are accessible. Commands scan_check, data_check,
   disk2net, and disk2file, do not affect DMS.

3. The ‘disk_state’ and ‘disk_state_mask’ commands were requested by
   NRAO and are designed primarily for use at a correlator.

4. If no modules are inserted, an error code 6 is returned.

5. When operating in ‘nb’ mode, disks in banks A and B are treated as a
   single module; no inactive bank information is returned in the reply.

disk_state_mask – Set mask to enable changes in DMS
===================================================

Command syntax: disk_state_mask = <erase_mask_enable> :
<play_mask_enable> : <record_mask_enable>;

Command response: !disk_state_mask = <return code> : <erase_mask_enable>
: <play_mask_enable> : <record_mask_enable> ;

Query syntax: disk_state_mask? ;

Query response: !disk_state_mask? <return code> : <erase_mask_enable> :
<play_mask_enable> : <record_mask_enable> ; Purpose: Set mask to enable
changes in DMS.

Settable parameters:

+---------+---+-------+----+------------------------------------------+
| **Para  | * | **Al  | *  | **Comments**                             |
| meter** | * | lowed | *D |                                          |
|         | T | val   | ef |                                          |
|         | y | ues** | au |                                          |
|         | p |       | lt |                                          |
|         | e |       | ** |                                          |
|         | * |       |    |                                          |
|         | * |       |    |                                          |
+=========+===+=======+====+==========================================+
| <eras   | i | 0 \|  | 1  | 0 – disable an erase operation from      |
| e_mask_ | n | 1     |    | modifying the DMS. 1 – enable erase      |
| enable> | t |       |    | operation to modify the DMS.             |
+---------+---+-------+----+------------------------------------------+
| <pla    | i | 0 \|  | 1  | 0 – disable a play operation from        |
| y_mask_ | n | 1     |    | modifying the DMS. 1 – enable play       |
| enable> | t |       |    | operation to modify the DMS.             |
+---------+---+-------+----+------------------------------------------+
| <recor  | i | 0 \|  | 1  | 0 – disable a record operation from      |
| d_mask_ | n | 1     |    | modifying the DMS. 1 – enable record     |
| enable> | t |       |    | operation to modify the DMS.             |
+---------+---+-------+----+------------------------------------------+

Notes:

1. The disk_state_mask is intended to prevent accidental changes in the
DMS. When a module is at a station, the disk_state_mask setting of 1:0:1
would disable a play operation from modifying the DMS. Likewise, at a
correlator one might want to disable the record_mask_enable.

disk2file – Transfer data from Mark 5 or FlexBuff/Mark6 to file (*jive5ab* >= 2.7.0)
=======================================================================================

Command syntax: disk2file = [<destination filename>] : [<start byte#>] :
[<end byte#>] : [<option>] ; Command response: !disk2file = <return
code> ;

Query syntax: disk2file? ;

Query response: !disk2file ? <return code> : <status> : <destination
filename> : <start byte#> : <current byte#> : <end byte#> : <option> ;

Purpose: Transfer data between start-scan and stop-scan pointers from
Mark 5 to file.

Settable parameters:

+----+---+-----+---+-------------------------------------------------+
| *  | * | **A | * | **Comments**                                    |
| *P | * | llo | * |                                                 |
| ar | T | wed | D |                                                 |
| am | y | va  | e |                                                 |
| et | p | lue | f |                                                 |
| er | e | s** | a |                                                 |
| ** | * |     | u |                                                 |
|    | * |     | l |                                                 |
|    |   |     | t |                                                 |
|    |   |     | * |                                                 |
|    |   |     | * |                                                 |
+====+===+=====+===+=================================================+
| <  | l | no  | S | Default <dest filename> is as specified in      |
| de | i | spa | e | Section 6 (i.e. ’<scan label>_bm=<bit           |
| st | t | ces | e | mask>.m5b’). Filename must include path if path |
| f  | e | a   | C | is not default (see Note 5).                    |
| il | r | llo | o |                                                 |
| en | a | wed | m |                                                 |
| am | l |     | m |                                                 |
| e> | A |     | e |                                                 |
|    | S |     | n |                                                 |
|    | C |     | t |                                                 |
|    | I |     | s |                                                 |
|    | I |     |   |                                                 |
+----+---+-----+---+-------------------------------------------------+
| <s | i |     | S | Absolute byte#; if null, defaults to start-scan |
| ta | n |     | e | pointer. See Notes 1 and 2.                     |
| rt | t |     | e |                                                 |
| by | \ |     | N |                                                 |
| te | | |     | o |                                                 |
| #> | n |     | t |                                                 |
|    | u |     | e |                                                 |
|    | l |     | s |                                                 |
|    | l |     |   |                                                 |
+----+---+-----+---+-------------------------------------------------+
| <e | i |     | S | Absolute end byte#; if preceded by ‘+’,         |
| nd | n |     | e | increment from <start byte#> by specified       |
| by | t |     | e | value; if null, defaults to stop-scan ponter.   |
| te | \ |     | N | See Notes 1 and 2.                              |
| #> | | |     | o |                                                 |
|    | n |     | t |                                                 |
|    | u |     | e |                                                 |
|    | l |     | s |                                                 |
|    | l |     |   |                                                 |
+----+---+-----+---+-------------------------------------------------+
| <o | c | n   | n | n – create file; error if existing file w       |
| pt | h | \|  |   | –erase existing file, if any; create new file.  |
| io | a | w   |   | a – create file if necessary, or append to      |
| n> | r | \|  |   | existing file                                   |
|    |   | a   |   |                                                 |
+----+---+-----+---+-------------------------------------------------+

Monitor-only parameters:

+--------+---+--------+-----------------------------------------------+
| *      | * | **Va   | **Comments**                                  |
| *Param | * | lues** |                                               |
| eter** | T |        |                                               |
|        | y |        |                                               |
|        | p |        |                                               |
|        | e |        |                                               |
|        | * |        |                                               |
|        | * |        |                                               |
+========+===+========+===============================================+
| <dest  |   |        | Destination filename (returned even if        |
| fil    |   |        | filename was defaulted in corresponding       |
| ename> |   |        | ‘disk2file’ command)                          |
+--------+---+--------+-----------------------------------------------+
| <s     | c | active | Current status of transfer                    |
| tatus> | h | \|     |                                               |
|        | a | in     |                                               |
|        | r | active |                                               |
+--------+---+--------+-----------------------------------------------+
| <c     | i |        | Current byte number being transferred         |
| urrent | n |        |                                               |
| byte#> | t |        |                                               |
+--------+---+--------+-----------------------------------------------+

Notes:

1. The ‘scan_set’ command is a convenient way to set the <start byte#>
   and <stop byte#>.
2. If <start byte#> and <end byte#> are null, the range of data defined
   by ‘scan_set’ will be transferred.
3. To abort data transfer: The ‘reset=abort’ command may be used to
   abort an active disk2file data transfer. See ‘reset’ command for
   details.
4. When <status> is ‘inactive’, a ‘disk2file?’ query returns the <dest
   filename> of the last transferred scan, if any.
5. Default path is the Linux default, which is the directory from which
   *dimino* or *Mark 5B* was started.

disk2net – Transfer data from Mark 5 or FlexBuff/Mark6 to network (*jive5ab* >= 2.7.0)
==========================================================================================

Command syntax: disk2net = connect : <target hostname> ;

disk2net = on : [<start byte#>] : [<end byte#>] ; disk2net = disconnect
;

Command response: !disk2net = <return code>;

Query syntax: disk2net? ;

Query response: !disk2net ? <return code> : <status> : <target hostname>
: <start byte#> : <current byte#> : <end byte#> ; Purpose: Transfer data
between start-scan and stop-scan pointers from Mark 5 to network.

Settable parameters:

+-----+---+-------+--------+-----------------------------------------+
| *   | * | **Al  | **Def  | **Comments**                            |
| *Pa | * | lowed | ault** |                                         |
| ram | T | val   |        |                                         |
| ete | y | ues** |        |                                         |
| r** | p |       |        |                                         |
|     | e |       |        |                                         |
|     | * |       |        |                                         |
|     | * |       |        |                                         |
+=====+===+=======+========+=========================================+
| <co | c | co    |        | ‘connect’ – connect to socket on        |
| ntr | h | nnect |        | receiving Mark 5 system ‘on’ – start    |
| ol> | a | \| on |        | data transfer ’disconnect’ – disconnect |
|     | r | \|    |        | socket See Notes.                       |
|     |   | disco |        |                                         |
|     |   | nnect |        |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <   | c |       | loc    | Required only on if                     |
| tar | h |       | alhost | <control>=‘connect’..                   |
| get | a |       | or     |                                         |
| hos | r |       | prev   |                                         |
| tna |   |       | iously |                                         |
| me> |   |       | set    |                                         |
|     |   |       | name   |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <st | [ |       | See    | Absolute byte# or relative offset; if   |
| art | + |       | Note 1 | null, defaults to start-scan pointer.   |
| byt | ] |       |        | See Note 1. See Note 10.                |
| e#> | i |       |        |                                         |
|     | n |       |        |                                         |
|     | t |       |        |                                         |
|     | \ |       |        |                                         |
|     | | |       |        |                                         |
|     | n |       |        |                                         |
|     | u |       |        |                                         |
|     | l |       |        |                                         |
|     | l |       |        |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <   | i |       | See    | Absolute end byte#; if preceded by ‘+’, |
| end | n |       | Note 1 | increment from <start byte#> by         |
| byt | t |       |        | specified value; if null, defaults to   |
| e#> | \ |       |        | start-scan pointer. See Note 1.         |
|     | | |       |        |                                         |
|     | n |       |        |                                         |
|     | u |       |        |                                         |
|     | l |       |        |                                         |
|     | l |       |        |                                         |
+-----+---+-------+--------+-----------------------------------------+

Monitor-only parameters:

+-------------+-----+----------------------+--------------------------+
| **          | **  | **Values**           | **Comments**             |
| Parameter** | Typ |                      |                          |
|             | e** |                      |                          |
+=============+=====+======================+==========================+
| <status>    | c   | connected \| active  | Current status of        |
|             | har | \| inactive          | transfer                 |
+-------------+-----+----------------------+--------------------------+
| <target     | c   |                      |                          |
| hostname>   | har |                      |                          |
+-------------+-----+----------------------+--------------------------+
| <current    | int |                      | Current byte number      |
| byte#>      |     |                      | being transferred        |
+-------------+-----+----------------------+--------------------------+

Notes:

1.  The ’<scan_set> command is a convenient way to set the <start byte#>
    and <stop byte#>.
2.  If <start byte#> and <end byte#> are null, the scan defined by
    ‘scan_set’ will be transferred.
3.  To set up connection: First, issue ‘open’ to the *receiving* system
    (‘net2disk=open’ or ‘net2out=open’ to Mark 5, or Net2file as
    standalone program; then issue ‘connect’ to the *sending* system
    (‘in2net=connect:..’ or ‘disk2net=connect:…’ to Mark 5).
4.  To start data transfer: Issue ‘on’ to *sending* system (‘in2net=on’
    or ‘disk2net=on’ to Mark 5). A ‘disk2net’ transfer will stop
    automatically after the specified number of bytes are sent.
5.  To stop data transfer: Issue ‘off’ to the sending system
    (‘in2net=off’ to Mark 5). After each transfer has been stopped or
    completed, another transfer may be initiated (see Note 2).
6.  To close connection: First, issue ‘disconnect’ to the sender
    (‘in2net=disconnect’ or ‘disk2net=disconnect’ to Mark 5). A
    ‘disk2net=disconnect’ command issued before the specified number of
    bytes are transferred will abort the transfer and close the
    connection. Then, ‘close’ the receiver (‘net2disk=close’ or
    ‘net2out=close’ to Mark 5; Net2file ends). Net2file ends
    automatically on a ‘disconnect’. After a ‘net2disk’ transfer, the
    data on disk are not ready for use until after a ‘net2disk=close’
    command has been issued.
7.  To abort data transfer: The ‘reset=abort’ command may be used to
    abort an active disk2net data transfer. A subsequent
    ‘disk2net=disconnect’ must be issued to close the socket and put the
    Mark 5B back into idle. See ‘reset’ command for details.
8.  Only one data transfer activity may be active at any given time.
    That is, among ‘record=on’, ‘in2net=..’, ‘disk2net=..’,
    ‘net2disk=..’, ‘net2out=..’, ‘disk2file=..’, ‘file2disk=..’,
    ‘data_check’ and ‘scan_check’, only one may be active at any given
    time.
9.  Note that the network protocol parameters are set by the
    ‘net_protocol’ command.
10. \ *jive5ab* 2.6.2 and later extend ‘disk2net = on : ..’ to support a
    relative start byte number by prefixing the byte number with a
    literal ‘+’. The start byte number for the transfer then becomes the
    sum of the start byte set via ‘scan_set = ..’ and <start byte#>.
    This is necessary to be able to support resuming a previously
    interrupted transfer.

DOT – Get DOT clock information (query only)
============================================

Query syntax: DOT? ;

Query response: !DOT ? <return code> : <current DOT reading> : <sync
status> : <FHG status> :

<current OS time> : <DOT-OS difference> ; Purpose: Get DOT clock
information

Monitor-only parameters:

+---+---+--------+-------------------------------------------------------+
| * | * | **Va   | **Comments**                                          |
| * | * | lues** |                                                       |
| P | T |        |                                                       |
| a | y |        |                                                       |
| r | p |        |                                                       |
| a | e |        |                                                       |
| m | * |        |                                                       |
| e | * |        |                                                       |
| t |   |        |                                                       |
| e |   |        |                                                       |
| r |   |        |                                                       |
| * |   |        |                                                       |
| * |   |        |                                                       |
+===+===+========+=======================================================+
| < | t |        | Current value of DOT clock. 1970/1/1 00h00m00s as     |
| c | i |        | long as not set using **DOT_set! See Note 4.**        |
| u | m |        |                                                       |
| r | e |        |                                                       |
| r |   |        |                                                       |
| e |   |        |                                                       |
| n |   |        |                                                       |
| t |   |        |                                                       |
| D |   |        |                                                       |
| O |   |        |                                                       |
| T |   |        |                                                       |
| r |   |        |                                                       |
| e |   |        |                                                       |
| a |   |        |                                                       |
| d |   |        |                                                       |
| i |   |        |                                                       |
| n |   |        |                                                       |
| g |   |        |                                                       |
| > |   |        |                                                       |
+---+---+--------+-------------------------------------------------------+
| < | c | not_   | ‘not_synced’ – DOT 1pps generator has not yet been    |
| s | h | synced | sync’ed. See Note 1. ‘syncerr_eq_0’ - DOT 1pps tick   |
| y | a | \|     | is exactly coincident with selected external 1pps     |
| n | r | syncer | tick. See Note 2. ‘syncerr_le_3’ – DOT 1pps tick      |
| c |   | r_eq_0 | within +/-2 clock cycles of selected external 1pps    |
| s |   | \|     | tick. ‘syncerr_gt_3’ – DOT 1pps tick more +/-3 clock  |
| t |   | syncer | cycles from selected external 1pps tick               |
| a |   | r_le_3 |                                                       |
| t |   | \|     |                                                       |
| u |   | syncer |                                                       |
| s |   | r_gt_3 |                                                       |
| > |   |        |                                                       |
+---+---+--------+-------------------------------------------------------+
| < | c | F      | ’FHG_off’ – Frame Header Generator is not running     |
| F | h | HG_off | (<current DOT reading> is software estimate) ‘FHG_on’ |
| H | a | \|     | – FHG is running (<current DOT reading> is read from  |
| G | r | FHG_on | hardware FHG). See Note 3.                            |
| s |   |        |                                                       |
| t |   |        |                                                       |
| a |   |        |                                                       |
| t |   |        |                                                       |
| u |   |        |                                                       |
| s |   |        |                                                       |
| > |   |        |                                                       |
+---+---+--------+-------------------------------------------------------+
| < | t |        | Corresponding OS time                                 |
| c | i |        |                                                       |
| u | m |        |                                                       |
| r | e |        |                                                       |
| r |   |        |                                                       |
| e |   |        |                                                       |
| n |   |        |                                                       |
| t |   |        |                                                       |
| O |   |        |                                                       |
| S |   |        |                                                       |
| t |   |        |                                                       |
| i |   |        |                                                       |
| m |   |        |                                                       |
| e |   |        |                                                       |
| > |   |        |                                                       |
+---+---+--------+-------------------------------------------------------+
| < | t |        | <current DOT reading> minus <current OS time>         |
| D | i |        |                                                       |
| O | m |        |                                                       |
| T | e |        |                                                       |
| - |   |        |                                                       |
| O |   |        |                                                       |
| S |   |        |                                                       |
| d |   |        |                                                       |
| i |   |        |                                                       |
| f |   |        |                                                       |
| f |   |        |                                                       |
| e |   |        |                                                       |
| r |   |        |                                                       |
| e |   |        |                                                       |
| n |   |        |                                                       |
| c |   |        |                                                       |
| e |   |        |                                                       |
| > |   |        |                                                       |
+---+---+--------+-------------------------------------------------------+

Notes:

1. A <sync status> of ‘not_synced’ indicates that the DOT hardware 1pps
   generator has not been sync’ed to an external 1pps tick. All other
   <sync status> returns indicate that the DOT 1pps generator has been
   sync’ed with a ‘DOT_set’ command. See also Note 2 of ‘DOT_set’
   command.
2. The <syncerr_eq/le/gt_n> status returns are only relevant provided
   the selected external 1pps that was used to sync the DOT clock
   remains connected, selected and active and indicates quantitatively
   the current relationship between the DOT 1pps and the external 1pps
   to which the DOT clock was originally sync’ed. Note that if the
   external 1pps tick is asynchronous to the data clock (e.g. comes from
   a GPS receiver), one would not expect that exact synchronization
   would be maintained. Caution: the 1pps tick on the VSI-80 connector
   from a VSI Mark 4 formatter has a slow leading edge and may not
   always pass the <exact sync> test, though it should always pass the
   <approx sync> test.
3. DOT 1pps ticks are always counted by dimino to keep higher-order
   time; when data collection is inactive, the sub-second part of
   <current DOT reading> is estimated by software measurement of the
   time interval from the last DOT 1pps second tick. The hardware Frame
   Header Generator (FHG) runs only during active data collection and
   creates the Disk Frame Headers inserted periodically into the data
   stream transmitted to the StreamStor card; during this time, the
   <current DOT reading> is read directly from the FHG. The FHG is set
   up according to the parameters of each recording and is started at
   the beginning of each recording. The time resolution of the FHG is
   the Mark 5B disk-frame-header period, which is given by
   80/(total-rate in Mbps) milliseconds, where the total data rate is
   determine by three parameters: clock rate (set by ‘clock_set’
   command), bit-stream mask and decimation ratio (both set by ‘mode’
   command); for example, for a total data rate of 1024Mbps, the time
   resolution of the FHG is ~78 *micro*\ seconds. For more details see
   memo ‘Data Input Module Mark 5B I/O Board Theory of Operation’.
4. \ *jive5ab* keeps the DOT at January 1st, 1970 00h00m00s as long as
   the DOT clock has not been set up correctly (1pps_source + clock_set
   + DOT_set). This is in direct contrast with DIMino. *jive5ab* does
   NOT try to estimate an initial DOT at start-up. A restart of
   *jive5ab* necessitates re- setting the DOT clock. If, during an
   observation a really excessive <DOT – OS difference> is measured
   (million of seconds), it is most likely that the DOT has not been
   properly initialized. *jive5ab* >= 2.5.0 will not start a recording
   at all if the DOT has not been set. See also Note 9 of DOT_set.

DOT_inc – Increment DOT clock
=============================

Command syntax: DOT_inc = <inc> ; Command response: ! DOT_inc = <return
code> ;

Query syntax: DOT_inc? ;

Query response: ! DOT_inc? <return code> : <inc> ;

Purpose: Increment DOT clock time by specified number of seconds

Settable parameters:

+----+---+-----+---+---------------------------------------------------+
| *  | * | **A | * | **Comments**                                      |
| *P | * | llo | * |                                                   |
| ar | T | wed | D |                                                   |
| am | y | va  | e |                                                   |
| et | p | lue | f |                                                   |
| er | e | s** | a |                                                   |
| ** | * |     | u |                                                   |
|    | * |     | l |                                                   |
|    |   |     | t |                                                   |
|    |   |     | * |                                                   |
|    |   |     | * |                                                   |
+====+===+=====+===+===================================================+
| <  | i |     | 0 | Number of seconds to increment DOT clock (may be  |
| in | n |     |   | positive or negative). >0 will advance the DOT    |
| c> | t |     |   | clock setting; <0 will retard the DOT clock       |
|    |   |     |   | setting                                           |
+----+---+-----+---+---------------------------------------------------+

Monitor-only parameters:

============= ======== ========== ============
**Parameter** **Type** **Values** **Comments**
============= ======== ========== ============
<inc>         int                 
============= ======== ========== ============

Notes:

1. The DOT_inc command should be used to adjust an error in the DOT
   clock only after the DOT clock has been synchronized to an external
   1pps tick with the ‘DOT_set’ command.
2. A ‘DOT_inc’ issued when the DOT clock is not running (1pps_source +
   clock_set + DOT_set) causes an error.

DOT_set – Set DOT clock on next external 1pps tick
==================================================

Command syntax: dot_set = <time> : [<option>] ;

Command response: !dot_set = <return code> ;Query syntax: dot_set? ;

Query response: !dot_set ? <return code> : <time> : <option> : <time
offset> ;

Purpose: Set initial value of Mark 5B DOT clock on next tick of selected
external 1pps source

Settable parameters:

+---+---+---+---+---------------------------------------------------------+
| * | * | * | * | **Comments**                                            |
| * | * | * | * |                                                         |
| P | T | A | D |                                                         |
| a | y | l | e |                                                         |
| r | p | l | f |                                                         |
| a | e | o | a |                                                         |
| m | * | w | u |                                                         |
| e | * | e | l |                                                         |
| t |   | d | t |                                                         |
| e |   | v | * |                                                         |
| r |   | a | * |                                                         |
| * |   | l |   |                                                         |
| * |   | u |   |                                                         |
|   |   | e |   |                                                         |
|   |   | s |   |                                                         |
|   |   | * |   |                                                         |
|   |   | * |   |                                                         |
+===+===+===+===+=========================================================+
| < | t | n | O | DOT clock can only be set to an integer second value.   |
| t | i | u | S | See Note 1. If null, sets DOT clock according to        |
| i | m | l | t | current OS time.                                        |
| m | e | l | i |                                                         |
| e |   | \ | m |                                                         |
| > |   | | | e |                                                         |
|   |   | t |   |                                                         |
|   |   | i |   |                                                         |
|   |   | m |   |                                                         |
|   |   | e |   |                                                         |
+---+---+---+---+---------------------------------------------------------+
| < | c | n | n | If ‘force’, 1pps generator will be re-synced even       |
| o | h | u | u | though the <DOT_synced> status returned by ‘DOT?’       |
| p | a | l | l | already indicates it is sync’ed. See Note 2. If null,   |
| t | r | l | l | 1pps generator will be synced only if <DOT_synced>      |
| i |   | \ |   | status indicates it is not already sync’ed.             |
| o |   | | |   |                                                         |
| n |   | f |   |                                                         |
| > |   | o |   |                                                         |
|   |   | r |   |                                                         |
|   |   | c |   |                                                         |
|   |   | e |   |                                                         |
+---+---+---+---+---------------------------------------------------------+

Monitor-only parameters:

+------+---+----+----------------------------------------------------+
| *    | * | ** | **Comments**                                       |
| *Par | * | Va |                                                    |
| amet | T | lu |                                                    |
| er** | y | es |                                                    |
|      | p | ** |                                                    |
|      | e |    |                                                    |
|      | * |    |                                                    |
|      | * |    |                                                    |
+======+===+====+====================================================+
| <t   | t |    | Includes all inferred higher-order time (e.g.,     |
| ime> | i |    | year, DOY, etc) that was not explicit in command   |
|      | m |    | value of <time>                                    |
|      | e |    |                                                    |
+------+---+----+----------------------------------------------------+
| <opt | c |    |                                                    |
| ion> | h |    |                                                    |
|      | a |    |                                                    |
|      | r |    |                                                    |
+------+---+----+----------------------------------------------------+
| <    | t |    | Estimated interval between time from receipt of    |
| time | i |    | ‘DOT_set’ command to 1pps tick that set the DOT    |
| off  | m |    | clock                                              |
| set> | e |    |                                                    |
+------+---+----+----------------------------------------------------+

Notes:

1. If <time> does not specify higher-order time (i.e. year, day, etc),
   the current values from the OS time are used. Of course, the DOT
   clock should be carefully checked with the ‘DOT?’ query after setting
   to verify that it is properly set.
2. Because the Mark 5B keeps higher-order time (above 1sec) in software,
   higher-order time will be lost *dimino* is restarted or the system is
   re-booted, but the DOT hardware 1pps generator will not lose sync as
   long as power and data clock (on the VSI 80-pin connector) are
   maintained; a re-issued ‘DOT_set’ command will set higher-order time
   without disturbing the existing 1pps synchronization unless the
   ‘force’ option is specified to force re- synchronization of the
   hardware 1pps generator.
3. After the ‘DOT_set’ command is issued, a ‘DOT?’ query should be
   subsequently issued to verify the proper time setting. If necessary,
   the ‘DOT_inc’ command may be used to adjust the DOT clock to the
   correct second.
4. If the DOT clock is set from OS time, care must be taken that the OS
   clock is reasonably well aligned with the external 1pps tick (to
   within a few tens of milliseconds, at worst). Otherwise, there is no
   requirement on the OS timekeeping apart from Note 2 above.
5. Following the setting of the DOT clock, the DOT clock keeps time
   based strictly on the selected CLOCK as specified in the ‘clock_set’
   command and runs completely independently of the OS time;
   furthermore, the external 1pps is not used for any further operations
   other than cross-checking against the internally generated DOT1PPS
   interrupt and, in principle, can be disconnected.
6. Normally the DOT_set operation is only done once at the beginning of
   an experiment. See also Note 1 for ‘DOT?’ query.
7. Any change in ‘clock_set’ parameters enacted after a ‘DOT_set’
   command will cause the value of the DOT clock to become
   indeterminate.
8. A ‘DOT_set’ command issued before a ‘clock_set’ command will cause an
   error.
9. \ *jive5ab* keeps the DOT at January 1st, 1970 00h00m00s as long as
   the DOT clock has not been set up correctly (1pps_source + clock_set
   + DOT_set). This is in direct contrast with DIMino. *jive5ab* does
   NOT try to estimate an initial DOT at start-up. A restart of
   *jive5ab* necessitates re- setting the DOT clock. Once a DOT has been
   set, *jive5ab* increases its value by exactly 1 second every 1PPS
   interrupt of the selected 1pps_source.

DTS_id – Get system information (query only)
============================================

Query syntax: DTS_id? ;

Query response: !DTS_id ? <return code> : <system type> : <software
revision date> : <media type> :

<serial number> : <#DIM ports> : <#DOM ports> : <command set revision> :

<Input design revision> : <Output design revision> ; Purpose: Get Mark 5
system information

Monitor-only parameters:

+-----+---+---------+---------------------------------------------------+
| *   | * | **V     | **Comments**                                      |
| *Pa | * | alues** |                                                   |
| ram | T |         |                                                   |
| ete | y |         |                                                   |
| r** | p |         |                                                   |
|     | e |         |                                                   |
|     | * |         |                                                   |
|     | * |         |                                                   |
+=====+===+=========+===================================================+
| <   | c | mark5A  | ‘Mark5A-C’ – case sensitivity apart these speak   |
| sys | h | \|      | for themselves (Mark5B+ returns mark5b as well)   |
| tem | a | mark5b  | ‘StreamStor’ – only StreamStor card detected (new |
| ty  | r | \|      | in jive5ab 2.8.2) see Notes ’-‘ – generic system, |
| pe> |   | Mark5C  | FlexBuff, Mark6 or just-some-computer             |
|     |   | \|      |                                                   |
|     |   | Str     |                                                   |
|     |   | eamStor |                                                   |
|     |   | \| -    |                                                   |
+-----+---+---------+---------------------------------------------------+
| <so | t |         | [STRIKEOUT:Date stamp on current version of       |
| ftw | i |         | dimino source code (\*.c) files] Timestamp of     |
| are | m |         | compilation of jive5ab                            |
| re  | e |         |                                                   |
| vis |   |         |                                                   |
| ion |   |         |                                                   |
| da  |   |         |                                                   |
| te> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <me | i | 1       | Per VSI-S spec: 1 – magnetic disk [0 – magnetic   |
| dia | n |         | tape; 2 – real-time (non-recording)]              |
| ty  | t |         |                                                   |
| pe> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <   | A |         | System serial number; generally is in the form    |
| ser | S |         | ‘mark5-xx’ where xx is the system serial number   |
| ial | C |         |                                                   |
| n   | I |         |                                                   |
| umb | I |         |                                                   |
| er> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <#  | i | 1       | Number of DIM ports in this DTS                   |
| DIM | n |         |                                                   |
| por | t |         |                                                   |
| ts> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <#  | i | 1       | Number of DOM ports in this DTS                   |
| DOM | n |         |                                                   |
| por | t |         |                                                   |
| ts> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <c  | c |         | Mark 5AB or C [STRIKEOUT:DIM] command set         |
| omm | h |         | revision level corresponding to this software     |
| and | a |         | release (e.g. ‘1.0’)                              |
| set | r |         |                                                   |
| rev |   |         |                                                   |
| isi |   |         |                                                   |
| on> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+
| <   | i |         | Revision level of DIM FPGA design on Mark 5B I/O  |
| DIM | n |         | board                                             |
| des | t |         |                                                   |
| ign |   |         |                                                   |
| rev |   |         |                                                   |
| isi |   |         |                                                   |
| on> |   |         |                                                   |
+-----+---+---------+---------------------------------------------------+

Notes:

**1.** Since version 2.8.2 jive5ab supports ‘crippled’ Mark5s
gracefully. A crippled Mark5 is a system where only a StreamStor card is
detected and no I/O board (5A, 5B) or daughter board (5B+, 5C). These
systems might be found at a correlator site where only disk playback is
required. On such a system *all* disk based transfers but ‘record=’ and
‘in2*=’ remain functional.

error – Get error number/message (query only)
=============================================

Query syntax: error? ;

Query response: !error ? <return code> : <error#> : <error message> :
<error time> [ : <last error time> : <# occurrences> ] ; Purpose: Get
error number, message [and time] causing bit 1 of ‘status’ query return
to be set

Monitor-only parameters:

+--------+------+----+------------------------------------------------+
| *      | **Ty | ** | **Comments**                                   |
| *Param | pe** | Va |                                                |
| eter** |      | lu |                                                |
|        |      | es |                                                |
|        |      | ** |                                                |
+========+======+====+================================================+
| <e     | int  |    | Error number associated with ’status’query     |
| rror#> |      |    | return bit 1                                   |
+--------+------+----+------------------------------------------------+
| <error | lit  |    | Associate error message, if any                |
| me     | eral |    |                                                |
| ssage> | A    |    |                                                |
|        | SCII |    |                                                |
+--------+------+----+------------------------------------------------+
| <error | time |    | System time when this error first occurred     |
| time>  |      |    |                                                |
+--------+------+----+------------------------------------------------+
| <last  | time |    | If the error occurred more than once, system   |
| error  |      |    | time when this specific error last occurred    |
| time>  |      |    | (since 2.6.0)                                  |
+--------+------+----+------------------------------------------------+
| <#     | int  |    | If the error occurred more than once, the      |
| occurr |      |    | number of times this specific error occurred   |
| ences> |      |    | (since 2.6.0)                                  |
+--------+------+----+------------------------------------------------+

Notes:

1. [STRIKEOUT:Most errors are ‘remembered’ (even if printed with debug)
   and printed (and cleared) by either a ‘status?’ or ‘error?’ query.
   Thus, errors may be remembered even after they have been corrected.]
2. \ *jive5ab* maintains a queue of errors. If the queue is non-empty at
   the time of a ‘status?’ query, bit 1 of the status word will be set.
   An ‘error?’ query will remove the oldest error message permanently
   from the queue.

evlbi – Get e-VLBI transfer packet reception and re-ordering statistics
=======================================================================

Command syntax: evlbi = [ <statistic> : <statistic> : <statistic> : … ];

Command response: !evlbi = <return code> : [ <statistic value> :
<statistic value> : … ];

Query syntax: evlbi? ;

Query response: !evlbi ? <return code> : <statistic value> : … ;

Purpose: Return specific or default set of packet statistics on a
receiving end of an UDPs/UDPsnor/VTP or UDT based transfer

Settable parameters:

+-----+---+-------+----+----------------------------------------------+
| *   | * | **Al  | *  | **Comments**                                 |
| *Pa | * | lowed | *D |                                              |
| ram | T | val   | ef |                                              |
| ete | y | ues** | au |                                              |
| r** | p |       | lt |                                              |
|     | e |       | ** |                                              |
|     | * |       |    |                                              |
|     | * |       |    |                                              |
+=====+===+=======+====+==============================================+
| <s  | c | See   | no | The parameters form a format string.         |
| tat | h | notes | ne | Recognized specifiers are replaced with the  |
| ist | a |       |    | current value they describe.                 |
| ic> | r |       |    |                                              |
+-----+---+-------+----+----------------------------------------------+

Monitor-only parameters:

+-----------+----+-----+----------------------------------------------+
| **Pa      | ** | *   | **Comments**                                 |
| rameter** | Ty | *Va |                                              |
|           | pe | lue |                                              |
|           | ** | s** |                                              |
+===========+====+=====+==============================================+
| <         | nu | see | evlbi? behaves like “evlbi=” with a default  |
| statistic | mb | no  | set of statistics that are returned          |
| value>    | er | tes |                                              |
+-----------+----+-----+----------------------------------------------+

Notes:

1. When the net_protocol is set to udp, udps (synonym for udp), udpsnor
   (*jive5ab* >=2.8), udt or vtp, the receiving *jive5ab* will collect
   packet reception statistics, to some extent following IETF
   metrics [20]_. One notable deviation is that out of performance
   considerations, *jive5ab* will not collect out-of-order statistics
   for packets that are re-ordered by more than 32 packets.
2. Much like how the C-library strftime(3) has time fields available,
   *jive5ab* has packet statistics counters available. The command is a
   printf-style “format string” and *jive5ab* will replace special
   formatters with their current value as per this table:

+---+-----------------------------------+---+-----------------------------+
| % | Total amount of packets received  |   |                             |
| t |                                   |   |                             |
+===+===================================+===+=============================+
| % | Amount of packets lost (count)    | % | Amount of packets lost      |
| l |                                   | L | (percentage)                |
+---+-----------------------------------+---+-----------------------------+
| % | Amount of packets out-of-order    | % | Amount of packets           |
| o | (count)                           | O | out-of-order (percentage)   |
+---+-----------------------------------+---+-----------------------------+
| % | Amount of discarded packets       | % | Amount of discarded packets |
| d | (count)                           | D | (percentage)                |
+---+-----------------------------------+---+-----------------------------+
| % | Total amount of re-orderdering    | % | Average amount of           |
| r | measured                          | R | re-ordering per packet      |
+---+-----------------------------------+---+-----------------------------+
| % | Time stamp, unix format + added   | % | Time stamp YYYY-MM-DD       |
| u | millisecond fraction              | U | HHhMMmSS.SSSs               |
+---+-----------------------------------+---+-----------------------------+

3. “evlbi?” is an alias for “evlbi = total : %u : ooo : %o : disc : %d :
   lost : %l : extent : %R ;”
4. For udpsnor the numbers returned are aggregated values of up to eight
   independent senders

file_check – Check recorded data between start and end of file (query only)
===========================================================================

Query syntax: file_check? [ <strict> ] : [ <#bytes to read> ] : <file> ;

Query response: !file_check ? <return code> : <data type> : <ntrack> :
<start time> :

<scan length> : <total recording rate> : <#missing bytes> [ : <data
array size> ]; Purpose: Offers ‘scan_check?’ functionality for
files-on-disk. This command was introduced in *jive5ab* 2.5.1.

Monitor arguments:

+---+---+---+---+----------------------------------------------------------+
| * | * | * | * | **Comments**                                             |
| * | * | * | * |                                                          |
| P | T | A | D |                                                          |
| a | y | l | e |                                                          |
| r | p | l | f |                                                          |
| a | e | o | a |                                                          |
| m | * | w | u |                                                          |
| e | * | e | l |                                                          |
| t |   | d | t |                                                          |
| e |   | v | * |                                                          |
| r |   | a | * |                                                          |
| * |   | l |   |                                                          |
| * |   | u |   |                                                          |
|   |   | e |   |                                                          |
|   |   | s |   |                                                          |
|   |   | * |   |                                                          |
|   |   | * |   |                                                          |
+===+===+===+===+==========================================================+
| < | i | 0 | 1 | 0 – Enable less strict checking: no CRC checks on frame  |
| s | n | \ |   | headers no check for invalid last digit in MarkIV time   |
| t | t | | |   | stamp or time stamp consistency with data rate no        |
| r |   | 1 |   | consistency check between frame number+data rate and     |
| i |   |   |   | VLBA time stamp in Mark5B header 1 – Everything has to   |
| c |   |   |   | be good for data format to be detected                   |
| t |   |   |   |                                                          |
| > |   |   |   |                                                          |
+---+---+---+---+----------------------------------------------------------+
| < | i | > | 1 | Amount of data to find frames in. The default ~1MB may   |
| # | n | 0 | 0 | be too low for high data rate detection (>>2Gbps)        |
| b | t |   | 0 |                                                          |
| y |   |   | 0 |                                                          |
| t |   |   | 0 |                                                          |
| e |   |   | 0 |                                                          |
| s |   |   | 0 |                                                          |
| t |   |   |   |                                                          |
| o |   |   |   |                                                          |
| r |   |   |   |                                                          |
| e |   |   |   |                                                          |
| a |   |   |   |                                                          |
| d |   |   |   |                                                          |
| > |   |   |   |                                                          |
+---+---+---+---+----------------------------------------------------------+
| < | A |   |   | Non-optional argument: the name of the file to check;    |
| f | S |   |   | there is no ‘file_set=’ analogous to ‘scan_set=’         |
| i | C |   |   |                                                          |
| l | I |   |   |                                                          |
| e | I |   |   |                                                          |
| > |   |   |   |                                                          |
+---+---+---+---+----------------------------------------------------------+

Monitor-only parameters:

+-----+---+---------------+---------------------------------------------+
| *   | * | **Values**    | **Comments**                                |
| *Pa | * |               |                                             |
| ram | T |               |                                             |
| ete | y |               |                                             |
| r** | p |               |                                             |
|     | e |               |                                             |
|     | * |               |                                             |
|     | * |               |                                             |
+=====+===+===============+=============================================+
| <d  | c | tvg \| SS \|  | tvg – undecimated 32-bit-wide tvg data (see |
| ata | h | ? vdif        | Note 2) SS – raw StreamStor test pattern    |
| ty  | a | (legacy) \|   | data ‘st :’ – this extra field is inserted  |
| pe> | r | mark5b \| [st | if straight-through MarkIV or VLBA is       |
|     |   | :] mark4 \|   | detected                                    |
|     |   | vlba          |                                             |
+-----+---+---------------+---------------------------------------------+
| <n  | i |               | Number of tracks detected (‘?’ if VDIF)     |
| tra | n |               |                                             |
| ck> | t |               |                                             |
+-----+---+---------------+---------------------------------------------+
| <st | t |               | Time tag at first frame header in scan. See |
| art | i |               | Note 3.                                     |
| ti  | m |               |                                             |
| me> | e |               |                                             |
+-----+---+---------------+---------------------------------------------+
| <s  | t |               |                                             |
| can | i |               |                                             |
| l   | m |               |                                             |
| eng | e |               |                                             |
| th> |   |               |                                             |
+-----+---+---------------+---------------------------------------------+
| <to | r | Mbps          |                                             |
| tal | e |               |                                             |
| rec | a |               |                                             |
| ord | l |               |                                             |
| ing |   |               |                                             |
| ra  |   |               |                                             |
| te> |   |               |                                             |
+-----+---+---------------+---------------------------------------------+
| <#m | i | See Note 4    | Should always be =0 for normally recorded   |
| iss | n |               | data. >0 indicates #bytes that have been    |
| ing | t |               | dropped somewhere within scan <0 indicates  |
| byt |   |               | #bytes that have been added somewhere       |
| es> |   |               | within scan                                 |
+-----+---+---------------+---------------------------------------------+
| [<d | i |               | Parameter is only returned if the detected  |
| ata | n |               | format is simple VDIF and reports the VDIF  |
| ar  | t |               | data array length.                          |
| ray |   |               |                                             |
| siz |   |               |                                             |
| e>] |   |               |                                             |
+-----+---+---------------+---------------------------------------------+

Notes:

1. The ‘file_check’ query essentially executes a ‘data_check’ starting
   at the start-of-file, followed by a ‘data_check’ just prior to the
   end-of-file. This allows information about the selected file to be
   conveniently determined.
2. Only tvg data that were recorded with a bit-stream mask of 0xffffffff
   and no decimation will be recognized.
3. Regarding the <start time> value returned by the ‘data_check?’ and,
   ‘file_check?’ queries: The year and DOY reported in <start time>
   represent the most recent date consistent with the 3-digit <date
   code> in the frame header time tag (modulo 1000 value of Modified
   Julian Day as defined in VLBA tape-format header); this algorithm
   reports the proper year and DOY provided the data were taken no more
   than 1000 days ago.
4. The <#missing bytes> parameter is calculated as the difference the
   expected number of bytes between two samples of recorded data based
   on embedded time tags and the actual observed number of bytes between
   the same time tags. The reported number is the *total* number of
   bytes missing (or added) between the two sample points.

file2disk – Transfer data from file to Mark 5B
==============================================

Command syntax: file2disk = <source filename> : [<start byte#>] : [<end
byte#>] : [<scan label>] : [<<bit-stream mask> ; Command response:
!file2disk = <return code>;

Query syntax: file2disk? ;

Query response: !file2disk ? <return code> : <status> : <source
filename> : <start byte#> : <current byte#> : <end byte#> :

<scan#> : <scan label> : <bit-stream mask> ; Purpose: Initiate data
transfer from file to Mark 5 data disks

Settable parameters:

+---+---+---+-------+--------------------------------------------------+
| * | * | * | *     | **Comments**                                     |
| * | * | * | *Defa |                                                  |
| P | T | A | ult** |                                                  |
| a | y | l |       |                                                  |
| r | p | l |       |                                                  |
| a | e | o |       |                                                  |
| m | * | w |       |                                                  |
| e | * | e |       |                                                  |
| t |   | d |       |                                                  |
| e |   | v |       |                                                  |
| r |   | a |       |                                                  |
| * |   | l |       |                                                  |
| * |   | u |       |                                                  |
|   |   | e |       |                                                  |
|   |   | s |       |                                                  |
|   |   | * |       |                                                  |
|   |   | * |       |                                                  |
+===+===+===+=======+==================================================+
| < | A | n | ‘     | If not in standardized filename format (see      |
| s | S | o | save. | Section 6), must specify at least <scan label>   |
| o | C | s | data’ | and recommend specifying <bit-stream mask> as    |
| u | I | p | or    | well. See Note 1. Filename must include path if  |
| r | I | a | last  | not default (see Note 5).                        |
| c |   | c | value |                                                  |
| e |   | e |       |                                                  |
| f |   | s |       |                                                  |
| i |   | a |       |                                                  |
| l |   | l |       |                                                  |
| e |   | l |       |                                                  |
| n |   | o |       |                                                  |
| a |   | w |       |                                                  |
| m |   | e |       |                                                  |
| e |   | d |       |                                                  |
| > |   |   |       |                                                  |
+---+---+---+-------+--------------------------------------------------+
| < | i |   | 0     | Absolute byte number; if unspecified, assumed to |
| s | n |   |       | be zero                                          |
| t | t |   |       |                                                  |
| a |   |   |       |                                                  |
| r |   |   |       |                                                  |
| t |   |   |       |                                                  |
| b |   |   |       |                                                  |
| y |   |   |       |                                                  |
| t |   |   |       |                                                  |
| e |   |   |       |                                                  |
| # |   |   |       |                                                  |
| > |   |   |       |                                                  |
+---+---+---+-------+--------------------------------------------------+
| < | i |   | 0     | If =0, will copy to end of file                  |
| e | n |   |       |                                                  |
| n | t |   |       |                                                  |
| d |   |   |       |                                                  |
| b |   |   |       |                                                  |
| y |   |   |       |                                                  |
| t |   |   |       |                                                  |
| e |   |   |       |                                                  |
| # |   |   |       |                                                  |
| > |   |   |       |                                                  |
+---+---+---+-------+--------------------------------------------------+
| < | A | 6 | Extr  | Required if <source filename> is not in          |
| s | S | 4 | acted | standardized format (see Section 6). Example:    |
| c | C | c | from  | ‘exp53_ef_scan123’                               |
| a | I | h | <s    |                                                  |
| n | I | a | ource |                                                  |
| l |   | r | file  |                                                  |
| a |   | s | name> |                                                  |
| b |   | m |       |                                                  |
| e |   | a |       |                                                  |
| l |   | x |       |                                                  |
| > |   |   |       |                                                  |
+---+---+---+-------+--------------------------------------------------+
| < | h |   | 0     | Should be specified if <scan label> is           |
| b | e |   |       | specified. See Note 1.                           |
| i | x |   |       |                                                  |
| t |   |   |       |                                                  |
| - |   |   |       |                                                  |
| s |   |   |       |                                                  |
| t |   |   |       |                                                  |
| r |   |   |       |                                                  |
| e |   |   |       |                                                  |
| a |   |   |       |                                                  |
| m |   |   |       |                                                  |
| m |   |   |       |                                                  |
| a |   |   |       |                                                  |
| s |   |   |       |                                                  |
| k |   |   |       |                                                  |
| > |   |   |       |                                                  |
+---+---+---+-------+--------------------------------------------------+

Monitor-only parameters:

+---------------+------+--------------+-------------------------------+
| **Parameter** | **Ty | **Values**   | **Comments**                  |
|               | pe** |              |                               |
+===============+======+==============+===============================+
| <status>      | char | active \|    | Current status of transfer    |
|               |      | inactive     |                               |
+---------------+------+--------------+-------------------------------+
| <current      | int  |              | Current source byte# being    |
| byte#>        |      |              | transferred                   |
+---------------+------+--------------+-------------------------------+
| <scan#>       | int  |              | Sequential scan number on     |
|               |      |              | disk module                   |
+---------------+------+--------------+-------------------------------+
| <bit-stream   | hex  |              | Bit-stream mask               |
| mask>         |      |              |                               |
+---------------+------+--------------+-------------------------------+

Notes:

1. If <source filename> is in the standardized format for Mark 5B (see
   Section 6), *dimino* will parse the constituent fields to determine

   <experiment name>, <station code>, <scan name> and <bit-stream mask>.
   If source filename does not include a scan label in the proper
   format, <scan label> must be specified. If <scan label> is specified,
   it is recommended that <bit-stream mask> also be specified so that
   the Mark 5B directory entry can be properly completed.

2. The data in the source file must be in Mark 5B data format.

3. To abort data transfer: The ‘reset=abort’ command may be used to
   abort an active file2disk data transfer. See ‘reset’ command for
   details.

4. When <status> is ‘inactive’, a ‘file2disk?’ query returns <source
   filename> of the last transferred scan, if any.

5. Default path is the Linux default, which is the directory from which
   *dimino* or *Mark 5B* was started.

file2net – Transfer data from file on disk to network
=====================================================

Command syntax: file2net = connect : <target hostname> : <filename>;
file2net = on : [<start byte#>] : [<end byte#>] ; file2net = disconnect
;

Command response: !file2net = <return code>;

Query syntax: file2net? ;

Query response: !file2net ? <return code> : <status> : <target hostname>
: <start byte#> : <current byte#> : <end byte#> ; Purpose: Transfer data
between start byte and end byte from file to network.

Settable parameters:

+-----+---+-------+--------+-----------------------------------------+
| *   | * | **Al  | **Def  | **Comments**                            |
| *Pa | * | lowed | ault** |                                         |
| ram | T | val   |        |                                         |
| ete | y | ues** |        |                                         |
| r** | p |       |        |                                         |
|     | e |       |        |                                         |
|     | * |       |        |                                         |
|     | * |       |        |                                         |
+=====+===+=======+========+=========================================+
| <co | c | co    |        | ‘connect’ – connect to socket on        |
| ntr | h | nnect |        | receiving Mark 5 system ‘on’ – start    |
| ol> | a | \| on |        | data transfer ’disconnect’ – disconnect |
|     | r | \|    |        | socket See Notes.                       |
|     |   | disco |        |                                         |
|     |   | nnect |        |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <   | c |       | loc    | Required only on if                     |
| tar | h |       | alhost | <control>=‘connect’..                   |
| get | a |       | or     |                                         |
| hos | r |       | prev   |                                         |
| tna |   |       | iously |                                         |
| me> |   |       | set    |                                         |
|     |   |       | name   |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <st | [ |       | See    | Absolute byte# or relative offset; if   |
| art | + |       | Note 1 | null, defaults to start-scan pointer.   |
| byt | ] |       |        | See Note 1. See Note 10.                |
| e#> | i |       |        |                                         |
|     | n |       |        |                                         |
|     | t |       |        |                                         |
|     | \ |       |        |                                         |
|     | | |       |        |                                         |
|     | n |       |        |                                         |
|     | u |       |        |                                         |
|     | l |       |        |                                         |
|     | l |       |        |                                         |
+-----+---+-------+--------+-----------------------------------------+
| <   | i |       | See    | Absolute end byte#; if preceded by ‘+’, |
| end | n |       | Note 1 | increment from <start byte#> by         |
| byt | t |       |        | specified value; if null, defaults to   |
| e#> | \ |       |        | start-scan pointer. See Note 1.         |
|     | | |       |        |                                         |
|     | n |       |        |                                         |
|     | u |       |        |                                         |
|     | l |       |        |                                         |
|     | l |       |        |                                         |
+-----+---+-------+--------+-----------------------------------------+

Monitor-only parameters:

+-------------+-----+----------------------+--------------------------+
| **          | **  | **Values**           | **Comments**             |
| Parameter** | Typ |                      |                          |
|             | e** |                      |                          |
+=============+=====+======================+==========================+
| <status>    | c   | connected \| active  | Current status of        |
|             | har | \| inactive          | transfer                 |
+-------------+-----+----------------------+--------------------------+
| <target     | c   |                      |                          |
| hostname>   | har |                      |                          |
+-------------+-----+----------------------+--------------------------+
| <current    | int |                      | Current byte number      |
| byte#>      |     |                      | being transferred        |
+-------------+-----+----------------------+--------------------------+

Notes:

1. The ’<scan_set> command is a convenient way to set the <start byte#>
   and <stop byte#>.
2. If <start byte#> and <end byte#> are null, the scan defined by
   ‘scan_set’ will be transferred.
3. To set up connection: First, issue ‘open’ to the *receiving* system
   (‘net2disk=open’ or ‘net2out=open’ to Mark 5, or Net2file as
   standalone program; then issue ‘connect’ to the *sending* system
   (‘in2net=connect:..’ or ‘disk2net=connect:…’ to Mark 5).
4. To start data transfer: Issue ‘on’ to *sending* system (‘in2net=on’
   or ‘disk2net=on’ to Mark 5). A ‘disk2net’ transfer will stop
   automatically after the specified number of bytes are sent.
5. To stop data transfer: Issue ‘off’ to the sending system
   (‘in2net=off’ to Mark 5). After each transfer has been stopped or
   completed, another transfer may be initiated (see Note 2).

fill2net/file – Transfer fill pattern from host to network or file on disk
==========================================================================

Command syntax: fill2[net|file] = connect : <hostname \| filename> [ :
<start> : <inc> : <real-time> ] ; fill2[net|file] = on [ : <nword> ] ;

fill2[net|file] = disconnect ; Command response: !fill2[net|file] =
<return code>;

Query syntax: fill2[net|file]? ;

Query response: !fill2net ? <return code> : <status> : <hostname> :
<byte#> ;

!fill2file ? <return code> : <status> : <filename> ;

Purpose: Send dynamically generated synthetic data frames with valid
time stamps in the headers over the network or to a file.

Settable parameters:

+------+---+-------+--------+------------------------------------------+
| *    | * | **Al  | **Def  | **Comments**                             |
| *Par | * | lowed | ault** |                                          |
| amet | T | val   |        |                                          |
| er** | y | ues** |        |                                          |
|      | p |       |        |                                          |
|      | e |       |        |                                          |
|      | * |       |        |                                          |
|      | * |       |        |                                          |
+======+===+=======+========+==========================================+
| <    | c | co    |        | ‘connect’ – connect to socket on         |
| cont | h | nnect |        | receiving Mark 5 system ‘on’ – start     |
| rol> | a | \| on |        | data transfer ’disconnect’ – disconnect  |
|      | r | \|    |        | socket (cf. ’in2net = ’; see             |
|      |   | disco |        | documentation)                           |
|      |   | nnect |        |                                          |
+------+---+-------+--------+------------------------------------------+
| <    | c |       | loc    | Required only on if <control>=‘connect’. |
| host | h |       | alhost | Interpreted as <filename> for            |
| name | a |       | or     | ‘fill2file’, as <hostname> when          |
| \|   | r |       | prev   | ‘fill2net’ is used.                      |
| f    |   |       | iously |                                          |
| ilen |   |       | set    |                                          |
| ame> |   |       | name   |                                          |
+------+---+-------+--------+------------------------------------------+
| <st  | i |       | 0x11   | Fill pattern start value. Only valid if  |
| art> | n |       | 223344 | <control>=’connect’                      |
|      | t |       |        |                                          |
|      | \ |       |        |                                          |
|      | | |       |        |                                          |
|      | h |       |        |                                          |
|      | e |       |        |                                          |
|      | x |       |        |                                          |
+------+---+-------+--------+------------------------------------------+
| <    | i |       | 0      | Each frame’s fill pattern value will be  |
| inc> | n |       |        | ‘current_value + <inc>’. Only valid if   |
|      | t |       |        | <control>=’connect’                      |
+------+---+-------+--------+------------------------------------------+
| <re  | i |       | 0      | If non-0, real-time mode enabled, 0 to   |
| al-t | n |       |        | disable. Only valid if                   |
| ime> | t |       |        | <control>=’connect’. See Note 3.         |
+------+---+-------+--------+------------------------------------------+
| <nw  | i |       | 100000 | The number of 8-byte (!) words of fill   |
| ord> | n |       |        | pattern to generate. ‘-1’ is 264-1, or   |
|      | t |       |        | ~267 bytes (i.e. \infinite)                      |
+------+---+-------+--------+------------------------------------------+

Monitor-only parameters:

+---------+---+------------+-------------------------------------------+
| **Para  | * | **Values** | **Comments**                              |
| meter** | * |            |                                           |
|         | T |            |                                           |
|         | y |            |                                           |
|         | p |            |                                           |
|         | e |            |                                           |
|         | * |            |                                           |
|         | * |            |                                           |
+=========+===+============+===========================================+
| <       | c | connected  | Current status of transfer                |
| status> | h | \| active  |                                           |
|         | a | \|         |                                           |
|         | r | inactive   |                                           |
+---------+---+------------+-------------------------------------------+
| <h      | c |            | Current destination of the fill pattern,  |
| ostname | h |            | either a <filename> from ‘fill2file’ or   |
| \|      | a |            | <hostname> from ‘fill2net’                |
| fi      | r |            |                                           |
| lename> |   |            |                                           |
+---------+---+------------+-------------------------------------------+
| <byte#> | i |            | Amount of bytes generated so far          |
|         | n |            |                                           |
|         | t |            |                                           |
+---------+---+------------+-------------------------------------------+

Notes:

1. If a valid data format has been set using ‘mode=’, data frames of
   this format will be generated. The first time stamp will be frame
   number 0 in the current UTC second of the O/S. If no data format has
   been configured (‘mode = none ;’), then header-less blocks of data
   will be put on the network.
2. The data content of the frames/blocks can be a configurable constant
   or incrementing value for easy identification. Each time a new block
   (no format) or frame (valid data format) is generated, its contents
   will be filled with the current fillpattern value. After that, the
   fillpattern value will be modified by adding the value of <inc> to
   it. The fillpattern value is initialized with the value from <start>.
3. If <real-time> mode has been enabled, the sending system will try to
   generate data frames at a rate as close as it can to the configured
   data format’s data rate. If <real-time> is ‘0’ (disabled), the system
   will generate-and-send data as fast as the hardware will allow.

fill2disk/vbs – Record fill pattern on Mark5 disk pack or FlexBuff/Mark6 (*jive5ab* >= 2.8)
============================================================================================

Command syntax: fill2[disk|vbs] = <on/off> : <scan label> [ : <start> :
<inc> : <real-time> ] ; Command response: !fill2[disk|vbs] = <return
code>;

Query syntax: fill2[disk|vbs]? ;

Query response: !fill2[disk|vbs] ? <return code> : <status> : <scan
label> : <byte#> ; (*jive5ab* = 2.8.0)

!fill2[disk|vbs] ? <return code> : <status> : <scan number> : <scan
label> : <byte#> ; (*jive5ab* >= 2.8.1)

Purpose: Record dynamically generated synthetic data with valid time
stamps on Mark5 disk pack or FlexBuff/Mark6.

Settable parameters:

+--------+-----+----------+------+------------------------------------+
| *      | **  | *        | **D  | **Comments**                       |
| *Param | Typ | *Allowed | efau |                                    |
| eter** | e** | values** | lt** |                                    |
+========+=====+==========+======+====================================+
| <co    | c   | on \|    |      | ‘on’ – start recording ’off’ –     |
| ntrol> | har | off      |      | stop recording                     |
+--------+-----+----------+------+------------------------------------+
| <scan  | c   |          |      | A valid scan label                 |
| label> | har |          |      |                                    |
+--------+-----+----------+------+------------------------------------+
| <      | int |          | 0x   | Fill pattern start value           |
| start> | \|  |          | 1122 |                                    |
|        | hex |          | 3344 |                                    |
+--------+-----+----------+------+------------------------------------+
| <inc>  | int |          | 0    | Each frame’s fill pattern value    |
|        |     |          |      | will be ‘current_value + <inc>’    |
+--------+-----+----------+------+------------------------------------+
| <real  | int |          | 1    | If non-0, real-time mode enabled,  |
| -time> |     |          |      | 0 to disable. See Note 3.          |
+--------+-----+----------+------+------------------------------------+

Monitor-only parameters:

+-------+---+--------+-----------------------------------------------+
| **P   | * | **Va   | **Comments**                                  |
| arame | * | lues** |                                               |
| ter** | T |        |                                               |
|       | y |        |                                               |
|       | p |        |                                               |
|       | e |        |                                               |
|       | * |        |                                               |
|       | * |        |                                               |
+=======+===+========+===============================================+
| <st   | c | active | Current status of recording                   |
| atus> | h | \|     |                                               |
|       | a | in     |                                               |
|       | r | active |                                               |
+-------+---+--------+-----------------------------------------------+
| <scan | i |        | Current scan number. Added in *jive5ab* 2.8.1 |
| nu    | n |        | to comply with standard Mark5 record? reply   |
| mber> | t |        | format                                        |
+-------+---+--------+-----------------------------------------------+
| <scan | c |        | Current scan name, only returned if <status>  |
| l     | h |        | == active                                     |
| abel> | a |        |                                               |
|       | r |        |                                               |
+-------+---+--------+-----------------------------------------------+
| <b    | i |        | Amount of bytes generated so far              |
| yte#> | n |        |                                               |
|       | t |        |                                               |
+-------+---+--------+-----------------------------------------------+

Notes:

1. fill2disk/fill2vbs behave like record=on/record=off with no physical
   data source required. Combined with tstat? this can be used to do
   performance measuring or just generating some data to test
   scan_check? or any other data related operation.
2. If a valid data format has been set using ‘mode=’, data frames of
   this format will be generated. The first time stamp will be frame
   number 0 in the current UTC second of the O/S. If no data format has
   been configured (‘mode = none ;’), then header-less blocks of data
   will be put on the network. **Note: fill2vbs requires a valid data
   format and will refuse to record if none has been set.**
3. The data content of the frames/blocks can be a configurable constant
   or incrementing value for easy identification. Each time a new block
   (no format) or frame (valid data format) is generated, its contents
   will be filled with the current fillpattern value. After that, the
   fillpattern value will be modified by adding the value of <inc> to
   it. The fillpattern value is initialized with the value from <start>.
4. If <real-time> mode has been enabled, the sending system will try to
   generate data frames at a rate as close as it can to the configured
   data format’s data rate. If <real-time> is ‘0’ (disabled), the system
   will generate-and-send data as fast as the hardware will allow.

get_stats – Get disk performance statistics (query only)
========================================================

Query syntax: get_stats? ;

Query response: !get_stats ? <return code> : < drive number> : <bin 0
count> : <bin 1 count> :….: <bin 7 count> :

<replaced-block count> : [STRIKEOUT:<SMART status>] ;

Purpose: Get detailed performance statistics on individual Mark 5 data
disks Monitor-only parameters:

+------+---+---+---------------------------------------------------------+
| *    | * | * | **Comments**                                            |
| *Par | * | * |                                                         |
| amet | T | V |                                                         |
| er** | y | a |                                                         |
|      | p | l |                                                         |
|      | e | u |                                                         |
|      | * | e |                                                         |
|      | * | s |                                                         |
|      |   | * |                                                         |
|      |   | * |                                                         |
+======+===+===+=========================================================+
| <d   | i |   | 0=0M, 1=0S, 2=1M, 3=1S,….,14=7M, 15=7S                  |
| rive | n |   |                                                         |
| num  | t |   |                                                         |
| ber> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 0 (see  |
| 0    | n |   | ‘start_stats’ command for explanation)                  |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 1       |
| 1    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 2       |
| 2    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 3       |
| 3    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 4       |
| 4    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 5       |
| 5    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 6       |
| 6    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <bin | i |   | Number of drive transactions falling in its bin 7       |
| 7    | n |   |                                                         |
| co   | t |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| <re  | i |   | Number of 65KB (actually 0xFFF8 bytes) data blocks      |
| plac | n |   | unavailable on readback from this drive; these blocks   |
| ed-b | t |   | have been replaced with fill pattern with even parity.  |
| lock |   |   | See ‘replaced_blks?’ query for more information.        |
| co   |   |   |                                                         |
| unt> |   |   |                                                         |
+------+---+---+---------------------------------------------------------+
| [    | [ |   | jive5ab does **NOT** return this field. See Note 6.     |
| STRI | S |   |                                                         |
| KEOU | T |   |                                                         |
| T:<S | R |   |                                                         |
| MART | I |   |                                                         |
| stat | K |   |                                                         |
| us>] | E |   |                                                         |
|      | O |   |                                                         |
|      | U |   |                                                         |
|      | T |   |                                                         |
|      | : |   |                                                         |
|      | c |   |                                                         |
|      | h |   |                                                         |
|      | a |   |                                                         |
|      | r |   |                                                         |
|      | ] |   |                                                         |
+------+---+---+---------------------------------------------------------+

Notes:

1. Each subsequent ‘get_stats’ query returns current performance
   statistics for the next mounted drive; recycles through mounted
   drives. Bin counts are not cleared. See details in Notes on
   ‘start_stats’ command.
2. The ‘get_stats’ query may not be issued during active recording or
   readback.
3. Drive statistics and replaced-block counts are cleared and re-started
   whenever a new disk module is mounted or a ‘start_stats’ command is
   issued.
4. The 8 bin counts in the 8 bins correspond to drive-response
   (transaction completion) times, with response time increasing from
   left to right. A good disk will have large numbers in bins 0 and 1
   and small numbers (or 0) in the last few bins. See ’start_stats for
   additional information.
5. When operating in ‘nb’ mode, disks in banks A and B are treated as a
   single module.
6. DIMino returns the SMART status of the disk (OK, Fault) if it is
   SMART capable or the string NotSMART in case the drive is, well, not
   SMART capable. This field is not mentioned in the MIT Haystack Mark5B
   command set v1.12 after which jive5ab was modelled. It was chosen to
   stick to the documentation and not return an undocumented field.

group_def – define/inspect aliases for set(s) of disks
======================================================

Command syntax: group_def = <control> : <GRP> [ : <pattern> [ :
<pattern> ] ] ;

Command response: !group_def = <return code>;

Query syntax: group_def? [ <GRP> ] ;

Query response: !group_def ? 0 : <GRP> [ : <GRP>\* ] ;

!group_def ? 0 : <pattern> [ : <pattern>\* ] ;

Purpose: Manage or query aliases of groups of patterns for disks (a
“group definition”).

Settable parameters:

+----+---+------+---+-------------------------------------------------+
| *  | * | *    | * | **Comments**                                    |
| *P | * | *All | * |                                                 |
| ar | T | owed | D |                                                 |
| am | y | valu | e |                                                 |
| et | p | es** | f |                                                 |
| er | e |      | a |                                                 |
| ** | * |      | u |                                                 |
|    | * |      | l |                                                 |
|    |   |      | t |                                                 |
|    |   |      | * |                                                 |
|    |   |      | * |                                                 |
+====+===+======+===+=================================================+
| <  | c | de   |   | ‘define’ – add or replace a definition of the   |
| co | h | fine |   | alias <GRP> to be the list of <pattern>s given  |
| nt | a | de   |   | ‘delete’ – remove the alias named <GRP>         |
| ro | r | lete |   |                                                 |
| l> |   |      |   |                                                 |
+----+---+------+---+-------------------------------------------------+
| <  | c |      |   | Name of the alias to define or delete           |
| GR | h |      |   |                                                 |
| P> | a |      |   |                                                 |
|    | r |      |   |                                                 |
+----+---+------+---+-------------------------------------------------+
| <  | c |      |   | Pattern (including shell wild cards) or other   |
| pa | h |      |   | alias – see ‘set_disks’ for details             |
| tt | a |      |   |                                                 |
| er | r |      |   |                                                 |
| n> |   |      |   |                                                 |
+----+---+------+---+-------------------------------------------------+

Monitor-only parameters:

+-----------+-------+--------+----------------------------------------+
| **Pa      | **T   | **Va   | **Comments**                           |
| rameter** | ype** | lues** |                                        |
+===========+=======+========+========================================+
| <GRP>     | char  |        | Optional name of group to list the     |
|           |       |        | definition for                         |
+-----------+-------+--------+----------------------------------------+

Notes:

1. Using ‘group_def=’ it is possible to define an alias for a (sub)set
of directories/disks. This alias can subsequently be used in
‘set_disks=’ to (quickly) select the indicated (sub)set of disks. The
pattern(s) associated with an alias/group are resolved recursively and
only expanded when used in a ‘set_disks=’ command. Since the
‘set_disks=’ command re-evaluates the actual mount points, the group
definition pattern(s) are matched against disks mounted *at that time*.

in2file – Transfer data directly from Mark 5 input to file on disk
==================================================================

Command syntax: in2file = <control> [ : <file name>,<file option> ];
Command response: !in2file = <return code> ;

Query syntax: in2file? ;

Query response: !in2file ? <return code> : <last file name> : <status> [
: <#bytes written> : <transfer sub status> ] ; Purpose: Control direct
data transfer from Mark 5 input to file on disk; bypass (Mark5) disks

Settable parameters:

+---+---+--------+---+--------------------------------------------------+
| * | * | **A    | * | **Comments**                                     |
| * | * | llowed | * |                                                  |
| P | T | va     | D |                                                  |
| a | y | lues** | e |                                                  |
| r | p |        | f |                                                  |
| a | e |        | a |                                                  |
| m | * |        | u |                                                  |
| e | * |        | l |                                                  |
| t |   |        | t |                                                  |
| e |   |        | * |                                                  |
| r |   |        | * |                                                  |
| * |   |        |   |                                                  |
| * |   |        |   |                                                  |
+===+===+========+===+==================================================+
| < | c | c      |   | ‘connect’ – connect to socket on receiving Mark  |
| c | h | onnect |   | 5 system; initially, data transfer is off. ‘on’  |
| o | a | \| on  |   | – start/resume data transfer ’off’ – pause data  |
| n | r | \| off |   | transfer ’disconnect’ – stop transfer, close     |
| t |   | \|     |   | file                                             |
| r |   | disc   |   |                                                  |
| o |   | onnect |   |                                                  |
| l |   |        |   |                                                  |
| > |   |        |   |                                                  |
+---+---+--------+---+--------------------------------------------------+
| < | c |        |   | Required only on first ‘connect’; otherwise      |
| f | h |        |   | ignored                                          |
| i | a |        |   |                                                  |
| l | r |        |   |                                                  |
| e |   |        |   |                                                  |
| n |   |        |   |                                                  |
| a |   |        |   |                                                  |
| m |   |        |   |                                                  |
| e |   |        |   |                                                  |
| > |   |        |   |                                                  |
+---+---+--------+---+--------------------------------------------------+
| < | c | n \| w |   | Create new file (‘n’), overwrite potential       |
| f | h | \| a   |   | existing file (‘w’) or append to potential       |
| i | a |        |   | existing file (‘a’). See Note 1.                 |
| l | r |        |   |                                                  |
| e |   |        |   |                                                  |
| o |   |        |   |                                                  |
| p |   |        |   |                                                  |
| t |   |        |   |                                                  |
| i |   |        |   |                                                  |
| o |   |        |   |                                                  |
| n |   |        |   |                                                  |
| > |   |        |   |                                                  |
+---+---+--------+---+--------------------------------------------------+

Monitor-only parameters:

+----------+---+-------+----------------------------------------------+
| **Par    | * | **Val | **Comments**                                 |
| ameter** | * | ues** |                                              |
|          | T |       |                                              |
|          | y |       |                                              |
|          | p |       |                                              |
|          | e |       |                                              |
|          | * |       |                                              |
|          | * |       |                                              |
+==========+===+=======+==============================================+
| <last    | c |       | File name given at last succesful ‘in2file = |
| file     | h |       | connect : ..’                                |
| name>    | a |       |                                              |
|          | r |       |                                              |
+----------+---+-------+----------------------------------------------+
| <status> | c | a     |                                              |
|          | h | ctive |                                              |
|          | a | \|    |                                              |
|          | r | ina   |                                              |
|          |   | ctive |                                              |
+----------+---+-------+----------------------------------------------+
| <#bytes  | i |       | Number of bytes written to the file since    |
| written> | n |       | ‘in2file = on ;’. Returned if <status> =     |
|          | t |       | ‘active’                                     |
+----------+---+-------+----------------------------------------------+
| <        | c |       | Current sub state, ‘WAIT’, ‘RUN’,            |
| transfer | h |       | ‘CONNECTED’ or a combination thereof.        |
| sub      | a |       | Returned if <status> = ‘active’              |
| status>  | r |       |                                              |
+----------+---+-------+----------------------------------------------+

Notes:

1. In contrast to Mark5A/DIMino, *jive5ab* sometimes decided to
   aggretate/encode the file-open option (‘n’, ‘w’ or ‘a’) in the file
   name field. This is done by suffixing the file name with a
   construction “**,<file-open option>**”. This was e.g. necessary for
   transfers where two files were included and for transfer modes not
   supported by Mark5A/DIMino. In transfers where Mark5A/DIMino
   documentation decides that the file-open mode character is a separate
   field, *jive5ab* follows this.
2. This transfer is mostly useful for debugging; to write a short
   section of raw telescope data to a file on disk. Unless the hard
   disks underlying the file storage are fast enough, ‘bad things’ might
   happen, see Note 3.
3. If the data rate is too fast for the file system to handle, the
   StreamStor FIFO will eventually overflow. An overflowing FIFO crashes
   the StreamStor firmware. *jive5ab* will **actively discard** data to
   keep the FIFO fill level under 60% in order to keep the firmware from
   crashing. If data is discarded, *jive5ab* will report on the terminal
   how much.

in2fork – Duplicate data from Mark 5 input to Mark 5 disks and network
======================================================================

Command syntax: in2fork = <control> [ : <host> : <scan label> ] ;
Command response: !in2fork = <return code> ;

Query syntax: in2fork ? ;

Query response: !in2fork ? <return code> : <last host>”f” : <status> [ :
<#bytes written> : <transfer sub status> ] ; Purpose: Control data
transfer that duplicates Mark 5 input to Mark5 disks and a remote
network destination

Settable parameters:

+---+---+-------+---+-----------------------------------------------------+
| * | * | **Al  | * | **Comments**                                        |
| * | * | lowed | * |                                                     |
| P | T | val   | D |                                                     |
| a | y | ues** | e |                                                     |
| r | p |       | f |                                                     |
| a | e |       | a |                                                     |
| m | * |       | u |                                                     |
| e | * |       | l |                                                     |
| t |   |       | t |                                                     |
| e |   |       | * |                                                     |
| r |   |       | * |                                                     |
| * |   |       |   |                                                     |
| * |   |       |   |                                                     |
+===+===+=======+===+=====================================================+
| < | c | co    |   | ‘connect’ – connect to socket on receiving Mark 5   |
| c | h | nnect |   | system, create new scan on Mark 5 disks; data       |
| o | a | \| on |   | transfer is off. ‘on’ – start/resume data transfer  |
| n | r | \|    |   | ’off’ – pause data transfer ’disconnect’ – stop     |
| t |   | off   |   | transfer, close file and scan on Mark 5 disks       |
| r |   | \|    |   |                                                     |
| o |   | disco |   |                                                     |
| l |   | nnect |   |                                                     |
| > |   |       |   |                                                     |
+---+---+-------+---+-----------------------------------------------------+
| < | c |       |   | Name or IPv4 address of the receiving system.       |
| h | h |       |   | Required only on first ‘connect’, ignored otherwise |
| o | a |       |   |                                                     |
| s | r |       |   |                                                     |
| t |   |       |   |                                                     |
| > |   |       |   |                                                     |
+---+---+-------+---+-----------------------------------------------------+
| < | c |       |   | Required only on first ‘connect’, ignored otherwise |
| s | h |       |   |                                                     |
| c | a |       |   |                                                     |
| a | r |       |   |                                                     |
| n |   |       |   |                                                     |
| l |   |       |   |                                                     |
| a |   |       |   |                                                     |
| b |   |       |   |                                                     |
| e |   |       |   |                                                     |
| l |   |       |   |                                                     |
| > |   |       |   |                                                     |
+---+---+-------+---+-----------------------------------------------------+

Monitor-only parameters:

+-------+---+-----+-----------------------------------------------------+
| **P   | * | *   | **Comments**                                        |
| arame | * | *Va |                                                     |
| ter** | T | lue |                                                     |
|       | y | s** |                                                     |
|       | p |     |                                                     |
|       | e |     |                                                     |
|       | * |     |                                                     |
|       | * |     |                                                     |
+=======+===+=====+=====================================================+
| <last | c |     | Host name given at last succesful ‘in2fork =        |
| host> | h |     | connect : ..’ or ‘in2net = connect : ..’. The       |
|       | a |     | suffix “f” is added to indicate that this was       |
|       | r |     | ‘in2fork’ rather than ‘in2net’                      |
+-------+---+-----+-----------------------------------------------------+
| <st   | c | act |                                                     |
| atus> | h | ive |                                                     |
|       | a | \|  |                                                     |
|       | r | in  |                                                     |
|       |   | act |                                                     |
|       |   | ive |                                                     |
+-------+---+-----+-----------------------------------------------------+
| <#    | i |     | Number of bytes written to the file since ‘in2fork  |
| bytes | n |     | = on ;’. Returned if <status> = ‘active’            |
| wri   | t |     |                                                     |
| tten> |   |     |                                                     |
+-------+---+-----+-----------------------------------------------------+
| <tra  | c |     | Current sub state, ‘WAIT’, ‘RUN’, ‘CONNECTED’ or a  |
| nsfer | h |     | combination thereof. Returned if <status> =         |
| sub   | a |     | ‘active’                                            |
| st    | r |     |                                                     |
| atus> |   |     |                                                     |
+-------+---+-----+-----------------------------------------------------+

Notes:

1. Older StreamStor cards (notably the V100 and VXF2) are not capable to
   perform this duplication at data rates > 512Mbps. The firmware will
   corrupt the recording on disk when used above this data rate.
   \ **This directly called ‘in2fork’ transfer does not protect against
   data corruption.**\  If *jive5ab* is run in buffering mode (see
   command line arguments, Section **2**) it *does* prevent data
   corruption upon ‘record=on’ by automatically disabling the
   duplication if the data rate is too high for the detected
   hardware.\ **This transfer does not do that!**\ 
2. If the data rate is too fast for the network to handle, the
   StreamStor FIFO will eventually overflow. An overflowing FIFO crashes
   the StreamStor firmware. *jive5ab* will **actively discard** data to
   keep the FIFO fill level under 60% in order to keep the firmware from
   crashing. If data is discarded, *jive5ab* will report on the terminal
   how much.

in2mem – Transfer data directly from Mark 5 input to *jive5ab* internal buffer
==============================================================================

Command syntax: in2mem = <control> ; Command response: !in2mem = <return
code> ;

Query syntax: in2mem? ;

Query response: !in2mem ? <return code> : <status> : <#bytes read> ;

Purpose: Control direct data transfer from Mark 5 input to *jive5ab*
internal buffer; bypass disks

Settable parameters:

+-----+---+--------+----+--------------------------------------------+
| *   | * | **A    | *  | **Comments**                               |
| *Pa | * | llowed | *D |                                            |
| ram | T | va     | ef |                                            |
| ete | y | lues** | au |                                            |
| r** | p |        | lt |                                            |
|     | e |        | ** |                                            |
|     | * |        |    |                                            |
|     | * |        |    |                                            |
+=====+===+========+====+============================================+
| <co | c | on \|  |    | ‘on’ – start copying data from I/O board   |
| ntr | h | off    |    | into circular buffer in memory ’off’ – end |
| ol> | a |        |    | data transfer                              |
|     | r |        |    |                                            |
+-----+---+--------+----+--------------------------------------------+

Monitor-only parameters:

+--------+----+---------+---------------------------------------------+
| *      | ** | **V     | **Comments**                                |
| *Param | Ty | alues** |                                             |
| eter** | pe |         |                                             |
|        | ** |         |                                             |
+========+====+=========+=============================================+
| <s     | ch | i       |                                             |
| tatus> | ar | nactive |                                             |
|        |    | \|      |                                             |
|        |    | active  |                                             |
+--------+----+---------+---------------------------------------------+
| <      | i  |         | #bytes copied since the start of this       |
| #bytes | nt |         | transfer. Only returned if <status> =       |
| read>  |    |         | ‘active’                                    |
+--------+----+---------+---------------------------------------------+

Notes:

1. If data is written to the shared memory circular buffer in *jive5ab*,
   this data can be picked up from a different runtime for further
   processing, see e.g. ‘mem2net = .. ;’, ’mem2file = ..; ‘
2. The shared memory buffer can be read by multiple ‘mem2\*’ transfers
   at the same time. Depending on the input data rate (‘recording’ data
   rate), the number of parallel ‘mem2\*’ transfers and the system’s
   resources (CPU, memory, disk) data may or may not be lost.

in2memfork – Duplicate data from Mark 5 input to Mark 5 disks and *jive5ab* internal buffer
===========================================================================================

Command syntax: in2memfork = <control> [ : <scan label> ] ; Command
response: !in2memfork = <return code> ;

Query syntax: in2memfork ? ;

Query response: !in2memfork ? <return code> : <status> [ : <#bytes
written> : <transfer sub status> ] ; Purpose: Control data transfer that
duplicates Mark 5 input to Mark5 disks and *jive5ab* internal buffer

Settable parameters:

+------+---+--------+----+--------------------------------------------+
| *    | * | **A    | *  | **Comments**                               |
| *Par | * | llowed | *D |                                            |
| amet | T | va     | ef |                                            |
| er** | y | lues** | au |                                            |
|      | p |        | lt |                                            |
|      | e |        | ** |                                            |
|      | * |        |    |                                            |
|      | * |        |    |                                            |
+======+===+========+====+============================================+
| <    | c | on \|  |    | ‘on’ – create scan on Mark 5 disks and     |
| cont | h | off    |    | start data transfer immediately ’off’ –    |
| rol> | a |        |    | stop data transfer                         |
|      | r |        |    |                                            |
+------+---+--------+----+--------------------------------------------+
| <    | c |        |    | Required only on first ‘connect’;          |
| scan | h |        |    | otherwise ignored                          |
| la   | a |        |    |                                            |
| bel> | r |        |    |                                            |
+------+---+--------+----+--------------------------------------------+

Monitor-only parameters:

+----------+---+-------+----------------------------------------------+
| **Par    | * | **Val | **Comments**                                 |
| ameter** | * | ues** |                                              |
|          | T |       |                                              |
|          | y |       |                                              |
|          | p |       |                                              |
|          | e |       |                                              |
|          | * |       |                                              |
|          | * |       |                                              |
+==========+===+=======+==============================================+
| <status> | c | a     |                                              |
|          | h | ctive |                                              |
|          | a | \|    |                                              |
|          | r | ina   |                                              |
|          |   | ctive |                                              |
+----------+---+-------+----------------------------------------------+
| <#bytes  | i |       | Number of bytes written to the file since    |
| written> | n |       | ‘in2file = on ;’. Returned if <status> =     |
|          | t |       | ‘active’                                     |
+----------+---+-------+----------------------------------------------+
| <        | c |       | Current sub state, ‘WAIT’, ‘RUN’,            |
| transfer | h |       | ‘CONNECTED’ or a combination thereof.        |
| sub      | a |       | Returned if <status> = ‘active’              |
| status>  | r |       |                                              |
+----------+---+-------+----------------------------------------------+

Notes:

1. This transfer underlies the *jive5ab* buffering mode (see command
   line options, section **2**) but is **most definitely not** it. This
   transfer does **NOT** prevent against data corruption if used with
   too high a data rate for the system’s StreamStor card. **Do read the
   notes for ‘in2fork’.**\ 

2. If the data rate is too high for the system to handle, the StreamStor
   FIFO will eventually overflow. An overflowing FIFO crashes the
   StreamStor firmware. *jive5ab* will **actively discard** data to keep
   the FIFO fill level under 60% in order to keep the firmware from
   crashing. If data is discarded, *jive5ab* will report on the terminal
   how much.

in2net – Transfer data directly from Mark 5 input to network
============================================================

Command syntax: in2net = <control> : <remote hostname> ; Command
response: !in2net = <return code> ;

Query syntax: in2net? ;

Query response: !in2net ? <return code> : <status> : <remote hostname> :
<#bytes received> : <#bytes in buffer> ; Purpose: Control direct data
transfer from Mark 5 input to network; bypass disks

Settable parameters:

+----+---+--------+---+--------------------------------------------------+
| *  | * | **A    | * | **Comments**                                     |
| *P | * | llowed | * |                                                  |
| ar | T | va     | D |                                                  |
| am | y | lues** | e |                                                  |
| et | p |        | f |                                                  |
| er | e |        | a |                                                  |
| ** | * |        | u |                                                  |
|    | * |        | l |                                                  |
|    |   |        | t |                                                  |
|    |   |        | * |                                                  |
|    |   |        | * |                                                  |
+====+===+========+===+==================================================+
| <  | c | c      |   | ‘connect’ – connect to socket on receiving Mark  |
| co | h | onnect |   | 5 system; initially, data transfer is off. ‘on’  |
| nt | a | \| on  |   | – start data transfer ’off’ – end data transfer  |
| ro | r | \| off |   | ’disconnect’ – disconnect socket See Notes with  |
| l> |   | \|     |   | ‘disk2net’                                       |
|    |   | disc   |   |                                                  |
|    |   | onnect |   |                                                  |
+----+---+--------+---+--------------------------------------------------+
| <  | c |        | l | Required only on first ‘connect’; otherwise      |
| re | h |        | o | ignored                                          |
| mo | a |        | c |                                                  |
| te | r |        | a |                                                  |
| h  |   |        | l |                                                  |
| os |   |        | h |                                                  |
| tn |   |        | o |                                                  |
| am |   |        | s |                                                  |
| e> |   |        | t |                                                  |
+----+---+--------+---+--------------------------------------------------+

Monitor-only parameters:

+----------+----+----------------+-------------------------------------+
| **Par    | ** | **Values**     | **Comments**                        |
| ameter** | Ty |                |                                     |
|          | pe |                |                                     |
|          | ** |                |                                     |
+==========+====+================+=====================================+
| <status> | ch | inactive \|    |                                     |
|          | ar | connected \|   |                                     |
|          |    | sending        |                                     |
+----------+----+----------------+-------------------------------------+
| <remote  |    |                |                                     |
| h        |    |                |                                     |
| ostname> |    |                |                                     |
+----------+----+----------------+-------------------------------------+
| <#bytes  | i  |                | #bytes received at the Input since  |
| r        | nt |                | ‘connect’ and while status is       |
| eceived> |    |                | ‘sending’                           |
+----------+----+----------------+-------------------------------------+
| <#bytes  | i  |                | #bytes remaining in buffer, waiting |
| in       | nt |                | to be sent                          |
| buffer>  |    |                |                                     |
+----------+----+----------------+-------------------------------------+

Notes:

1. [STRIKEOUT:Important: Due to current software problem, a scratch disk
   is required in Bank A for in2net operation; will be fixed in a future
   update.]\ \ *jive5ab* does not have this limitation; in2net can be
   run without any diskpacks present.

2. See Notes with ‘disk2net’ command for usage rules and restrictions.

3. If the data rate is too fast for the network to handle, the FIFO will
   eventually overflow; this will be reported by either a ‘status?’
   query or an ‘in2net?’ query with an error message. An overflowing
   FIFO crashes the Streamstor firmware. *jive5ab* will data to keep the
   FIFO fill level under 60% in order to keep the firmware from
   crashing. If data is discarded, *jive5ab* will report on the terminal
   how much.

4. After ‘in2net=off’, but before ‘in2net=disconnect’, <#bytes received>
   shows the approximate total #bytes transferred from the input source;
   the #bytes currently sent out through the network is ~<#bytes
   received> minus <#bytes in buffer>. As <#bytes in buffer> drains to
   zero (as remaining data is sent out over the network), <#bytes
   received> becomes somewhat more precise.

5. If ‘in2net=disconnect’ is issued while <#bytes in buffer> is >0, data
   will be lost.

6. [STRIKEOUT:For operation in special disk-FIFO mode, see Section
   7.]\ \ *jive5ab* has no disk FIFO mode but has the *fork* transfer,
   see Section 4.

7. Note that the network protocol parameters are set by the
   ‘net_protocol’ command.

ipd – Set packet spacing (inter-packet delay)
=============================================

Command syntax: ipd = <packet spacing> ; Command response: !ipd =
<return code> ;

Query syntax: ipd? ;

Query response: !ipd ? <return code> : <packet spacing> ; Purpose: Set
packet spacing for UDP-based transfers

Settable parameters:

+----+---+----+---+----------------------------------------------------+
| *  | * | *  | * | **Comments**                                       |
| *P | * | *A | * |                                                    |
| ar | T | ll | D |                                                    |
| am | y | ow | e |                                                    |
| et | p | ed | f |                                                    |
| er | e | va | a |                                                    |
| ** | * | lu | u |                                                    |
|    | * | es | l |                                                    |
|    |   | ** | t |                                                    |
|    |   |    | * |                                                    |
|    |   |    | * |                                                    |
+====+===+====+===+====================================================+
| <  | i | >= | 0 | packet spacing in units of microseconds (no unit)  |
| pa | n | -1 |   | or micro/nanoseconds if suffixed by the unit       |
| ck | t | [  |   | **us** or **ns**. Support for the **us** and       |
| et |   | ns |   | **ns** suffixes appeared in *jive5ab* 2.5.0.       |
| sp |   | \| |   |                                                    |
| ac |   | u  |   |                                                    |
| in |   | s] |   |                                                    |
| g> |   |    |   |                                                    |
+----+---+----+---+----------------------------------------------------+

Monitor-only parameters:

+------------+------+--------------+-----------------------------------+
| **P        | **Ty | **Values**   | **Comments**                      |
| arameter** | pe** |              |                                   |
+============+======+==============+===================================+
| <packet    | i    | current      | Always returned in units of       |
| spacing>   | nt/f | packet       | microseconds. See Note 7.         |
|            | loat | spacing      |                                   |
+------------+------+--------------+-----------------------------------+

Notes:

1. When using UDP/IPv4 based transfers typically it is important to
   rate-control the sender in order not to incur excessive packet loss.
   Packet loss may originate from overwriting the socket buffer in the
   sender if packets are ingested at too high a rate. A typical case is
   when the data source bandwidth >> network bandwidth, as is sometimes
   with file transfers; data can be read from file faster than
   transferred over the network. Secondly, long-haul networks tend to
   respond badly to bursty traffic, especially if there are sections in
   the end-to-end link which have a different nominal bandwidth. It is
   not uncommon to have a trans-continental point-to-point connection
   which is built up of sections of 10Gbps, 2.5Gbps and/or other
   bandwidths. In general the best results with UDP/IPv4 based transfers
   is if the sender injects packets at a steady rate. *jive5ab* supports
   control over this parameter via this command: packets will be
   injected into the UDP transfer every <packet spacing> amount of
   microseconds.
2. The special setting “-1” means “automatic”, or “theoretical” mode.
   From the current data format and track bit rate the total data rate
   is computed. Together with the configured MTU value this yields an
   optimal, theoretical, value for the packet spacing.
3. A value of “0” means that the packets are sent back-to-back.
4. This setting does not apply to the UDT protocol, even though it runs
   over UDP. In *jive5ab* >= 2.6.0, transfers supporting the UDT
   protocol should honour this value to implement rate limiting.
5. To give an idea: for 1Gbps links ipd \approx 60-70us, at 4Gbps ipd \approx 17us.
6. *jive5ab* implements this packet scheduling by busy-waiting.
   Depending on the amount of CPUs (notably the discrimination between
   >1 CPU or not) and the load on your system, the optimal ipd value for
   your system might differ from the auto/theoretical value. Some
   experimentation with this parameter may be necessary to find it.
7. Since *jive5ab* 2.5.0 support for ipd’s of less than 1us is
   implemented. Upon query, if ipd < 1us, a float is returned in stead
   of an integer value. E.g.“0.4” would be returned for and ipd that was
   set to 400ns.

JIVE5AB COMMAND SET 36

layout – Get current User Directory format (query only)
=======================================================

Query syntax: layout? ;

Query response: !layout ? <return code> : <UD layout> ;

Purpose: Get exact details of the layout of the User Directory on Mark5
disk pack. This command appeared in *jive5ab* 2.5.0 Monitor-only
parameters:

+---------+-----+-------+---------------------------------------------+
| **Para  | **  | **Val | **Comments**                                |
| meter** | Typ | ues** |                                             |
|         | e** |       |                                             |
+=========+=====+=======+=============================================+
| <UD     | c   |       | Canonical *jive5ab* User Directory format   |
| layout> | har |       | identifier. See Notes.                      |
+---------+-----+-------+---------------------------------------------+

Notes:

1. The format of the User Directory (‘Scan Directory’) as written on the
   Mark5 disk packs has seen a number of versions through the years;
   some of them dependant on actual Conduant SDK version. *jive5ab*
   supports all layouts on all platforms for read back
   (e.g. ‘scan_set=’). Due to StreamStor firmware limitations not all
   disk packs can be appended to on all other systems. One notable
   example is that it is impossible to append to an SDK9-formatted disk
   pack on an SDK8-based Mark5. It is therefore of paramount importance
   to erase a disk pack on the actual system that will be recorded with.

2. Each time a new disk pack is mounted and detected (an activated disk
   pack at startup of *jive5ab* also counts as ‘new mount’) the layout
   foundon that disk pack is reported on *jive5ab*\ ’s terminal. This
   command is a convenient way of retrieving this information remotely.

3. The canonical *jive5ab* User Directory format identifier is a human-
   as well as machine readable string. Examples are:

   Mark5A8DisksSDK8, Mark5B16DisksSDK9. Please refer to Mark5 Memo
   #100 [21]_ for intricate details plus full description.

mode – Set data recording/playback mode (Mark5A, A+)
====================================================

Command syntax: mode = <data mode> : <data submode> : [<output data
mode> : <output submode>] ; Command response: !mode = <return code> ;

Query syntax: mode? ;

Query response: !mode ? <return code> : <data mode> : <data submode> :
<output mode> : <output submode> :

<sync status> : <#sync attempts> ;

Purpose: Set the recording and playback mode of the Mark 5 I/O card or
*jive5ab’s* internal data format.

Settable parameters:

+---+---+------+----+---------------------------------------------------+
| * | * | *    | *  | **Comments**                                      |
| * | * | *All | *D |                                                   |
| P | T | owed | ef |                                                   |
| a | y | valu | au |                                                   |
| r | p | es** | lt |                                                   |
| a | e |      | ** |                                                   |
| m | * |      |    |                                                   |
| e | * |      |    |                                                   |
| t |   |      |    |                                                   |
| e |   |      |    |                                                   |
| r |   |      |    |                                                   |
| * |   |      |    |                                                   |
| * |   |      |    |                                                   |
+===+===+======+====+===================================================+
| < | c | m    | st | ‘mark4’ or ‘vlba’: strips and restores parity     |
| d | h | ark4 |    | bits. ’st’ (‘straight-through’) mode records 32   |
| a | a | \|   |    | input ‘tracks’ directly ’tvg’ – takes data from   |
| t | r | vlba |    | internal TVG – see Note 8. A null field is        |
| a |   | st   |    | special case for correlator. See Note 5.          |
| m |   | \|   |    |                                                   |
| o |   | tvg  |    |                                                   |
| d |   | \|   |    |                                                   |
| e |   | mark |    |                                                   |
| > |   | 5a+n |    |                                                   |
+---+---+------+----+---------------------------------------------------+
|   |   |      |    | For Mark 5A+ operation, ‘n’ is track-map # to be  |
|   |   |      |    | used; see Note 14.                                |
+---+---+------+----+---------------------------------------------------+
|   |   | none |    | ‘none’ – unknown format/don’t care, see Section 6 |
+---+---+------+----+---------------------------------------------------+
|   |   | ‘m   |    | ‘magic mode’; see Note 15                         |
|   |   | agic |    |                                                   |
|   |   | m    |    |                                                   |
|   |   | ode’ |    |                                                   |
+---+---+------+----+---------------------------------------------------+
| < | c | 8 \| | S  | 8,16,32,64 is relevant only for ‘mark4’, ‘vlba’   |
| d | h | 16   | ee | and ‘mark5a+’ modes and corresponds to number of  |
| a | a | \|   | No | tracks. ’mark4’ and ‘vlba’ relevant only for ‘st’ |
| t | r | 32   | te | mode. Not relevant when <data mode> is ‘tvg’. A   |
| a |   | \|   | 2  | null field is special case for correlator. See    |
| s |   | 64   | a  | Note 5.                                           |
| u |   | \|   | nd |                                                   |
| b |   | m    | No |                                                   |
| m |   | ark4 | te |                                                   |
| o |   | \|   | 14 |                                                   |
| d |   | vlba |    |                                                   |
| e |   |      |    |                                                   |
| > |   |      |    |                                                   |
+---+---+------+----+---------------------------------------------------+
| < | c | m    | <  | Optional: For correlator or diagnostic use only:  |
| o | h | ark4 | da | Forces the Output Section of the Mark 5A I/O      |
| u | a | \|   | ta | board into specified mode and submode             |
| t | r | vlba | m  | independently of the Input Section – see Note 5.  |
| p |   | \|   | od |                                                   |
| u |   | st   | e> |                                                   |
| t |   |      |    |                                                   |
| m |   |      |    |                                                   |
| o |   |      |    |                                                   |
| d |   |      |    |                                                   |
| e |   |      |    |                                                   |
| > |   |      |    |                                                   |
+---+---+------+----+---------------------------------------------------+
| < | c | 8 \| | <  | Optional: For correlator or diagnostic use only – |
| o | h | 16   | da | see Note 5.                                       |
| u | a | \|   | ta |                                                   |
| t | r | 32   | su |                                                   |
| p |   | \|   | bm |                                                   |
| u |   | 64   | od |                                                   |
| t |   | \|   | e> |                                                   |
| s |   | m    |    |                                                   |
| u |   | ark4 |    |                                                   |
| b |   | \|   |    |                                                   |
| m |   | vlba |    |                                                   |
| o |   |      |    |                                                   |
| d |   |      |    |                                                   |
| e |   |      |    |                                                   |
| > |   |      |    |                                                   |
+---+---+------+----+---------------------------------------------------+

Monitor-only parameters:

+---------+---+----+--------------------------------------------------+
| **Para  | * | ** | **Comments**                                     |
| meter** | * | Va |                                                  |
|         | T | lu |                                                  |
|         | y | es |                                                  |
|         | p | ** |                                                  |
|         | e |    |                                                  |
|         | * |    |                                                  |
|         | * |    |                                                  |
+=========+===+====+==================================================+
| <sync   | c | s  | ‘s’ indicates Output Section of I/O board is     |
| status> | h | \| | sync’ed; ’-‘ indicates not sync’ed. See Note 9   |
|         | a | -  |                                                  |
|         | r |    |                                                  |
+---------+---+----+--------------------------------------------------+
| <#sync  | i |    | Number of sync attempts by output section.       |
| at      | n |    | Relevant only for ‘mark4’ and ‘vlba’ modes only. |
| tempts> | t |    | See Note 10.                                     |
+---------+---+----+--------------------------------------------------+

Notes:

1. The ‘mode=’ command sets both the input and output modes to be the
   same unless overridden by <output data mode> and <output submode>
   parameters or the format is not supported by the hardware but is by
   *jive5ab (see section 7 of this document)*.
2. Power-on default <data mode>:<data submode> is ‘st:mark4’. For <data
   mode> of ‘st’, default <data submode> is ‘mark4; for <data mode> of
   ‘mark4’ or ‘vlba’, default <data submode> is ‘32’.
3. In ‘mark4’ or ‘vlba’ mode, the Mark 5A strips parity on record and
   restores it on playback to save storage space. If the number of
   tracks is 8, 16 or 64, the Mark 5 I/O does the necessary
   multiplexing/demultiplexing to always fully utilize all FPDP 32 bit
   streams driving the disk array. In ‘st’ (‘straight-through’) mode,
   the input data are recorded and played back with no processing.
4. **In ‘mark4:xx’ mode, the station ID (set by jumpers in the Mark 4
   DAS rack) must be an even number**. Attempting to record in ‘mark4’
   mode with an odd station ID will result in an error. This is due to
   the fact that, with parity stripped, an odd station ID considerably
   complicates the job of properly recovering synchronization during
   playback, and is therefore not allowed.
5. At a correlator, where there is normally nothing connected to the
   Mark 5A input, it is suggested that the desired playback mode be
   specified in <output mode> and <output submode> and that <data mode>
   and <data submode> both be null fields. This will cause the input
   section of the I/O board to be set to default (‘st:mark4’) mode and
   prevents spurious error messages from appearing regarding the input
   station ID.
6. The only reason to distinguish between ‘st:mark4’ and ‘st:vlba’ modes
   is to allow the ‘play_rate’ command’ to properly set the internal
   clock generator for a specified data rate; the setting is slightly
   different for the Mark4 and VLBA cases.
7. The tracks expected from a Mark4 or VLBA formatter in the various
   modes are as follows:

+--------------+-------------------+-----------------------------------+
|              | **Recorded        | **FPDP bit streams**              |
|              | formatter         |                                   |
|              | track#’s**        |                                   |
+==============+===================+===================================+
| ‘mark4:8’ or |                   | Trk 2 to FPDP streams 0,8,16,24;  |
| ‘vlba:8’     |                   | trk 4 to 1,9,17,25; etc.          |
+--------------+-------------------+-----------------------------------+
| ‘mark4:16’   | 2-33 even         | Trk 2 to FPDP streams 0,16; trk 4 |
| or ‘vlba:16’ | (headstack 1)     | to 1,17; etc.                     |
+--------------+-------------------+-----------------------------------+
| ‘mark4:32’   | 2-33 all          | Correspond to FPDP bit streams    |
| or ‘vlba:32’ | (headstack 1)     | 0-31, respectively                |
+--------------+-------------------+-----------------------------------+
| ‘mark4:64’   | 2-33 (headstacks  | Trks 2 from both hdstks mux’ed to |
| or ‘vlba:64’ | 1 and 2)          | FPDP bit stream 0, etc.           |
+--------------+-------------------+-----------------------------------+
| ‘st’ (any    | 2-33 all          | Correspond to FPDP bit streams    |
| submode)     | (headstack 1)     | 0-31, respectively                |
+--------------+-------------------+-----------------------------------+

8.  In all modes except ‘tvg’ mode, the data clock is provided by the
    external data source. In ‘tvg’ mode, the clock-rate is set by
    ‘play_rate’ command.
9.  The ‘sync status’ parameter is relevant only in output mode ‘mark4’
    or ‘vlba’ where parity must be restored. If ‘sync’ed’, the I/O board
    has properly synchronized to the data frames and is properly
    de-multiplexing and restoring parity.
10. The ‘# of sync attempts’ returned value in the ‘mode=’ command
    counts the number of sync attempts the Mark 5A I/O board output
    section had to make before parity-stripped data (‘mark4’ or ‘vlba’)
    was re-sync’ed, as necessary for parity re-insertion. A large number
    indicates a problem, perhaps in the output clock or the data itself.
    The counter is reset to zero on a subsequent ‘mode=’ command.
11. If in ‘tvg’ mode, TVG is operated at clock-rate set by ‘play_rate’
    command.
12. When <data mode> is ‘mark4’ or ‘vlba’, the NRZM output coding
    present on the data received from the formatter is converted to NRZL
    for transmission to the FPDP bus, and hence for recording on disk or
    transmission over a network. On output, the inverse operation
    (conversion back to NRZM) is performed, so that the output data on
    the Mark5A I/O Panel are in the NRZM format for input to the
    correlator. When operating in <st> or <tvg> mode, no coding
    conversions are done.
13. When operating in tvg mode, the 32-bit-wide tvg pattern is written
    directly to the disk with no tape-frame headers or synchronization
    information of any sort. Furthermore, the tvg runs continuously with
    no forced resets unless an external 1pps signal is connected to J11
    on the Mark 5A I/O board, in which case the tvg is asynchronously
    reset on each 1pps tick.
14. Operation in Mark 5A+ mode allows a Mark 5B disk module to be read
    on the Mark 5A and create VLBA output tracks; the Mark 5B data are
    read, transformed into VLBA track format (including the addition of
    parity bits), and VLBA-format headers are inserted. The Mark 5A must
    have the proper Xilinx code and software upgrades installed for this
    mode to work. The track mapping option (‘n’ in ‘mark5a+n’), as well
    as the number of output tracks, must be specified. Details are given
    in Mark 5 memo #39 available at
    `http://www.haystack.edu/tech/vlbi/mark5/memo.html. <http://www.haystack.edu/tech/vlbi/mark5/memo.html>`__
    Note that the VLBA auxiliary data field will be identically zero for
    all Mark 5A+ playback.
15. ‘magic mode’ is a one-string-sets-all version of the mode command.
    The string is parsed and sets the data frame format, number of
    tracks and track bitrate all in one go. The string follows the
    Walter Brisken/DiFX mk5access library canonical format as described
    in Section 7.1 of this manual.

mode – Set data recording mode (Mark5B, B+ /DIM)
================================================

Command syntax: mode = <data source> : <bit-stream mask> : [<decimation
ratio>] : [<FPDP mode>] ; Command response: !mode = <return code> ;

Query syntax: mode? ;

Query response: !mode ? <return code> : <data source> : <bit-stream
mask> : <decimation ratio> : <FPDP mode> ; Purpose: Set the recording
mode of the Mark 5B DIM

Settable parameters:

+----+---+---------+---+-------------------------------------------------+
| *  | * | **      | * | **Comments**                                    |
| *P | * | Allowed | * |                                                 |
| ar | T | v       | D |                                                 |
| am | y | alues** | e |                                                 |
| et | p |         | f |                                                 |
| er | e |         | a |                                                 |
| ** | * |         | u |                                                 |
|    | * |         | l |                                                 |
|    |   |         | t |                                                 |
|    |   |         | * |                                                 |
|    |   |         | * |                                                 |
+====+===+=========+===+=================================================+
| <  | c | ext \|  | e | ‘ext’ – data on VSI 80-pin connector ‘tvg’ –    |
| da | h | tvg \|  | x | internal Test Vector Generator; see Note 1      |
| ta | a | ramp    | t | ‘ramp’ – internal ramp generator; see Note 2    |
| s  | r | \       |   | ‘none’ – unknown format/don’t care, see section |
| ou |   | |none\| |   | 6. ‘magic mode’, see Note 4                     |
| rc |   | ‘magic  |   |                                                 |
| e> |   | mode’   |   |                                                 |
+----+---+---------+---+-------------------------------------------------+
| <  | h | See     | 0 | 5B format: up to 64 bitstreams!                 |
| bi | e | Note 1  | x |                                                 |
| t- | x |         | f |                                                 |
| st | 6 |         | f |                                                 |
| re | 4 |         | f |                                                 |
| am |   |         | f |                                                 |
| m  |   |         | f |                                                 |
| as |   |         | f |                                                 |
| k> |   |         | f |                                                 |
|    |   |         | f |                                                 |
+----+---+---------+---+-------------------------------------------------+
| <  | i | 1, 2,   | 1 | Specifies ratio of data clock to recorded       |
| de | n | 4, 8,   |   | sample rate; if <data source> is ‘tvg’ or       |
| ci | t | 16      |   | ‘ramp’, value of ‘1’ should always be used -    |
| ma |   |         |   | see Notes 1 and 2                               |
| ti |   |         |   |                                                 |
| on |   |         |   |                                                 |
| ra |   |         |   |                                                 |
| ti |   |         |   |                                                 |
| o> |   |         |   |                                                 |
+----+---+---------+---+-------------------------------------------------+
| <  | i | 1 \| 2  | S | *For diagnostic use only*: Sets FPDP mode       |
| FP | n |         | e | (FPDP1 or FPDP2). See Note 3                    |
| DP | t |         | e |                                                 |
| m  |   |         | N |                                                 |
| od |   |         | o |                                                 |
| e> |   |         | t |                                                 |
|    |   |         | e |                                                 |
|    |   |         | 3 |                                                 |
+----+---+---------+---+-------------------------------------------------+

Monitor-only parameters:

+--------------------------+-----------+-------------+----------------+
| **Parameter**            | **Type**  | **Values**  | **Comments**   |
+==========================+===========+=============+================+
| <data source>            | char      |             |                |
+--------------------------+-----------+-------------+----------------+
| <bit-stream mask>        | hex64     |             |                |
+--------------------------+-----------+-------------+----------------+
| <decimation ratio>       | int       |             |                |
+--------------------------+-----------+-------------+----------------+
| <FPDP mode>              | int       | 1 \| 2      |                |
+--------------------------+-----------+-------------+----------------+

Notes:

1. As per the VSI-H specification, the tvg pattern resets at every
   second tick. The bit-stream mask selects which bits of the tvg
   pattern are recorded; a decimation value other than ‘1’ should not be
   used except for special diagnostic testing as the resulting recorded
   data may not be recognized as tvg pattern by ‘data_check’ or
   ‘scan_check’. The bit-stream mask has been extended to support 64
   bit-streams, necessary for 4Gbps Mark5B or non- dechannelized VDIF
   format.
2. A ‘ramp’ pattern replaces the tvg with a 32-bit counter that starts
   at zero on the next second tick and increments each clock tick for
   100 seconds before resetting to zero again. The bit-stream mask
   selects which bits of the ramp pattern are recorded; a decimation
   value other than ‘1’ should not be used except for special diagnostic
   testing as the resulting recorded data may not be recognized as a
   ramp pattern by ‘data_check’ or ‘scan_check’.
3. Mark 5B only supports FPDP1; any attempt to set to FPDP2 will cause
   an error. Mark 5B+ always defaults to FPDP2, but may be forced to
   FPDP1 for test purposes; maximum aggregate date rate for FPDP1 is
   1024Mbps - an attempt to record at 2048Mbps in FPDP1 will cause
   error.
4. ‘magic mode’ is a one-string-sets-all version of the mode command.
   The string is parsed and sets the data frame format, number of tracks
   and track bitrate all in one go. The string follows the Walter
   Brisken/DiFX mk5access library canonical format as described in
   Section 7.1 of this manual.

mode – Set data recording/playback mode (Mark5B DOM, Mark5C, generic)
=====================================================================

Command syntax: mode = <data mode> : <data submode> : [<extra info>] ;
Command response: !mode = <return code> ;

Query syntax: mode? ;

Query response: !mode ? <return code> : <data mode> : <data submode> :
<extra info>

Purpose: Set the *jive5ab* internal format. This is a union of the
Mark5A and Mark5B/DIM and Mark5C “mode=” commands. Nothing will be sent
to the I/O board because, typically, on these systems there isn’t any to
program.

Settable parameters:

+---+---+-------+---+--------------------------------------------------------+
| * | * | **Al  | * | **Comments**                                           |
| * | * | lowed | * |                                                        |
| P | T | val   | D |                                                        |
| a | y | ues** | e |                                                        |
| r | p |       | f |                                                        |
| a | e |       | a |                                                        |
| m | * |       | u |                                                        |
| e | * |       | l |                                                        |
| t |   |       | t |                                                        |
| e |   |       | * |                                                        |
| r |   |       | * |                                                        |
| * |   |       |   |                                                        |
| * |   |       |   |                                                        |
+===+===+=======+===+========================================================+
| < | c | mark4 | s | ‘mark4’ or ‘vlba’: expect parity bit stripped MarkIV   |
| d | h | \|vl  | t | or VLBA data. ’st’ (‘straight-through’) mode records   |
| a | a | ba|st |   | 32 input ‘tracks’ directly ’tvg’ , ‘ramp’ expect       |
| t | r | tvg   |   | Mark5A or Mark5B (ramp) I/O board generated TVG        |
| a |   | \     |   | pattern ‘unk’ – on Mark5C this translates to ‘none’,   |
| m |   | |ramp |   | ‘mark5b’ + ‘unk’ only supported on Mark5C ‘ext’ –      |
| o |   | unk|m |   | expect Mark5B formatted data ‘none’ – unknown          |
| d |   | ark5b |   | format/don’t care, see Section 6 ‘magic mode’, see     |
| e |   | ext   |   | Note 1                                                 |
| > |   | none  |   |                                                        |
|   |   | ‘     |   |                                                        |
|   |   | magic |   |                                                        |
|   |   | mode’ |   |                                                        |
+---+---+-------+---+--------------------------------------------------------+
| < | c | 8 \|  |   | 8,16,32,64 is relevant only for ‘mark4’, ‘vlba’ and    |
| d | h | 16 \| |   | modes and corresponds to number of tracks. ‘int’ is    |
| a | a | 32 \| |   | VDIF number of bitstreams; see section 6.1. ‘hex64’ is |
| t | r | 64 \| |   | the 64-bit bit-stream mask for ‘ext’ Mark5B formats    |
| a |   | mark4 |   | (supports up to 4Gbps Mark5B)                          |
| s |   | \|    |   |                                                        |
| u |   | vlb   |   |                                                        |
| b |   | a|int |   |                                                        |
| m |   | hex64 |   |                                                        |
| o |   |       |   |                                                        |
| d |   |       |   |                                                        |
| e |   |       |   |                                                        |
| > |   |       |   |                                                        |
+---+---+-------+---+--------------------------------------------------------+
| < | i | m     | ( | ‘vdif’ or ‘legacyvdif’ - VDIF payload (data array)     |
| e | n | odulo | e | size, must be multiple of eight                        |
| x | t | 8     | m |                                                        |
| t |   |       | p |                                                        |
| r |   |       | t |                                                        |
| a |   |       | y |                                                        |
| i |   |       | ) |                                                        |
| n |   |       |   |                                                        |
| f |   |       |   |                                                        |
| o |   |       |   |                                                        |
| > |   |       |   |                                                        |
+---+---+-------+---+--------------------------------------------------------+

Monitor-only parameters:

+---------------+-----+----+------------------------------------------+
| **Parameter** | **  | ** | **Comments**                             |
|               | Typ | Va |                                          |
|               | e** | lu |                                          |
|               |     | es |                                          |
|               |     | ** |                                          |
+===============+=====+====+==========================================+
| <data mode>   | c   |    | reflects *jive5ab*\ ’s thoughts on what  |
|               | har |    | the data format is                       |
+---------------+-----+----+------------------------------------------+
| <data         | mi  |    | filled in as appropriate for <data mode> |
| submode>      | xed |    | filled in as appropriate for <data mode> |
| <extra info>  | mi  |    |                                          |
|               | xed |    |                                          |
+---------------+-----+----+------------------------------------------+

For format specific notes, refer to the respective Mark5A, Mark5B/DIM
“mode” commands. Which notes apply is, obviously, a function of which
format is attempted to set.

Notes:

1. ‘magic mode’ is a one-string-sets-all version of the mode command.
The string is parsed and sets the data frame format, number of tracks
and track bitrate all in one go. The string follows the Walter
Brisken/DiFX mk5access library canonical format as described in Section
7.1 of this manual.

mem2file – Transfer data from *jive5ab* internal buffer to file on disk
=======================================================================

Command syntax: mem2file = <control> : <filename> : [ <#buffer buffer> ]
: [<file option>] ; Command response: !mem2file = <return code> ;

Query syntax: mem2file? ;

Query response: !mem2file ? <return code> : <status> : <#bytes written>
; Purpose: Control data transfer from *jive5ab* internal buffer to file
on disk

Settable parameters:

+----+---+----+---+------------------------------------------------------+
| *  | * | *  | * | **Comments**                                         |
| *P | * | *A | * |                                                      |
| ar | T | ll | D |                                                      |
| am | y | ow | e |                                                      |
| et | p | ed | f |                                                      |
| er | e | va | a |                                                      |
| ** | * | lu | u |                                                      |
|    | * | es | l |                                                      |
|    |   | ** | t |                                                      |
|    |   |    | * |                                                      |
|    |   |    | * |                                                      |
+====+===+====+===+======================================================+
| <  | c | on |   | ‘on’ – start data transfer, subject to availability, |
| co | h | \| |   | see Note 1. ’off’ – stop transfer immediately,       |
| nt | a | st |   | discarding bytes currently in buffer ‘stop’ –        |
| ro | r | op |   | initiate stop of transfer, flush all buffered bytes  |
| l> |   | \| |   | to disk. See Note 3                                  |
|    |   | o  |   |                                                      |
|    |   | ff |   |                                                      |
+----+---+----+---+------------------------------------------------------+
| <f | c |    |   | Required only on first ‘on’; otherwise ignored       |
| il | h |    |   |                                                      |
| en | a |    |   |                                                      |
| am | r |    |   |                                                      |
| e> |   |    |   |                                                      |
+----+---+----+---+------------------------------------------------------+
| <# | i |    | 2 | Size of snapshot buffer between *jive5ab* internal   |
| bu | n |    | 5 | buffer and file writer. See Note 2                   |
| ff | t |    | 6 |                                                      |
| er |   |    | M |                                                      |
| by |   |    | B |                                                      |
| te |   |    |   |                                                      |
| s> |   |    |   |                                                      |
+----+---+----+---+------------------------------------------------------+
| <  | c | n  | n | Create new file (‘n’), truncate existing file (‘w’)  |
| fi | h | \| |   | or append to existing file (‘a’)                     |
| le | a | w  |   |                                                      |
| o  | r | \| |   |                                                      |
| pt |   | a  |   |                                                      |
| io |   |    |   |                                                      |
| n> |   |    |   |                                                      |
+----+---+----+---+------------------------------------------------------+

Monitor-only parameters:

+-------+---+-------------+--------------------------------------------+
| **P   | * | **Values**  | **Comments**                               |
| arame | * |             |                                            |
| ter** | T |             |                                            |
|       | y |             |                                            |
|       | p |             |                                            |
|       | e |             |                                            |
|       | * |             |                                            |
|       | * |             |                                            |
+=======+===+=============+============================================+
| <st   | c | inactive \| |                                            |
| atus> | h | active \|   |                                            |
|       | a | flushing \| |                                            |
|       | r | done        |                                            |
+-------+---+-------------+--------------------------------------------+
| <     | c |             | Current connection state, ‘WAIT’, ‘RUN’,   |
| conne | h |             | ‘CONNECTED’ or a combination thereof. Only |
| ction | a |             | returned if <status> != ‘inactive’         |
| st    | r |             |                                            |
| atus> |   |             |                                            |
+-------+---+-------------+--------------------------------------------+

Notes:

1. This transfer can be started independently from a ‘\*2mem’ transfer.
   After ‘mem2file = on ;’ still no data may be transferred; data flow
   will start as soon as data appears in the internal buffer. If a
   ‘\*2mem’ transfer is already writing data in the internal buffer,
   this transfer will start immediately after ‘mem2file = on : .. ;’

2. In order to support writing to slow disks, a separate ‘snapshot’
   buffer is used by this transfer. This allows for the real-time
   capture of

   <#buffer bytes> from the internal buffer whilst the file may be
   written to at a much lower speed. After the snapshot buffer is full,
   no data will be read from the *jive5ab* internal buffer until space
   becomes available due to bytes being written to disk.

3. A ‘mem2file = stop’ will instruct the transfer to stop reading from
   *jive5ab*\ ’s internal buffer. The transfer remains alive, flushing
   all currently buffered bytes in the snapshot buffer to disk. The
   command will return immediately with <return code> equal “1” (“action
   initiated but not completed”). Subsequent queries will return a
   <status> of ‘flushing’ until all bytes have been flushed. Then
   <status> will transition to ‘done’. Issue ‘mem2file = off ;’ to clear
   the status to ‘idle’.

mem2net – Transfer data from *jive5ab* internal buffer to network
=================================================================

Command syntax: mem2net = <control> : <remote hostname> ; Command
response: !mem2net = <return code> ;

Query syntax: mem2net? ;

Query response: !mem2net ? <return code> : <status \| remote hostname> :
<connection status> ; Purpose: Control data transfer from *jive5ab*
internal buffer to network

Settable parameters:

+----+---+------+---+----------------------------------------------------+
| *  | * | *    | * | **Comments**                                       |
| *P | * | *All | * |                                                    |
| ar | T | owed | D |                                                    |
| am | y | valu | e |                                                    |
| et | p | es** | f |                                                    |
| er | e |      | a |                                                    |
| ** | * |      | u |                                                    |
|    | * |      | l |                                                    |
|    |   |      | t |                                                    |
|    |   |      | * |                                                    |
|    |   |      | * |                                                    |
+====+===+======+===+====================================================+
| <  | c | con  |   | ‘connect’ – connect to socket on receiving Mark 5  |
| co | h | nect |   | system; initially, data transfer is off. ‘on’ –    |
| nt | a | \|   |   | start data transfer, subject to availability, see  |
| ro | r | on   |   | Note 1 ’disconnect’ – disconnect socket See Notes  |
| l> |   | \|   |   | with ‘disk2net’                                    |
|    |   | di   |   |                                                    |
|    |   | scon |   |                                                    |
|    |   | nect |   |                                                    |
+----+---+------+---+----------------------------------------------------+
| <  | c |      | l | Required only on first ‘connect’; otherwise        |
| re | h |      | o | ignored                                            |
| mo | a |      | c |                                                    |
| te | r |      | a |                                                    |
| h  |   |      | l |                                                    |
| os |   |      | h |                                                    |
| tn |   |      | o |                                                    |
| am |   |      | s |                                                    |
| e> |   |      | t |                                                    |
+----+---+------+---+----------------------------------------------------+

Monitor-only parameters:

+----------+---+-----------+-------------------------------------------+
| **Par    | * | *         | **Comments**                              |
| ameter** | * | *Values** |                                           |
|          | T |           |                                           |
|          | y |           |                                           |
|          | p |           |                                           |
|          | e |           |                                           |
|          | * |           |                                           |
|          | * |           |                                           |
+==========+===+===========+===========================================+
| <status  | c | inactive  |                                           |
| \|       | h | \|        |                                           |
| remote   | a | <remote   |                                           |
| h        | r | hostname> |                                           |
| ostname> |   |           |                                           |
+----------+---+-----------+-------------------------------------------+
| <co      | c |           | Current connection state, ‘WAIT’, ‘RUN’,  |
| nnection | h |           | ‘CONNECTED’ or a combination thereof.     |
| status>  | a |           | Only returned if <status> != ‘inactive’   |
|          | r |           |                                           |
+----------+---+-----------+-------------------------------------------+

Notes:

1. This transfer can be started independently from a ‘\*2mem’ transfer.
After ‘mem2net = on ;’ still no data may be transferred; data flow will
start as soon as data appears in the internal buffer. If a ‘\*2mem’
transfer is already writing data in the internal buffer, this transfer
will start immediately after ‘mem2net = on ;’

mem2time – Decode data from *jive5ab* internal buffer into queryable time stamp
===============================================================================

Command syntax: mem2time = <control> ; Command response: !mem2time =
<return code> ;

Query syntax: mem2time? ;

Query response: !mem2time ? <return code> : inactive ;

!mem2time ? <return code> : O/S : <current O/S time> : data : <current
data time> : <delta O/S – data> ; Purpose: Monitor data stream coming
into memory by continuously decoding data frames and retaining the last
time stamp

Settable parameters:

+----+---+------+-----------------------------------------------------+
| *  | * | *    | **Comments**                                        |
| *P | * | *All |                                                     |
| ar | T | owed |                                                     |
| am | y | valu |                                                     |
| et | p | es** |                                                     |
| er | e |      |                                                     |
| ** | * |      |                                                     |
|    | * |      |                                                     |
+====+===+======+=====================================================+
| <  | c | open | ‘open’ – start data transfer (subject to            |
| co | h | \|   | availability, see Note 1) and decoding (subject to  |
| nt | a | c    | format, see Note 2), ’close’ – stop transfer        |
| ro | r | lose |                                                     |
| l> |   |      |                                                     |
+----+---+------+-----------------------------------------------------+

Monitor-only parameters:

+-------------+-----+--------------------------------------------------+
| **          | **  | **Comments**                                     |
| Parameter** | Typ |                                                  |
|             | e** |                                                  |
+=============+=====+==================================================+
| <current    | t   | Current O/S time                                 |
| O/S time>   | ime |                                                  |
+-------------+-----+--------------------------------------------------+
| <current    | t   | Time stamp of last succesfully decoded data      |
| data time>  | ime | frame                                            |
+-------------+-----+--------------------------------------------------+
| <delta O/S  | fl  | Time difference between O/S and data, in         |
| - data>     | oat | seconds. The unit string “s” is appended.        |
|             | ”s” |                                                  |
+-------------+-----+--------------------------------------------------+

Notes:

1. This transfer can be started independently from a ‘\*2mem’ transfer.
   After ‘mem2time = open ;’ still no data may be decoded; decoding will
   start as soon as data appears in the internal buffer. If a ‘\*2mem’
   transfer is already writing data in the internal buffer, this
   transfer will start immediately after ‘mem2time = open ;’
2. For this transfer to succesfully decode the incoming data stream, the
   runtime in which this transfer is started should have its “mode = “
   be set to the expected data format. Refer to “mode = ..”
   documentation for setting the mode in a convenient way. Note that the
   runtimes also support setting a ‘hardware’ format. E.g. to set Mark5B
   data format “mode = ext : 0xffffffff ; clock_set = 32 : int;” could
   be used – equivalent to how a hardware Mark5B would be configured.
3. If data is being decoded, the last decoded data time stamp can be
   retrieved using ‘mem2time?’. Repeated querying of ‘mem2time?’ would
   allow an inquisitive application to monitor the data stream.

mtu – Set network Maximum Transmission Unit (packet) size
=========================================================

Command syntax: mtu = <MTU> ; Command response: !mtu = <return code> ;

Query syntax: mtu? ;

Query response: !mtu ? <return code> : <MTU> ;

Purpose: Set network MTU for subsequent UDP based data transfers

Settable parameters:

+--------------+---------+-------------------+------------+-------------+
| *            | *       | **Allowed         | *          | *           |
| *Parameter** | *Type** | values**          | *Default** | *Comments** |
+==============+=========+===================+============+=============+
| <MTU>        | int     | 64 - 9000         | 1500       | See Notes   |
+--------------+---------+-------------------+------------+-------------+

Monitor-only parameters:

============= ======== ========== ====================
**Parameter** **Type** **Values** **Comments**
============= ======== ========== ====================
<MTU>         int      <mtu>      Current value of MTU
============= ======== ========== ====================

Notes:

1. The actual MTU value is only used for determining the (maximum)
   packet size that *jive5ab* can send in a subsequent UDP/IPv4 network
   transfer. *jive5ab* uses this value as a maximum value, **unrelated**
   to the MTU actually configured on the ethernet device! In certain
   cases this may lead to a failure of data passing through the
   interface, notably if *jive5ab’s* idea of the MTU is > the MTU
   configured on the device.
2. The limits mentioned in the allowed values are enforced by *jive5ab*;
   these limits are typical (hard) ethernet limits.
3. From the MTU and the actual network protocol and configured data
   format (and optional configured channel dropping) *jive5ab* computes
   the size of the packet payload section, taking various constraints
   into account. Example constraints are: payload size must be multiple
   of eight, an integral number of packets fitting into a <workbuf size>
   (see ‘net_protocol’, p.50), losing a compressed packet does not loose
   sync in the data stream to name but a few. These constraints are not
   solved until a transfer is actually started. To review the values
   actually used in a running transfer see the “constraints?” query,
   p. .
4. The packets on the wire are guaranteed to have a size <= the set MTU.
5. This setting also applies to the UDT protocol because it is based on
   UDP and as such it can be told to honour this size.

net2disk – Transfer data from network to disks
==============================================

Command syntax: net2disk = <control> : <scan label> [ :
[STRIKEOUT:<bit-stream mask>]\ <ip \| host> ]; Command response:
!net2disk = <return code> ;

Query syntax: net2disk? ;

Query response: !net2disk ? <return code> : <status> : <scan#> : <scan
label> : <bit-stream mask> ; Purpose: Enable data transfer from network
to local disks

Settable parameters:

+----------+----+-----+---+-------------------------------------------+
| **Par    | ** | **A | * | **Comments**                              |
| ameter** | Ty | llo | * |                                           |
|          | pe | wed | D |                                           |
|          | ** | va  | e |                                           |
|          |    | lue | f |                                           |
|          |    | s** | a |                                           |
|          |    |     | u |                                           |
|          |    |     | l |                                           |
|          |    |     | t |                                           |
|          |    |     | * |                                           |
|          |    |     | * |                                           |
+==========+====+=====+===+===========================================+
| <        | ch | o   |   | ‘open’ or ‘close’ socket                  |
| control> | ar | pen |   |                                           |
|          |    | \|  |   |                                           |
|          |    | cl  |   |                                           |
|          |    | ose |   |                                           |
+----------+----+-----+---+-------------------------------------------+
| <scan    | l  |     |   | Scan label to be assigned to this data;   |
| label>   | it |     |   | if not specified, defaults to             |
|          | er |     |   | ‘EXP_STN_net2disk’ See Section 6 for      |
|          | al |     |   | format of scan label.                     |
|          | A  |     |   |                                           |
|          | SC |     |   |                                           |
|          | II |     |   |                                           |
+----------+----+-----+---+-------------------------------------------+
| [STRIK   | [  |     | 0 | [STRIKEOUT:<bit-stream mask> associated   |
| EOUT:<bi | ST |     |   | with data. See Note 1.] Host name or IPv4 |
| t-stream | RI |     |   | address, see Note 5.                      |
| mask>]   | KE |     |   |                                           |
| <        | OU |     |   |                                           |
| ip/host> | T: |     |   |                                           |
|          | he |     |   |                                           |
|          | x] |     |   |                                           |
|          | A  |     |   |                                           |
|          | SC |     |   |                                           |
|          | II |     |   |                                           |
+----------+----+-----+---+-------------------------------------------+

Monitor-only parameters:

+--------------+---+-------------+-----------------------------------+
| *            | * | **Values**  | **Comments**                      |
| *Parameter** | * |             |                                   |
|              | T |             |                                   |
|              | y |             |                                   |
|              | p |             |                                   |
|              | e |             |                                   |
|              | * |             |                                   |
|              | * |             |                                   |
+==============+===+=============+===================================+
| <status>     | c | active \|   | Current status of transfer        |
|              | h | inactive \| |                                   |
|              | a | waiting     |                                   |
|              | r |             |                                   |
+--------------+---+-------------+-----------------------------------+
| <scan#>      | i |             | Sequential scan number on disk    |
|              | n |             | module                            |
|              | t |             |                                   |
+--------------+---+-------------+-----------------------------------+
| <scan label> | A |             | Assigned scan label               |
|              | S |             |                                   |
|              | C |             |                                   |
|              | I |             |                                   |
|              | I |             |                                   |
+--------------+---+-------------+-----------------------------------+
| [STRIKEOUT   | [ |             | [STRIKEOUT:Assigned bit-stream    |
| :<bit-stream | S |             | mask] Number of bytes written to  |
| mask>]       | T |             | disk (FIFO, in reality)           |
| <nbytes>     | R |             |                                   |
|              | I |             |                                   |
|              | K |             |                                   |
|              | E |             |                                   |
|              | O |             |                                   |
|              | U |             |                                   |
|              | T |             |                                   |
|              | : |             |                                   |
|              | h |             |                                   |
|              | e |             |                                   |
|              | x |             |                                   |
|              | ] |             |                                   |
|              | i |             |                                   |
|              | n |             |                                   |
|              | t |             |                                   |
+--------------+---+-------------+-----------------------------------+

Notes:

1. [STRIKEOUT:The <bit-stream> mask should always specified so that the
   Mark 5B directory entry can be properly completed.]\ See Note 5.
2. See Notes with ‘disk2net’ command for usage rules and restrictions.
3. When <status> is ‘inactive’, a ‘net2disk?’ query returns <scan label>
   of the last transferred scan, if any.
4. Note that the network protocol parameters are set by the
   ‘net_protocol’ command.
5. The <bit-stream mask> is not used by *jive5ab*. This parameter is now
   an optional IPv4 address or host name which will be used to connect
   to in case the reverse-TCP protocol is selected (see “net_protocol”);
   it reverses client/server roles.

net2file – Transfer data from network to file on disk
=====================================================

Command syntax: net2file = <control> : <file name>,<file option> [ :
<strictness> ] ; Command response: !net2file = <return code> [ : <file
size> ] ;

Query syntax: net2file? ;

Query response: !net2file ? <return code> : <status> : <#bytes written>
; Purpose: Control data transfer from network to file on disk.

Settable parameters:

+---+---+---+---+--------------------------------------------------------+
| * | * | * | * | **Comments**                                           |
| * | * | * | * |                                                        |
| P | T | A | D |                                                        |
| a | y | l | e |                                                        |
| r | p | l | f |                                                        |
| a | e | o | a |                                                        |
| m | * | w | u |                                                        |
| e | * | e | l |                                                        |
| t |   | d | t |                                                        |
| e |   | v | * |                                                        |
| r |   | a | * |                                                        |
| * |   | l |   |                                                        |
| * |   | u |   |                                                        |
|   |   | e |   |                                                        |
|   |   | s |   |                                                        |
|   |   | * |   |                                                        |
|   |   | * |   |                                                        |
+===+===+===+===+========================================================+
| < | c | o |   | ‘open’ – set up a receiver for incoming data, using    |
| c | h | p |   | current net_protocol and net_port values. \ *jive5ab*  |
| o | a | e |   | >= 2.6.2 returns file size in the reply to implement   |
| n | r | n |   | m5copy resume capability ’close’ – stop the transfer;  |
| t |   | \ |   | close connection and file                              |
| r |   | | |   |                                                        |
| o |   | c |   |                                                        |
| l |   | l |   |                                                        |
| > |   | o |   |                                                        |
|   |   | s |   |                                                        |
|   |   | e |   |                                                        |
+---+---+---+---+--------------------------------------------------------+
| < | c |   |   | Required only on first ‘open’; otherwise ignored       |
| f | h |   |   |                                                        |
| i | a |   |   |                                                        |
| l | r |   |   |                                                        |
| e |   |   |   |                                                        |
| n |   |   |   |                                                        |
| a |   |   |   |                                                        |
| m |   |   |   |                                                        |
| e |   |   |   |                                                        |
| > |   |   |   |                                                        |
+---+---+---+---+--------------------------------------------------------+
| < | c | n | n | Create new file (‘n’), truncate existing file (‘w’) or |
| f | h | \ |   | append to existing file (‘a’). See Note 1.             |
| i | a | | |   |                                                        |
| l | r | w |   |                                                        |
| e |   | \ |   |                                                        |
| o |   | | |   |                                                        |
| p |   | a |   |                                                        |
| t |   |   |   |                                                        |
| i |   |   |   |                                                        |
| o |   |   |   |                                                        |
| n |   |   |   |                                                        |
| > |   |   |   |                                                        |
+---+---+---+---+--------------------------------------------------------+
| < | i | [ | 0 | Strictness level of decoding incoming frames. See Note |
| s | n | 1 |   | 2.                                                     |
| t | t | , |   |                                                        |
| r |   | ] |   |                                                        |
| i |   | 2 |   |                                                        |
| c |   |   |   |                                                        |
| t |   |   |   |                                                        |
| n |   |   |   |                                                        |
| e |   |   |   |                                                        |
| s |   |   |   |                                                        |
| s |   |   |   |                                                        |
| > |   |   |   |                                                        |
+---+---+---+---+--------------------------------------------------------+

Monitor-only parameters:

+-----------------+-------+----------------+---------------------------+
| **Parameter**   | **T   | **Values**     | **Comments**              |
|                 | ype** |                |                           |
+=================+=======+================+===========================+
| <status>        | char  | inactive \|    |                           |
|                 |       | active         |                           |
+-----------------+-------+----------------+---------------------------+
| <#bytes         | int   |                | Amount of bytes written   |
| written>        |       |                | to disk                   |
+-----------------+-------+----------------+---------------------------+

Notes:

1. In contrast to Mark5A/DIMino, *jive5ab* sometimes decided to
   aggretate/encode the file-open option (‘n’, ‘w’ or ‘a’) in the file
   name field. This is done by suffixing the file name with a
   construction “**,<file-open option>**”. This was e.g. necessary for
   transfers where two files were included and for transfer modes not
   supported by Mark5A/DIMino. In transfers where Mark5A/DIMino
   documentation decides that the file-open mode character is a separate
   field, *jive5ab* follows this.
2. If a valid data format has been set via “mode=” *jive5ab* can be
   instructed to decode incoming data frames and filter only the valid
   ones. The strictness parameter determines how strict the filtering
   will be done. Future releases may support finer grained control than
   on/off. Currently, the value of ‘1’ is allowed by the code but will
   have no effect; only a value >1 is currently meaningful

+-----+----------------------------------------------------------------+
| *   | **Filtering characteristics**                                  |
| *st |                                                                |
| ric |                                                                |
| t** |                                                                |
+=====+================================================================+
| 0,  | Look for syncword to find data frames. Check for Mark5B        |
| 1   | consistency. Allow DBE Mark5B frames; they’re broken (no VLBA  |
|     | time stamp)                                                    |
+-----+----------------------------------------------------------------+
| 2   | All of strict = 0 plus CRC checks for data formats that        |
|     | support that. Header must strictly comply to format’s          |
|     | definition.                                                    |
+-----+----------------------------------------------------------------+

net2mem – Transfer data from network to *jive5ab* internal buffer
=================================================================

Command syntax: net2mem = <control> ; Command response: !net2mem =
<return code> ;

Query syntax: net2mem? ;

Query response: !net2mem ? <return code> : <status> ;

Purpose: Control data transfer from network to *jive5ab* internal buffer

Settable parameters:

+----+---+------+---+-------------------------------------------------+
| *  | * | *    | * | **Comments**                                    |
| *P | * | *All | * |                                                 |
| ar | T | owed | D |                                                 |
| am | y | valu | e |                                                 |
| et | p | es** | f |                                                 |
| er | e |      | a |                                                 |
| ** | * |      | u |                                                 |
|    | * |      | l |                                                 |
|    |   |      | t |                                                 |
|    |   |      | * |                                                 |
|    |   |      | * |                                                 |
+====+===+======+===+=================================================+
| <  | c | open |   | ‘open’ – set up a receiver for incoming data    |
| co | h | \|   |   | using current net_protocol and net_port values  |
| nt | a | c    |   | ’close’ – stop the transfer; close connection   |
| ro | r | lose |   |                                                 |
| l> |   |      |   |                                                 |
+----+---+------+---+-------------------------------------------------+

Monitor-only parameters:

============= ======== ================== ============
**Parameter** **Type** **Values**         **Comments**
============= ======== ================== ============
<status>      char     inactive \| active 
============= ======== ================== ============

Notes:

1. -

net2out – Transfer data directly from network to Mark 5 output
==============================================================

Command syntax: net2out = <control> ; Command response: !net2out =
<return code> ;

Query syntax: net2out? ;

Query response: !net2out ? <return code> : <status> : <nowbyte> ;
Purpose: Enable data transfer from network to Mark 5 output; bypass
disks

Settable parameters:

+-----------+-------+----------------+----------+---------------------+
| **Pa      | **T   | **Allowed      | **D      | **Comments**        |
| rameter** | ype** | values**       | efault** |                     |
+===========+=======+================+==========+=====================+
| <control> | char  | open \| close  | -        | ‘open’ or ‘close’   |
|           |       |                |          | socket              |
+-----------+-------+----------------+----------+---------------------+

Monitor-only parameters:

+---+---+----------+-----------------------------------------------------+
| * | * | **       | **Comments**                                        |
| * | * | Values** |                                                     |
| P | T |          |                                                     |
| a | y |          |                                                     |
| r | p |          |                                                     |
| a | e |          |                                                     |
| m | * |          |                                                     |
| e | * |          |                                                     |
| t |   |          |                                                     |
| e |   |          |                                                     |
| r |   |          |                                                     |
| * |   |          |                                                     |
| * |   |          |                                                     |
+===+===+==========+=====================================================+
| < | c | active   | active – connected and data flowing inactive – no   |
| s | h | \|       | socket open; doing nothing waiting – socket open    |
| t | a | inactive | and waiting for a connection paused – socket        |
| a | r | \|       | connected but no data waiting to be read See Note   |
| t |   | waiting  | 3.                                                  |
| u |   | \|       |                                                     |
| s |   | paused   |                                                     |
| > |   |          |                                                     |
+---+---+----------+-----------------------------------------------------+
| < | i | -        | Total number of bytes transferred to the output     |
| n | n |          | since ‘open’; the ‘open’ command resets <nowbyte>   |
| o | t |          | to 0.                                               |
| w |   |          |                                                     |
| b |   |          |                                                     |
| y |   |          |                                                     |
| t |   |          |                                                     |
| e |   |          |                                                     |
| > |   |          |                                                     |
+---+---+----------+-----------------------------------------------------+

Notes:

1. See Notes with ‘disk2net’ command for usage rules and restrictions.
2. Note that the network protocol parameters are set by the
   ‘net_protocol’ command.
3. Note that none of the status returns necessarily indicate an error –
   depends on context. At modest data-transfer speed, <status> may be
   “paused” (indicating no data waiting to be read) most of the time
   even if <nowbyte> is incrementing; incrementing <nowbyte> indicates
   that data are flowing.

net_port – Set IPv4 port and optional local IPv4 address for the data channel (*jive5ab* >= 3.0.0)
=====================================================================================================

Command syntax: net_port = [<ip|host>@]<port> ; Command response: !mtu =
<return code> ;

Query syntax: net_port? ;

Query response: !net_port ? <return code> : [<ip|host>@]<port> ;

Purpose: Set the port number and optional local address for network
transfers to connect/listen, and send/receive data.Settable parameters:

+--------+-----+-----------+------+------------------------------------+
| *      | **  | **Allowed | **D  | **Comments**                       |
| *Param | Typ | values**  | efau |                                    |
| eter** | e** |           | lt** |                                    |
+========+=====+===========+======+====================================+
| <ip|   | c   |           | none | Resolvable host name or            |
| host>@ | har |           |      | dotted-quad IPv4 address (see      |
|        |     |           |      | Notes)                             |
+--------+-----+-----------+------+------------------------------------+
| <port> | int | 0- 65536  | 2630 | See Notes                          |
+--------+-----+-----------+------+------------------------------------+

Monitor-only parameters:

+-------+----+-------+------------------------------------------------+
| **P   | ** | **Val | **Comments**                                   |
| arame | Ty | ues** |                                                |
| ter** | pe |       |                                                |
|       | ** |       |                                                |
+=======+====+=======+================================================+
| <ip|h | ch | <ip|  | Current value of the local IPv4 or hostname    |
| ost>@ | ar | host> | configured. Only reported if one is set.       |
+-------+----+-------+------------------------------------------------+
| <     | i  | <     | Current value of the data port                 |
| port> | nt | port> |                                                |
+-------+----+-------+------------------------------------------------+

Notes:

1. The \*2net and net2\* transfers involve a network client and server
   component respectively. The sender (client) connects to a
   PROTOCOL:IPv4:PORT combination, the receiver (server) listens on
   PROTOCOL:\*:PORT (all interfaces on the machine). This command can be
   used to program the PORT part of the connection; the PROTOCOL and
   IPv4 parts are set by ‘net_protocol=’ and ’\*2net

   = connect : …’ commands. The compiled-in default for this value is
   2630 and is compatible with MIT Haystack Mark5A, DIMino and drs.

2. This parameter also dictates which port number UDP data streams will
   be captured from for recording on Mark6, FlexBuff or during real-
   time e-VLBI. Even though UDP is connectionless, the receiver (server)
   has to listen on “udp:\*:port” for incoming UDP data packets. Because
   this parameter is stored per runtime, data streams sent to different
   ports must be captured in different runtimes.

3. The limits mentioned in the allowed values are governed by internet
   standards. The port number is a 16-bit unsigned integer value
   limiting the physical values from 0 to 65535 (216-1). Besides these
   numerical limits, the internet standard qualifies some numerical port
   ranges differently. E.g. for a process to be allowed to start a
   server on a port number <= 1024, root privilege is required. Also it
   is important to realize that port selection may not be arbitrary:
   many ports are registered to specific protocols. The default 2630 was
   meant to be registered at the Internet Assigned Numbers Authority
   (IANA) (ICANN since 2001) but that never materialized. The current
   list of registered ports can be found at the following URL:
   http://www.iana.org/assignments/service-names-port-numbers/service-names-port-
   numbers.xhtml.

4. Since *jive5ab* 3.0.0 the net_port command supports specifying an
   optional **<ip|host>@\*\* prefix before the port number. This is
   mainly useful for FlexBuff/Mark6 systems to force jive5ab, after
   record=on:… to open a listening socket only on that address, in stead
   of listening on all interfaces, which is the default. Using this
   feature, multiple parallel streams can easily be recorded by
   different runtimes (see Section 6). The** <ip>*\* address may be a
   multicast address, after which *jive5ab* joins that group.

5. Sending a net_port= command with only a <port> parameter resets the
   currently defined local address to all interfaces.

net_protocol – Set network data-transfer protocol and I/O block sizes for all transfers
=======================================================================================

Command syntax: net_protocol = <protocol> : [<socbuf size>] : [<workbuf
size>] : [<nbuf>] ; Command response: !net_protocol = <return code> ;

Query syntax: net_protocol? ;

Query response: !net_protocol? <return code> : <protocol> : <socbuf
size> : <workbuf size> : <nbuf> ; Purpose: Set network data-transfer
protocol and I/O block sizes for all transfers

Settable parameters:

+---+---+----------+-----+----------------------------------------------+
| * | * | *        | **  | **Comments**                                 |
| * | * | *Allowed | Def |                                              |
| P | T | values** | aul |                                              |
| a | y |          | t** |                                              |
| r | p |          |     |                                              |
| a | e |          |     |                                              |
| m | * |          |     |                                              |
| e | * |          |     |                                              |
| t |   |          |     |                                              |
| e |   |          |     |                                              |
| r |   |          |     |                                              |
| * |   |          |     |                                              |
| * |   |          |     |                                              |
+===+===+==========+=====+==============================================+
| < | c | tcp \|   | tcp | Select which network protocol to use for     |
| p | h | unix     |     | net2\* and \*2net transfers. See Note 5. The |
| r | a | rtcp|ud  |     | udpsnor protocol was added in *jive5ab* 2.8. |
| o | r | p|udps\| |     | See Note 9.                                  |
| t |   | p        |     |                                              |
| o |   | udp|udt\ |     |                                              |
| c |   | |udpsnor |     |                                              |
| o |   |          |     |                                              |
| l |   |          |     |                                              |
| > |   |          |     |                                              |
+---+---+----------+-----+----------------------------------------------+
| < | i | bytes    | Li  | Used as receive buffer size [STRIKEOUT:in    |
| s | n | (see     | nux | ‘net2out’ and ‘net2disk’] net2\*. Used as    |
| o | t | Note 3)  | soc | send buffer size in [STRIKEOUT:‘in2net’ and  |
| c |   |          | ket | ‘disk2net’]\ \*2net . Defaults to            |
| b |   |          | buf | [STRIKEOUT:0] 4MB [STRIKEOUT:which causes    |
| u |   |          | fer | use of Linux OS defaults buffer size.]       |
| f |   |          | s   |                                              |
| s |   |          | ize |                                              |
| i |   |          |     |                                              |
| z |   |          |     |                                              |
| e |   |          |     |                                              |
| > |   |          |     |                                              |
+---+---+----------+-----+----------------------------------------------+
| < | i | bytes    | 131 | [STRIKEOUT:buffer size to send to data       |
| w | n | (see     | 072 | sockets in ‘in2net’ and ‘disk2net’.] The     |
| o | t | Note 3)  | or  | basic unit of I/O in any transfer. See Note  |
| r |   |          | l   | 4. Also very important for Mark6/FlexBuff,   |
| k |   |          | ast | see Note 7.                                  |
| b |   |          | va  |                                              |
| u |   |          | lue |                                              |
| f |   |          | set |                                              |
| s |   |          |     |                                              |
| i |   |          |     |                                              |
| z |   |          |     |                                              |
| e |   |          |     |                                              |
| > |   |          |     |                                              |
+---+---+----------+-----+----------------------------------------------+
| < | i | 1-16     | 8   | [STRIKEOUT:Number of blocks, each of size    |
| n | n |          |     | <workbuf size>, allocated in a circular      |
| b | t |          |     | FIFO; see Note 2] The usage of this          |
| u |   |          |     | parameter has changed completely. See Note   |
| f |   |          |     | 6. Also important for Mark6/FlexBuff, see    |
| > |   |          |     | Note 8.                                      |
+---+---+----------+-----+----------------------------------------------+

Notes:

1. Query returns protocol and buffer sizes currently in force.
2. [STRIKEOUT:<nbuf> times <workbuf size> must not exceed 134,217,728
   bytes.] See Note 8.
3. For your convenience the suffixes “k” (value x 1024 bytes) and “M”
   (value x 1024k bytes) are supported
4. The <workbuf size>, if supplied in a command, will be taken as a hint
   and at least be made a multiple of eight. It is the basic unit of I/O
   in **any** transfer. E.g. for “disk2net” the data will be read from
   the disk pack in chunks of size <workbuf size>. Disk pack I/O
   transfer speed reaches it maximum at a size of 4MB. With (very) small
   <workbuf size> blocks the overhead will be huge and performance
   severely affected.
5. *jive5ab* supports various network protocols. For the unix protocol
   the format of the destination in the “connect” string is the path to
   a local file. For all other protocols it is an IPv4 dotted quad
   notation address or a resolvable internet host name.

+-------+--------------------------------------------------------------+
| tcp,  | the standard UNIX network protocols                          |
| unix  |                                                              |
+=======+==============================================================+
| udp,  | these are synonyms – udps means: UDP + 8-byte sequence       |
| udps, | number (“net_protocol=udps” + “mode=vdif” => VTP) for udpsnor |
| ud    | see Note 9.                                                  |
| psnor |                                                              |
+-------+--------------------------------------------------------------+
| pudp  | “plain UDP” for vanilla, UNIX style, UDP protocol without    |
|       | 8-byte sequence number                                       |
+-------+--------------------------------------------------------------+
| rtcp  | reverse TCP – identical to TCP but with reversed connection  |
|       | direction. Use in case of firewall issues: \*2net becomes    |
|       | server, net2\* becomes client.                               |
+-------+--------------------------------------------------------------+
| udt   | High performance reliable protocol layered on top of UDP.    |
|       | See http://udt.sourceforge.net/                              |
+-------+--------------------------------------------------------------+

6. The processing chains mentioned in Section 4 of this manual yield
   their data from one processing step to the next through a queue. The
   depths of these queues are fixed at compile time, typically rendering
   this parameter meaningless but in one – important – place. When the
   UDPs (a.k.a. VTP, UDP-with-sequence number) protocol is used, this
   protocol keeps <nbuf> <workbuf size>’d blocks in cache as ‘read
   ahead’ buffer such that it can deal with reordering.
7. On Mark6 and FlexBuff the <workbuf size> value sets the size of the
   blocks. On Mark6 a block of size <workbuf size> is written to the
   next available file; on FlexBuff, files of size <workbuf size> will
   be written. MIT Haystack c-plane/d-plane uses 10000000 (10x106 bytes)
   byte blocks. Useful values for Mark6 recordings are between 8 – 16MB.
   For FlexBuff a minimum file size of 128MB is enforced by *jive5ab*.
8. For a number of transfers, *jive5ab* will pre-allocate <nbuf> \*
   <workbuf size> bytes when a transfer is started. It is therefore
   advised to keep the value of this product within reasonable limits –
   pre-allocating too little (say 8 buffers of 16kB) or too much (64
   buffers of 512MB (=32GB!)) should be avoided. Reasonable values
   typically depend on the target data rate, the system’s speed and
   amount of installed memory. Some experimentation may be required to
   fine-tune the combination of <nbuf> and <workbuf size> although this
   is expected to be necessary only in edge cases e.g. pushing a system
   close to its performance limits.
9. In order to support recording multi-stream FiLa10G or RDBE data, the
   **udpsnor** protocol was introduced in *jive5ab* 2.8. “udpsnor” is
   short for “UDP with sequence number, no reordering”. FiLa10G and RDBE
   output VDIF frames with a 64-bit sequence number prepended. Throwing
   away the sequence numbers would be an option, but *jive5ab* >= 2.8
   recording **udpsnor** keeps sequence number statistics from up to
   eight (8) individual senders. All data received on the configured UDP
   port number (see net_port=) will be recorded in order of delivery to
   *jive5ab* in stead of it attempting to reorder the frames based on
   sequence number. Thus udpsnor is great for local recording: it is
   simpler but still gives valuable and accurate packet statistics
   on-the-fly (see documentation for the evlbi? query).

OS_rev – Get details of operating system (query only)
=====================================================

Query syntax: OS_rev? ;

Query response: !OS_rev? <return code> : <OS field1> : <OS field2>: …..
: <OS fieldn> ; Purpose: Get detailed information about operating
system.

Monitor-only parameters:

+--------+---+---+-----------------------------------------------------+
| *      | * | * | **Comments**                                        |
| *Param | * | * |                                                     |
| eter** | T | V |                                                     |
|        | y | a |                                                     |
|        | p | l |                                                     |
|        | e | u |                                                     |
|        | * | e |                                                     |
|        | * | s |                                                     |
|        |   | * |                                                     |
|        |   | * |                                                     |
+========+===+===+=====================================================+
| <OSf   | l |   | Primarily for diagnostic purposes. The character    |
| ield1> | i |   | stream returned from OS, which is very long, is     |
| t      | t |   | divided into 32-character fields separated by       |
| hrough | e |   | colons to stay within Field System limits. See      |
| <OSf   | r |   | Notes.                                              |
| ieldn> | a |   |                                                     |
|        | l |   |                                                     |
|        | A |   |                                                     |
|        | S |   |                                                     |
|        | C |   |                                                     |
|        | I |   |                                                     |
|        | I |   |                                                     |
+--------+---+---+-----------------------------------------------------+

Notes:

1. ‘OS_rev?’ is a replacement for the old ‘OS_rev1?’ and ‘OS_rev2?’
   queries; all three of these queries are now synonyms.

packet – Set/get packet acceptance criteria
===========================================

Command syntax: packet = <DPOFST> : <DFOFST> : <length> : <PSN Mode> :
<PSNOFST> ; Command response: !packet = <return code>;

Query syntax: packet? ;

Query response: !packet? <return code> : <DPOFST> : <DFOFST> : <length>
: <PSN Mode> : <PSNOFST> ; Purpose: Set / get the packet acceptance
criteria.

Settable parameters:

+-------+----+---------+-----+-----------------------------------------+
| **P   | ** | **      | **  | **Comments**                            |
| arame | Ty | Allowed | Def |                                         |
| ter** | pe | v       | aul |                                         |
|       | ** | alues** | t** |                                         |
+=======+====+=========+=====+=========================================+
| <     | i  | >= 0     | 0   | payload byte offset from beginning of   |
| D     | nt |         |     | payload to first recorded data          |
| POFST |    |         |     |                                         |
| >     |    |         |     |                                         |
+-------+----+---------+-----+-----------------------------------------+
| <     | i  | >= 0     | 0   | payload byte offset to beginning of     |
| D     | nt |         |     | recording                               |
| FOFST |    |         |     |                                         |
| >     |    |         |     |                                         |
+-------+----+---------+-----+-----------------------------------------+
| <     | i  | > 0     | 5   | number of bytes to record per packet    |
| l     | nt |         | 008 | (see Note 1)                            |
| ength |    |         |     |                                         |
| >     |    |         |     |                                         |
+-------+----+---------+-----+-----------------------------------------+
| < PSN | i  | 0 \| 1  | 0   | Packet Serial Number (PSN) monitor mode |
| Mode  | nt | \| 2    |     | (see Note 2)                            |
| >     |    |         |     |                                         |
+-------+----+---------+-----+-----------------------------------------+
| <     | i  | >= 0     | 0   | payload byte offset from beginning of   |
| PS    | nt |         |     | payload to PSN (for PSN monitor mode 1  |
| NOFST |    |         |     | or 2)                                   |
| >     |    |         |     |                                         |
+-------+----+---------+-----+-----------------------------------------+

M onitor-only parameters:

+--------+----+------+------------------------------------------------+
| *      | ** | **   | **Comments**                                   |
| *Param | Ty | Valu |                                                |
| eter** | pe | es** |                                                |
|        | ** |      |                                                |
+========+====+======+================================================+
| <      | i  | >= 0  | payload byte offset from beginning of payload  |
| DPOFST | nt |      | to first recorded data                         |
| >      |    |      |                                                |
+--------+----+------+------------------------------------------------+
| <      | i  | >= 0  | payload byte offset to beginning of recording  |
| DFOFST | nt |      |                                                |
| >      |    |      |                                                |
+--------+----+------+------------------------------------------------+
| <      | i  | > 0  | number of bytes to record per packet (see Note |
| length | nt |      | 1)                                             |
| >      |    |      |                                                |
+--------+----+------+------------------------------------------------+
| < PSN  | i  | 0 \| | Packet Serial Number (PSN) monitor mode (see   |
| Mode > | nt | 1 \| | Note 2)                                        |
|        |    | 2    |                                                |
+--------+----+------+------------------------------------------------+
| <      | i  | >= 0  | payload byte offset from beginning of payload  |
| P      | nt |      | to PSN (for PSN monitor mode 1 or 2)           |
| SNOFST |    |      |                                                |
| >      |    |      |                                                |
+--------+----+------+------------------------------------------------+

Notes:

1. The length of data to be recorded must be a multiple of 8 bytes.
2. PSN-monitor 0 mode will disable packet serial number checking and
   record all data in the order received. PSN-monitor mode 1 will
   replace invalid packets with the specified fill pattern and guarantee
   order. PSN-monitor mode 2 will prevent packets from being written to
   disk if the most significant bit is set.

personality – Set/get personality (available on 5A/5B with *jive5ab* >= 2.8)
===============================================================================

Command syntax: personality = <type> : <root > ; Command response:
!personality = <return code> ;

Query syntax: personality? ;

Query response: !personality? <return code> : < type >: < root > ;
Purpose: Set / get the application personality (i.e., the emulation
mode).

Settable parameters:

+---+---+---+---+-----------------------------------------------------------+
| * | * | * | * | **Comments**                                              |
| * | * | * | * |                                                           |
| P | T | A | D |                                                           |
| a | y | l | e |                                                           |
| r | p | l | f |                                                           |
| a | e | o | a |                                                           |
| m | * | w | u |                                                           |
| e | * | e | l |                                                           |
| t |   | d | t |                                                           |
| e |   | v | * |                                                           |
| r |   | a | * |                                                           |
| * |   | l |   |                                                           |
| * |   | u |   |                                                           |
|   |   | e |   |                                                           |
|   |   | s |   |                                                           |
|   |   | * |   |                                                           |
|   |   | * |   |                                                           |
+===+===+===+===+===========================================================+
| < | c | m | N | Mark5C– Normal operating mode (default) [STRIKEOUT:file – |
| t | h | a | U | write data to the OS system disk, not streamstor (see     |
| y | a | r | L | Note 3)] Supported by *jive5ab* indirectly, see Note 5    |
| p | r | k | L | **Must** be left empty when issued to a Mark5A or Mark5B  |
| e |   | 5 |   | system (*jive5ab* >= 2.8)                                  |
| > |   | C |   |                                                           |
|   |   | \ |   |                                                           |
|   |   | | |   |                                                           |
|   |   | f |   |                                                           |
|   |   | i |   |                                                           |
|   |   | l |   |                                                           |
|   |   | e |   |                                                           |
+---+---+---+---+-----------------------------------------------------------+
| < | c | b | N | [STRIKEOUT:For <type> file this is the root file system   |
| r | h | a | U | path on where to store the incoming scans. (see Note 3)]  |
| o | a | n | L | Sent to Mark5C: for <type> Mark5C sets up the StreamStor  |
| o | r | k | L | disk module to store data in either bank or non-bank      |
| t |   | \ |   | mode. Sent to Mark5A/Mark5B: sets up StreamStor to store  |
| > |   | | |   | data in either bank or non-bank mode (*jive5ab* >= 2.8)    |
|   |   | n |   |                                                           |
|   |   | o |   |                                                           |
|   |   | n |   |                                                           |
|   |   | b |   |                                                           |
|   |   | a |   |                                                           |
|   |   | n |   |                                                           |
|   |   | k |   |                                                           |
|   |   | n |   |                                                           |
|   |   | o |   |                                                           |
|   |   | n |   |                                                           |
|   |   | b |   |                                                           |
|   |   | a |   |                                                           |
|   |   | n |   |                                                           |
|   |   | k |   |                                                           |
+---+---+---+---+-----------------------------------------------------------+

Monitor-only parameters:

+---+---+----+------------------------------------------------------------+
| * | * | ** | **Comments**                                               |
| * | * | Va |                                                            |
| P | T | lu |                                                            |
| a | y | es |                                                            |
| r | p | ** |                                                            |
| a | e |    |                                                            |
| m | * |    |                                                            |
| e | * |    |                                                            |
| t |   |    |                                                            |
| e |   |    |                                                            |
| r |   |    |                                                            |
| * |   |    |                                                            |
| * |   |    |                                                            |
+===+===+====+============================================================+
| < | c | ma | Mark5C– Normal operating mode (default), empty on Mark5A,  |
| t | h | rk | Mark5B (*jive5ab* >= 2.8) [STRIKEOUT:file – write data to   |
| y | a | 5C | the OS system disk, not streamstor (see Note 3)]           |
| p | r | \| |                                                            |
| e |   | [S |                                                            |
| > |   | TR |                                                            |
|   |   | IK |                                                            |
|   |   | EO |                                                            |
|   |   | UT |                                                            |
|   |   | :f |                                                            |
|   |   | il |                                                            |
|   |   | e] |                                                            |
+---+---+----+------------------------------------------------------------+
| < | c | sy | [STRIKEOUT:For <type> file this is the root file system    |
| r | h | st | path on where to store the incoming scans. (see Note 3)]   |
| o | a | em | Reply from Mark5C: for <type> Mark5C indicates if the      |
| o | r | pa | system is running in bank or non-bank mode Reply from      |
| t |   | th | Mark5A/Mark5B: indicates if the system is running in bank  |
| > |   | ba | or non-bank mode (*jive5ab* >= 2.8)                         |
|   |   | nk |                                                            |
|   |   | \| |                                                            |
|   |   | [S |                                                            |
|   |   | TR |                                                            |
|   |   | IK |                                                            |
|   |   | EO |                                                            |
|   |   | UT |                                                            |
|   |   | :n |                                                            |
|   |   | on |                                                            |
|   |   | b  |                                                            |
|   |   | an |                                                            |
|   |   | k] |                                                            |
+---+---+----+------------------------------------------------------------+

Notes:

1. [STRIKEOUT:A personality is here defined as a set of functions bound
   to the commands and queries described in this document. A possible
   implementation of this is for witch personality to have a setup()
   function and a shutdown() function that are called to initialize and
   clean up from a personality change, and a set of functions that are
   mapped to the commands and queries. Nothing is to preclude the
   various personalities from sharing a subset of functionality. The
   implementation of drs should make it easy to add new personalities to
   the program.]
2. This command cannot be issued while a delayed completion operation is
   in effect or while data is being recorded on any type of medium.
3. [STRIKEOUT:The file personality causes incoming data received through
   the system NIC to be written to a file system (e.g., RAID Array). The
   optional parameter should be a directory specifying the root of the
   file system to write. Files written to this directory will have
   systematically determined file names bearing close resemblance to
   scan names on Mark5 Modules.. Additionally, a file containing the
   equivalent of a Mark5 scan list will be created in the specified
   directory.]
4. Implementation of this command is optional; its absence will not
   imply non-conformance with the Mark5C software specification.
5. \ *jive5ab* >= 2.6.0 implements the ‘file’ personality as generic
   packet recorder using the FlexBuff/Mark6 recording model. For this to
   be enabled on Mark5C it is sufficient to issue ‘set_disks=’ and
   ‘record=on : …;’ (besides configuring the ‘mode=’) in a non-default
   runtime (see Section **6**, and **6.2** specifically).

play – Play data from from current play pointer position
========================================================

Command syntax: play = <play arm/on/off> : [<start play pointer>] :
[<ROT start>]; Command response: !play = <return code>;

Query syntax: play? ;

Query response: !play ? <return code> : <status> ;

Purpose: Initiate playback from disk data at current or specified
play-pointer position.

Settable parameters:

+---+---+---+---+----------------------------------------------------------------+
| * | * | * | * | **Comments**                                                   |
| * | * | * | * |                                                                |
| P | T | A | D |                                                                |
| a | y | l | e |                                                                |
| r | p | l | f |                                                                |
| a | e | o | a |                                                                |
| m | * | w | u |                                                                |
| e | * | e | l |                                                                |
| t |   | d | t |                                                                |
| e |   | v | * |                                                                |
| r |   | a | * |                                                                |
| * |   | l |   |                                                                |
| * |   | u |   |                                                                |
|   |   | e |   |                                                                |
|   |   | s |   |                                                                |
|   |   | * |   |                                                                |
|   |   | * |   |                                                                |
+===+===+===+===+================================================================+
| < | c | a | o | ‘arm’ - causes Mark5A to pre-fill buffer and prepare to play   |
| p | h | r | f | at position specified by field 2 – see Note 2. ‘on’ – causes   |
| l | a | m | f | playback to start at position specified by field 2. If field 2 |
| a | r | \ |   | is null, starts playback from current play pointer; Field 2    |
| y |   | | |   | should be null for ‘play=on’ command following a successful    |
| a |   | o |   | ‘play=arm’. ’off’ = stops playback (if active) and             |
| r |   | n |   | unconditionally updates playback pointer to current play       |
| m |   | \ |   | position or, if field 2 is non-null, to the position           |
| / |   | | |   | specified. See also Note 1. Cannot be issued while ‘record’ is |
| o |   | o |   | ‘on’ (error). In all play modes, all 64 output tracks are      |
| n |   | f |   | active; if fewer than 64 tracks were recorded, the recorded    |
| / |   | f |   | track set is duplicated to unused output tracks; see Note 2.   |
| o |   |   |   |                                                                |
| f |   |   |   |                                                                |
| f |   |   |   |                                                                |
| > |   |   |   |                                                                |
+---+---+---+---+----------------------------------------------------------------+
| < | i | > | c | Absolute byte number in recorded data stream; if null field,   |
| s | n | = | u | maintains current value; if <start play pointer> and <ROT      |
| t | t | 0 | r | start> are both null fields, play starts at current play       |
| a |   |   | r | pointer value.                                                 |
| r |   |   | e |                                                                |
| t |   |   | n |                                                                |
| p |   |   | t |                                                                |
| l |   |   | p |                                                                |
| a |   |   | l |                                                                |
| y |   |   | a |                                                                |
| p |   |   | y |                                                                |
| o |   |   | p |                                                                |
| i |   |   | n |                                                                |
| n |   |   | t |                                                                |
| t |   |   | r |                                                                |
| e |   |   |   |                                                                |
| r |   |   |   |                                                                |
| > |   |   |   |                                                                |
+---+---+---+---+----------------------------------------------------------------+
| < | i | # |   | For use with Mark 4 correlator only: cause play to start after |
| R | n | s |   | specified number of sysclk periods (counting from beginning of |
| O | t | y |   | year); sysclk frequency is normally 32MHz, so these can be     |
| T |   | s |   | very large numbers.                                            |
| s |   | c |   |                                                                |
| t |   | l |   |                                                                |
| a |   | k |   |                                                                |
| r |   | s |   |                                                                |
| t |   |   |   |                                                                |
| > |   |   |   |                                                                |
+---+---+---+---+----------------------------------------------------------------+

Monitor-only parameters:

+---+---+--------+---------------------------------------------------------+
| * | * | **Va   | **Comments**                                            |
| * | * | lues** |                                                         |
| P | T |        |                                                         |
| a | y |        |                                                         |
| r | p |        |                                                         |
| a | e |        |                                                         |
| m | * |        |                                                         |
| e | * |        |                                                         |
| t |   |        |                                                         |
| e |   |        |                                                         |
| r |   |        |                                                         |
| * |   |        |                                                         |
| * |   |        |                                                         |
+===+===+========+=========================================================+
| < | c | arming | ‘arming’ – arming is in progress ’armed’ – system is    |
| s | h | \|     | armed and ready to start play ‘on’ – playback active    |
| t | a | armed  | ‘off’ – playback inactive ’halted’ – playback stopped   |
| a | r | \| on  | due to reaching end-of-media or (end-of-scan when       |
| t |   | \| off | playback initiated by ‘scan_play’) ’waiting’ – delayed  |
| u |   | \|     | start of playback (special mode for correlator only)    |
| s |   | halted |                                                         |
| > |   | \|     |                                                         |
|   |   | w      |                                                         |
|   |   | aiting |                                                         |
+---+---+--------+---------------------------------------------------------+

Notes:

1. After play is turned ‘on’, the user should periodically query
   ‘status’ for details; if playback stops on its own accord (due to
   end-of-media, etc.), this will be reflected in the response to the
   ‘status’ query as ‘halted’, and a ‘play’ query will show the status
   as well; a subsequent command to turn play ‘off’ or ‘on’ will reset
   the relevant bits (9-8) in the ‘status’ response.
2. The ‘play=arm’ command causes the Mark 5A to prefill its buffers
   according to the prescribed position so that playing will start
   almost instantaneously after a subsequent ‘play=on’ command is
   issued; this is intended primarily for use at a correlator. The
   amount of time need to prefill the buffer can range from a few tens
   of msec to a few seconds. If all disks are good and all data have
   been recorded properly, the time will be relatively short; however,
   if difficulties with disks or recorded data are encountered during
   the prefill period, up to several seconds may be required. A ‘play?’
   query should be issued to verify the system is armed for playback
   before issuing a ‘play=on’ command. A ‘play=on’ without a preceding
   ‘play=arm’ will begin play, but after an indeterminate delay.
3. During playback initiated by a ‘scan_play’ command, a ‘play?’ query
   will indicated the playback status.
4. When playing back in a mode with fewer than 64 tracks, groups of
   tracks are duplicated so that all 64 track outputs are always active,
   as follows:

+---------------+------------+-----------------------------------------+
| **mode**      | **Primary  | **Duplicated playback tracks**          |
|               | playback   |                                         |
|               | tracks**   |                                         |
+===============+============+=========================================+
| ‘mark4:8’ or  | 2-16 even  | Duplicated to 3-17 odd, 18-32 even,     |
| ‘vlba:8’      | (headstack | 19-33 even on hdstk1; hdstk2 is         |
|               | 1)         | duplicate of hdstk1                     |
+---------------+------------+-----------------------------------------+
| ‘mark4:16’ or | 2-33 even  | Duplicated to 2-33 even on hdstk1;      |
| ‘vlba:16’ -   | (headstack | hdstk2 is duplicate of hdstk1           |
| 16            | 1)         |                                         |
+---------------+------------+-----------------------------------------+
| ‘mark4:32’ or | 2-33 all   | Headstack 1 output is duplicated to     |
| ‘vlba:32’ -   | (headstack | Headstack 2                             |
| 32 tks        | 1)         |                                         |
+---------------+------------+-----------------------------------------+
| ‘mark4:64’ or | 2-33       | None                                    |
| ‘vlba:64’ -   | (          |                                         |
| 64 tks        | headstacks |                                         |
|               | 1 and 2)   |                                         |
+---------------+------------+-----------------------------------------+
| ‘st’ (any     | 2-33 all   | Headstack 1 output is duplicated to     |
| submode) – 32 | (headstack | Headstack 2                             |
| tks           | 1)         |                                         |
+---------------+------------+-----------------------------------------+
| tvg           | equivalent | Headstack 1 output is duplicated to     |
|               | to tracks  | Headstack 2                             |
|               | 2-33       |                                         |
+---------------+------------+-----------------------------------------+

Playback of ‘Mark5A+n:k’ data is to same set of output tracks as for
‘vlba:k’ data.

5. Note that record/play pointers may have values as large as ~2x1013
   (~44 bits), so pointer arithmetic must be handled appropriately.
6. Playback clock rate is set by the ‘play_rate’ command
7. When playing, the playback pointer will update to show the
   approximate position. If the playback pointer is noted not to be
   incrementing, an error flag is set in the ‘status?’ query which can
   be used as a first order check of proper playback.

play_rate – Set playback data rate; set tvg rate
================================================

Command syntax: play_rate = <play rate reference> : <rate> ; Command
response: !play_rate = <return code> ;

Query syntax: play_rate? ;

Query response: !play_rate ? <return code> : <track data rate> : <track
clock rate> : <clockgen freq> ; Purpose: Set the playback rate
(specified as <track data rate>, <track clock rate> or <clock generator
frequency>. Settable parameters:

+---+---+-----+---+-------------------------------------------------------+
| * | * | **A | * | **Comments**                                          |
| * | * | llo | * |                                                       |
| P | T | wed | D |                                                       |
| a | y | va  | e |                                                       |
| r | p | lue | f |                                                       |
| a | e | s** | a |                                                       |
| m | * |     | u |                                                       |
| e | * |     | l |                                                       |
| t |   |     | t |                                                       |
| e |   |     | * |                                                       |
| r |   |     | * |                                                       |
| * |   |     |   |                                                       |
| * |   |     |   |                                                       |
+===+===+=====+===+=======================================================+
| < | c | d   | c | ‘data’ – set output track data rate (not including    |
| p | h | ata | l | parity) to specified value. ’clock’ – set output      |
| l | a | \|  | o | track clock rate (including parity) to specified      |
| a | r | cl  | c | value. ’clockgen’ – set clock generator chip to       |
| y |   | ock | k | specified frequency; max is 40 MHz.. ’ext’ – external |
| r |   | \|  |   | clock select See also Notes below.                    |
| a |   | cl  |   |                                                       |
| t |   | ock |   |                                                       |
| e |   | gen |   |                                                       |
| r |   | \|  |   |                                                       |
| e |   | ext |   |                                                       |
| f |   |     |   |                                                       |
| e |   |     |   |                                                       |
| r |   |     |   |                                                       |
| e |   |     |   |                                                       |
| n |   |     |   |                                                       |
| c |   |     |   |                                                       |
| e |   |     |   |                                                       |
| > |   |     |   |                                                       |
+---+---+-----+---+-------------------------------------------------------+
| < | r | MHz | 8 | >0 – set rate to specified value; freq resolution of  |
| r | e |     |   | clock generator chip is ~20 mHz. If in ‘tvg’ mode,    |
| a | a |     |   | sets on-board clock generator rate regardless of      |
| t | l |     |   | value of <play rate reference>; see Notes.            |
| e |   |     |   |                                                       |
| > |   |     |   |                                                       |
+---+---+-----+---+-------------------------------------------------------+

Monitor-only parameters:

+-----------+----+-----+-----------------------------------------------+
| **Pa      | ** | *   | **Comments**                                  |
| rameter** | Ty | *Va |                                               |
|           | pe | lue |                                               |
|           | ** | s** |                                               |
+===========+====+=====+===============================================+
| <track    | re | M   | Track data rate (without parity); =0 if       |
| data      | al | bps | external clock selected                       |
| rate>     |    |     |                                               |
+-----------+----+-----+-----------------------------------------------+
| <track    | re | MHz | Track clock rate; see Note 1 for relationship |
| clock     | al |     | to <track data rate>                          |
| rate>     |    |     |                                               |
+-----------+----+-----+-----------------------------------------------+
| <clockgen | re | MHz | Internal clock generator frequency; see Note  |
| freq>     | al |     | 1 for relationship to <track data rate>       |
+-----------+----+-----+-----------------------------------------------+

Notes:

1. For a given operating mode, the relationships between <track data
   rate>, <track clock rate> and <clockgen freq> are as follows:

+------+--------+---------------+------------+-----------+----------+
| **mo | **     | **Typical     | **Cor      | **Corr    | **Total  |
| de:s | <track | ‘standard’    | responding | esponding | playback |
| ubmo | data   | values of     | <track     | <         | data     |
| de** | rate>  | <track data   | clock      | clockgen> | rate     |
|      | (M     | rate>**       | rate>      | frq       | (Mbps)** |
|      | bps)** |               | (MHz)**    | (MHz)**   |          |
+======+========+===============+============+===========+==========+
| st:m | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 9         | 32*9/    |
| ark4 |        | \| 16         |            | /8\*\ *f* | 8\*\ *f* |
+------+--------+---------------+------------+-----------+----------+
| st:  | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 9         | 32*9/    |
| vlba |        |               |            | /8\*\ *f* | 8\*\ *f* |
+------+--------+---------------+------------+-----------+----------+
| mar  | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 9         | 8\*\ *f* |
| k4:8 |        | \| 16         |            | /8\*\ *f* |          |
+------+--------+---------------+------------+-----------+----------+
| mark | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 9         | 1        |
| 4:16 |        | \| 16         |            | /8\*\ *f* | 6\*\ *f* |
+------+--------+---------------+------------+-----------+----------+
| mark | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 9         | 3        |
| 4:32 |        | \| 16         |            | /8\*\ *f* | 2\*\ *f* |
+------+--------+---------------+------------+-----------+----------+
| mark | *f*    | 2 \| 4 \| 8   | 9/8\*\ *f* | 2*9       | 6        |
| 4:64 |        | \| 16         |            | /8\*\ *f* | 4\*\ *f* |
+------+--------+---------------+------------+-----------+----------+
| vl   | *f*    | 2 \| 4 \| 8   | 1.008*     | 1.008*9   | 1.008*   |
| ba:8 |        |               | 9/8\*\ *f* | /8\*\ *f* | 8\*\ *f* |
| *or* |        |               |            |           |          |
| ma   |        |               |            |           |          |
| rk5a |        |               |            |           |          |
| +n:8 |        |               |            |           |          |
+------+--------+---------------+------------+-----------+----------+
| vlb  | *f*    | 2 \| 4 \| 8   | 1.008*     | 1.008*9   | 1.008*1  |
| a:16 |        |               | 9/8\*\ *f* | /8\*\ *f* | 6\*\ *f* |
| *or* |        |               |            |           |          |
| mar  |        |               |            |           |          |
| k5a+ |        |               |            |           |          |
| n:16 |        |               |            |           |          |
+------+--------+---------------+------------+-----------+----------+
| vlb  | *f*    | 2 \| 4 \| 8   | 1.008*     | 1.008*9   | 1.008*3  |
| a:32 |        |               | 9/8\*\ *f* | /8\*\ *f* | 2\*\ *f* |
| *or* |        |               |            |           |          |
| mar  |        |               |            |           |          |
| k5a+ |        |               |            |           |          |
| n:32 |        |               |            |           |          |
+------+--------+---------------+------------+-----------+----------+
| vlb  | *f*    | 2 \| 4 \| 8   | 1.008*     | 2*1.008*9 | 1.008*6  |
| a:64 |        |               | 9/8\*\ *f* | /8\*\ *f* | 4\*\ *f* |
| *or* |        |               |            |           |          |
| mar  |        |               |            |           |          |
| k5a+ |        |               |            |           |          |
| n:64 |        |               |            |           |          |
+------+--------+---------------+------------+-----------+----------+
| tvg  | *f*    | any up to 40  | *f*        | *f*       | 3        |
| (see |        |               |            |           | 2\*\ *f* |
| Note |        |               |            |           |          |
| 4)   |        |               |            |           |          |
+------+--------+---------------+------------+-----------+----------+

2. Upon a ‘mode’ change, the *jive5ab* software automatically makes any
   necessary adjustments to the clock generator to meet the current
   <track data rate> value (e.g. as returned by a ‘play_rate?’ query).
3. The value of the ‘play_rate’ parameters has no effect when recording
   data from an external source; the recording rate is strictly
   determined by the operating mode and input clock frequency. However,
   when using the ‘rtime?’ query to determine the remaining available
   recording time, the ‘play_rate’ parameters must correspond to the
   input data rate.
4. When recording or playing tvg data, <rate> record/playback aggregate
   rate is always 32*<rate>; set <rate> equal to 32 for tvg
   record/playback at 1024Mbps.
5. The maximum clock generator rate is 40 MHz, which results in
   corresponding maximum <track data rates> and <track clock rates> as
   follows:

+------------------+-------------------------+-------------------------+
| **data           | **<track data rate>**   | **<track clock rate>**  |
| mode:submode**   | **(Mbps)**              | **(MHz)**               |
+==================+=========================+=========================+
| st:mark4         | 35.56                   | 40                      |
+------------------+-------------------------+-------------------------+
| st:vlba          | 35.27                   | 40                      |
+------------------+-------------------------+-------------------------+
| mark4:8          | 35.56                   | 40                      |
+------------------+-------------------------+-------------------------+
| mark4:16         | 35.56                   | 40                      |
+------------------+-------------------------+-------------------------+
| mark4:32         | 35.56                   | 40                      |
+------------------+-------------------------+-------------------------+
| mark4:64         | 17.78                   | 20                      |
+------------------+-------------------------+-------------------------+
| vlba:8 *or*      | 35.27                   | 40                      |
| mark5a+n:8       |                         |                         |
+------------------+-------------------------+-------------------------+
| vlba:16 *or*     | 35.27                   | 40                      |
| mark5a+n:16      |                         |                         |
+------------------+-------------------------+-------------------------+
| vlba:32 *or*     | 35.27                   | 40                      |
| mark5a+n:32      |                         |                         |
+------------------+-------------------------+-------------------------+
| vlba:64 *or*     | 17.64                   | 20                      |
| mark5a+n:64      |                         |                         |
+------------------+-------------------------+-------------------------+
| tvg              | 40                      | 40                      |
+------------------+-------------------------+-------------------------+

6. *jive5ab* supports the play_rate on Mark5B/DOM and generic systems to
   allow it to specify Mark4/VLBA data format. Use in conjunction with
   “mode=mark4:…” or “mode=vlba:…” to set the complete data format.

pointers – Get current value of record, start-scan and stop-scan pointers (query only)
======================================================================================

Query syntax: pointers? ;

Query response: !pointers? <record pointer> : <start-scan pointer> :
<stop-scan pointer>; Purpose: Get current value of record, start-scan
and stop-scan pointers

Monitor-only parameters:

+-------+---+---+------------------------------------------------------+
| **P   | * | * | **Comments**                                         |
| arame | * | * |                                                      |
| ter** | T | V |                                                      |
|       | y | a |                                                      |
|       | p | l |                                                      |
|       | e | u |                                                      |
|       | * | e |                                                      |
|       | * | s |                                                      |
|       |   | * |                                                      |
|       |   | * |                                                      |
+=======+===+===+======================================================+
| <r    | i | b | If stopped, returns position at which ‘record=on’    |
| ecord | n | y | command will begin recording (always appends to      |
| poi   | t | t | existing); if recording, returns current record      |
| nter> |   | e | position.                                            |
|       |   | s |                                                      |
+-------+---+---+------------------------------------------------------+
| <     | i | b | Current value of <start-scan pointer>                |
| start | n | y |                                                      |
| -scan | t | t |                                                      |
| poi   |   | e |                                                      |
| nter> |   | s |                                                      |
+-------+---+---+------------------------------------------------------+
| <stop | i | b | Current value of <stop-scan pointer>; ’-‘ if         |
| -scan | n | y | undefined.                                           |
| poi   | t | t |                                                      |
| nter> |   | e |                                                      |
|       |   | s |                                                      |
+-------+---+---+------------------------------------------------------+

Notes:

1. Note that the returned byte numbers may have values as large as
   ~2x1013 (~44 bits), so pointer arithmetic must be handled
   appropriately.
2. When recording, the <record pointer> will be updated to show the
   approximate current recording position. If the record pointer is
   noted not to be incrementing during recording, an error flag is set
   in the ‘status?’ query which can be used as a first order check of
   proper operation.

position – Get current value of record and play pointers (query only)
=====================================================================

Query syntax: position? ;

Query response: !position? <return code> : <record pointer> : <play
pointer>; Purpose: Get current value of record and play pointers

Monitor-only parameters:

+------+---+---+-------------------------------------------------------+
| *    | * | * | **Comments**                                          |
| *Par | * | * |                                                       |
| amet | T | V |                                                       |
| er** | y | a |                                                       |
|      | p | l |                                                       |
|      | e | u |                                                       |
|      | * | e |                                                       |
|      | * | s |                                                       |
|      |   | * |                                                       |
|      |   | * |                                                       |
+======+===+===+=======================================================+
| <re  | i | b | If stopped, returns position at which ‘record=on’     |
| cord | n | y | command will begin recording (always appends to       |
| poin | t | t | existing); if recording, returns current record       |
| ter> |   | e | position.                                             |
|      |   | s |                                                       |
+------+---+---+-------------------------------------------------------+
| <    | i | b | Current value of <play pointer>                       |
| play | n | y |                                                       |
| poin | t | t |                                                       |
| ter> |   | e |                                                       |
|      |   | s |                                                       |
+------+---+---+-------------------------------------------------------+

Notes:

1. Note that the returned byte numbers may have values as large as
   ~2x1013 (~44 bits), so pointer arithmetic must be handled
   appropriately.
2. When recording, the <record pointer> will be updated to show the
   approximate current recording position. If the record pointer is
   noted not to be incrementing during recording, an error flag is set
   in the ‘status?’ query which can be used as a first order check of
   proper operation.

protect – Set write protection for active module
================================================

Command syntax: protect = <on \| off> ; Command response: !protect =
<return code> ;

Query syntax: protect? ;

Query response: !protect? <return code> : <protect on/off>; Purpose: Set
write protection on/off for active disk module Settable parameters:

+--------------+---------+-------------------+------------+-------------+
| *            | *       | **Allowed         | *          | *           |
| *Parameter** | *Type** | values**          | *Default** | *Comments** |
+==============+=========+===================+============+=============+
| <off>        | char    | on \| off         | off        |             |
+--------------+---------+-------------------+------------+-------------+

Notes:

1. A ‘protect=on’ command prevents any additional writing to module.
2. A ’protect=off” command allows writing to a module.
3. A ‘protect=off’ command is required to *immediately* precede a
   ‘reset=erase’, ‘reset=erase_last_scan’ or ‘VSN=…’ command, even if
   protection is already off. This protects the module from any
   accidental erasure or rewriting of the VSN.

record – Turn recording on|off; assign scan label (5A, 5B/DIM, 5C)
==================================================================

Command syntax: record = <record on/off> : <scan label/name> :
[<experiment name>] : [<station code>] ; Command response: !record =
<return code> ;

Query syntax: record? ;

Query response: !record ? <return code> : <status>: <scan#> : <scan
label> ; Purpose: Turn recording on|off; assign scan name, experiment
name and station code

Settable parameters:

+---+---+---+---+-------------------------------------------------------------+
| * | * | * | * | **Comments**                                                |
| * | * | * | * |                                                             |
| P | T | A | D |                                                             |
| a | y | l | e |                                                             |
| r | p | l | f |                                                             |
| a | e | o | a |                                                             |
| m | * | w | u |                                                             |
| e | * | e | l |                                                             |
| t |   | d | t |                                                             |
| e |   | v | * |                                                             |
| r |   | a | * |                                                             |
| * |   | l |   |                                                             |
| * |   | u |   |                                                             |
|   |   | e |   |                                                             |
|   |   | s |   |                                                             |
|   |   | * |   |                                                             |
|   |   | * |   |                                                             |
+===+===+===+===+=============================================================+
| < | c | o |   | ‘on’ automatically appends to the end of the existing       |
| r | h | n |   | recording. ’off’ stops recording and leaves system in       |
| e | a | \ |   | ‘idle’ mode.                                                |
| c | r | | |   |                                                             |
| o |   | o |   |                                                             |
| r |   | f |   |                                                             |
| d |   | f |   |                                                             |
| o |   |   |   |                                                             |
| n |   |   |   |                                                             |
| / |   |   |   |                                                             |
| o |   |   |   |                                                             |
| f |   |   |   |                                                             |
| f |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 3 |   | Relevant only if record is ‘on’. If in <scan label> format, |
| s | S | 2 |   | field is parsed for <exp name>, <station code> and <scan    |
| c | C | c |   | name>. Otherwise, interpreted as <scan name>, in which case |
| a | I | h |   | <experiment name> and <station code> should be specified    |
| n | I | a |   | separately. If <scan name> is duplicate of already-recorded |
| n |   | r |   | scan, a suffix will be added to the <scan name> part of the |
| a |   | s |   | <scan label> – see Note 6.                                  |
| m |   | m |   |                                                             |
| e |   | a |   |                                                             |
| > |   | x |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 8 |   | Experiment name; ignored if <record on/off> is ‘off’        |
| e | S | c |   |                                                             |
| x | C | h |   |                                                             |
| p | I | a |   |                                                             |
| e | I | r |   |                                                             |
| r |   | s |   |                                                             |
| i |   | m |   |                                                             |
| m |   | a |   |                                                             |
| e |   | x |   |                                                             |
| n |   |   |   |                                                             |
| t |   |   |   |                                                             |
| n |   |   |   |                                                             |
| a |   |   |   |                                                             |
| m |   |   |   |                                                             |
| e |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 8 |   | Station code; ignored if <record on/off> is ‘off’           |
| s | S | c |   |                                                             |
| t | C | h |   |                                                             |
| a | I | a |   |                                                             |
| t | I | r |   |                                                             |
| i |   | s |   |                                                             |
| o |   | m |   |                                                             |
| n |   | a |   |                                                             |
| c |   | x |   |                                                             |
| o |   |   |   |                                                             |
| d |   |   |   |                                                             |
| e |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+

Monitor-only parameters:

+----+---+------------------+------------------------------------------+
| *  | * | **Values**       | **Comments**                             |
| *P | * |                  |                                          |
| ar | T |                  |                                          |
| am | y |                  |                                          |
| et | p |                  |                                          |
| er | e |                  |                                          |
| ** | * |                  |                                          |
|    | * |                  |                                          |
+====+===+==================+==========================================+
| <s | c | on \| off \|     | ‘halted’ indicates end-of-media was      |
| ta | h | halted \|        | encountered while recording.             |
| tu | a | throttled \|     | ’throttled’, ‘overflow’ and ‘waiting’    |
| s> | r | overflow \|      | are all error conditions.                |
|    |   | waiting          |                                          |
+----+---+------------------+------------------------------------------+
| <  | i |                  | Sequential scan number; starts at 1 for  |
| sc | n |                  | first recorded scan.                     |
| an | t |                  |                                          |
| #> |   |                  |                                          |
+----+---+------------------+------------------------------------------+
| <  | A |                  | Scan label – see Notes 5 & 6. See        |
| sc | S |                  | Section 6 for definition of scan label.  |
| an | C |                  |                                          |
| la | I |                  |                                          |
| be | I |                  |                                          |
| l> |   |                  |                                          |
+----+---+------------------+------------------------------------------+

Notes:

1. After record is turned ‘on’, the user should periodically query
   ‘status’ for details; if recording stops on its own accord (due to
   end-of- media, etc.), this will be reflected in the response to the
   ‘status’ query as ‘recording stopped’, and a ‘record’ query will show
   the status as ‘halted’; a subsequent command to turn record ‘off’ or
   ‘on’ will reset the relevant bits (5-4) in the ‘status’ response.
2. When recording, the record pointer will update to show the
   approximate position. If the record pointer is noted not to be
   incrementing, an error flag is set in the ‘status?’ query which can
   be used as a first order check of proper recording.
3. When <status> is ‘off’, a ‘record?’ query returns the <scan label> of
   the last recorded scan, if any.
4. Typical causes for status errors:

   1. “throttled” – data rate from Mark 5B I/O card is too fast for
      disks to keep up (flag received by I/O board from StreamStor card)
   2. “overflow” – FIFO overflow on Mark 5B I/O card
   3. “waiting” – CLOCK has stopped or is faulty

5. The <scan label> field is created in the standardized format
   specified in Section 6, namely ‘<exp name>\ *<station code>*\ <scan
   name>’. If <experiment name> and/or <station code> are null, they
   will be replaced with ‘EXP’ and ‘STN’, respectively.
6. An attempt to record a scan with a duplicate scan name on the same
   disk module will cause a trailing alphabetical character (‘a-z’, then
   ‘A-Z’) to be automatically appended to the scan name (example:
   ‘312-1245a’). If more than 52 scans with same user-specified name,
   the suffix sequence will repeat.
7. Starting from jive5ab >= 2.8.2 the ‘record=’ command will check
   wether the requested data rate can be handled by the underlying
   hardware. This defect was found due to a station attempting to record
   2Gbps on a plain Mark5B/DIM (a 5B+/DIM is needed for this), resulting
   in 100% corrupted data.

record – Turn recording on|off; assign scan label; configure Mark6/FlexBuff record setup (G)
============================================================================================

Command syntax: record = <on/off> : <scan label/name> : [<experiment
name>] : [<station code>] ; record = mk6 : <recording format> ;

record = nthread : <nReaders> : <nWriters> ; Command response: !record =
<return code> ;

Query syntax: record? [ mk6 \| nthread ] ;

Query response: !record ? <return code> : <status> : <scan#> : <scan
label> : <byte count> ;

!record ? <return code> : <recording format> ;

!record ? <return code> : <nReaders> : <nWriters> ;

Purpose: Turn recording on|off; assign scan name, experiment name and
station code. Additions were introduced in *jive5ab* 2.6.2.

Settable parameters:

+---+---+---+---+-------------------------------------------------------------+
| * | * | * | * | **Comments**                                                |
| * | * | * | * |                                                             |
| P | T | A | D |                                                             |
| a | y | l | e |                                                             |
| r | p | l | f |                                                             |
| a | e | o | a |                                                             |
| m | * | w | u |                                                             |
| e | * | e | l |                                                             |
| t |   | d | t |                                                             |
| e |   | v | * |                                                             |
| r |   | a | * |                                                             |
| * |   | l |   |                                                             |
| * |   | u |   |                                                             |
|   |   | e |   |                                                             |
|   |   | s |   |                                                             |
|   |   | * |   |                                                             |
|   |   | * |   |                                                             |
+===+===+===+===+=============================================================+
| < | c | o |   | ‘on’ automatically appends to the end of the existing       |
| r | h | n |   | recording. ’off’ stops recording and leaves system in       |
| e | a | \ |   | ‘idle’ mode. ‘mk6’ sets Mark6 or FlexBuff recording mode    |
| c | r | | |   | depending on next argument. See Note 4. ‘nthread’ sets      |
| o |   | o |   | number of simultaneous network reader and/or disk writer    |
| r |   | f |   | threads. See Note 5.                                        |
| d |   | f |   |                                                             |
| o |   | m |   |                                                             |
| n |   | k |   |                                                             |
| / |   | 6 |   |                                                             |
| o |   | n |   |                                                             |
| f |   | t |   |                                                             |
| f |   | h |   |                                                             |
| > |   | r |   |                                                             |
|   |   | e |   |                                                             |
|   |   | a |   |                                                             |
|   |   | d |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 3 | 0 | Relevant only if record is ‘on’. If in <scan label> format, |
| s | S | 2 | 1 | field is parsed for <exp name>, <station code> and <scan    |
| c | C | c |   | name>. Otherwise, interpreted as <scan name>, in which case |
| a | I | h |   | <experiment name> and <station code> should be specified    |
| n | I | a |   | separately. If <scan name> is duplicate of already-recorded |
| n | i | r |   | scan, a suffix will be added to the <scan name> part of the |
| a | n | s |   | <scan label> – see Note 3. ‘0’, ‘1’ – only allowed when     |
| m | t | m |   | <record on/off> = ‘mk6’, set Mark6 or FlexBuff record mode. |
| e | i | a |   | See Note 4. The default for this value can be set from the  |
| > | n | x |   | command line. The compiled in default is ‘0’. 1 - <n> -     |
| < | t | 0 |   | when <record on/off> = ‘nthread’. See Note 5.               |
| r |   | | |   |                                                             |
| e |   | 1 |   |                                                             |
| c |   | 1 |   |                                                             |
| o |   | - |   |                                                             |
| r |   | < |   |                                                             |
| d |   | n |   |                                                             |
| i |   | > |   |                                                             |
| n |   |   |   |                                                             |
| g |   |   |   |                                                             |
| f |   |   |   |                                                             |
| o |   |   |   |                                                             |
| r |   |   |   |                                                             |
| m |   |   |   |                                                             |
| a |   |   |   |                                                             |
| t |   |   |   |                                                             |
| > |   |   |   |                                                             |
| < |   |   |   |                                                             |
| n |   |   |   |                                                             |
| R |   |   |   |                                                             |
| e |   |   |   |                                                             |
| a |   |   |   |                                                             |
| d |   |   |   |                                                             |
| e |   |   |   |                                                             |
| r |   |   |   |                                                             |
| s |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 8 | 1 | Experiment name; ignored if <record on/off> is ‘off’ or     |
| e | S | c |   | ‘mk6’ 1 - <n> when <record on/off> is ‘nthread’. See Note   |
| x | C | h |   | 5.                                                          |
| p | I | a |   |                                                             |
| e | I | r |   |                                                             |
| r | i | s |   |                                                             |
| i | n | m |   |                                                             |
| m | t | a |   |                                                             |
| e |   | x |   |                                                             |
| n |   | 1 |   |                                                             |
| t |   | - |   |                                                             |
| n |   | < |   |                                                             |
| a |   | n |   |                                                             |
| m |   | > |   |                                                             |
| e |   |   |   |                                                             |
| > |   |   |   |                                                             |
| < |   |   |   |                                                             |
| n |   |   |   |                                                             |
| W |   |   |   |                                                             |
| r |   |   |   |                                                             |
| i |   |   |   |                                                             |
| t |   |   |   |                                                             |
| e |   |   |   |                                                             |
| r |   |   |   |                                                             |
| s |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+
| < | A | 8 |   | Station code; ignored if <record on/off> is ‘off’, ‘mk6’ or |
| s | S | c |   | ‘nthread’                                                   |
| t | C | h |   |                                                             |
| a | I | a |   |                                                             |
| t | I | r |   |                                                             |
| i |   | s |   |                                                             |
| o |   | m |   |                                                             |
| n |   | a |   |                                                             |
| c |   | x |   |                                                             |
| o |   |   |   |                                                             |
| d |   |   |   |                                                             |
| e |   |   |   |                                                             |
| > |   |   |   |                                                             |
+---+---+---+---+-------------------------------------------------------------+

Monitor-only parameters:

+-------+---+--------+-------------------------------------------------+
| **P   | * | **Va   | **Comments**                                    |
| arame | * | lues** |                                                 |
| ter** | T |        |                                                 |
|       | y |        |                                                 |
|       | p |        |                                                 |
|       | e |        |                                                 |
|       | * |        |                                                 |
|       | * |        |                                                 |
+=======+===+========+=================================================+
| <st   | c | active | ‘active’ if the system is currently recording,  |
| atus> | h | \|     | ‘inactive’ otherwise. \ **on/off since jive5ab  |
| <reco | a | in     | 2.8.1** ‘0’, ‘1’, indicating Mark6 (‘1’) or     |
| rding | r | active | FlexBuff mode (‘0’), if query argument was      |
| fo    | i | \| on  | ‘mk6’ configured number of network reader       |
| rmat> | n | \| off | threads if query argument was ‘nthread’         |
| <nRea | t | 0 \| 1 |                                                 |
| ders> | i | 1-<n>  |                                                 |
|       | n |        |                                                 |
|       | t |        |                                                 |
+-------+---+--------+-------------------------------------------------+
| <s    | i |        | Sequential scan number; starts at 1 for first   |
| can#> | n |        | recorded scan.Not returned if query argument    |
| <nWri | t |        | was ‘mk6’ configured number of disk writer      |
| ters> | i |        | threads if query argument was ‘nthread’         |
|       | n |        |                                                 |
|       | t |        |                                                 |
+-------+---+--------+-------------------------------------------------+
| <scan | A |        | Scan label – see Notes 5 & 6. See Section 6 for |
| l     | S |        | definition of scan label. Id.                   |
| abel> | C |        |                                                 |
|       | I |        |                                                 |
|       | I |        |                                                 |
+-------+---+--------+-------------------------------------------------+
| <byte | i |        | Count of how many bytes have been recorded      |
| c     | n |        | since record=on. Used by m5copy.                |
| ount> | t |        |                                                 |
+-------+---+--------+-------------------------------------------------+

Notes:

1. When <status> is ‘off’, a ‘record?’ query returns the <scan label> of
   the last recorded scan, if any.
2. The <scan label> field is created in the standardized format
   specified in Section 6, namely ‘<exp name>\ *<station code>*\ <scan
   name>’. If <experiment name> and/or <station code> are null, they
   will be replaced with ‘EXP’ and ‘STN’, respectively.
3. An attempt to record a scan with a duplicate scan name on the same
   disk module will cause a trailing alphabetical character (‘a-z’, then
   ‘A-Z’) to be automatically appended to the scan name (example:
   ‘312-1245a’). [STRIKEOUT:If more than 52 scans with same
   user-specified name, the suffix sequence will repeat.] On
   FlexBuff/Mark6 this is impossible and an error + refusal to record
   will happen if an attempt is made to record more than 52 scans with
   the same label.
4. If the mk6 value is set to ‘1’, next time a recording is started, the
   data will be recorded in MIT Haystack d-plane file version 2.0
   compatible format. This means one file per disk (on all disks
   returned by set_disks?) by the name of <scan label>. If the mk6 value
   is ‘0’, data will be recorded in FlexBuff mode: data will be striped
   across all disks and stored in files of a configurable size (see
   net_protocol=) in a directory per disk, called <scan label>/. The
   data are stored in files named <scan_label>.<sequence nr>.
5. Recording on Mark6/FlexBuff requires tuning the number of parallel
   disk writers to a combination of recorded data rate and number of CPU
   cores available to *jive5ab*. The default number if disk writers is
   ‘1’. On modern Mark6/FlexBuff platforms this should be sufficient for
   operation up to 8192 Mbps. For 16384 Mbps recording set nWriter to a
   value in the ball park of 3 (three) or four (4). Configuring less
   possibly yields in not-enough-performance whilst higher values will
   result in many threads competing for the same resource, also hurting
   performance. The number of network readers (nReaders) is settable and
   queryable but not yet actively *used* in *jive5ab* 2.8!

recover – Recover record pointer which was reset abnormally during recording
============================================================================

Command syntax: recover = <recovery mode> ;

Command response: !recover = <return code> : <recovery mode>;

Query syntax: recover? ;

Query response: !recover ? <return code> ;

Purpose: Recover record pointer which was reset abnormally during
recording. Settable parameters:

+---+---+---+---+--------------------------------------------------------+
| * | * | * | * | **Comments**                                           |
| * | * | * | * |                                                        |
| P | T | A | D |                                                        |
| a | y | l | e |                                                        |
| r | p | l | f |                                                        |
| a | e | o | a |                                                        |
| m | * | w | u |                                                        |
| e | * | e | l |                                                        |
| t |   | d | t |                                                        |
| e |   | v | * |                                                        |
| r |   | a | * |                                                        |
| * |   | l |   |                                                        |
| * |   | u |   |                                                        |
|   |   | e |   |                                                        |
|   |   | s |   |                                                        |
|   |   | * |   |                                                        |
|   |   | * |   |                                                        |
+===+===+===+===+========================================================+
| < | i | 0 | 1 | 0 – attempt to recover data from scan that was         |
| r | n | \ |   | terminated abnormally during recording; see Note 1. 1  |
| e | t | | |   | – attempt to recover from accidental use of ‘sstest’   |
| c |   | 1 |   | or ‘WRSpeed Test’; see Note 2. 2 – attempt to recover  |
| o |   | \ |   | from StreamStor abnormality; see Note 3.               |
| v |   | | |   |                                                        |
| e |   | 2 |   |                                                        |
| r |   |   |   |                                                        |
| y |   |   |   |                                                        |
| m |   |   |   |                                                        |
| o |   |   |   |                                                        |
| d |   |   |   |                                                        |
| e |   |   |   |                                                        |
| > |   |   |   |                                                        |
+---+---+---+---+--------------------------------------------------------+

Notes:

1. A scan terminated abnormally during recording (for example, by a
   power failure or a keyswitch being accidentally turned to the ‘off’
   position) will not be accessible unless special actions are taken to
   recover it by forcing the record pointer to the end of recorded data;
   the scan will be overwritten if a new ‘record=on’ command is issued
   before a recovery attempt is made. It is suggested that a
   ‘record=off’ command be tried before a ‘recover=0’ command; this will
   not cause any harm and might fix the problem by itself. It has also
   been reported that success with ‘recover=0’ is demonstrably higher if
   a ‘scan_set’ command to select a scan [seemingly any scan, but
   perhaps preferably to the last (incomplete) scan] is issued before
   the ‘recover=0’ is attempted.
2. The utility programs ‘*sstest*’ and ‘*WRSpeedTest*’ will overwrite
   any existing data near the beginning of a disk module, but will
   prevent access to user data recorded beyond that point. A ‘recover=1’
   command will attempt to recover the data beyond the overwritten
   section; the overwritten data are irrecoverable.
3. (Most common) Try recover=2 to recover data that were erased, or if
   the record pointer has been set to a point near the beginning (often
   to zero).

replaced_blks – Get number of replaced blocks on playback (query only)
======================================================================

Query syntax: replaced_blks? ;

Query response: !replaced_blks? <return code> : <disk 0> : <disk 1> :
<disk 2> : <disk 3> : < disk 4> : <disk 5> :

<disk 6> : <disk 7> : <total replaced blks> ;

Purpose: Get number of replaced blocks during playback on disk-by-disk
basis. Monitor-only parameters:

+--------------------+-------+---------+------------------------------+
| **Parameter**      | **T   | **V     | **Comments**                 |
|                    | ype** | alues** |                              |
+====================+=======+=========+==============================+
| <disk 0>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 0                       |
+--------------------+-------+---------+------------------------------+
| <disk 1>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 1                       |
+--------------------+-------+---------+------------------------------+
| <disk 2>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 2                       |
+--------------------+-------+---------+------------------------------+
| <disk 3>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 3                       |
+--------------------+-------+---------+------------------------------+
| <disk 4>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 4                       |
+--------------------+-------+---------+------------------------------+
| <disk 5>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 5                       |
+--------------------+-------+---------+------------------------------+
| <disk 6>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 6                       |
+--------------------+-------+---------+------------------------------+
| <disk 7>           | int   |         | Number of replaced blocks on |
|                    |       |         | disk 7                       |
+--------------------+-------+---------+------------------------------+
| <total replaced    | int   |         | Total number of replaced     |
| blks>              |       |         | blocks.                      |
+--------------------+-------+---------+------------------------------+

Notes:

1. If a disk is unable to provide a requested 65KB (actually 0xfff8
   bytes) block of data within the allowed time limits, due to a slow or
   failed drive, the Mark 5A replaces the requested data block with a
   data block with even parity that can be detected by as invalid by a
   correlator. See ‘Mark 5A User’s Manual’ for details.
2. Drive statistics and replaced-block counts are cleared and re-started
   whenever a new disk module is mounted or a ‘start_stats’ command is
   issued. Replaced-block counts restart on each ‘play=on’ or
   ‘scan_play=on’ command.
3. If the case of a totally failed drive, the replaced-block count for
   that drive will be 0 since the StreamStor ceases to ask for data from
   that drive, but the <total replaced blks> will be accurate.
   Statistics gathered from the ‘get_stats?’ query should be used to
   help diagnose the failed drive.
4. Replaced-block statistics are updated only after playback has ceased
   (i.e. replaced-block statistics are not updated during playback).

reset – Reset Mark 5 unit (command only)
========================================

Command syntax: reset = <control> [ : <layout> ] ; Command response:
!reset = <return code> ;

Purpose: Reset system; mount/dismount disks

Settable parameters:

+---+---+---+---+----------------------------------------------------------------+
| * | * | * | * | **Comments**                                                   |
| * | * | * | * |                                                                |
| P | T | A | D |                                                                |
| a | y | l | e |                                                                |
| r | p | l | f |                                                                |
| a | e | o | a |                                                                |
| m | * | w | u |                                                                |
| e | * | e | l |                                                                |
| t |   | d | t |                                                                |
| e |   | v | * |                                                                |
| r |   | a | * |                                                                |
| * |   | l |   |                                                                |
| * |   | u |   |                                                                |
|   |   | e |   |                                                                |
|   |   | s |   |                                                                |
|   |   | * |   |                                                                |
|   |   | * |   |                                                                |
+===+===+===+===+================================================================+
| < | c | e |   | ’erase’sets record, start-scan and stop-scan pointers to zero  |
| c | h | r |   | (i.e. beginning of media); effectively erasing media;          |
| o | a | a |   | ‘nberase’ is a ‘non-bank’ erase that performs the same actions |
| n | r | s |   | as the ‘erase’ command except the modules in Banks A and B are |
| t |   | e |   | initialized as a single unit and recording takes place across  |
| r |   | \ |   | the disks in both modules; normally used only for 2Gbps        |
| o |   | | |   | operation with Mark 5B+. ’erase_last_scan’ erases the last     |
| l |   | n |   | recorded scan; sets record pointer to end-of-scan just prior   |
| > |   | b |   | to erased scan; sets start-scan and stop-scan pointer to       |
|   |   | e |   | beginning and end, respectively, of scan just prior to erased  |
|   |   | r |   | scan. ’abort’ aborts active disk2net, disk2file or file2disk   |
|   |   | a |   | transfers (only) – See Note 2 ‘condition’ starts a disk        |
|   |   | s |   | conditioning cycle (lenghty process!) much like SSErase w/o    |
|   |   | e |   | the need for stopping the server and starting a separate       |
|   |   | e |   | program. Best used togeter with SSErase.py, also written by    |
|   |   | r |   | JIVE. System is always left in ‘idle’ mode after any reset     |
|   |   | a |   | command. See Note 1.                                           |
|   |   | s |   |                                                                |
|   |   | e |   |                                                                |
|   |   | _ |   |                                                                |
|   |   | l |   |                                                                |
|   |   | a |   |                                                                |
|   |   | s |   |                                                                |
|   |   | t |   |                                                                |
|   |   | _ |   |                                                                |
|   |   | s |   |                                                                |
|   |   | c |   |                                                                |
|   |   | a |   |                                                                |
|   |   | n |   |                                                                |
|   |   | \ |   |                                                                |
|   |   | | |   |                                                                |
|   |   | a |   |                                                                |
|   |   | b |   |                                                                |
|   |   | o |   |                                                                |
|   |   | r |   |                                                                |
|   |   | t |   |                                                                |
|   |   | \ |   |                                                                |
|   |   | | |   |                                                                |
|   |   | c |   |                                                                |
|   |   | o |   |                                                                |
|   |   | n |   |                                                                |
|   |   | d |   |                                                                |
|   |   | i |   |                                                                |
|   |   | t |   |                                                                |
|   |   | i |   |                                                                |
|   |   | o |   |                                                                |
|   |   | n |   |                                                                |
+---+---+---+---+----------------------------------------------------------------+
| < | c | l |   | If <control> = ‘erase’, and not blank, force UserDirectory     |
| l | h | e |   | layout to write. By default *jive5ab* will automatically       |
| a | a | g |   | choose the layout most appropriate for the system it is        |
| y | r | a |   | running on. See Note 5.                                        |
| o |   | c |   |                                                                |
| u |   | y |   |                                                                |
| t |   | \ |   |                                                                |
| > |   | | |   |                                                                |
|   |   | b |   |                                                                |
|   |   | i |   |                                                                |
|   |   | g |   |                                                                |
|   |   | b |   |                                                                |
|   |   | l |   |                                                                |
|   |   | o |   |                                                                |
|   |   | c |   |                                                                |
|   |   | k |   |                                                                |
|   |   | \ |   |                                                                |
|   |   | | |   |                                                                |
|   |   | < |   |                                                                |
|   |   | e |   |                                                                |
|   |   | x |   |                                                                |
|   |   | p |   |                                                                |
|   |   | l |   |                                                                |
|   |   | i |   |                                                                |
|   |   | c |   |                                                                |
|   |   | i |   |                                                                |
|   |   | t |   |                                                                |
|   |   | l |   |                                                                |
|   |   | a |   |                                                                |
|   |   | y |   |                                                                |
|   |   | o |   |                                                                |
|   |   | u |   |                                                                |
|   |   | t |   |                                                                |
|   |   | > |   |                                                                |
+---+---+---+---+----------------------------------------------------------------+

Notes:

1. The former ‘reset=mount’ and ‘reset=dismount’ commands are no longer
   supported; the keyswitches associated with the disk modules are used
   for all mount and dismount operations.
2. ‘reset=abort’ returns immediately, but there may be a delay of up to
   two seconds before the data transfer stops. During this delay, a
   ‘status?’ query will show what is happening. [STRIKEOUT:The
   ‘reset=abort’ command simulates the end of data by setting
   ‘nowbyte=endbyte’, which then executes a normal termination.]
   \ *jive5ab* terminates the transfer immediately but some delay may
   happen because of O/S or hardware latency. E.g. when doing disk2file
   (especially) the Linux O/S buffers a lot of data. Upon closing the
   file these bytes must be flushed which will take a variable amount of
   time (see disk2file Notes on how to influence this!). Bytes still in
   *jive5ab*\ ’s internal buffers are discarded.
3. A ‘protect=off’ command is required *immediately* prior to a
   ‘reset=erase’ or ‘reset=erase_last_scan’ command, even if protection
   is already off.
4. The ‘reset=nberase’ command requires that disk modules are mounted
   and ready in both banks. Bank A must contain eight disks; bank B may
   have fewer, though it will normally have the same number. After the
   ‘reset=nberase’ command is completed, each module will have recorded
   on it (until the module is again erased) the following information:
   1) bank position of the module (A or B), and 2) VSN of the module in
   the opposite bank. Each subsequent occasion when the modules are
   mounted for record or readback operation, the location and
   identification of the modules is checked; only if the proper modules
   are mounted in the proper positions will *jive5ab* place the system
   into non-bank mode or allow any read or write operations.
5. The format of the User Directory (‘Scan Directory’) as written on the
   Mark5 disk packs has seen a number of versions through the years;
   some of them dependant on actual Conduant SDK version. ‘legacy’
   refers to Mark5A, 1024 scans max, ‘bigblock’ allows 4 Gbps operation
   on Mark5C (requires firmware > 16.38). Please refer to Mark5 Memo
   #100 [22]_ for intricate details plus a description of what are
   allowed values for <explicit layout>. See also the ‘layout?’ query.

rtime – Get remaining record time on current disk set (query only) (5A)
=======================================================================

Query syntax: rtime? ;

Query response: !rtime ? <return code> : <remaining time> : <remaining
GB> : <remaining percent> : <data source> :

<bit-stream mask> : <decimation ratio> : <total recording rate> ;

Purpose: Get remaining record time of current disk set; assumes
recording will be in the mode currently set by the ‘mode’ command and
data rate set by ‘play_rate’ command.

Monitor-only parameters:

+------+---+----------+------------------------------------------------+
| *    | * | **       | **Comments**                                   |
| *Par | * | Values** |                                                |
| amet | T |          |                                                |
| er** | y |          |                                                |
|      | p |          |                                                |
|      | e |          |                                                |
|      | * |          |                                                |
|      | * |          |                                                |
+======+===+==========+================================================+
| <r   | r | seconds  | Approximate remaining record time for current  |
| emai | e |          | ‘mode’ and ‘play_rate’ parameters; Requires    |
| ning | a |          | that ‘play_rate’ be set to current record rate |
| t    | l |          | – see Notes. See Note 2.                       |
| ime> |   |          |                                                |
+------+---+----------+------------------------------------------------+
| <r   | r | GB       | GB remaining on current disk set (1 GB = 109   |
| emai | e |          | bytes). See Note 2.                            |
| ning | a |          |                                                |
| GB>  | l |          |                                                |
+------+---+----------+------------------------------------------------+
| <r   | r | 0-100    | Remaining percentage of disk space still       |
| emai | e |          | available. See Note 2.                         |
| ning | a |          |                                                |
| perc | l |          |                                                |
| ent> |   |          |                                                |
+------+---+----------+------------------------------------------------+
| <m   | c | mark4 \| | Mode assumed in calculation of <remaining      |
| ode> | h | vlba \|  | time>. See ‘mode’ command.                     |
|      | a | st \|    |                                                |
|      | r | tvg      |                                                |
+------+---+----------+------------------------------------------------+
| <    | c | 8 \| 16  | Submode assumed in calculation of <remaining   |
| subm | h | \| 32 \| | time>                                          |
| ode> | a | 64 \|    |                                                |
|      | r | mark4 \| |                                                |
|      |   | vlba     |                                                |
+------+---+----------+------------------------------------------------+
| <t   | r | MHz      | Track data rate assumed in calculation of      |
| rack | e |          | <remaining time>; see ‘play_rate’ command. See |
| data | a |          | Note 2.                                        |
| r    | l |          |                                                |
| ate> |   |          |                                                |
+------+---+----------+------------------------------------------------+
| <t   | r | Mbps     | Net recording rate assumed in calculation of   |
| otal | e |          | <remaining time>, based on current clock       |
| r    | a |          | frequency and ‘mode’ parameters. See Note 2.   |
| ecor | l |          |                                                |
| ding |   |          |                                                |
| r    |   |          |                                                |
| ate> |   |          |                                                |
+------+---+----------+------------------------------------------------+

Notes:

1. Each ‘rtime?’ query returns an updated estimate during recording; a
   somewhat more accurate estimate is obtained when recording is stopped
   and the effects of any slow or bad disks can be more accurately
   measured.
2. For readability *jive5ab* returns these values with their unit
   character(s) appended: “s”, “GB”, “%”, “MHz” and “Mbps” respectively.

rtime – Get remaining record time on current disk set (query only) (5B/DIM, 5C)
===============================================================================

Query syntax: rtime? ;

Query response: !rtime ? <return code> : <remaining time> : <remaining
GB> : <remaining percent> : <data source> :

<bit-stream mask> : <decimation ratio> : <total recording rate> ;

Purpose: Get remaining record time of current disk set; assumes
recording will be in the mode currently set by the ‘mode’ command and
data rate set by ‘clock_set’ command. On 5C “mode = unk ;” is supported

Monitor-only parameters:

+-------+---+---+------------------------------------------------------+
| **P   | * | * | **Comments**                                         |
| arame | * | * |                                                      |
| ter** | T | V |                                                      |
|       | y | a |                                                      |
|       | p | l |                                                      |
|       | e | u |                                                      |
|       | * | e |                                                      |
|       | * | s |                                                      |
|       |   | * |                                                      |
|       |   | * |                                                      |
+=======+===+===+======================================================+
| <rema | r | s | Approximate remaining record time for current ‘mode’ |
| ining | e | e | and ‘clock_set’ parameters; Requires that            |
| time> | a | c | ‘clock_set’ be set to current record rate – see      |
|       | l | o | Notes. See Note 3.                                   |
|       |   | n |                                                      |
|       |   | d |                                                      |
|       |   | s |                                                      |
+-------+---+---+------------------------------------------------------+
| <rema | r | G | GB remaining on current disk set (1 GB = 109 bytes). |
| ining | e | B | See Note 3.                                          |
| GB>   | a |   |                                                      |
|       | l |   |                                                      |
+-------+---+---+------------------------------------------------------+
| <rema | r | 0 | Remaining percentage of disk space still available.  |
| ining | e | - | See Note 3.                                          |
| per   | a | 1 |                                                      |
| cent> | l | 0 |                                                      |
|       |   | 0 |                                                      |
+-------+---+---+------------------------------------------------------+
| <data | c | e | Assumed to be same as specified in last ‘mode’       |
| so    | h | x | command                                              |
| urce> | a | t |                                                      |
|       | r | \ |                                                      |
|       |   | | |                                                      |
|       |   | t |                                                      |
|       |   | v |                                                      |
|       |   | g |                                                      |
+-------+---+---+------------------------------------------------------+
| <     | h |   | Assumed to be same as specified in last ‘mode’       |
| bit-s | e |   | command                                              |
| tream | x |   |                                                      |
| mask> |   |   |                                                      |
+-------+---+---+------------------------------------------------------+
| <     | i |   | Assumed to be same as specified in last ‘mode’       |
| decim | n |   | command                                              |
| ation | t |   |                                                      |
| r     |   |   |                                                      |
| atio> |   |   |                                                      |
+-------+---+---+------------------------------------------------------+
| <     | r | M | Net recording rate assumed in calculation of         |
| total | e | b | <remaining time>, based on current clock frequency   |
| reco  | a | p | and ‘mode’ parameters. See Note 3.                   |
| rding | l | s |                                                      |
| rate> |   |   |                                                      |
+-------+---+---+------------------------------------------------------+

Notes:

1. Each ‘rtime?’ query returns an updated estimate during recording; a
   somewhat more accurate estimate is obtained when recording is stopped
   and the effects of any slow or bad disks can be more accurately
   measured.
2. The difference between the implementation on Mark5B and Mark5C is the
   fact that on Mark5C the mode can be set to ‘unk’ – for unknown data.
   This makes the recording data rate also unknown and as such the
   remaining time as well.
3. For readability *jive5ab* returns these values with their unit
   character(s) appended: “s”, “GB”, “%” and “Mbps” respectively.

rtime – Get remaining record time on current disk set (query only) (G)
======================================================================

Query syntax: rtime? ;

Query response: !rtime ? <return code> : <remaining time> : <remaining
GB> : <remaining percent> : <data format> :

<#tracks> : <decimation ratio> : <total recording rate> ;

Purpose: Get remaining record time of accumulated free disk space over
all disks currently selected; assumes recording will be in the mode
currently set by the ‘mode’ command. This command appeared in *jive5ab*
2.6.2.

Monitor-only parameters:

+-------+---+------------------------+---------------------------------+
| **P   | * | **Values**             | **Comments**                    |
| arame | * |                        |                                 |
| ter** | T |                        |                                 |
|       | y |                        |                                 |
|       | p |                        |                                 |
|       | e |                        |                                 |
|       | * |                        |                                 |
|       | * |                        |                                 |
+=======+===+========================+=================================+
| <rema | r | seconds                | Approximate remaining record    |
| ining | e |                        | time for current ‘mode’. See    |
| time> | a |                        | Note 3.                         |
|       | l |                        |                                 |
+-------+---+------------------------+---------------------------------+
| <rema | r | GB                     | GB remaining on current disk    |
| ining | e |                        | set (1 GB = 109 bytes). See     |
| GB>   | a |                        | Note 3.                         |
|       | l |                        |                                 |
+-------+---+------------------------+---------------------------------+
| <rema | r | 0-100                  | Remaining percentage of disk    |
| ining | e |                        | space still available. See Note |
| per   | a |                        | 3.                              |
| cent> | l |                        |                                 |
+-------+---+------------------------+---------------------------------+
| <data | c | mark4 \| vlba \|       | Assumed to be same as specified |
| fo    | h | Mark5B \| vdif \|      | in last ‘mode’ command          |
| rmat> | a | legacy vdif \| tvg \|  |                                 |
|       | r | SS \| <unknown>        |                                 |
+-------+---+------------------------+---------------------------------+
| <#tr  | i |                        | Number of bit streams (tracks)  |
| acks> | n |                        | derived from last ‘mode’        |
|       | t |                        | command                         |
+-------+---+------------------------+---------------------------------+
| <     | i | 0                      | Currently fixed to “0”          |
| decim | n |                        |                                 |
| ation | t |                        |                                 |
| r     |   |                        |                                 |
| atio> |   |                        |                                 |
+-------+---+------------------------+---------------------------------+
| <     | r | Mbps                   | Net recording rate assumed in   |
| total | e |                        | calculation of <remaining       |
| reco  | a |                        | time>, based on current ‘mode’. |
| rding | l |                        | See Note 3.                     |
| rate> |   |                        |                                 |
+-------+---+------------------------+---------------------------------+

Notes:

1. Each ‘rtime?’ query returns an updated estimate during recording; a
   somewhat more accurate estimate is obtained when recording is stopped
   and the effects of any slow or bad disks can be more accurately
   measured.
2. The ‘rtime?’ implementation in *jive5ab* >= 2.6.2 running on
   Mark6/FlexBuff returns the free space accumulated over all disks
   currently selected, as returned by “set_disks?”
3. For readability *jive5ab* returns these values with their unit
   character(s) appended: “s”, “GB”, “%” and “Mbps” respectively.

runtime – Control multiple simultaneous transfer environments
=============================================================

Command syntax: runtime = <name> [ : <action> ] ; Command response:
!runtime = <return code> ;

Query syntax: runtime? ;

Query response: !runtime ? <return code> : <current> : <#runtimes> [ :
<name\ *2*> : … : <name\ *N*> ] ; Purpose: Manage transfer environments
to allow for multiple, simultaneous, transfers

Settable parameters:

+---+---+----+---+-----------------------------------------------------------+
| * | * | *  | * | **Comments**                                              |
| * | * | *A | * |                                                           |
| P | T | ll | D |                                                           |
| a | y | ow | e |                                                           |
| r | p | ed | f |                                                           |
| a | e | va | a |                                                           |
| m | * | lu | u |                                                           |
| e | * | es | l |                                                           |
| t |   | ** | t |                                                           |
| e |   |    | * |                                                           |
| r |   |    | * |                                                           |
| * |   |    |   |                                                           |
| * |   |    |   |                                                           |
+===+===+====+===+===========================================================+
| < | c |    |   | Switch to runtime <name>. Create it if runtime <name>     |
| n | h |    |   | does not exist yet. See Notes, specifically Note 2.       |
| a | a |    |   |                                                           |
| m | r |    |   |                                                           |
| e |   |    |   |                                                           |
| > |   |    |   |                                                           |
+---+---+----+---+-----------------------------------------------------------+
| < | c | n  |   | If given, modifies the default behaviour according to     |
| a | h | ew |   | this: ‘new’ – only succeeds if <name> does not exist yet; |
| c | a | \| |   | an exclusive flag ‘transient’ – create if not exists and  |
| t | r | ex |   | mark <name> for automatic deletion. See Note 6.           |
| i |   | is |   | (*jive5ab* >= 2.7.3) ‘exists’ – do not create if <name>   |
| o |   | ts |   | does not exist; only switch if <name> already exists      |
| n |   | \| |   | ‘delete’ – delete <name> and all its resources. See Note  |
| > |   | de |   | 4.                                                        |
|   |   | le |   |                                                           |
|   |   | te |   |                                                           |
|   |   | \| |   |                                                           |
|   |   | t  |   |                                                           |
|   |   | ra |   |                                                           |
|   |   | ns |   |                                                           |
|   |   | ie |   |                                                           |
|   |   | nt |   |                                                           |
+---+---+----+---+-----------------------------------------------------------+

Monitor-only parameters:

+-----------+------+---------------------------------------------------+
| **Pa      | **Ty | **Comments**                                      |
| rameter** | pe** |                                                   |
+===========+======+===================================================+
| <current> | char | Name of runtime this control connection is        |
|           |      | associated with                                   |
+-----------+------+---------------------------------------------------+
| <#        | int  | Total number of runtimes currently defined in the |
| runtimes> |      | system                                            |
+-----------+------+---------------------------------------------------+
| <n        | char | Names of the other runtimes; <current> is missing |
| ame\ *N*> |      | from this list.                                   |
+-----------+------+---------------------------------------------------+

Notes:

1. New command connections to *jive5ab*\ ’s control port are
   automatically associated with the default runtime, runtime “0”. See
   Section **6** of this document. Each runtime is uniquely defined by
   its name – a meaningless (to *jive5ab*) sequence of characters.
2. Versions < 2.6.3 take the statement from Note 1 very literally; the
   name can be an empty string too. Thus a literal command ‘runtime = ;’
   would create-and-switch-to a runtime by the name “” (the empty
   string). It will **NOT** put the connection back in the default
   runtime. The empty string does not read nicely and the behaviour is
   counterintuitive. Starting from 2.6.3 *jive5ab* disallows the empty
   string as <name>.
3. A control’s runtime association remains fixed for the duration of the
   connection, unless it is altered using ‘runtime = … ;’. After a
   succesfull ‘runtime=…;’ the association remains fixed again, until
   the connection is closed or the association is altered using another
   ‘runtime=…;’ command.
4. Runtimes are dynamically created on an as-needed basis. Switching to
   a non-existent runtime is a sufficient ‘needed’ basis, unless the
   behaviour of ‘runtime=…;’ is altered through specification of an
   <action>.
5. Runtimes should be deleted when done using the specific runtime. If
   the current runtime is deleted, the system will automatically put the
   connection back into the default runtime. (See Note 6 for possible
   automatic runtime deletion in *jive5ab* >= 2.7.3)
6. Starting from version 2.7.3 runtimes can be created with the
   ‘transient’ flag. The control connection creating the runtime will be
   marked as owner of the runtime. If this connection is closed, the
   runtime and its resources will be automatically deleted from the
   system.

scan_check – Check recorded data between start-scan and stop-scan pointers (query only)
=======================================================================================

Query syntax: scan_check? [ <strict> : <#bytes to read> ] ;

Query response: !scan_check ? <return code> : <scan#> : <scan label> :
<data type> : <ntrack> : <start time> :

<scan length> : <total recording rate> : <#missing bytes> [ : <data
array size> ];

Purpose: Check recorded data between the start-scan and stop-scan
pointers (e.g. returned by ‘pointers?’ query). The query arguments
<strict> and <#bytes to read> were introduced in *jive5ab* 2.5.1.
Mark6/FlexBuff support was added in *jive5ab* 2.6.2.

Monitor arguments:

+---+---+---+---+----------------------------------------------------------+
| \ | \ | \ | \ | \ **Comments**\                                          |
|   |   |   |   |                                                          |
| * | * | * | * |                                                          |
| * | * | * | * |                                                          |
| P | T | A | D |                                                          |
| a | y | l | e |                                                          |
| r | p | l | f |                                                          |
| a | e | o | a |                                                          |
| m | * | w | u |                                                          |
| e | * | e | l |                                                          |
| t | \ | d | t |                                                          |
| e |   | v | * |                                                          |
| r |   | a | * |                                                          |
| * |   | l | \ |                                                          |
| * |   | u |   |                                                          |
|   |   | e |   |                                                          |
|   |   | s |   |                                                          |
|   |   | * |   |                                                          |
|   |   | * |   |                                                          |
|   |   | \ |   |                                                          |
|   |   |   |   |                                                          |
+===+===+===+===+==========================================================+
| < | i | 0 | 1 | 0 – Enable less strict checking: no CRC checks on frame  |
| s | n | \ |   | headers no check for invalid last digit in MarkIV time   |
| t | t | | |   | stamp or time stamp consistency with data rate no        |
| r |   | 1 |   | consistency check between frame number+data rate and     |
| i |   |   |   | VLBA time stamp in Mark5B header 1 – Everything has to   |
| c |   |   |   | be good for data format to be detected                   |
| t |   |   |   |                                                          |
| > |   |   |   |                                                          |
+---+---+---+---+----------------------------------------------------------+
| < | i |   | 1 | Amount of data to find frames in. The default ~1MB may   |
| # | n |   | 0 | be too low for high data rate detection (>>2Gbps)        |
| b | t |   | 0 |                                                          |
| y |   |   | 0 |                                                          |
| t |   |   | 0 |                                                          |
| e |   |   | 0 |                                                          |
| s |   |   | 0 |                                                          |
| t |   |   |   |                                                          |
| o |   |   |   |                                                          |
| r |   |   |   |                                                          |
| e |   |   |   |                                                          |
| a |   |   |   |                                                          |
| d |   |   |   |                                                          |
| > |   |   |   |                                                          |
+---+---+---+---+----------------------------------------------------------+

Monitor-only parameters:

+---+---+---------+------------------------------------------------------+
| * | * | **V     | **Comments**                                         |
| * | * | alues** |                                                      |
| P | T |         |                                                      |
| a | y |         |                                                      |
| r | p |         |                                                      |
| a | e |         |                                                      |
| m | * |         |                                                      |
| e | * |         |                                                      |
| t |   |         |                                                      |
| e |   |         |                                                      |
| r |   |         |                                                      |
| * |   |         |                                                      |
| * |   |         |                                                      |
+===+===+=========+======================================================+
| < | i |         | Start at 1 for first recorded scan, but see Notes 7, |
| s | n |         | 8.                                                   |
| c | t |         |                                                      |
| a |   |         |                                                      |
| n |   |         |                                                      |
| # |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | l |         |                                                      |
| s | i |         |                                                      |
| c | t |         |                                                      |
| a | e |         |                                                      |
| n | r |         |                                                      |
| l | a |         |                                                      |
| a | l |         |                                                      |
| b | A |         |                                                      |
| e | S |         |                                                      |
| l | C |         |                                                      |
| > | I |         |                                                      |
|   | I |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | c | - \|    | dash – Mark 5B format, but undetermined data type    |
| d | h | tvg \|  | (probably real data) tvg – undecimated 32-bit-wide   |
| a | a | SS \| ? | tvg data (see Note 4) SS – raw StreamStor test       |
| t | r | vdif    | pattern data ? – [STRIKEOUT:recording not in Mark 5B |
| a |   | (       | format (might be Mark 5A, for example); all          |
| t |   | legacy) | subsequent return fields are null.] See Note 6. ‘st  |
| y |   | \|      | :’ – this extra field is inserted if                 |
| p |   | mark5b  | straight-through MarkIV or VLBA is detected          |
| e |   | \| [st  |                                                      |
| > |   | :]      |                                                      |
|   |   | mark4   |                                                      |
|   |   | \| vlba |                                                      |
+---+---+---------+------------------------------------------------------+
| [ | i |         | [STRIKEOUT:3-digit date code written in first disk   |
| S | n |         | frame header] Number of tracks detected (‘?’ if      |
| T | t |         | VDIF)                                                |
| R |   |         |                                                      |
| I |   |         |                                                      |
| K |   |         |                                                      |
| E |   |         |                                                      |
| O |   |         |                                                      |
| U |   |         |                                                      |
| T |   |         |                                                      |
| : |   |         |                                                      |
| < |   |         |                                                      |
| d |   |         |                                                      |
| a |   |         |                                                      |
| t |   |         |                                                      |
| e |   |         |                                                      |
| c |   |         |                                                      |
| o |   |         |                                                      |
| d |   |         |                                                      |
| e |   |         |                                                      |
| > |   |         |                                                      |
| ] |   |         |                                                      |
| < |   |         |                                                      |
| n |   |         |                                                      |
| t |   |         |                                                      |
| r |   |         |                                                      |
| a |   |         |                                                      |
| c |   |         |                                                      |
| k |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | t |         | Time tag at first frame header in scan. See Note 5.  |
| s | i |         |                                                      |
| t | m |         |                                                      |
| a | e |         |                                                      |
| r |   |         |                                                      |
| t |   |         |                                                      |
| t |   |         |                                                      |
| i |   |         |                                                      |
| m |   |         |                                                      |
| e |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | t |         |                                                      |
| s | i |         |                                                      |
| c | m |         |                                                      |
| a | e |         |                                                      |
| n |   |         |                                                      |
| l |   |         |                                                      |
| e |   |         |                                                      |
| n |   |         |                                                      |
| g |   |         |                                                      |
| t |   |         |                                                      |
| h |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | r | Mbps    |                                                      |
| t | e |         |                                                      |
| o | a |         |                                                      |
| t | l |         |                                                      |
| a |   |         |                                                      |
| l |   |         |                                                      |
| r |   |         |                                                      |
| e |   |         |                                                      |
| c |   |         |                                                      |
| o |   |         |                                                      |
| r |   |         |                                                      |
| d |   |         |                                                      |
| i |   |         |                                                      |
| n |   |         |                                                      |
| g |   |         |                                                      |
| r |   |         |                                                      |
| a |   |         |                                                      |
| t |   |         |                                                      |
| e |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| < | i | See     | Should always be =0 for normally recorded data. >0   |
| # | n | Note 5  | indicates #bytes that have been dropped somewhere    |
| m | t |         | within scan <0 indicates #bytes that have been added |
| i |   |         | somewhere within scan                                |
| s |   |         |                                                      |
| s |   |         |                                                      |
| i |   |         |                                                      |
| n |   |         |                                                      |
| g |   |         |                                                      |
| b |   |         |                                                      |
| y |   |         |                                                      |
| t |   |         |                                                      |
| e |   |         |                                                      |
| s |   |         |                                                      |
| > |   |         |                                                      |
+---+---+---------+------------------------------------------------------+
| [ | i |         | Parameter is only returned if the detected format is |
| < | n |         | simple VDIF and reports the VDIF data array length.  |
| d | t |         |                                                      |
| a |   |         |                                                      |
| t |   |         |                                                      |
| a |   |         |                                                      |
| a |   |         |                                                      |
| r |   |         |                                                      |
| r |   |         |                                                      |
| a |   |         |                                                      |
| y |   |         |                                                      |
| s |   |         |                                                      |
| i |   |         |                                                      |
| z |   |         |                                                      |
| e |   |         |                                                      |
| > |   |         |                                                      |
| ] |   |         |                                                      |
+---+---+---------+------------------------------------------------------+

Notes:

1. The ‘scan_check’ query will be honored only if record and play are
   both off and does not affect the start-scan or stop-scan pointers.
2. The ‘scan_check’ query essentially executes a ‘data_check’ starting
   at the start-scan pointer, followed by a ‘data_check’ just prior to
   the stop-scan pointer. This allows information about the selected
   scan to be conveniently determined.
3. Only tvg data that were recorded with a bit-stream mask of 0xffffffff
   and no decimation will be recognized.
4. Regarding the <start time> value returned by the ‘data_check?’ and,
   ‘scan_check?’ queries: The year and DOY reported in <start time>
   represent the most recent date consistent with the 3-digit <date
   code> in the frame header time tag (modulo 1000 value of Modified
   Julian Day as defined in VLBA tape-format header); this algorithm
   reports the proper year and DOY provided the data were taken no more
   than 1000 days ago.
5. The <#missing bytes> parameter is calculated as the difference the
   expected number of bytes between two samples of recorded data based
   on embedded time tags and the actual observed number of bytes between
   the same time tags. The reported number is the *total* number of
   bytes missing (or added) between the two sample points.
6. \ *jive5ab* extends scan_check? to recognize all data formats, not
   just the recorder’s native data type (see Section 7). scan_check?’s
   output may be subtly different depending on wether it is issued on a
   Mark5B/DIM, Mark6/FlexBuff or any of the other Mark5’s.
7. \ *jive5ab* with Mark6/FlexBuff support does not keep global scan
   numbers on these systems as was done on the Mark5 disk packs. On
   Mark6/FlexBuff it remembers and numbers the scans it has recorded
   since the program’s invocation from *0* .. *n-1*. As such, after a
   restart, scan numbers start back at *0*.
8. There is no autodetection of Mark6/FlexBuff recorded format. If a
   previously recorded Mark6 or FlexBuff recording is to be
   scan_check’ed it *may* be necessary to set the recording format
   appropriately (see record = mk6 : 0|1).

scan_set – Set start-scan and stop-scan pointers for data readback
==================================================================

Command syntax: scan_set = <search string> : [<start scan>] : [<stop
scan>] ; Command response: !scan_set = <return code> ;

Query syntax: scan_set? ;

Query response: !scan_set? <return code> : <scan number> : <scan label>
: <start byte#> : <stop byte#> ;

Purpose: Set start-scan and stop-scan pointers for data_check,
scan_check, disk2file and disk2net. Mark6/FlexBuff support was added in
*jive5ab* 2.6.2. See Note 7 for *jive5ab*\ ’s difference in query
response!

Settable parameters:

+------+---+-------+----+----------------------------------------------+
| *    | * | **Al  | *  | **Comments**                                 |
| *Par | * | lowed | *D |                                              |
| amet | T | val   | ef |                                              |
| er** | y | ues** | au |                                              |
|      | p |       | lt |                                              |
|      | e |       | ** |                                              |
|      | * |       |    |                                              |
|      | * |       |    |                                              |
+======+===+=======+====+==============================================+
| <se  | i | scan  | la | First attempts to interpret as scan number   |
| arch | n | n     | st | (first scan is number 1); if not numeric or  |
| str  | t | umber | re | no match, attempts to match                  |
| ing> | o | \|    | co |                                              |
|      | r |       | rd |                                              |
|      |   |       | ed |                                              |
+------+---+-------+----+----------------------------------------------+
|      | A | scan  | sc | all or part of existing scan label, case     |
|      | S | label | an | insensitive (see Note 1).                    |
|      | C | \|    |    |                                              |
|      | I |       |    |                                              |
|      | I |       |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | ’inc’ |    | ‘inc’ increments to next scan; cycles back   |
|      |   | \|    |    | to first scan at end; ‘dec’ decrements to    |
|      |   |       |    | previous scan.                               |
+------+---+-------+----+----------------------------------------------+
|      |   | ’dec’ |    | ’next’ finds next scan with previous value   |
|      |   | \|    |    | of <search string>.                          |
+------+---+-------+----+----------------------------------------------+
|      |   | ’     |    | If null field, defaults to last fully        |
|      |   | next’ |    | recorded scan.                               |
+------+---+-------+----+----------------------------------------------+
| <s   | c | s \|  | s  | s|c|e|s+: Set start scan position to         |
| tart | h | c \|  |    | ‘start’, ‘center’, ‘end’ (actually ~1MB      |
| s    | a | e \|  |    | before end) of scan, or specified <time>     |
| can> | r | s+ \| |    |                                              |
|      | \ |       |    |                                              |
|      | | |       |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      | t | <     |    | within scan; this is convenient if you want  |
|      | i | time> |    | to do a subsequent ‘data_check’ at a         |
|      | m | \|    |    | prescribed position. ’s+’ sets the           |
|      | e |       |    |                                              |
|      | \ |       |    |                                              |
|      | | |       |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      | i | +<    |    | start-scan pointer to 65536 bytes past the   |
|      | n | time> |    | start of the scan.                           |
|      | t | \|    |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | -<    |    | <time>: time within scan: see Notes 2 & 3    |
|      |   | time> |    |                                              |
|      |   | \|    |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | +<b   |    | +<time>: offset time from beginning of scan  |
|      |   | ytes> |    | (i.e. ‘+30s’ will start 30 seconds from      |
|      |   | \|    |    | beginning of scan)                           |
+------+---+-------+----+----------------------------------------------+
|      |   | -<b   |    | -<time>: offset time from end of scan        |
|      |   | ytes> |    | (i.e. ‘-30s’ will start 30 seconds before    |
|      |   |       |    | end of scan)                                 |
+------+---+-------+----+----------------------------------------------+
|      |   |       |    | +<bytes>: offset number of bytes from        |
|      |   |       |    | beginning of scan.                           |
+------+---+-------+----+----------------------------------------------+
|      |   |       |    | -<bytes>: offset number of bytes from end of |
|      |   |       |    | scan                                         |
+------+---+-------+----+----------------------------------------------+
| <    | t | <     | en | <time>: Time at which to end readback; see   |
| stop | i | time> | d- | Notes 2 & 3. If preceded by ‘+’, indicates   |
| s    | m | \|    | of | duration of data (in record-                 |
| can> | e |       | sc |                                              |
|      | \ |       | an |                                              |
|      | | |       |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      | i | +<    |    | clock time) from <start scan> time.          |
|      | n | time> |    |                                              |
|      | t | \|    |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | -<    |    | +<time>: offset time from <start scan>       |
|      |   | time> |    | position.                                    |
|      |   | \|    |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | +<b   |    | -<time>: offset time from end-of-scan        |
|      |   | ytes> |    |                                              |
|      |   | \|    |    |                                              |
+------+---+-------+----+----------------------------------------------+
|      |   | -<b   |    | +<bytes>: offset bytes from <start scan>     |
|      |   | ytes> |    | position                                     |
+------+---+-------+----+----------------------------------------------+
|      |   |       |    | -<bytes>: offset bytes from end of scan      |
+------+---+-------+----+----------------------------------------------+

Monitor-only parameters: NOTE *jive5ab* returns this Mark5A-style reply
on **all** systems! DIMino/drs replies are subtly different.

+-------+---+----+---------------------------------------------------+
| **P   | * | ** | **Comments**                                      |
| arame | * | Va |                                                   |
| ter** | T | lu |                                                   |
|       | y | es |                                                   |
|       | p | ** |                                                   |
|       | e |    |                                                   |
|       | * |    |                                                   |
|       | * |    |                                                   |
+=======+===+====+===================================================+
| <scan | i |    | Scan number of scan matching <search string>. ‘?’ |
| nu    | n |    | on FlexBuff/Mark6 because those have no scan      |
| mber> | t |    | numbers.                                          |
+-------+---+----+---------------------------------------------------+
| <scan | t |    | Scan label of scan matching <search string>       |
| l     | i |    |                                                   |
| abel> | m |    |                                                   |
|       | e |    |                                                   |
+-------+---+----+---------------------------------------------------+
| <     | i |    | Absolute start byte number of selected data range |
| start | n |    |                                                   |
| b     | t |    |                                                   |
| yte#> |   |    |                                                   |
+-------+---+----+---------------------------------------------------+
| <stop | i |    | Absolute end byte number of selected data range   |
| b     | n |    |                                                   |
| yte#> | t |    |                                                   |
+-------+---+----+---------------------------------------------------+

Notes:

1. If <search string> is all numeric, scan_set will first try to
   interpret it as a scan number. If it is not all numeric or the scan
   number does not exist, scan_set will find the first scan label that
   matches all or part of the corresponding non-null subfields in

   <search string>; null subfields in <search string> match all scans.
   All searches start from the first scan except if ‘scan_set=next’; if
   ‘scan_set’ is already pointing at last scan, then ‘scan_set=next’
   will start search at first scan. Searches are case insensitive.

   Examples:

+-----------+----------------------------------------------------------+
| **<search | **Matches**                                              |
| string>** |                                                          |
+===========+==========================================================+
| 105       | Scan #105, if it exists; otherwise, first scan label     |
|           | containing ‘105’ anywhere (e.g. ‘grf103_ef_123-1056’)    |
+-----------+----------------------------------------------------------+
| grf103\_  | First scan label with 1st subfield containing ‘grf103’   |
+-----------+----------------------------------------------------------+
| \_EF      | First scan label with 2nd subfield containing ‘EF’       |
|           | (searches are case insensitive)                          |
+-----------+----------------------------------------------------------+
| 1056      | First scan label with 3rd subfield containing ‘1056’     |
+-----------+----------------------------------------------------------+
| \_ef_1056 | First scan label with 2nd subfield containing ‘ef’ and   |
|           | 3rd subfield containing ‘1056’                           |
+-----------+----------------------------------------------------------+

2. When ‘record=off’ is issued or end-of-media (following a ‘record=on’)
   is encountered, the start-scan and stop-scan pointers are set to span
   the entire just-recorded scan.
3. If the <start scan> or <stop scan> parameter is a <time> value, this
   time must be specified with sufficient significance to resolve any
   ambiguity within the scan. For example, ‘30s’ would set the
   start-scan pointer to start at the first ‘30s’ mark in the scan
   (regardless of the value of the minute). If a calculated byte
   position is outside the bounds of a scan, an error code ‘0’, but the
   default will be retained and an error code will be posted, which can
   be recovered by an ‘error?’ or ‘status?’ query.
4. A ‘scan_set=’ command is not allowed during active data transfers.
5. The specified values of <start scan> and <stop scan> must be within
   the target scan.
6. The ‘pointers’ query can be issued at any time to retrieve the
   current value of the start-scan and stop-scan pointers.
7. Due to an oversight on *jive5ab*\ ’s authors’ behalf, the ‘scan_set?’
   query response is strangely coherent across all systems.\ *jive5ab*
   returns the exact same response format always: it returns the
   response as documented in the Mark5A Command Set 2.73. The subtle
   differences between the documented Mark5A, Mark5B/DIM and Mark5C
   responses to this query had elided the authors and were only brought
   to their attention in August 2015. To wit: Mark5B/DIM should not
   return the scan number at all and return the selected start/end as
   *time stamps* rather than byte numbers. The Mark5C version should be
   a strange amalgamation between the 5A and 5B/DIM responses: it should
   not return the scan number (cf. 5B/DIM) but should return the
   start/end as absolute byte numbers (cf. 5A).

set_disks – Select mount points to record on (FlexBuff/Mark6)
=============================================================

Command syntax: set_disks = <pattern1> [ : <pattern2> : … : <patternN> ]
; Command response: !set_disks = <return code> : <#mount points> ;

Query syntax: set_disks? ;

Query response: !set_disks ? <return code> : <#disks> : <disk0> :
<disk1> : … : <diskN> ;

Purpose: Select mount points to next record on with FlexBuff or Mark6.
This command appeared in *jive5ab* 2.6.0

Settable parameters:

+----+---+------+-----------------------------------------------------+
| *  | * | *    | **Comments**                                        |
| *P | * | *All |                                                     |
| ar | T | owed |                                                     |
| am | y | valu |                                                     |
| et | p | es** |                                                     |
| er | e |      |                                                     |
| ** | * |      |                                                     |
|    | * |      |                                                     |
+====+===+======+=====================================================+
| <p | c | flex | ‘flexbuff’ – select all active FlexBuff mount       |
| at | h | buff | points “/mnt/disk[0-9]+”                            |
| te | a | \|   |                                                     |
| rn | r |      |                                                     |
| N> |   |      |                                                     |
+----+---+------+-----------------------------------------------------+
|    |   | mk6  | ‘mk6’ – select all active Mark6 mount points        |
|    |   | \|   | “/mnt/disks/[1-4]/[0-7]”                            |
+----+---+------+-----------------------------------------------------+
|    |   | nu   | ‘null’ – select no mountpoints succesfully (no      |
|    |   | ll\| | matches is an error), useful for testing w/o        |
|    |   |      | physical disk writing (jive5ab >= 3.0.0)             |
+----+---+------+-----------------------------------------------------+
|    |   | [12  | ‘1’ .. ‘4’ – (or a concatenation thereof) select    |
|    |   | 34]+ | all disks in Mark6 slot ‘1’, ‘2’ or the combination |
|    |   | \|   | thereof. (‘1234’ is equivalent to ‘mk6’)            |
+----+---+------+-----------------------------------------------------+
|    |   | <    | <MSN> - select all disks of mounted Mark6 disk pack |
|    |   | MSN> | with extended Module Serial Number <MSN> (case      |
|    |   |      | insensitive)                                        |
+----+---+------+-----------------------------------------------------+
|    |   | s    | ‘shell pattern’ – any path name, may contain shell  |
|    |   | hell | wild cards like ‘\*’ and ‘?’                        |
|    |   | pat  |                                                     |
|    |   | tern |                                                     |
|    |   | \|   |                                                     |
+----+---+------+-----------------------------------------------------+
|    |   | r    | ‘regex’ – any non-builtin alias (like ‘flexbuff’ or |
|    |   | egex | ‘1’) that starts with “^” and ends with “$”, as in  |
|    |   | \|   | regular expression full string match                |
+----+---+------+-----------------------------------------------------+
|    |   | <    | <GRP> – a group (a.k.a. alias) as defined by the    |
|    |   | GRP> | ‘group_def=’ command                                |
+----+---+------+-----------------------------------------------------+
|    |   |      | See Notes.                                          |
+----+---+------+-----------------------------------------------------+

Monitor-only parameters:

+-------------+--------+-----------------------------------------------+
| **          | **     | **Comments**                                  |
| Parameter** | Type** |                                               |
+=============+========+===============================================+
| <diskN>     | char   | Full path of selected mount point N. See Note |
|             |        | 3.                                            |
+-------------+--------+-----------------------------------------------+

Notes:

1. When this command executes, *jive5ab* queries the Operating System
   for currently mounted volumes. Only mounted volumes that are not the
   root file system are eligible for selection to be recorded on. The
   command will return the amount of actually selected mount points. A
   command that resulted in 0 (zero) selected eligible mount points
   returns an error code ‘4’. Since *jive5ab* 3.0.0 it is possible to
   select no mountpoints succesfully, using the special pattern ‘null’.
   This feature can be used to test raw network capture performance by
   inhibiting writing to disk.
2. The patterns supported by the command are quite flexible. There are a
   number of built-in aliases, or shortcuts if you will, that ease
   selection. E.g. the built-in pattern ‘mk6’ is an alias for the
   regex-based selection “^/mnt/disks/[1-4]/[0-7]$”. The regex form
   ensures that only disks exactly mounted by Mark6 software are
   selected. Using a shell pattern like “/mnt/disks/\*/\*” looks similar
   but *might* select more/unintended disks than wanted.
3. The query returns the current list of mount points that will be
   recorded on when a ‘record = on : <scan label> ;’ is issued.
4. The compiled-in default behaviour of *jive5ab* is to always
   find-and-select the ‘flexbuff’ mount points. This behaviour can be
   altered from the command line, see Section 2, “Running the *jive5ab*
   program”.

sp*2\* – Configure, start, stop corner turning: split [in/fill/net/file/disk] to [net/file]
===========================================================================================

Command syntax: sp*2\* = station : <VDIF station>

sp*2\* = vdifsize : <output VDIF frame size> sp*2\* = bitspersample :
<bits per sample> sp*2\* = bitsperchannel : <bits per channel> spill2\*
= realtime : [0|1]

sp*2\* = net_protocol : <protocol> : <sockbufsz> sp*2\* = mtu : <mtu>

sp*2\* = ipd : <ipd>

spif2\* = connect : <file> : <corner turning chain> : <outputX> = <dstY>
[ : … ] ; sp*2\* = connect : <corner turning chain> : <outputX> = <dstY>
[ : … ] ;

spill2\* = on [ : <nword> : <start> : <inc> ] ; spin2\* = on [ : <nbyte>
]

sp[id/if]2\* = on [ : [+]<start byte> : [+]<end byte> ]

spin2\* = off

sp*2\* = disconnect ; Command response: !sp*2\* = <return code>;

Query syntax: sp*2\*? [ <parameter> ];

Query response: !sp*2\*? <return code> : <status> \| <parameter value> ;
\ *(<status> if no <parameter> given, else <parameter>’s value)*\ 

Purpose: The corner turning transfers allow dechannelization of all
supported input data formats into single- or multichannel multi-thread
legacy VDIF. See Section **8** (and specifically **8.2**) of this
manual. The data format must be properly configured using “mode=“
(magic-mode) or “mode=” in combination with “play_rate=” or
“clock_set=”, depending on MarkIV or Mark5B format.

The most important parameters to set before running are the
bits-per-sample and bits-per-channel values; those cannot be inferred
from the data format. Usually they are identical, except for VLBA/MarkIV
formats with non 1:1 fan-in or fan-out. The VDIF station name is
informative only.

When doing splet2net (read from network – corner turn – write to
network) there are two sets of network parameters to be considered. The
network configuration set by the normal ‘net_protocol = ..’ and ‘mtu =
..’ commands is used for the input section. Using ‘splet2net =
net_protocol : …’, ‘splet2net = mtu : …’ (and optionally ‘splet2net =
ipd : …’) the output network configuration can be set.

The <corner turning chain> and <outputX> = <dstY> syntaxes are
separately described in sections **8.2.1** and **8.2.2**

respectively.

Settable parameters, in general: sp*2\* = <control> : <control value>

+---+---+---------+-------------------------------------------------------+
| * | * | **      | **Comments**                                          |
| * | * | Allowed |                                                       |
| P | T | values/ |                                                       |
| a | y | types** |                                                       |
| r | p |         |                                                       |
| a | e |         |                                                       |
| m | / |         |                                                       |
| e | v |         |                                                       |
| t | a |         |                                                       |
| e | l |         |                                                       |
| r | u |         |                                                       |
| * | e |         |                                                       |
| * | d |         |                                                       |
|   | e |         |                                                       |
|   | s |         |                                                       |
|   | c |         |                                                       |
|   | r |         |                                                       |
|   | i |         |                                                       |
|   | p |         |                                                       |
|   | t |         |                                                       |
|   | i |         |                                                       |
|   | o |         |                                                       |
|   | n |         |                                                       |
|   | * |         |                                                       |
|   | * |         |                                                       |
+===+===+=========+=======================================================+
| < | c | station | Control the sub-commands of the sp*2\* functions.     |
| c | h | v       | ‘connect’, ‘on/off’ and ‘disconnect’ are used to set  |
| o | a | difsize | up and tear down the transfer, not unintentionally    |
| n | r | bitspe  | quite like ‘in2net =’ and friends. See the connection |
| t |   | rsample | set up/teardown notes for ‘in2net=’ Note that ‘off’   |
| r |   | bitsper | only applies to ‘spin2\*’ transfers and pauses the    |
| o |   | channel | transfer, not stop it. ‘spin2*=on’ resumes the        |
| l |   | net_p   | transfer (again, not quite unlike ‘in2net=’) The      |
| > |   | rotocol | other controls can be used to configure the corner    |
|   |   | mtu ipd | turning itself and/or output details                  |
|   |   | connect |                                                       |
|   |   | on off  |                                                       |
|   |   | dis     |                                                       |
|   |   | connect |                                                       |
+---+---+---------+-------------------------------------------------------+
| < | V | char    | Two-letter station code.                              |
| c | D |         |                                                       |
| o | I |         |                                                       |
| n | F |         |                                                       |
| t | s |         |                                                       |
| r | t |         |                                                       |
| o | a |         |                                                       |
| l | t |         |                                                       |
| v | i |         |                                                       |
| a | o |         |                                                       |
| l | n |         |                                                       |
| u |   |         |                                                       |
| e |   |         |                                                       |
| > |   |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | o | int (>0 | Override automatic-VDIF frame size selection. ‘-1’ to |
|   | u | \| -1)  | select automatic selection. See Note 1.               |
|   | t |         |                                                       |
|   | p |         |                                                       |
|   | u |         |                                                       |
|   | t |         |                                                       |
|   | V |         |                                                       |
|   | D |         |                                                       |
|   | I |         |                                                       |
|   | F |         |                                                       |
|   | f |         |                                                       |
|   | r |         |                                                       |
|   | a |         |                                                       |
|   | m |         |                                                       |
|   | e |         |                                                       |
|   | s |         |                                                       |
|   | i |         |                                                       |
|   | z |         |                                                       |
|   | e |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | b | int     | Sets the indicated bit widths for decoding input data |
|   | i |         | format                                                |
|   | t |         |                                                       |
|   | s |         |                                                       |
|   | p |         |                                                       |
|   | e |         |                                                       |
|   | r |         |                                                       |
|   | s |         |                                                       |
|   | a |         |                                                       |
|   | m |         |                                                       |
|   | p |         |                                                       |
|   | l |         |                                                       |
|   | e |         |                                                       |
|   | / |         |                                                       |
|   | c |         |                                                       |
|   | h |         |                                                       |
|   | a |         |                                                       |
|   | n |         |                                                       |
|   | n |         |                                                       |
|   | e |         |                                                       |
|   | l |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | p | see     | Refer to the documentation of the standard            |
|   | r | ‘       | ‘net_protocol = ‘ command Id. for ‘mtu =’             |
|   | o | net_pro |                                                       |
|   | t | tocol=’ |                                                       |
|   | o | see     |                                                       |
|   | c | ‘mtu=’  |                                                       |
|   | o |         |                                                       |
|   | l |         |                                                       |
|   | , |         |                                                       |
|   | s |         |                                                       |
|   | o |         |                                                       |
|   | c |         |                                                       |
|   | k |         |                                                       |
|   | b |         |                                                       |
|   | u |         |                                                       |
|   | f |         |                                                       |
|   | s |         |                                                       |
|   | z |         |                                                       |
|   | m |         |                                                       |
|   | t |         |                                                       |
|   | u |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | i | see     | Id for ‘ipd =’                                        |
|   | p | ‘ipd=’  |                                                       |
|   | d |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | f | ASCII   | File name of file to open for reading data from       |
|   | i |         |                                                       |
|   | l |         |                                                       |
|   | e |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | n | int,    | Fill-pattern specifics, refer to ’fill2net = ‘        |
|   | w | i       | command documentation                                 |
|   | o | nt/hex, |                                                       |
|   | r | int     |                                                       |
|   | d |         |                                                       |
|   | , |         |                                                       |
|   | s |         |                                                       |
|   | t |         |                                                       |
|   | a |         |                                                       |
|   | r |         |                                                       |
|   | t |         |                                                       |
|   | , |         |                                                       |
|   | i |         |                                                       |
|   | n |         |                                                       |
|   | c |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | r | int     | If ‘0’ the system will run as fast as the hardware    |
|   | e |         | will, ‘1’ means try to follow data rate from ‘mode’   |
|   | a |         |                                                       |
|   | l |         |                                                       |
|   | t |         |                                                       |
|   | i |         |                                                       |
|   | m |         |                                                       |
|   | e |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | n | int,    | Indicates how many bytes to read from the I/O board.  |
|   | b | int     | Default 264-1                                         |
|   | y |         |                                                       |
|   | t |         |                                                       |
|   | e |         |                                                       |
+---+---+---------+-------------------------------------------------------+
|   | [ |         | Absolute start or end byte number; offset from        |
|   | + |         | scan_set= or amount if starts with ‘+’ prefix         |
|   | ] |         |                                                       |
|   | s |         |                                                       |
|   | t |         |                                                       |
|   | a |         |                                                       |
|   | r |         |                                                       |
|   | t |         |                                                       |
|   | b |         |                                                       |
|   | y |         |                                                       |
|   | t |         |                                                       |
|   | e |         |                                                       |
|   | , |         |                                                       |
|   | [ |         |                                                       |
|   | + |         |                                                       |
|   | ] |         |                                                       |
|   | e |         |                                                       |
|   | n |         |                                                       |
|   | d |         |                                                       |
|   | b |         |                                                       |
|   | y |         |                                                       |
|   | t |         |                                                       |
|   | e |         |                                                       |
+---+---+---------+-------------------------------------------------------+

Monitor-only arguments:

+-----+---+------------------------------------+----------------------+
| *   | * | **Values**                         | **Comments**         |
| *Pa | * |                                    |                      |
| ram | T |                                    |                      |
| ete | y |                                    |                      |
| r** | p |                                    |                      |
|     | e |                                    |                      |
|     | * |                                    |                      |
|     | * |                                    |                      |
+=====+===+====================================+======================+
| <p  | c | station \| vdifsize \|             | Retrieve the value   |
| ara | h | bitspersample \| bitsperchannel \| | of any of the        |
| met | a | net_protocol \| mtu \| ipd         | settable parameters  |
| er> | r |                                    |                      |
+-----+---+------------------------------------+----------------------+

Monitor-only parameters:

Only one of the two fields below is returned, depending on wether the
query had a <parameter> argument or not:

+-------------+-----+---------------------+---------------------------+
| **          | **  | **Values**          | **Comments**              |
| Parameter** | Typ |                     |                           |
|             | e** |                     |                           |
+=============+=====+=====================+===========================+
| <status>    | c   | connected \| active | Current status of         |
|             | har | \| inactive         | transfer.                 |
+-------------+-----+---------------------+---------------------------+
| <parameter  | c   |                     | Current value of          |
| value>      | har |                     | requested <parameter>     |
+-------------+-----+---------------------+---------------------------+

Notes:

1. The corner turner VDIF output section automatically selects an
appropriate output VDIF frame size, unless explicitly overriden. The
selection mechanism defaults to making one frame per output of the
corner turner. If the output is sent to the network using udp or udps
(==vtp) as protocol, however, it chooses otherwise. In this case it will
compute the largest compatible VDIF frame size that will fit into the
configured MTU and breaks up the corner turner output into an integer
number of VDIF frames, if such a size exists.

SS_rev[12] – Get StreamStor firmware/software revision levels (query only)
==========================================================================

Query syntax: SS_rev? ;

Query response: !SS_rev ? <return code> : <SS field1> : <SS field2>: …..
: <SS fieldn> ; Purpose: Get information on StreamStor firmware/software
revision levels.

Monitor-only parameters:

+--------+---+---+------------------------------------------------------+
| *      | * | * | **Comments**                                         |
| *Param | * | * |                                                      |
| eter** | T | V |                                                      |
|        | y | a |                                                      |
|        | p | l |                                                      |
|        | e | u |                                                      |
|        | * | e |                                                      |
|        | * | s |                                                      |
|        |   | * |                                                      |
|        |   | * |                                                      |
+========+===+===+======================================================+
| <SSf   | l |   | Primarily for diagnostic purposes. The character     |
| ield1> | i |   | stream returned from StreamStor, which is very long, |
| t      | t |   | is divided into 32-character fields separated by     |
| hrough | e |   | colons to stay within Field System limits. See       |
| <SSf   | r |   | Notes.                                               |
| ieldn> | a |   |                                                      |
|        | l |   |                                                      |
|        | A |   |                                                      |
|        | S |   |                                                      |
|        | C |   |                                                      |
|        | I |   |                                                      |
|        | I |   |                                                      |
+--------+---+---+------------------------------------------------------+

Notes:

1. ‘SS_rev?’ is a replacement for the old ‘SS_rev1?’ and ‘SS_rev2?’
queries; all three of these queries are now synonyms. *jive5ab*

supports the ‘SS_rev1?’ and ‘SS_rev2?’ queries only on Mark5A systems.

start_stats – Start gathering disk-performance statistics
=========================================================

Command syntax: start_stats = [<t0> : <t1> :….: <t6>] ; Command
response: !start_stats = <return code> ;

Query syntax: start_stats? ;

Query response: !start_stats ? <return code> : <t0> : <t1> :….: <t6> ;
Purpose: Start gather disk performance statistics

Settable parameters:

+---+---+---+------+----------------------------------------------------------+
| * | * | * | **D  | **Comments**                                             |
| * | * | * | efau |                                                          |
| P | T | A | lt** |                                                          |
| a | y | l |      |                                                          |
| r | p | l |      |                                                          |
| a | e | o |      |                                                          |
| m | * | w |      |                                                          |
| e | * | e |      |                                                          |
| t |   | d |      |                                                          |
| e |   | v |      |                                                          |
| r |   | a |      |                                                          |
| * |   | l |      |                                                          |
| * |   | u |      |                                                          |
|   |   | e |      |                                                          |
|   |   | s |      |                                                          |
|   |   | * |      |                                                          |
|   |   | * |      |                                                          |
+===+===+===+======+==========================================================+
| < | t |   | 0    | Clears and restarts gathering of drive statistics. See   |
| t | i |   | .001 | Notes. Seven optional values define 8 bins corresponding |
| n | m |   | 125s | to drive-response (i.e. transaction completion) times;   |
| > | e |   | 0.00 | values must increase monotonically; a separate set of    |
|   |   |   | 225s | bins is maintained for each mounted drive. The count in  |
|   |   |   | 0.0  | a bin is incremented according to the following rules,   |
|   |   |   | 045s | where ‘t’ is drive-response time of a single read or     |
|   |   |   | 0.   | write transaction: Bin 0: t<t0 Bin 1: t0<t<t1 . Bin 6:   |
|   |   |   | 009s | t5<t<t6 Bin 7: t>t6                                      |
|   |   |   | 0.   |                                                          |
|   |   |   | 018s |                                                          |
|   |   |   | 0.   |                                                          |
|   |   |   | 036s |                                                          |
|   |   |   | 0.   |                                                          |
|   |   |   | 072s |                                                          |
+---+---+---+------+----------------------------------------------------------+

Notes:

1. Drive statistics and replaced-block counts are cleared and re-started
   whenever a new disk module is mounted or a ‘start_stats’ command is
   issued. Read drive statistics with ‘get_stats’ query. Bin values are
   common for all drives. Each count within a bin represents a transfer
   of 65528 bytes (216-8).
2. The ‘start_stats’ command may not be issued during active recording
   or readback.

status – Get system status (query only)
=======================================

Query syntax: status? ;

Query response: !status ? <return code> : <status word> [ : <error
number> : <error message> [ : <error time> ] ] ; Purpose: Get general
system status

Monitor-only parameters:

+---+---+---+---------------------------------------------------------------------+
| * | * | * | **Comments**                                                        |
| * | * | * |                                                                     |
| P | T | V |                                                                     |
| a | y | a |                                                                     |
| r | p | l |                                                                     |
| a | e | u |                                                                     |
| m | * | e |                                                                     |
| e | * | s |                                                                     |
| t |   | * |                                                                     |
| e |   | * |                                                                     |
| r |   |   |                                                                     |
| * |   |   |                                                                     |
| * |   |   |                                                                     |
+===+===+===+=====================================================================+
| < | h | - | Bit 0 – (0x0001) system ‘ready’ Bit 1 – (0x0002) error message(s)   |
| s | e |   | pending; (message may be appended); messages may be queued; error   |
| t | x |   | is NOT cleared by this command. See also ‘error?’ query Bit 2 –     |
| a |   |   | (0x0004) not used Bit 3 – (0x0008) one or more ‘delayed-completion’ |
| t |   |   | commands are pending. Also set whenever any data- transfer          |
| u |   |   | activity, such as recording, playing, or transfer to or from disk   |
| s |   |   | or net, is active or waiting. Bit 4 – (0x0010) one or more          |
| w |   |   | ‘delayed-completion’ queries are pending Bit 5 – (0x0020) Disk-FIFO |
| o |   |   | mode Bit 6 - (0x0040) record ‘on’ Bit 7 - (0x0080) media full       |
| r |   |   | (recording halted) Bit 8 - (0x0100) readback ‘on’ Bit 9 - (0x0200)  |
| d |   |   | end-of-scan or end-of-media (readback halted) Bit 10 – (0x0400)     |
| > |   |   | recording can’t keep up; some lost data Bit 11 – (0x0800) not used  |
|   |   |   | Bit 12 – (0x1000) disk2file active Bit 13 – (0x2000) file2disk      |
|   |   |   | active Bit 14 – (0x4000) disk2net active Bit 15 – (0x8000) net2disk |
|   |   |   | active or waiting Bit 16 – (0x10000) in2net sending (on) Bit 17 –   |
|   |   |   | (0x20000) net2out active or waiting Bit 18 – (0x40000) DIM ready to |
|   |   |   | record Bit 19 – (0x80000) DOM ready to play Bits 20-27 are set      |
|   |   |   | properly even if a data transfer is in progress. Bit 20 –           |
|   |   |   | (0x100000) Bank A selected Bit 21 – (0x200000) Bank A ready Bit 22  |
|   |   |   | – (0x400000) Bank A media full or faulty (not writable) Bit 23 –    |
|   |   |   | (0x800000) Bank A write protected Bit 24 – (0x1000000) Bank B       |
|   |   |   | selected Bit 25 – (0x2000000) Bank B ready Bit 26 – (0x4000000)     |
|   |   |   | Bank B media full or faulty (not writable) Bit 27 – (0x8000000)     |
|   |   |   | Bank B write protected                                              |
+---+---+---+---------------------------------------------------------------------+
| < | i | - | (optional) error number of error pending causing bit 1 to be set –  |
| e | n |   | see Note                                                            |
| r | t |   |                                                                     |
| r |   |   |                                                                     |
| o |   |   |                                                                     |
| r |   |   |                                                                     |
| n |   |   |                                                                     |
| u |   |   |                                                                     |
| m |   |   |                                                                     |
| b |   |   |                                                                     |
| e |   |   |                                                                     |
| r |   |   |                                                                     |
| > |   |   |                                                                     |
+---+---+---+---------------------------------------------------------------------+
| < | c | - | (optional) error message of error pending causing bit 1 to be set – |
| e | h |   | see Note                                                            |
| r | a |   |                                                                     |
| r | r |   |                                                                     |
| o |   |   |                                                                     |
| r |   |   |                                                                     |
| m |   |   |                                                                     |
| e |   |   |                                                                     |
| s |   |   |                                                                     |
| s |   |   |                                                                     |
| a |   |   |                                                                     |
| g |   |   |                                                                     |
| e |   |   |                                                                     |
| > |   |   |                                                                     |
+---+---+---+---------------------------------------------------------------------+
| < | t | - | (since 2.6.0) system time when the error was added to the queue     |
| e | i |   |                                                                     |
| r | m |   |                                                                     |
| r | e |   |                                                                     |
| o |   |   |                                                                     |
| r |   |   |                                                                     |
| t |   |   |                                                                     |
| i |   |   |                                                                     |
| m |   |   |                                                                     |
| e |   |   |                                                                     |
| > |   |   |                                                                     |
+---+---+---+---------------------------------------------------------------------+

Notes:

1. \ *jive5ab* returns the error details only if an error is pending,
   like Mark5A/DIMino does. This is undocumented behaviour for
   Mark5A/DIMino. In *jive5ab* the error is **not** cleared by this
   status? query, in direct contrast to Mark5A/DIMino.

task_ID – Set task ID (primarily for correlator use)
====================================================

Command syntax: task_ID = <task_ID> ; Command response: !task_ID =
<return code> ;

Query syntax: task_ID? ;

Query response: !task_ID ? <return code> : <task_ID> ; Purpose: Set task
ID (primarily for correlator use)

Settable parameters:

+----+---+------+---+-------------------------------------------------+
| *  | * | *    | * | **Comments**                                    |
| *P | * | *All | * |                                                 |
| ar | T | owed | D |                                                 |
| am | y | valu | e |                                                 |
| et | p | es** | f |                                                 |
| er | e |      | a |                                                 |
| ** | * |      | u |                                                 |
|    | * |      | l |                                                 |
|    |   |      | t |                                                 |
|    |   |      | * |                                                 |
|    |   |      | * |                                                 |
+====+===+======+===+=================================================+
| <  | i |      |   | For use with Mark 4 correlator only: Causes     |
| ta | n |      |   | Mark 5 system to listen to only ROT broadcasts  |
| sk | t |      |   | with the corresponding ‘task ID’. See Notes.    |
| _I |   |      |   |                                                 |
| D> |   |      |   |                                                 |
+----+---+------+---+-------------------------------------------------+

Notes:

1. The ‘task_ID’ command is used in conjunction with the ‘play’ command
   for accurate synchronization of Mark 5 playback-start with correlator
   ROT clock.
2. On generic systems the task_ID command is recognized and ROT
   broadcasts are monitored but at the moment none of the data transfers
   available on those systems observe the actual ROT clock.

track_check – Check data on selected track (query only)
=======================================================

Query syntax: track_check? ;

Query response: !track_check ? <return code> : <data mode> : <data
submode> : <data time> : <byte offset> :

<track frame period> : <track data rate> : <decoded track#> : <#missing
bytes>; Purpose: Check recorded track which, on playback, will output
data to track pointed to by current ‘track_set’ value. Monitor-only
parameters:

+----+---+-------+-------------------------------------------------------+
| P  | T | V     | Comments                                              |
| ar | y | alues |                                                       |
| am | p |       |                                                       |
| et | e |       |                                                       |
| er |   |       |                                                       |
+====+===+=======+=======================================================+
| <  | c | st \| | See ‘mode’ command for explanation of data modes;     |
| da | h | mark4 | ’tvg’ corresponds to VSI test pattern;’ SS’           |
| ta | a | \|    | corresponds to StreamStor test pattern ’?’ indicates  |
| m  | r | vlba  | unknown format. **Note** – If a Mark 5B module is     |
| od |   | \|    | present, a ‘data_check’ is done instead – See Note 6  |
| e> |   | tvg   |                                                       |
|    |   | \| SS |                                                       |
+----+---+-------+-------------------------------------------------------+
| <  | c | 8 \|  | ‘8|16|32|64’ if <data mode> is ‘mark4’ or ‘vlba’;     |
| da | h | 16 \| | ’mark4|vlba’ if <data mode> is ‘st’                   |
| ta | a | 32 \| |                                                       |
| su | r | 64 \| |                                                       |
| bm |   | mark4 |                                                       |
| od |   | \|    |                                                       |
| e> |   | vlba  |                                                       |
+----+---+-------+-------------------------------------------------------+
| <  | t |       | Time tag from next ‘track’ frame header beyond        |
| da | i |       | current play pointer. See Note 5 of ‘scan_check’.     |
| ta | m |       |                                                       |
| t  | e |       |                                                       |
| im |   |       |                                                       |
| e> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+
| <  | i | b     | Byte offset from current play pointer to beginning of |
| by | n | ytes- | next ‘track’ frame header of target track             |
| te | t |       |                                                       |
| o  |   |       |                                                       |
| ff |   |       |                                                       |
| se |   |       |                                                       |
| t> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+
| <t | t |       | Time tag difference between adjacent track frames;    |
| ra | i |       | allows original track data rate to be determined.     |
| ck | m |       |                                                       |
| f  | e |       |                                                       |
| ra |   |       |                                                       |
| me |   |       |                                                       |
| p  |   |       |                                                       |
| er |   |       |                                                       |
| io |   |       |                                                       |
| d> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+
| <t | r | MHz   | Track data rate of source data from formatter.        |
| ra | e |       |                                                       |
| ck | a |       |                                                       |
| da | l |       |                                                       |
| ta |   |       |                                                       |
| r  |   |       |                                                       |
| at |   |       |                                                       |
| e> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+
| <d | i |       | Track# decoded from auxiliary data field of target    |
| ec | n |       | track; followed by ‘D’ if track is a ‘duplicated’     |
| od | t |       | track; followed by‘?’ if unallowed track# in this     |
| ed |   |       | position. See Note 3.                                 |
| t  |   |       |                                                       |
| ra |   |       |                                                       |
| ck |   |       |                                                       |
| #> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+
| <  | i | bytes | Number of missing bytes between last and current      |
| #m | n |       | ‘track_check’; Should be =0 if immediately previous   |
| is | t |       | ‘track_check’ was within same scan Meaningless if     |
| si |   |       | immediately previous ‘track_check’ was in a different |
| ng |   |       | scan. See Note 4. See also Note 6 in ‘scan_check’     |
| by |   |       |                                                       |
| te |   |       |                                                       |
| s> |   |       |                                                       |
+----+---+-------+-------------------------------------------------------+

Notes:

1. The ‘track_check’ query will be honored only if record and play are
   both off.
2. The ‘track_check’ query checks data beginning at the current position
   of the play pointer; the play pointer is not affected.
3. The ‘track_check’ query targets the first of the two selected
   ‘track_set’ tracks and executes the following actions:

   1. Determines the data mode/submode based on the format of the disk
      data.
   2. If the target track is a track which is actually recorded in this
      mode/submode (see ‘mode’ command Notes), several frames of data
      are collected from the expected position of this track in the disk
      data. If the target track is not recorded, the data are collected
      from the position of the recorded track number which, during
      playback, is duplicated onto the target track (see ‘play’ command
      Notes) in this mode/submode.
   3. A ‘track frame header’ is extracted from the collected data and
      the embedded <data time> and <track#> information is decoded. Note
      that the <decoded track#> will match the target track only in the
      case in which the target track was actually recorded.

4. Further analysis is done to determine the <track frame period> and
   <#missing bytes>. A ‘blank’ is returned in the <#missing bytes> field
   if the # of missing bytes cannot be calculated.
5. Regarding the ‘data time’ value returned by the ‘data_check?’,
   ‘scan_check?’ and ‘track_check?’ queries: The Mark 4 time-tags
   contain the day-of-year (DOY) but only the final digit of the year;
   the VLBA time-tags contain, instead, the last 3 digits of the Julian
   day number (misnamed MJD). To show the year and DOY in the returned
   values of ‘data time’ requires some assumptions. For Mark 4, we
   assume the most recent year consistent with the unit-year and DOY
   written in the Mark 4 time-tag; this algorithm reports the proper
   year provided the data were taken no more than 10 years ago. For
   VLBA, we assume the most recent Julian Day Number (JDN) consistent
   with the last 3 digits available in the VLBA time-tag; this algorithm
   reports the proper year provided the data were taken no more than
   1000 days ago.
6. When a Mark 5B module is inserted into the Mark 5A (operating in
   so-called ‘Mark 5A+’ mode), a ‘track_check?’ query is meaningless
   since Mark 5B has no notion of ‘tracks’. Instead, a ‘track_check?’
   query performs a ‘data_check’. This is done to maintain backward
   compatibility with existing Mark 4 correlator software.

trackmask – Configure channel dropping setup
============================================

Command syntax: trackmask = <bit mask> ; Command response: !trackmask =
<return code> ;

Query syntax: trackmask? ;

Query response: !trackmask? <return code> : <bit mask> ; Purpose: Set up
channel dropping data compression.

Settable parameters:

+--------+-----+--------------------------------------------------------+
| *      | **  | **Comments**                                           |
| *Param | Typ |                                                        |
| eter** | e** |                                                        |
+========+=====+========================================================+
| <bit   | hex | 64 bit hexadecimal value 0x… Bit streams with a ‘1’ in |
| mask>  |     | their position are kept. See Note 3.                   |
+--------+-----+--------------------------------------------------------+

Monitor-only parameters:

+----------+------+----------------------------------------------------+
| **Par    | **Ty | **Comments**                                       |
| ameter** | pe** |                                                    |
+==========+======+====================================================+
| <bit     | hex  | The current active bit mask. ‘0’ means channel     |
| mask>    |      | dropping is not active.                            |
+----------+------+----------------------------------------------------+

Notes:

1. The channel dropping mechanism is described in Section **8.1** of
   this manual.
2. The ‘trackmask=’ will return ‘1’ (“initiated command but not
   finished”) if the bit mask is not equal to 0. *jive5ab* is computing
   the compression algorithm and generating the (de)compression code in
   the background. It is only safe to start a transfer when ‘trackmask?’
   does not return a ‘1’ return code any more.
3. The bit mask resembles the function of the bit stream mask in the
   Mark5B mode = ext : 0x… command. It is vitally important to realize
   that the trackmask bit mask needs to be 64 bit value. If less than 64
   bits are specified in the bit mask, the lower bits of the 64 bit mask
   will be filled in with the <bit mask> parameter, higher order bits
   are zeroed. As such, when dealing with 32 bit streams (e.g. on the
   Mark5B), the mask must be replicated twice or more data will be
   thrown away than expected.

track_set – Select tracks for monitoring with DQA or ‘track_check’
==================================================================

Command syntax: track_set = <track A> : <track B> ; Command response:
!track_set = <return code> ;

Query syntax: track_set? ;

Query response: !track_set ? <return code> : <track A> : <track B> ;

Purpose: The ‘track_set’ command serves a two-fold purpose: 1) to select
two tracks to be output to the Mark 4 decoder or VLBA DQA and 2) to
select the track examined by the ‘track_check’ query.

Settable parameters:

+---+---+------+---+---------------------------------------------------------+
| * | * | *    | * | **Comments**                                            |
| * | * | *All | * |                                                         |
| P | T | owed | D |                                                         |
| a | y | valu | e |                                                         |
| r | p | es** | f |                                                         |
| a | e |      | a |                                                         |
| m | * |      | u |                                                         |
| e | * |      | l |                                                         |
| t |   |      | t |                                                         |
| e |   |      | * |                                                         |
| r |   |      | * |                                                         |
| * |   |      |   |                                                         |
| * |   |      |   |                                                         |
+===+===+======+===+=========================================================+
| < | i | 2-33 | 1 | Track selected to be sent to DQA/decoder channel A;     |
| t | n | (h   | 5 | track to be analyzed by ‘track_check’. Default is       |
| r | t | dstk |   | headstack 1; add 100 for headstack 2, if present. Track |
| a | / | 1)   |   | numbers follow the ‘VLBA’ convention; i.e. 2-33 for     |
| c | c | 102  |   | headstack 1, 102-133 for headstack 2. ’inc’ increments  |
| k | h | -133 |   | current value – see Note 3; ‘dec’ decrements current    |
| A | a | (h   |   | value. If null field, current value is maintained.      |
| > | r | dstk |   |                                                         |
|   |   | 2)   |   |                                                         |
|   |   | \|   |   |                                                         |
|   |   | inc  |   |                                                         |
|   |   | \|   |   |                                                         |
|   |   | dec  |   |                                                         |
+---+---+------+---+---------------------------------------------------------+
| < | i | 2-33 | 1 | Track selected to be sent to DQA/decoder channel B.     |
| t | n | (h   | 6 | ’inc’ increments current value – see Note 3; ‘dec’      |
| r | t | dstk |   | decrements current value. If null field, current value  |
| a | / | 1)   |   | is maintained.                                          |
| c | c | 102  |   |                                                         |
| k | h | -133 |   |                                                         |
| B | a | (h   |   |                                                         |
| > | r | dstk |   |                                                         |
|   |   | 2)   |   |                                                         |
|   |   | \|   |   |                                                         |
|   |   | inc  |   |                                                         |
|   |   | \|   |   |                                                         |
|   |   | dec  |   |                                                         |
+---+---+------+---+---------------------------------------------------------+

Notes:

1. Note that tracks are duplicated according to the table in the Notes
   with the ‘play’ command. Any of the ‘primary’ or ‘duplicated’ tracks
   may be selected to go to the DQA/decoder.
2. <track A> is also used as the track to be examined by the
   ‘track_check’ query and should correspond to a track that is actually
   recorded in the selected data mode (see table with ‘record’ command).
3. The ‘inc’ value increments the current selected track value by one;
   cycles through all 32 tracks on each headstack, then begins again.
   This is a convenient method of cycling through all tracks during
   system testing.

tstat – Get current runtime status and performance
==================================================

Command syntax: tstat = <ignored> ;

Command response: !tstat = <return code> : <time> : <status> [ : <step
#1> : <byte count #1> : … ] ;

Query syntax: tstat? ;

Query response: !tstat ? <return code> : <delta-time> : <status> [ :
<step #1> : <performance #1> : … ] ;

Purpose: The ‘tstat’ query/command return information of which transfer
the runtime the command is sent to is doing (<status>) and how fast
(query) or the raw byte counts since start-of-transfer (command) each
step in the processing chain is operating.

Settable parameters: none. Any parameters given are ignored; it’s only
to differentiate between query or command. By providing time stamps and
raw byte counts, an inquisitive application can do monitoring and/or
differencing (“bytes/second”) itself:

+---------+-------+---------------------------------------------------+
| **Para  | **T   | **Comments**                                      |
| meter** | ype** |                                                   |
+=========+=======+===================================================+
| <time>  | UNIX  | UNIX time stamp when ‘tstat=’ command was         |
|         | time  | processed, with fractional decimal seconds value  |
|         | stamp | added                                             |
+---------+-------+---------------------------------------------------+
| <       | ASCII | String describing which transfer is executing.    |
| status> |       | ‘idle’ if nothing happening                       |
+---------+-------+---------------------------------------------------+
| <step   | ASCII | If <status> is not ‘idle’, the name of step       |
| #\ *N*> |       | #\ *N* in the current processing chain            |
+---------+-------+---------------------------------------------------+
| <byte   | ASCII | If <status> is not ‘idle’, the number of bytes    |
| count   |       | processed by step #\ *N* in the current           |
| #\ *N*> |       | processing chain                                  |
+---------+-------+---------------------------------------------------+

Monitor-only parameters:

+----+---+--------------------------------------------------------------+
| *  | * | **Comments**                                                 |
| *P | * |                                                              |
| ar | T |                                                              |
| am | y |                                                              |
| et | p |                                                              |
| er | e |                                                              |
| ** | * |                                                              |
|    | * |                                                              |
+====+===+==============================================================+
| <d | d | Amount of seconds since last ‘tstat?’ query was processed    |
| el | o |                                                              |
| ta | u |                                                              |
| -t | b |                                                              |
| im | l |                                                              |
| e> | e |                                                              |
+----+---+--------------------------------------------------------------+
| <s | A | String describing which transfer is executing. ‘idle’ if     |
| ta | S | nothing happening                                            |
| tu | C |                                                              |
| s> | I |                                                              |
|    | I |                                                              |
+----+---+--------------------------------------------------------------+
| <  | A | If <status> is not ‘idle’, the name of step #\ *N* in the    |
| st | S | current processing chain (repeat for all steps)              |
| ep | C |                                                              |
| #  | I |                                                              |
| \  | I |                                                              |
| *N |   |                                                              |
| *> |   |                                                              |
+----+---+--------------------------------------------------------------+
| <p | A | If <status> is not ‘idle’, the processing speed of step      |
| er | S | #\ *N* in bytes/second in the current processing chain. The  |
| fo | C | performance is computed by differencing the current byte     |
| rm | I | count for step #\ *N* and its previous byte count and divide |
| an | I | by <delta-time>                                              |
| ce |   |                                                              |
| #  |   |                                                              |
| \  |   |                                                              |
| *N |   |                                                              |
| *> |   |                                                              |
+----+---+--------------------------------------------------------------+

Notes:

1. The ‘tstat?’ query version does not attempt to deal with multiple
   simultaneous pollers. So if two users and/or applications are polling
   ‘tstat?’ in the same runtime, the returned delta-time may not be the
   poller’s intended poll-time interval. For predictable time intervals
   the application should use the ‘tstat=’ command version and do the
   differencing/dividing-by-time-span itself.
2. If <status> is not ‘idle’, the reply always contains pairs of <name>
   and <count> (or <performance>). The <name> of a processing step is
   usally self-explanatory.

TVR – Start TVR testing
=======================

Command syntax: TVR = <tvr mask> ; Command response: ! TVR = <return
code>

Query syntax: TVR? ;

Query response: ! TVR? <return code> : <status> : <tvr mask> : <tvr
error> ; Purpose: Start TVR testing

Settable parameters:

+------+----+---------+------------+---------------------------------+
| *    | ** | **      | *          | **Comments**                    |
| *Par | Ty | Allowed | *Default** |                                 |
| amet | pe | v       |            |                                 |
| er** | ** | alues** |            |                                 |
+======+====+=========+============+=================================+
| <tvr | h  |         | Current    | Defines bit streams to be       |
| m    | ex |         | bit-stream | tested; reset <tvr error> flag  |
| ask> |    |         | mask       | to 0.                           |
+------+----+---------+------------+---------------------------------+

Monitor-only parameters:

+--------+-----+------+------------------------------------------------+
| *      | **  | **   | **Comments**                                   |
| *Param | Typ | Valu |                                                |
| eter** | e** | es** |                                                |
+========+=====+======+================================================+
| <s     | int | 0 \| | 0 – TVR not running 1 – TVR running            |
| tatus> |     | 1    |                                                |
+--------+-----+------+------------------------------------------------+
| <tvr   | hex |      |                                                |
| mask>  |     |      |                                                |
+--------+-----+------+------------------------------------------------+
| <tvr   | int | 0 \| | – no errors detected – at least one error      |
| error> |     | 1    | detected; reset <tvr error> flag to 0          |
+--------+-----+------+------------------------------------------------+

Notes:

1. The TVR receives data from the VSI input and can be used only when
the ext (VSI) clock has been selected by the ‘clock_set’ command. After
issuing the ‘TVR’ command, the TVR will start operation on the next DOT
1pps tick. Only the bit-streams specified in the <tvr mask> will be
tested; only undecimated TVG data can be tested. The tested bit streams
must appear on the VSI input bit-streams in the positions expected for
full 32-bit TVG pattern. A ‘TVR?’ query may be issued anytime after the
TVR has started. Any single bit error on any of the selected bit-streams
sets the <tvr error> status bit to ‘1’, then and clears the <tvr error>
in anticipation of the next ‘tvr?’ query.

(un)mount – power up/down bank (as if keying the bank on or off) (jive5ab >= 2.8.2)
===================================================================================

Command syntax: mount = <bank> [ : <bank> ] ;

unmount = <bank> [ : <bank> ] ; Command response: !(un)mount = <return
code> ;

Purpose: Power one or more banks up or down, as if a bank’s key was
turned Settable parameters:

+---------+-----+------------+--------+------------------------------+
| **Para  | **  | **Allowed  | **Def  | **Comments**                 |
| meter** | Typ | values**   | ault** |                              |
|         | e** |            |        |                              |
+=========+=====+============+========+==============================+
| <bank>  | c   | A|B        |        | Only banks ‘A’ and ‘B’ are   |
|         | har |            |        | valid bank labels            |
+---------+-----+------------+--------+------------------------------+

Notes:

1. The key on the Mark5 front panel switches power to the corresponding
disk pack on or off and brings the disks up (or down) in a controlled
fashion. This power management is done by the StreamStor firmware. The
firmware responds to a key transition from ‘off’ to ‘on’ (bring power to
the disk pack) or from ‘on’ to ‘off’ to power the disk pack off. This
powering on/off can also be triggered via the Conduant StreamStor SDK
and since jive5ab 2.8.2 through these VSI/S commands.

version – Get detailed version information of this *jive5ab*\copyright  (query only)
=======================================================================================

Query syntax: version? ;

Query response: !version ? <return code> : <program> : <version> :
<#bits> : <release> :

<build info> : <StreamStor SDK path> [ : <more gunk> ]\* ; Purpose: Get
detailed properties of the *jive5ab* binary.

Monitor-only parameters:

+------+---+--------+----------------------------------------------------+
| P    | T | Values | Comments                                           |
| aram | y |        |                                                    |
| eter | p |        |                                                    |
|      | e |        |                                                    |
+======+===+========+====================================================+
| <    | c | j      | If really talking to *jive5ab*\ @copyright         |
| prog | h | ive5ab |  it will say jive5ab here.                         |
| ram> | a |        |                                                    |
|      | r |        |                                                    |
+------+---+--------+----------------------------------------------------+
| <    | c | x.     | Numeric version, at least                          |
| vers | h | y[.z[- | *major*\ \*\*.*\*\ *minor*. Sometimes including    |
| ion> | a | gunk]] | *patchlevel* and extra *gunk*. See Note 2. Updated |
|      | r |        | >= 3.0.0                                           |
+------+---+--------+----------------------------------------------------+
| <#b  | c | 32bit  | Wether *jive5ab* was compiled as 32-bit or 64-bit  |
| its> | h | \|     | binary                                             |
|      | a | 64bit  |                                                    |
|      | r |        |                                                    |
+------+---+--------+----------------------------------------------------+
| <    | c | dev \| | Wether this was a development or a release build.  |
| rele | h | r      | \ *jive5ab* >= 3.0.0 is built using CMake, which   |
| ase> | a | elease | has its own, built-in, configuration management    |
|      | r | Debug  | which is now adopted by *jive5ab*.                 |
|      |   | \|     |                                                    |
|      |   | R      |                                                    |
|      |   | elease |                                                    |
+------+---+--------+----------------------------------------------------+
| <b   | c |        | Host name and host’s date and time at the time     |
| uild | h |        | when this binary was compiled. See Note 1.         |
| i    | a |        |                                                    |
| nfo> | r |        |                                                    |
+------+---+--------+----------------------------------------------------+
| <St  | c |        | The path to Conduant’s SDK library that this       |
| ream | h |        | *jive5ab* was linked with. See Note 3.             |
| Stor | a |        |                                                    |
| SDK  | r |        |                                                    |
| p    |   |        |                                                    |
| ath> |   |        |                                                    |
+------+---+--------+----------------------------------------------------+
| <    | c |        | In releases >= 3.0.0 more optional functionality   |
| more | h |        | might be compiled in, the gunk will contain        |
| g    | a |        | information which.                                 |
| unk> | r |        |                                                    |
+------+---+--------+----------------------------------------------------+

Notes:

1. Each time *jive5ab* is compiled, the host name and host’s date/time
   are compiled into the binary.
2. *gunk* may be ‘FiLa10G’ if this *jive5ab* was compiled with the
   FILA=1 make command line argument. This compile-time switch makes
   *jive5ab* deal correctly with the broken 64 bit sequence numbers that
   the RDBE and FiLa10G output when the output format is set to mark5b
   on the latter. *jive5ab*\ ’s compiled with this flag will fail to
   read data from correctly sent sequence numbers, e.g. when FiLa10G’s
   output format is changed to VDIF, or when data from another *jive5ab*
   is attempted to read. Starting from *jive5ab* 3.0.0, using CMake’s
   optional configuration settings, these are reported separately
   through the <more gunk> fields.
3. If no StreamStor SDK was available (e.g. on generic hardware) or when
   compiling with StreamStor support was disabled, this will read
   ‘nossapi’ – “no streamstor API”.

VSN – Write extended-VSN to permanent area
==========================================

Command syntax: VSN = <VSN> ; Command response: !VSN = <return code> ;

Query syntax: VSN? ;Query response: !VSN ? <return code> : <extended
VSN> : <status> :

[: <disk#> : <original S/N> : <new S/N> : ‘Disk serial-number mismatch’]
:

<companion extended VSN> : <companion bank>;

Purpose: Write module extended-VSN (volume serial number) to permanent
area on active module

Settable parameters:

+---+---+---+---+-------------------------------------------------------------+
| * | * | * | * | **Comments**                                                |
| * | * | * | * |                                                             |
| P | T | A | D |                                                             |
| a | y | l | e |                                                             |
| r | p | l | f |                                                             |
| a | e | o | a |                                                             |
| m | * | w | u |                                                             |
| e | * | e | l |                                                             |
| t |   | d | t |                                                             |
| e |   | v | * |                                                             |
| r |   | a | * |                                                             |
| * |   | l |   |                                                             |
| * |   | u |   |                                                             |
|   |   | e |   |                                                             |
|   |   | s |   |                                                             |
|   |   | * |   |                                                             |
|   |   | * |   |                                                             |
+===+===+===+===+=============================================================+
| V | c |   |   | Permanent 8-character VSN, analogous to tape VSN, which     |
| S | h |   |   | survives ‘reset=erase’ command and module conditioning      |
| N | a |   |   | (example: ‘MPI-0153’). VSN format rules are enforced – see  |
|   | r |   |   | Note 4. The module capacity and maximum data rate for the   |
|   |   |   |   | extended-VSN are calculated and appended to the VSN to      |
|   |   |   |   | create the ‘extended-VSN’ (example ‘MPI-0153/960/1024’).    |
|   |   |   |   | For non-bank-mode modules, see Note 7.                      |
+---+---+---+---+-------------------------------------------------------------+

Monitor-only parameters:

+------------+---+---+--------------------------------------------------+
| **P        | * | * | **Comments**                                     |
| arameter** | * | * |                                                  |
|            | T | A |                                                  |
|            | y | l |                                                  |
|            | p | l |                                                  |
|            | e | o |                                                  |
|            | * | w |                                                  |
|            | * | e |                                                  |
|            |   | d |                                                  |
|            |   | v |                                                  |
|            |   | a |                                                  |
|            |   | l |                                                  |
|            |   | u |                                                  |
|            |   | e |                                                  |
|            |   | s |                                                  |
|            |   | * |                                                  |
|            |   | * |                                                  |
+============+===+===+==================================================+
| <extended  | c |   | Example: ‘MPI-0153/960/1024’; see Notes 4 and 5. |
| VSN>       | h |   | For non-bank-mode modules, see Note 7.           |
|            | a |   |                                                  |
|            | r |   |                                                  |
+------------+---+---+--------------------------------------------------+
| <status>   | c | O | OK – disk serial #’s on current set of disks     |
|            | h | K | matches serial #’s when VSN was last written.    |
|            | a | \ | Unknown – disk serial #’s have not been written  |
|            | r | | | Fail – current disk serial #’s do not match      |
|            |   | U | serial #’s when VSN was last written. See Note   |
|            |   | n | 6.                                               |
|            |   | k |                                                  |
|            |   | n |                                                  |
|            |   | o |                                                  |
|            |   | w |                                                  |
|            |   | n |                                                  |
|            |   | \ |                                                  |
|            |   | | |                                                  |
|            |   | F |                                                  |
|            |   | a |                                                  |
|            |   | i |                                                  |
|            |   | l |                                                  |
+------------+---+---+--------------------------------------------------+
| Following  |   |   |                                                  |
| parameters |   |   |                                                  |
| are        |   |   |                                                  |
| returned   |   |   |                                                  |
| only if    |   |   |                                                  |
| <status>   |   |   |                                                  |
| is ’Fail”: |   |   |                                                  |
+------------+---+---+--------------------------------------------------+
| <disk#>    | i | 0 | First disk# in module in which there is a        |
|            | n | - | serial-number discrepancy                        |
|            | t | 7 |                                                  |
+------------+---+---+--------------------------------------------------+
| <original  | c |   | Serial number of disk in position <disk#> when   |
| S/N>       | h |   | VSN was written                                  |
|            | a |   |                                                  |
|            | r |   |                                                  |
+------------+---+---+--------------------------------------------------+
| <new S/N>  | c |   | Serial number of disk now in position <disk#>    |
|            | h |   |                                                  |
|            | a |   |                                                  |
|            | r |   |                                                  |
+------------+---+---+--------------------------------------------------+
| ‘Disk      | c |   | Warning message                                  |
| ser        | h |   |                                                  |
| ial-number | a |   |                                                  |
| mismatch’  | r |   |                                                  |
+------------+---+---+--------------------------------------------------+
| <companion | c |   | If non-bank-mode module, returns VSN of          |
| extended   | h |   | companion non-bank module. See Note 8.           |
| VSN>       | a |   |                                                  |
|            | r |   |                                                  |
+------------+---+---+--------------------------------------------------+
| <companion | c | B | Bank position of companion non-bank module       |
| bank>      | h | \ |                                                  |
|            | a | | |                                                  |
|            | r | A |                                                  |
+------------+---+---+--------------------------------------------------+

Notes:

1. The ‘VSN=..’ command is normally issued only when the module is first
   procured or assembled, or when the disk configuration is changed. The
   serial numbers of the resident disks are noted.
2. The ‘VSN?’ query compares the serial numbers of the original disks to
   the serial numbers of the currently-resident disks and reports only
   the first discrepancy. Issuing a ‘VSN=…’ command or a ‘reset=erase’
   command will update the disk-serial# list to the currently-resident
   disks.
3. A ‘protect=off’ command is required *immediately* preceding a ‘VSN=’
   command, even if protection is already off.
4. The format of the extended-VSN is
   “VSN/capacity(GB)/maxdatarate(Mbps)” – example ‘MPI-0153/960/1024’.
   The following rules are enforced by the *dimino*:

   1. VSN – Must be 8 characters in length and in format
      “ownerID-serial#” (for parallel-ATA modules) or “ownerID+serial#”
      (for serial-ATA modules, when they become available)
   2. ownerID – 2 to 6 upper-case alphabetic characters (A-Z). The
      ‘ownerID’ must be registered with Jon Romney at NRAO
      (jromney@nrao.edu) to prevent duplicates. Numeric characters are
      not allowed. Any lower-case characters will automatically be
      converted to upper case.
   3. serial# - numeric module serial number, with leading zeroes as
      necessary to make the VSN exactly 8 characters long. Alphabetic
      characters are not allowed in the serial#.

5. *dimino* and \ *jive5ab*\  will compute the capacity of the module in
   GB and the maximum data rate in Mbps (number of disks times 128 Mbps)
   and append these to the VSN to create the extended VSN. Module
   capacity in GB is calculated as capacity of the smallest disk,
   rounded down to nearest 10GB, and multiplied by the number of disks
   in the module.
6. [STRIKEOUT:The recorded disk serial #’s are updated each time a scan
   is recorded.] This is way too expensive (in disk access) so only Note
   2 applies.
7. [STRIKEOUT:7. A “VSN=..” command may not be issued to any module
   which has been initialized in non-bank mode.] \ *jive5ab* allows
   writing a VSN always, updating the extended VSN with the capacity and
   maximum record rate of what is currently active, bank or non-bank
   mode.
8. [STRIKEOUT:When a non-bank-mode pair of modules is mounted and the
   unit is operating in non-bank mode, a “VSN?” query will return the
   VSN of both modules as indicated in the return parameters.] Because
   of changes in Note 7, the vsn? query will always return the actual
   VSN of the active module, wether it is bank or non-bank-mode. Use the
   bank_set? query to find the original constituent VSNs of a non-bank-
   mode pair. See Section **11**.
9. [STRIKEOUT:When only a single module of a non-bank-mode module-pair
   is mounted, a “VSN?” query will return the both the VSN of the
   mounted module plus the VSN and bank position of its unmounted
   companion; however, no reading or writing of data will be allowed in
   this situation.] Because of changes in how non-bank-mode is
   implemented this behaviour is also not true. See Section **11** for
   all details of what (can) happen(s) if only a single module is
   inserted and any data access is attempted. The VSN? and bank_set?
   queries always work.


.. [5]
   \ \ http://www.haystack.mit.edu/tech/vlbi/mark5/mark5_memos/100.pdf\ \ 

.. [6]
   In hindsight a horrible choice. However, in practice the actual
   runtime name(s) should hardly ever be exposed to human users

.. [7]
   an alternative would be to send each command separately, prefixing
   each command with “runtime=xferToBonn; “

.. [8]
   d’oh!

.. [9]
   e.g. file2net on machine A and net2disk on machine B

.. [10]
   https://github.com/demorest/mark5access (although this seems an old
   version)

.. [11]
   http://cira.ivec.org/dokuwiki/doku.php/difx/documentation

.. [12]
   again not a real splitter, unless the definition of ‘splitting’ is
   expanded to support 1:1 splitting (which, in *jive5ab*, it is)

.. [13]
   See Section 5 for the origin of the names of these routines

.. [14]
   https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_Form

.. [15]
   the default is the number of pieces this step breaks up the input
   frame in. E.g. “8bitx4” breaks a frame into four pieces and thus will
   accumulate four input frames before creating output

.. [16]
   you were warned: for this one really intimate knowledge of the data
   format is needed!

.. [17]
   if splet2net (read from network - corner turn - output to network) is
   used, the output network parameters have to be set differently. The
   standard network configuration will define the input setup of the
   corner turner.

.. [18]
   The vbs utilities as described (+download)
   http://www.jive.eu/~verkout/flexbuff/README.vbs

.. [19]
   Available from
   http://www.jive.eu/~verkout/flexbuff/flexbuf.recording.txt

.. [20]
   “Packet reordering metrics” - http://www.ietf.org/rfc/rfc4737.txt

.. [21]
   http://www.haystack.mit.edu/tech/vlbi/mark5/mark5_memos/100.pdf

.. [22]
   \ \ http://www.haystack.mit.edu/tech/vlbi/mark5/mark5_memos/100.pdf\ \ 
