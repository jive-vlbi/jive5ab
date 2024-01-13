.. _jive5ab-commandquery-summary-by-category-1:

*jive5ab* Command/Query Summary (by category)
=============================================

In the tables below, each command’s availability per system is listed.
“5A”, “5C” etc. mean the command is available on the indicated Mark5
system. “5I” = Mark5B / DIM, “5O” = Mark5B / DOM. The “G” category means
availability on a generic, non Mark5 platform, \ **including
Mark6/FlexBuff**\ . This includes *jive5ab* compiled for a Mark5 system
without Mark5 support, and *all non-default* runtimes (see Section 6) on
all systems.

.. _general-1:

General
-------

+---------+---+---+---+---+---+------------------------------------------------+
| Availa  | 5 | 5 | 5 | 5 | G |                                                |
| bility: | A | I | O | C |   |                                                |
+=========+===+===+===+===+===+================================================+
| dis     | Y | Y | Y | Y |   | Set/get Disk Module Status (DMS): last         |
| k_state |   |   |   |   |   | significant disk operation                     |
+---------+---+---+---+---+---+------------------------------------------------+
| d       | Y | Y | Y | Y |   | Set mask to enable changes in DMS              |
| isk_sta |   |   |   |   |   |                                                |
| te_mask |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| DTS_id? | Y | Y | Y | Y | Y | Get system information (query only)            |
+---------+---+---+---+---+---+------------------------------------------------+
| O       | Y |   |   |   |   | Get details of operating system (query only)   |
| S_rev1? |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| O       | Y |   |   |   |   | Get details of operating system (query only)   |
| S_rev2? |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| OS_rev? | Y | Y | Y | Y | Y | Get details of operating system (query only)   |
+---------+---+---+---+---+---+------------------------------------------------+
| mount   | Y | Y | Y | Y |   | Power bank on as if keyed on (command only,    |
|         |   |   |   |   |   | *jive5ab* > 2.8.1, see unmount)                |
+---------+---+---+---+---+---+------------------------------------------------+
| protect | Y | Y | Y | Y |   | Set/remove erase protection for active module  |
+---------+---+---+---+---+---+------------------------------------------------+
| recover | Y | Y | Y | Y |   | Recover record pointer which was reset         |
|         |   |   |   |   |   | abnormally during recording                    |
+---------+---+---+---+---+---+------------------------------------------------+
| reset   | Y | Y | Y | Y |   | Reset Mark5 unit (command only)                |
+---------+---+---+---+---+---+------------------------------------------------+
| runtime | Y | Y | Y | Y | Y | Control multiple simultaneous transfer         |
|         |   |   |   |   |   | environments                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| S       | Y |   |   |   |   | Get StreamStor firmware/software revision      |
| S_rev1? |   |   |   |   |   | levels (query only)                            |
+---------+---+---+---+---+---+------------------------------------------------+
| S       | Y |   |   |   |   | Get StreamStor firmware/software revision      |
| S_rev2? |   |   |   |   |   | levels (query only)                            |
+---------+---+---+---+---+---+------------------------------------------------+
| SS_rev? | Y | Y | Y | Y |   | Get StreamStor firmware/software revision      |
|         |   |   |   |   |   | levels (query only)                            |
+---------+---+---+---+---+---+------------------------------------------------+
| task_id | Y |   |   |   | Y | Set task ID (primarily for correlator use)     |
+---------+---+---+---+---+---+------------------------------------------------+
| unmount | Y | Y | Y | Y |   | Power bank off as if keyed off (command only,  |
|         |   |   |   |   |   | *jive5ab* > 2.8.1)                             |
+---------+---+---+---+---+---+------------------------------------------------+
| v       | Y | Y | Y | Y | Y | Get detailed version information of this       |
| ersion? |   |   |   |   |   | *jive5ab* (query only)                         |
+---------+---+---+---+---+---+------------------------------------------------+

.. _network-setup-and-monitoring-1:

Network Setup and monitoring
----------------------------

+----------+---+---+---+---+---+---------------------------------------------+
| Avail    | 5 | 5 | 5 | 5 | G |                                             |
| ability: | A | I | O | C |   |                                             |
+==========+===+===+===+===+===+=============================================+
| ack      | Y | Y | Y | Y | Y | Set UDP backtraffic acknowledge period      |
|          |   |   |   |   |   | (\ *jive5ab* > 2.7.3)                       |
+----------+---+---+---+---+---+---------------------------------------------+
| evlbi    | Y | Y | Y | Y | Y | Query e-VLBI UDP/UDT statistics (query      |
|          |   |   |   |   |   | only)                                       |
+----------+---+---+---+---+---+---------------------------------------------+
| ipd      | Y | Y | Y | Y | Y | Set packet spacing/inter-packet delay       |
+----------+---+---+---+---+---+---------------------------------------------+
| net_port | Y | Y | Y | Y | Y | Set IPv4 port number for the data channel   |
+----------+---+---+---+---+---+---------------------------------------------+
| net_     | Y | Y | Y | Y | Y | Set network data-transfer protocol          |
| protocol |   |   |   |   |   |                                             |
+----------+---+---+---+---+---+---------------------------------------------+
| mtu      | Y | Y | Y | Y | Y | Set network Maximum Transmission Unit       |
|          |   |   |   |   |   | (packet) size                               |
+----------+---+---+---+---+---+---------------------------------------------+

.. _data-checking-1:

Data Checking
-------------

+--------+---+---+---+---+---+-------------------------------------------------+
| A      | 5 | 5 | 5 | 5 | G |                                                 |
| vailab | A | I | O | C |   |                                                 |
| ility: |   |   |   |   |   |                                                 |
+========+===+===+===+===+===+=================================================+
| data_  | Y | Y | Y | Y |   | Check data starting at position of start-scan   |
| check? |   |   |   |   |   | pointer (query only)                            |
+--------+---+---+---+---+---+-------------------------------------------------+
| file_  | Y | Y | Y | Y | Y | Check data between start and end of file (query |
| check? |   |   |   |   |   | only)                                           |
+--------+---+---+---+---+---+-------------------------------------------------+
| scan_  | Y | Y | Y | Y | Y | Check data between start-scan and stop-scan     |
| check? |   |   |   |   |   | pointers (query only) (G >= 2.6.2)               |
+--------+---+---+---+---+---+-------------------------------------------------+
| sc     | Y | Y | Y | Y | Y | Set start- and stop-scan pointers for scan/data |
| an_set |   |   |   |   |   | check and disk2\* (G >= 2.7.0)                   |
+--------+---+---+---+---+---+-------------------------------------------------+
| track_ | Y |   |   |   |   | Check data on selected track (query only)       |
| check? |   |   |   |   |   |                                                 |
+--------+---+---+---+---+---+-------------------------------------------------+
| tra    | Y |   |   |   |   | Select tracks for monitoring with DQA or        |
| ck_set |   |   |   |   |   | ‘track_check’                                   |
+--------+---+---+---+---+---+-------------------------------------------------+

.. _system-setup-and-monitoring-1:

System Setup and Monitoring
---------------------------

+---------+---+---+---+---+---+------------------------------------------------+
| Availa  | 5 | 5 | 5 | 5 | G |                                                |
| bility: | A | I | O | C |   |                                                |
+=========+===+===+===+===+===+================================================+
| 1pps    |   | Y |   |   |   | Select source of 1pps synchronization tick     |
| _source |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| cl      |   | Y |   |   | Y | Specify frequency and source of the CLOCK      |
| ock_set |   |   |   |   |   | driving the DIM                                |
+---------+---+---+---+---+---+------------------------------------------------+
| disk_   | Y | Y | Y | Y |   | Return serial #s of all currently active disks |
| serial? |   |   |   |   |   | (query only)                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| dis     | Y | Y | Y | Y |   | Return individual size of all currently active |
| k_size? |   |   |   |   |   | disks (query only)                             |
+---------+---+---+---+---+---+------------------------------------------------+
| DOT?    |   | Y |   |   |   | Get DOT (Data Observe Time) clock information  |
|         |   |   |   |   |   | (query only)                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| DOT_inc |   | Y |   |   |   | Increment DOT clock                            |
+---------+---+---+---+---+---+------------------------------------------------+
| DOT_set |   | Y |   |   |   | Set DOT clock on next external 1pps tick       |
+---------+---+---+---+---+---+------------------------------------------------+
| error?  | Y | Y | Y | Y | Y | Get error number/message (query only)          |
+---------+---+---+---+---+---+------------------------------------------------+
| get     | Y | Y | Y | Y |   | Get disk performance statistics (query only)   |
| _stats? |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| gr      |   |   |   |   | Y | Manage aliases for groups of disks for use in  |
| oup_def |   |   |   |   |   | set_disks= (*jive5ab* >= 2.7.0)                 |
+---------+---+---+---+---+---+------------------------------------------------+
| layout? | Y | Y | Y | Y |   | Get current User Directory format (query only) |
+---------+---+---+---+---+---+------------------------------------------------+
| mode    | Y |   |   |   |   | Set data recording/readback mode/format        |
|         |   |   |   |   |   | (Mark5A)                                       |
+---------+---+---+---+---+---+------------------------------------------------+
| mode    |   | Y |   |   |   | Set data recording/readback mode/format        |
|         |   |   |   |   |   | (Mark5B/DIM)                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| mode    |   |   | Y | Y | Y | Set data recording/readback mode/format        |
|         |   |   |   |   |   | (Mark5B/DOM, Mark5C, generic)                  |
+---------+---+---+---+---+---+------------------------------------------------+
| packet  |   |   |   | Y |   | Set/get packet acceptance criteria             |
+---------+---+---+---+---+---+------------------------------------------------+
| pers    | Y | Y | Y | Y |   | Set/get personality (available on 5A, 5B since |
| onality |   |   |   |   |   | *jive5ab* >= 2.8)                               |
+---------+---+---+---+---+---+------------------------------------------------+
| pl      | Y |   |   |   | Y | Set playback data rate; set tvg rate           |
| ay_rate |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| po      |   | Y | Y | Y |   | Get current value of record, start- and        |
| inters? |   |   |   |   |   | stop-scan pointers (query only)                |
+---------+---+---+---+---+---+------------------------------------------------+
| po      | Y |   |   | Y |   | Get current value of record and play pointers  |
| sition? |   |   |   |   |   | (query only)                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| replace | Y | Y | Y |   |   | Get number of replaced blocks on playback      |
| d_blks? |   |   |   |   |   | (query only)                                   |
+---------+---+---+---+---+---+------------------------------------------------+
| reset   | Y | Y | Y | Y |   | Reset Mark5 unit (command only)                |
+---------+---+---+---+---+---+------------------------------------------------+
| rtime   | Y |   |   |   |   | Get remaining record time on current disk set  |
|         |   |   |   |   |   | (Mark5A)                                       |
+---------+---+---+---+---+---+------------------------------------------------+
| rtime   |   | Y |   | Y |   | Get remaining record time on current disk set  |
|         |   |   |   |   |   | (Mark5B/DIM, Mark5C)                           |
+---------+---+---+---+---+---+------------------------------------------------+
| rtime   |   |   |   |   | Y | Get remaining record time on current disk set  |
|         |   |   |   |   |   | (generic) (*jive5ab* >= 2.7.0)                  |
+---------+---+---+---+---+---+------------------------------------------------+
| se      |   |   |   |   | Y | Select mount points to record on               |
| t_disks |   |   |   |   |   | (FlexBuff/Mark6) (*jive5ab* >= 2.7.0)           |
+---------+---+---+---+---+---+------------------------------------------------+
| star    | Y | Y | Y | Y |   | Start gathering disk-performance statistics    |
| t_stats |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| status? | Y | Y | Y | Y | Y | Get system status (query only)                 |
+---------+---+---+---+---+---+------------------------------------------------+
| tr      | Y | Y | Y | Y | Y | Configure channel dropping setup               |
| ackmask |   |   |   |   |   |                                                |
+---------+---+---+---+---+---+------------------------------------------------+
| tstat?  | Y | Y | Y | Y | Y | Get current runtime status and performance     |
+---------+---+---+---+---+---+------------------------------------------------+
| vsn     | Y | Y | Y | Y |   | Write extended-VSN to permanent area           |
+---------+---+---+---+---+---+------------------------------------------------+

.. _data-transfer-record-play-1:

Data Transfer, Record, Play
---------------------------

Note: data transfers can be monitored using “tstat?”

+----------+---+---+---+---+---+------------------------------------------------+
| Avail    | 5 | 5 | 5 | 5 | G |                                                |
| ability: | A | I | O | C |   |                                                |
+==========+===+===+===+===+===+================================================+
| da       |   |   |   |   | Y | Manage storing of VDIF frames in separate      |
| tastream |   |   |   |   |   | recordings (*jive5ab* >= 3.0.0)                 |
+----------+---+---+---+---+---+------------------------------------------------+
| d        | Y | Y | Y | Y | Y | Transfer data between start- and stop-scan     |
| isk2file |   |   |   |   |   | pointers to file (G >= 2.7.0)                   |
+----------+---+---+---+---+---+------------------------------------------------+
| disk2net | Y | Y | Y | Y | Y | Transfer data between start- and stop-scan     |
|          |   |   |   |   |   | pointers to network (G >= 2.7.0)                |
+----------+---+---+---+---+---+------------------------------------------------+
| f        | Y | Y | Y | Y |   | Transfer data from file to Mark5 disk pack     |
| ile2disk |   |   |   |   |   |                                                |
+----------+---+---+---+---+---+------------------------------------------------+
| file2net | Y | Y | Y | Y | Y | Transfer data from file on disk to network     |
+----------+---+---+---+---+---+------------------------------------------------+
| fi       | Y | Y | Y | Y | Y | Transfer fill pattern from host to network or  |
| ll2net/f |   |   |   |   |   | file on disk                                   |
| ill2file |   |   |   |   |   |                                                |
+----------+---+---+---+---+---+------------------------------------------------+
| f        | Y | Y | Y | Y |   | Record fill pattern to Mark5 disk pack         |
| ill2disk |   |   |   |   |   | (*jive5ab* >= 2.8)                              |
+----------+---+---+---+---+---+------------------------------------------------+
| fill2vbs |   |   |   |   | Y | Record fill pattern to FlexBuff/Mark6 disks    |
|          |   |   |   |   |   | (*jive5ab* >= 2.8)                              |
+----------+---+---+---+---+---+------------------------------------------------+
| in2file  | Y | Y |   | Y |   | Transfer data directly from Mark5 input to     |
|          |   |   |   |   |   | file on disk                                   |
+----------+---+---+---+---+---+------------------------------------------------+
| in2fork  | Y | Y |   | Y |   | Duplicate data from Mark5 input to Mark5 disks |
|          |   |   |   |   |   | and network                                    |
+----------+---+---+---+---+---+------------------------------------------------+
| in2mem   | Y | Y |   | Y |   | Transfer data directly from Mark5 input to     |
|          |   |   |   |   |   | *jive5ab* internal buffer                      |
+----------+---+---+---+---+---+------------------------------------------------+
| in       | Y | Y |   | Y |   | Duplicate data from Mark5 input to Mark5 disks |
| 2memfork |   |   |   |   |   | and *jive5ab* internal buffer                  |
+----------+---+---+---+---+---+------------------------------------------------+
| in2net   | Y | Y |   | Y |   | Transfer data directly from Mark5 input to     |
|          |   |   |   |   |   | network                                        |
+----------+---+---+---+---+---+------------------------------------------------+
| mem2file | Y | Y | Y | Y | Y | Transfer data from *jive5ab* internal buffer   |
|          |   |   |   |   |   | to file on disk                                |
+----------+---+---+---+---+---+------------------------------------------------+
| mem2net  | Y | Y | Y | Y | Y | Transfer data from *jive5ab* internal buffer   |
|          |   |   |   |   |   | to network                                     |
+----------+---+---+---+---+---+------------------------------------------------+
| mem2time | Y | Y | Y | Y | Y | Decode data from *jive5ab* internal buffer     |
|          |   |   |   |   |   | into queryable time stamp                      |
+----------+---+---+---+---+---+------------------------------------------------+
| net2disk | Y | Y | Y | Y |   | Transfer data from network to Mark5 disk pack  |
+----------+---+---+---+---+---+------------------------------------------------+
| net2file | Y | Y | Y | Y | Y | Transfer data from network to file on disk     |
+----------+---+---+---+---+---+------------------------------------------------+
| net2mem  | Y | Y | Y | Y | Y | Transfer data from network to file on disk     |
+----------+---+---+---+---+---+------------------------------------------------+
| net2out  | Y |   |   |   |   | Transfer data from network to Mark5 output     |
+----------+---+---+---+---+---+------------------------------------------------+
| play     | Y |   |   |   |   | Play data from current play pointer position   |
+----------+---+---+---+---+---+------------------------------------------------+
| record   | Y | Y |   | Y |   | Turn recording on|off; set scan label          |
+----------+---+---+---+---+---+------------------------------------------------+
| record   |   |   |   |   | Y | Turn recording on|off; set scan                |
|          |   |   |   |   |   | label;configure Mark6/FlexBuff setup           |
+----------+---+---+---+---+---+------------------------------------------------+
| sp*2\*   | Y | Y | Y | Y | Y | Configure, start, stop corner turning: split   |
|          |   |   |   |   |   | [in/fill/net/file/disk/vbs] to [net/file]      |
+----------+---+---+---+---+---+------------------------------------------------+
