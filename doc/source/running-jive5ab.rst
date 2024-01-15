.. _running-the-jive5ab-program:

Running the jive5ab program
===========================

The startup command-line for *jive5ab* is as follows:

.. code:: bash

   $ jive5ab [–hbned6*] [–m <level>] [-c <idx>] [-p <port>] [-f <fmt>]

Where


+-----+----------------------------------------------------------------+
| -h, | help on startup parameters; other options are ignored if –h is |
| –h  | present                                                        |
| elp |                                                                |
+=====+================================================================+
| -m, | message level. Range 0 to n (positive integer), default 0.     |
| –m  | Higher n produces more detailed output. Useful level for       |
| ess | operations is 0 – 3, a level greater than 3 is only useful for |
| age | developers                                                     |
| -le |                                                                |
| vel |                                                                |
| <n> |                                                                |
+-----+----------------------------------------------------------------+
| -c, | card index of Streamstor card to use. Default is ‘1’ which is  |
| –c  | appropriate for systems with only one Streamstor card          |
| ard | installed. It is possible to use ‘-1’ to have jive5ab skip     |
| <i  | driving the Streamstor card. This allows >1 jive5ab process to |
| dx> | run on a Mark5 system. Use in conjuction with ‘-p’ option      |
+-----+----------------------------------------------------------------+
| -p, | TCP port number on which to listen for incoming commands.      |
| –p  | Default is 2620                                                |
| ort |                                                                |
| <po |                                                                |
| rt> |                                                                |
+-----+----------------------------------------------------------------+
| -b, | do (b) or do not (n) copy recorded data to Mark5’s main memory |
| -n  | whilst recording (‘buffering’)                                 |
| —(n |                                                                |
| o-) |                                                                |
| buf |                                                                |
| fer |                                                                |
| ing |                                                                |
+-----+----------------------------------------------------------------+
| -e, | even if the message level would indicate it, do not echo       |
| –e  | command/reply statements on the screen                         |
| cho |                                                                |
+-----+----------------------------------------------------------------+
| -d, | start in dual bank mode                                        |
| –   |                                                                |
| dua |                                                                |
| l-b |                                                                |
| ank |                                                                |
+-----+----------------------------------------------------------------+
| -6, | find Mark6 disk mountpoints by default in stead of FlexBuff    |
| –ma | ones (jive5ab > 2.6.0)                                         |
| rk6 |                                                                |
+-----+----------------------------------------------------------------+
| -f, | set Mark6 (fmt = mk6) or FlexBuff (fmt = flexbuff) recording   |
| –   | mode as default. Default is flexbuff. (jive5ab > 2.6.0)        |
| for |                                                                |
| mat |                                                                |
| <f  |                                                                |
| mt> |                                                                |
+-----+----------------------------------------------------------------+
| -B, | Override minimum block size for vbs/mk6 recordings (jive5ab >= |
| –mi | 2.9.0)                                                         |
| n-b |                                                                |
| loc |                                                                |
| k-s |                                                                |
| ize |                                                                |
| <s> |                                                                |
+-----+----------------------------------------------------------------+
| -   | Do not drop privileges, continue running with root privileges  |
| \*, | if the program is being run suid root (jive5ab >= 3.0.0)       |
| –a  |                                                                |
| llo |                                                                |
| w-r |                                                                |
| oot |                                                                |
+-----+----------------------------------------------------------------+
| -S, | Start server for SFXC commands on port <port>                  |
| –   |                                                                |
| sfx |                                                                |
| c-p |                                                                |
| ort |                                                                |
| <po |                                                                |
| rt> |                                                                |
+-----+----------------------------------------------------------------+

