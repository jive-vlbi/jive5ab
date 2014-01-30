#!/usr/bin/env python

from SSErase import generate_parser, Mark5, get_banks_to_erase, erase, erase_test

import time
import MySQLdb

def reconstruct_query(split, separator = '?'):
    return "!{command} {sep} {reply} ;".format(command = split[0], 
                                               sep = separator,
                                               reply = " : ".join(split[1:]))

def write_results_to_database(mk5, args, erase_results):
    now = time.time()
    vsn = mk5.send_query("bank_set?")[3]
    pack_size = int(mk5.send_query("dir_info?")[4])
    dts_id = reconstruct_query(mk5.send_query("dts_id?"))
    ss_rev = reconstruct_query(mk5.send_query("ss_rev?"))
    os_rev = reconstruct_query(mk5.send_query("os_rev?"))
    
    connection = MySQLdb.connect (host = "ccs",
                                  user = "jops",
                                  passwd = "seioryjtseru",
                                  db = "disk_statistics",
                                  connect_timeout = 5)
    cursor = connection.cursor()

    query = "INSERT INTO environment (extended_VSN, mark5_ID, {bin_names}, data_rate, source, DTS_ID, SS_revision, OS_revision) VALUES ('{vsn}', '{mark5_id}', {bin_values}, {data_rate}, 'condition', '{dts_id}', '{ss_rev}', '{os_rev}');".format(
        bin_names = ", ".join(["bin%d" % bin_number for bin_number in xrange(len(erase_results.stat_thresholds))]),
        vsn = vsn,
        mark5_id = args.address,
        bin_values = ", ".join(["%.6f" % bin_value for bin_value in erase_results.stat_thresholds]),
        data_rate = pack_size / erase_results.duration,
        dts_id = dts_id,
        ss_rev = ss_rev,
        os_rev = os_rev)
    cursor.execute(query)
    environment_id = cursor.lastrowid
    for ((slot, disk_serial), statistics) in erase_results.disk_stats.items():
        query = "INSERT INTO disk (environment_ID, disk_serial, slot) VALUES ({environment_id}, '{disk_serial}', {slot});".format(
            environment_id = environment_id,
            disk_serial = disk_serial,
            slot = slot)
        cursor.execute(query)
        disk_id = cursor.lastrowid
        query = "INSERT INTO statistics (disk_ID, {stat_names}, replaced_blocks, query_time, disk_byte_position_start, disk_byte_position_end) VALUES ({disk_id}, {stat_values}, FROM_UNIXTIME({now}), 0, {pack_size});".format(
            stat_names = ", ".join(["stats%d" % stat_number for stat_number in xrange(len(statistics)-1)]), # statistics includes replaced_blocks
            disk_id = disk_id,
            stat_values = ", ".join(["%d" % stat_value for stat_value in statistics]),
            now = now,
            pack_size = pack_size)
        cursor.execute(query)
    connection.commit()

if __name__ == "__main__":
    parser = generate_parser()
    args = parser.parse_args()

    if args.test:
        print "============== WARNING in test mode ==============="
        erase = erase_test

    mk5 = Mark5(args.address, args.port)
    
    banks = get_banks_to_erase(mk5, args)
    if len(banks) == 0:
        print "Nothing to erase"
        sys.exit()
    for bank in banks:
        erase_results = erase(mk5, args, bank)

        for ((drive, serial), stats) in sorted(erase_results.disk_stats.items()):
            print "%d, %s: %s" % (drive, serial, " : ".join(map(str,stats)))
        if args.condition:
            pack_size = int(mk5.send_query("dir_info?")[4])
            print "Conditioning %.1f GB in Bank %s took %d secs ie. %.1f mins" % (pack_size/1000000000, bank, erase_results.duration, (erase_results.duration)/60)
            to_mbps = lambda x: x * 8 / 1000**2
            print "Minimum data rate %.0f Mbps, maximum data rate %.0f Mbps" % (to_mbps(erase_results.min_data_rate), to_mbps(erase_results.max_data_rate))
            write_results_to_database(mk5, args, erase_results)
