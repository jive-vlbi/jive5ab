.. _non-bank-mode-operation-1:

Non-bank mode operation
=======================

\ **(jive5ab >= 2.8, significant rewrite from orig doc.)**\  \ *jive5ab*
always had a very basic support for non-bank mode. *jive5ab* >= 2.8
introduces “working support for non-bank mode of the Mark5 system” for
some arbitrary value of “working”.

However, it is recommended, if possible, a FlexBuff or Mark6 recorder be
used for > 2048 Mbps recordings instead:

Tests conducted at JIVE and HartRAO have shown the firmware being rather
feeble in non-bank mode and may result in not-working, lock up, or even
crashing the operating system kernel, **specifically when de-activing
disk packs!**\ 

Handled with kid gloves some mileage may yet be gotten out of the system
- do read the section “\ **11.3 Rules for non-bank mode operation”**
below carefully. They differ from the original Mark5C documentation.

.. _introduction-1:

Introduction
------------

The normal operation of the Mark5 is in so-called ‘bank’ mode where only
one disk module is active at any given time; bank mode operation is
adequate for data rates up to 1024 Mbps, or 2048 Mbps with a SATA disk
pack.

The StreamStor firmware allows for running the system in so-called
“non-bank” mode: two disk packs operating as a single logical module,
doubling the sustained write speed. *jive5ab* enables usage of this
feature on all of the Mark5 platforms.

Given the Mark5B+’s single VSI port limit of 2048 Mbps, this “non-bank”
configuration is mostly useful for Mark5C systems where data can be
recorded from the 10 Gbps ethernet interface which does not have this
limitation.

.. _notes-on-firmware-limitation-from-an-application-perspective-1:

Notes on firmware limitation from an application perspective
------------------------------------------------------------

When disk packs are erased, *then and only then* the firmware records on
the disk pack(s) in which mode the disk pack(s) should be subsequently
used: bank or non-bank mode. This depends on in which mode the
StreamStor card is operating at the time of execution of the erase
command.

One of the biggest issues with bank versus non-bank mode operation is
that the firmware does not allow the application (*DIMino* or jive5ab)
to query in which mode a disk pack was erased,

i.e. the application cannot enquire how a disk pack *should* be used.

This leaves the application at the mercy of the firmware in combination
with the operator to rely on both ‘doing the right thing’ as it is
impossible to detect an attempt to use a disk pack in a mode it was not
erased in.

The only thing the application can do is to try an operation and hope
for the best. Unfortunately, sometimes ‘the best’ translates to any/all
of: lock-up, hanging or crashing the O/S.

.. _rules-for-non-bank-mode-operation-1:

Rules for non-bank mode operation:
----------------------------------

\ **The major key to reliable operation**\  in non-bank mode is to
\ **always insert both disk packs in their original bank position they
were in**\  at the time of creation of the non-bank mode pack. *jive5ab*
can not enforce this because it cannot read the stickered barcode
labels; all disk packs of a non-bank mode pack get the same (software)
VSN. A bank_set? query when *jive5ab* is operating in non-bank mode
returns the positions of the original VSNs at the time of creation.

The stickered barcode labels on the disk packs should be used for visual
identification.

In general: attempting any data access, read or write, when the green ready LED on a disk bay is blinking, when non-bank disk packs are inserted in the wrong order, or if a disk pack is used in a mode it was not erased in, may or may not work and may or may not confuse the firmware (necessitating SSReset) and may or may not crash your operating system kernel.
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

1. *jive5ab* must be running in non-bank mode, which can be achieved by:

   1. restarting jive5ab with the appropriate command line option (see
      Section 2)
   2. using the personality= : [non]bank; command to dynamically switch
      mode on any Mark5 system (requires *jive5ab* >= 2.8)

2. A non-bank module is created by inserting two disk packs, activating
   both, and sending protect=off; reset=erase; to *jive5ab* running in
   non-bank mode.

3. After the reset=erase; has completed, the user directory will have
   recorded in it the VSNs of the constituent disk packs the non-bank
   module was created of. Both disk packs now have the same VSN: the VSN
   of the module in bank A. This is a feature of the firmware combined
   with reset=erase; preserving the VSN of the module being erased.

4. We suggest assigning the newly created non-bank pack a different or
   special VSN, such that whenever the VSN of either of the modules is
   read one knows *a.)* this disk pack is part of a non-bank module and
   when bank_set? is queried: *b.)* what the constituent physical
   modules are and *c.)* in which order they need to be inserted.
   *jive5ab* **must** be operating in non-bank mode for bank_set? to
   return this information!

5. If only a single module of a non-bank module pair is ready, no
   operations involving recording or reading data are permitted, see the
   disclaimer above. #### When deactivating a non-bank mode module pair,
   be sure to deactivate bank A before deactivating bank B. If you
   deactivate bank B first, then, depending on how slowly you deactivate
   bank A, your StreamStor may still work, lock up completely or crash
   the operating system. Hint: don’t. Thus: deactivate A first, then B.

6. A single disk pack can be returned to normal bank-mode operation by
   issuing a reset=erase; on the activated and selected bank whilst
   *jive5ab* is operating in bank mode. The module’s original VSN will
   have to be restored manually.

7. Any attempts to use a bank-mode disk pack in non-bank mode or vice
   versa may or may not result in working, crashing or lock up. The
   firmware cannot inform *jive5ab* in which mode a disk pack was erased
   so there’s no way to prevent this.

