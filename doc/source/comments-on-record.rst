.. _comments-on-record--start-scan--and-stop-scan-pointer-1:

Comments on Record-, Start-scan- and Stop-scan Pointer
======================================================

Three different pointers are maintained by the Mark5 system and it is
important to understand what they are, what they mean, and how they are
managed. The *record pointer* is associated only with recording data to
the disks; the *start-scan* and *stop-scan pointer*\ s are used to
control reading data from the disks.

.. _record-pointer-1:

Record pointer
--------------

The Mark5 system records data to a disk set much as if it were a tape.
That is, recording starts from the beginning and gradually fills the
disk set as scans are recorded one after another. The ‘record pointer’
indicates the current recording position (in bytes, always a multiple of
8) which, at any instant, is just the current total number of recorded
bytes. Arbitrary recorded scans cannot be erased; however, individual
scans may be erased in order from last to first. The entire disk set is
erased by setting the record pointer back to zero using the
‘reset=erase’ command. Table 1 lists commands that modify the record
pointer; Table 2 lists commands that are affected by the record pointer.

+--------+-------------------------------------------------------------+
| **Com  | **Comment**                                                 |
| mand** |                                                             |
+========+=============================================================+
| reset  | Forces record pointer to zero.                              |
| =erase |                                                             |
+--------+-------------------------------------------------------------+
| res    | Sets record pointer to beginning of the disk space occupied |
| et=era | by the last scan (effectively erases the last scan).        |
| se_las |                                                             |
| t_scan |                                                             |
+--------+-------------------------------------------------------------+
| res    | Effectively erase disk pack, full write + read cycle over   |
| et=con | the whole disk pack                                         |
| dition |                                                             |
+--------+-------------------------------------------------------------+
| rec    | Starts writing at current value of record pointer; advances |
| ord=on | record pointer as data are recorded.                        |
+--------+-------------------------------------------------------------+
| f      | Data transfer from Linux disk to Mark5: Starts writing to   |
| ile2di | Mark5 disks at current value of record pointer; record      |
| sk/fil | pointer advances as data are written.                       |
| l2disk |                                                             |
+--------+-------------------------------------------------------------+
| ne     | Data transfer from network to Mark5: Starts writing to      |
| t2disk | Mark5 disks at current value of record pointer; record      |
|        | pointer advances as data are written.                       |
+--------+-------------------------------------------------------------+

Table 1: Commands that modify the record pointer

+------+---------------------------------------------------------------+
| **C  | **Comment**                                                   |
| omma |                                                               |
| nd** |                                                               |
+======+===============================================================+
| r    | Starts writing to Mark5 disks at record pointer; increments   |
| ecor | record pointer as recording progresses.                       |
| d=on |                                                               |
+------+---------------------------------------------------------------+
| f    | Starts transfer to Mark5 disks at record pointer; increments  |
| ile2 | record pointer as data transfer progresses.                   |
| disk |                                                               |
+------+---------------------------------------------------------------+
| net2 | Starts transfer to Mark5 disks at record pointer; increments  |
| disk | record pointer as data transfer progresses.                   |
+------+---------------------------------------------------------------+

Table 2: Commands affected by the record pointer

The current value of the record pointer can be queried with the
‘pointers?’ query.

.. _start-scan-and-stop-scan-pointers-1:

Start-scan and Stop-scan pointers
---------------------------------

The ‘start-*scan*’ and ‘stop-scan’ pointers specify the start and end
points for reading all or part of pre-recorded scan for data checking or
data-transfer purposes. By default, these pointers are normally set to
the beginning and end of a block of continuously recorded data, but for
special purposes may be set to include only a portion of the recorded
scan. Table 3 lists commands that modify these pointers; Table 4 lists
commands that are affected by these pointers.

+---------+------------------------------------------------------------+
| **Co    | **Comment**                                                |
| mmand** |                                                            |
+=========+============================================================+
| rese    | Resets start-scan and stop-scan pointers to zero.          |
| t=erase |                                                            |
+---------+------------------------------------------------------------+
| r       | Resets start-scan and stop-scan pointers to zero.          |
| eset=co |                                                            |
| ndition |                                                            |
+---------+------------------------------------------------------------+
| reset=e | Sets start-scan pointer to beginning of ‘new’ last scan;   |
| rase_la | sets stop-scan pointer to end of ‘new’ last scan.          |
| st_scan |                                                            |
+---------+------------------------------------------------------------+
| rec     | Sets start-scan pointer to beginning of scan just          |
| ord=off | recorded; sets stop-scan pointer to end of scan just       |
|         | recorded.                                                  |
+---------+------------------------------------------------------------+
| fi      | Sets start-scan pointer to beginning of scan just          |
| ll2disk | recorded; sets stop-scan pointer to end of scan just       |
|         | recorded.                                                  |
+---------+------------------------------------------------------------+
| f       | Sets start-scan pointer to beginning of scan just          |
| ill2vbs | recorded; sets stop-scan pointer to end of scan just       |
|         | recorded.                                                  |
+---------+------------------------------------------------------------+
| fi      | Sets start-scan pointer to beginning of scan just          |
| le2disk | transferred; sets stop-scan pointer to end of scan just    |
|         | transferred.                                               |
+---------+------------------------------------------------------------+
| n       | Sets start-scan pointer to beginning of scan just          |
| et2disk | transferred; sets stop-scan pointer to end of scan just    |
|         | transferred. Since *jive5ab* >= 2.8                        |
+---------+------------------------------------------------------------+
| s       | Sets start-scan and stop-scan pointers to a data range     |
| can_set | within a scan as specified.                                |
+---------+------------------------------------------------------------+

Table 3: Commands that modify the start-scan and stop-scan pointers

+------+---------------------------------------------------------------+
| **C  | **Comment**                                                   |
| omma |                                                               |
| nd** |                                                               |
+======+===============================================================+
| da   | Reads and checks a small amount of data beginning at          |
| ta_c | start-scan pointer                                            |
| heck |                                                               |
+------+---------------------------------------------------------------+
| sc   | Checks small amount just after start-scan pointer and before  |
| an_c | end pointer.                                                  |
| heck |                                                               |
+------+---------------------------------------------------------------+
| d    | Unless specific start/stop byte numbers are specified,        |
| isk2 | transfers data between start-scan and stop-scan pointers      |
| file |                                                               |
+------+---------------------------------------------------------------+
| disk | Unless specific start/stop byte numbers are specified,        |
| 2net | transfers data between start-scan and stop-scan pointers      |
+------+---------------------------------------------------------------+

Table 4: Commands affected by the start-scan and stop-scan pointers

A ‘scan_set?’ or ‘pointers?’ query returns information about the current
value of the start-scan and stop-scan pointers.
