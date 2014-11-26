#!/usr/bin/env python

import SSErase

import time
import MySQLdb
import sys
import math

version = "$Id$"

def reconstruct_query(split, separator = '?'):
    return "!{command} {sep} {reply} ;".format(command = split[0], 
                                               sep = separator,
                                               reply = " : ".join(split[1:]))

def write_results_to_database(mk5, args, erase_results, intermediate_results, source, data_rate):
    now = time.time()
    vsn = mk5.send_query("bank_set?")[3]
    pack_size = int(mk5.send_query("dir_info?")[4])
    dts_id = reconstruct_query(mk5.send_query("dts_id?"))
    ss_rev = reconstruct_query(mk5.send_query("ss_rev?"))
    os_rev = reconstruct_query(mk5.send_query("os_rev?"))
    
    connection = MySQLdb.connect (host = "db0.jive.nl",
                                  read_default_file = "~/.my.cnf",
                                  db = "disk_statistics",
                                  connect_timeout = 5)
    cursor = connection.cursor()

    query = "INSERT INTO environment (extended_VSN, mark5_ID, {bin_names}, data_rate, source, DTS_ID, SS_revision, OS_revision) VALUES ('{vsn}', '{mark5_id}', {bin_values}, {data_rate}, '{source}', '{dts_id}', '{ss_rev}', '{os_rev}');".format(
        bin_names = ", ".join(["bin%d" % bin_number for bin_number in xrange(len(erase_results.stat_thresholds))]),
        vsn = vsn,
        mark5_id = args.address,
        bin_values = ", ".join(["%.6f" % bin_value for bin_value in erase_results.stat_thresholds]),
        data_rate = data_rate,
        source = source,
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
    if len(intermediate_results) > 0:
        query = "INSERT INTO condition_intermediate_data_rate (environment_ID, start_byte, end_byte, duration) VALUES ({values});".format(values = "),(".join(map(lambda e: ",".join([str(environment_id)] + map(str, e)), intermediate_results[1:]))) # the first element has unknown start byte
        cursor.execute(query)
    connection.commit()

def read_write(mk5, args, bank, pass_name, progress_callback = SSErase.progress_do_nothing):
    """
    Perform an read or write cycle of the given bank (A or B) on mk5 (of type Mark5), 
    using arguments args (object with members the arguments given)
    Returns an object with members:
     -duration
     -disk_stats ({(drive_number, serial) : [bin stats]})
     -min_data_rate
     -max_data_rate
     -stat_thresholds
    """

    assert pass_name in ["Write", "Read"]

    SSErase.set_bank(mk5, args, bank)

    print "Bank", bank, pass_name
    if pass_name == "Write":
        mk5.send_queries([("protect=off", ["0", "1", "4"]),"reset=erase"]) # the first protect=off might fail, if this disk pack is in a "bad" state
    
    dir_info = mk5.send_query("dir_info?")
    pack_size = int(dir_info[4])
    bytes_recorded = int(dir_info[3])
    
    then = time.time()

    results = SSErase.Erase_Results()
    results.stat_thresholds = [ 0.001125 * 2**i for i in xrange(7) ]
    mk5.send_query("start_stats=%s" % " : ".join(map(lambda x: "%.6fs" % x, results.stat_thresholds)))

    target_data_rate = 2 ** int(round(math.log(args.read_write,2)))
    blocksize = 2*1024*1024

    try:
        if pass_name == "Write":
            poll_runtime = "generate"
            mk5.send_query("net_protocol=tcp:2M:{block}".format(block = blocksize))
            mk5.send_query("mode=none")
            mk5.send_query("net2disk=open:write")
            tstat_index = 7
            mk5.send_query("runtime={r}".format(r = poll_runtime))
            mk5.send_query("net_protocol=tcp:2M:{block}".format(block = blocksize))
            tracks = min(64, max(8, target_data_rate / 16))
            mk5.send_query("mode=mark4:{tracks}".format(tracks = tracks))
            mk5.send_query("play_rate=data:{Mbps_per_track}".format(Mbps_per_track = target_data_rate / tracks))
            mk5.send_query("fill2net=connect:localhost:::1")
            mk5.send_query("fill2net=on:{words}".format(words = pack_size / 8))
        else:
            poll_runtime = "0"
            mk5.send_query("runtime=dump")
            mk5.send_query("net_protocol=udp:2M:{block}".format(block = blocksize))
            mk5.send_query("net2file=open:/dev/null,a")
            mk5.send_query("runtime=0")
            mk5.send_query("net_protocol=udp:2M:{block}".format(block = blocksize))
            mk5.send_query("mtu=9000")
            mk5.send_query("ipd={delay}".format(delay = 9000 * 8 / target_data_rate)) # 8 == bits/byte, target_data_rate is in Mbps and delay in us, so those factors 1e6 even out
            mk5.send_query("disk2net=connect:localhost")
            time.sleep(1)
            mk5.send_query("disk2net=on:0:{bytes}".format(bytes = bytes_recorded))
            tstat_index = 5

        if args.debug:
            prev_time = time.time()
            prev_byte = None
        while True:
            mk5.send_query("runtime={r}".format(r = poll_runtime))
            transfer = mk5.send_query("tstat=")
            if transfer[3] == "no_transfer":
                break

            now = time.time()
            if args.debug and (now - prev_time >= args.debug_time):
                if poll_runtime != "0":
                    mk5.send_query("runtime=0")
                    transfer = mk5.send_query("tstat=")
                byte = int(transfer[tstat_index])

                data_rate_text = ""
                if prev_byte != None:
                    data_rate = (byte - prev_byte)/(now - prev_time)
                    if (results.min_data_rate == None) or (data_rate < results.min_data_rate):
                        results.min_data_rate = data_rate
                    if (results.max_data_rate == None) or (data_rate > results.max_data_rate):
                        results.max_data_rate = data_rate

                    data_rate_text = " at %.0f Mbps" % (data_rate * 8 / 1000**2)

                if args.gigabyte:
                    bytes_text = "%13.7f GB" % SSErase.to_gb(byte)
                else:
                    bytes_text = "%d B" % byte

                print "Bank %s %s cycle progress: %s done (%d%%)%s" % (bank, pass_name, bytes_text, 100*byte/pack_size, data_rate_text)
                progress_callback(prev_byte, byte, (now - prev_time))

                prev_byte = byte
                prev_time = now
            time.sleep(min(args.debug_time - (now - prev_time), 5))
    except:
        print "Exception during {pass_name} pass, trying to abort, exception: {ex}".format(pass_name = pass_name, ex = str(sys.exc_info()[1]))
        print poll_runtime
        if pass_name == "Write":
            mk5.send_query("runtime={r}".format(r = poll_runtime))
            mk5.send_query("fill2net=disconnect")
        else:
            mk5.send_query("runtime=0")
            mk5.send_query("disk2net=disconnect")
        raise
    finally:
        if pass_name == "Write":
            mk5.send_query("runtime=0")        
            mk5.send_query("net2disk=close")
        else:
            mk5.send_query("runtime=dump")        
            mk5.send_query("net2file=close")
            mk5.send_query("runtime=0")

    results.duration = time.time() - then
    
    serials = mk5.send_query("disk_serial?")
    stats = mk5.send_query("get_stats?")
    start_drive = int(stats[2])
    while True:
        drive = int(stats[2])
        results.disk_stats[(drive, serials[drive + 2])] = map(int, stats[3:12])
        stats = mk5.send_query("get_stats?")
        if int(stats[2]) == start_drive:
            break

    return results


if __name__ == "__main__":
    parser = SSErase.generate_parser()
    parser.add_argument("-rw", "--read_write", default = 0, type = int, help = "data rate in Mbps to do the software read+write cycle, 0 (the default) means: don't do a software read+write cycle, will be rounded to the nearest power of 2")
    args = parser.parse_args()

    if args.version:
        print SSErase.version
        print version
        sys.exit(0);

    if args.test:
        if args.read_write > 0:
            raise RuntimeError("No read+write test available")
        print "============== WARNING in test mode ==============="
        erase = SSErase.erase_test
    else:
        erase = SSErase.erase

    mk5 = SSErase.Mark5(args.address, args.port, args.timeout)

    # try to set the xterm title
    print "\x1B]0;Conditioning %s\x07" % args.address
        
    banks = SSErase.get_banks_to_erase(mk5, args)
    if len(banks) == 0:
        print "Nothing to erase"
        sys.exit()
    for bank in banks:
        if args.read_write > 0:
            write_func = lambda mk5, args, bank, progress_callback: read_write(mk5, args, bank, "Write", progress_callback)
            read_func = lambda mk5, args, bank, progress_callback: read_write(mk5, args, bank, "Read", progress_callback)
            erase_funcs = [("write", write_func), ("read", read_func), ("condition", erase)]
        else:
            erase_funcs = [("condition", erase)]
        for source, erase_func in erase_funcs:
            intermediate_results = []
            progress_callback = lambda start, end, duration: intermediate_results.append((start, end, duration))
            erase_results = erase_func(mk5, args, bank, progress_callback)

            for ((drive, serial), stats) in sorted(erase_results.disk_stats.items()):
                print "%d, %s: %s" % (drive, serial, " : ".join(map(str,stats)))
            if args.condition:
                pack_size = int(mk5.send_query("dir_info?")[4])
                print "%.1f GB in Bank %s took %d secs ie. %.1f mins" % (pack_size/1000000000, bank, erase_results.duration, (erase_results.duration)/60)
                to_mbps = lambda x: x * 8 / 1000**2
                print "Minimum data rate %.0f Mbps, maximum data rate %.0f Mbps" % (to_mbps(erase_results.min_data_rate), to_mbps(erase_results.max_data_rate))

                if source == "condition":
                    data_rate = 8 * 2 * pack_size / erase_results.duration # 8: byte -> bits, 2: read + write cycle
                else:
                    data_rate = 8 * intermediate_results[-1][1] / erase_results.duration # 8: byte to bit
                write_results_to_database(mk5, args, erase_results, intermediate_results, source, data_rate)
