.. _multiple-independent-data-transfers-1:

Multiple independent Data Transfers
===================================

A feature of *jive5ab* is that can do multiple, independent, transfers
at the same time, provided none of the endpoints are a unique resource.
E.g. it is not possible to do in2net and disk2file at the same time
because the I/O board + StreamStor combination can only be used in one
direction at any instance of time. On the other hand multiple file2net
and/or net2file can be run, next to e.g. a record or disk2file.

If one or more of the simultaneous transfers should be real-time, this
can only be guaranteed provided sufficient resources (CPU, network,
memory) are available to sustain them.

.. _implementation-note-the-concept-of-runtimes-1:

Implementation note — the concept of ‘runtimes’
-----------------------------------------------

In order to support this, *jive5ab* has the notion of *runtimes*. Each
*runtime* can do one data transfer. Initially, when *jive5ab* starts up,
there is only one runtime present and all commands received by *jive5ab*
are processed by this one; the default one.

*runtimes* are created on the fly, as you request them. Each *runtime*
has a name and the first time a name is used, a new *runtime* with that
name will be created. Due to historical reasons the default runtime’s
name is an un-informative “0” (zero) [6]_.

In order to send command(s) to a specific *runtime*, prefix the
command(s) with the “runtime

= <name>;” command. All commands on that line will be sent to (and
processed by) the indicated *runtime*. When a transfer in a specific,
non-default, runtime is done and is not expected to be reused it is best
to delete it and its resources. Please see the runtime command.

To prevent concurrent access to the unique resource hardware **only the
default runtime has access to the Mark5 StreamStor hardware**. In other,
non-default runtimes, commands like “record=” or “fill2out=” cannot be
issued; an error code 7 (“no such keyword”) is returned if such an
attempt is made. Since *jive5ab* 2.6.0 record=on is valid in these
runtimes and triggers FlexBuff/Mark6 network packet recording
mode.Non-default runtimes on *all* supported systems have the same
“mode=” command; they expose the “Mark5B/DOM/Mark5C/ generic” version
rather than the one intended for the current hardware. In fact all
non-default runtimes only expose the “generic” command set, including
record=on since *jive5ab* 2.6.0.On generic, non-Mark5 systems, the
default runtime exposes the “generic” command set too.

.. _configuring-a-runtimes-1:

Configuring (a) runtime(s)
--------------------------

It is important to realise that each runtime has its own set of mode,
net_protocol and play_rate/ clock_set values. It is not sufficient to
rely on the FieldSystem to set the mode in the default runtime and
expect things to work in other runtimes.

If a data format dependent transfer is to be done by a specific runtime
it is necessary to configure the correct mode and play_rate or clock_set
in that runtime. An example would be to do time stamp printing: this
requires the data format and data rate to be specified for a successful
decode.

If, on the other hand, a network transfer is to be done, the
net_protocol should be set (in case the default is not acceptable).

Transfers like in2net and disk2net remember their last network
destination for a subsequent “=connect” without argument. *jive5ab*
extends this behaviour on a per runtime basis: the last used host/ip
that was used in a “connect” in a runtime is stored. If a runtime is
deleted, so is this information.

An example would be:

First time transferring a file to Bonn; note that all commands have to
be given on one (1) line [7]_ or else they will be sent to/processed by
the default runtime, not the ‘xferToBonn’ runtime:

::

   runtime=xferToBonn; net_protocol=udt; file2net=connect : XX.YY.ZZ.UU : /path/to/file; file2net=on

For subsequent sending files to Bonn, the following line would suffice:

::

   runtime=xferToBonn; file2net=connect : : /path/to/file2; file2net = on

.. _running-a-network-transfer-1:

Running a network transfer
--------------------------

In general all \*2net + net2\* transfer combinations follow the same
mechanism for setting up, running and tearing down.

1. Set network parameters (net_protocol, mtu, net_port, ipd) to the
   desired values on both the source and destination host.

2. Issue net2\* = open : <…> to the destination host to prepare that
   system for accepting incoming data and verify/configure the data
   sink.

3. Issue \*2net = connect : <…> to the source host to verify/configure
   the data source and open the data channel.

4. Now the data flow may be started by issuing a \*2net = on [ : ]. Some

   transfers accept options after the “on” (e.g. start byte/end byte
   numbers), refer to the specific command documentation for detailed
   information.

5. Some transfers allow pausing the transfer. Currently they are in2net,
   in2file, in2fork.

6. To stop the transfer properly, issue net2\* = close to the recipient
   of the data stream and \*2net = disconnect to the sender. The order
   is unimportant; *jive5ab* has no particularly strong feelings about
   this. In case it does you should inform the author forthwith.

7. To comply with Mark5A/DIMino standards, the transfers disk2file,
   disk2net and file2disk may also be terminated by issuing a “reset =
   abort;” command. Note that of those three only disk2net really
   resides under this section.
