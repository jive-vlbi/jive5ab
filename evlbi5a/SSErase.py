#!/usr/bin/env python

# see http://pypi.python.org/pypi/argparse for installation instructions of argparse (for python 2.6 or lower, 2.7 and up have it by default)
import argparse
import socket
import time
import sys

to_gb = lambda x: float(x)/1.0e9

def split_reply(reply):
    end_index = reply.rfind(';')
    if end_index != -1:
        reply = reply[:end_index]
    separator_index = reply.find('=')
    if separator_index == -1:
        separator_index = reply.find('?')
        if separator_index == -1:
            return [reply]

    return map(lambda x: x.strip(), [reply[0:separator_index]] + reply[separator_index+1:].split(': '))

class Mark5(object):
    def __init__(self, address, port):
        self.connect_point = (address, port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.settimeout(5)
        self.socket.connect(self.connect_point)
    
        self.type = self.check_type()
        if self.type not in ["mark5A", "mark5b", "Mark5C"]:
            raise RuntimeError("Failed to recognize Mark5 type '%s'" % self.type)

    def check_type(self):
        return self.send_query("dts_id?")[2]

    def send_query(self, query, acceptable = ["0", "1"]):
        self.socket.send(query + "\n\r")
        now = time.time()
        time_struct = time.gmtime(now)
        reply = self.socket.recv(1024)
        now = time.time()
        time_struct = time.gmtime(now)
        split = split_reply(reply)
        if split[1] not in acceptable:
            raise RuntimeError("Query ('%s') execution failed, reply: '%s'" % (query, reply)) # all command send in this program require succesful completion
        return split

def generate_parser():
    parser = argparse.ArgumentParser(description = "Erase disk(s) mounted in the target machine. Apply conditioning while erasing if requested.")
    
    parser.add_argument("-a", "--address", default = "localhost", help = "Mark5 IP or host name")
    parser.add_argument("-p", "--port", default = 2620, type = int, help = "port to send queries to")
    parser.add_argument("-c", "--condition", default = 0, type = int, help = "apply conditioning to the disk (1: apply, default 0)")
    parser.add_argument("-d", "--debug", default = 0, type = int, help = "print progress of conditioning (1: print, default 0)")
    parser.add_argument("-t", "--debug_time", default = 60, type = int, help = "amount of time between progress updates")
    parser.add_argument("-g", "--gigabyte", action = "store_true", help = "show values DirList in units of GB (10^9 bytes)")
    parser.add_argument("--test", action = "store_true", help = argparse.SUPPRESS)
    
    return parser

def set_bank(mk5, args, bank):
    mk5.send_query("bank_set=%s" % bank)

    start = time.time()
    timeout = 5
    while (time.time() - start) < timeout:
        # bank_set will return 5 or 6 while switching banks (depends on version)
        bank_status = mk5.send_query("bank_set?", ["0", "1", "5", "6"])[1]
        if bank_status == "0":
            break
        time.sleep(0.1)
    if bank_status != "0":
        raise RuntimeError("Bank switching timed out after %ds" % timeout)

def print_dir_list(mk5, args, bank, vsn):
    set_bank(mk5, args, bank)
    dir_info = mk5.send_query("dir_info?")
    nscan = int(dir_info[2])
    recptr = int(dir_info[3])
    
    # copied frm DirList.py

    # if user specified "-g" (for Gigabytes) we list the start + length, not start + end
    # as well as translate to 10^9 bytes
    e_value  = "end byte"
    fmt      = "%5d %-40s %13s  %13s"
    strt_end = lambda x1, x2: (x1, x2)
    if args.gigabyte:
        e_value  = "length"
        fmt      = "%5d %-40s %13.7f  %13.7f"
        strt_end = lambda x1, x2: (to_gb(x1), to_gb(int(x2)-int(x1)))
    print
    print "  nscans %d, recpnt %d, VSN <%s>" % (nscan, recptr, vsn)
    print "   n' scan name                                   start byte       %8s" % e_value
    print " ---- ---------                                -------------  -------------"
    for i in xrange(1,nscan+1):
        # set current scan
        mk5.send_query("scan_set=%d" % i)
        scan = mk5.send_query("scan_set?")
        # assert sanity!
        if int(scan[2]) != i:
            raise RuntimeError("Failed to set scan %d" % i)
        (start, end) = strt_end(scan[4], scan[5])
        print  fmt % (i, scan[3], start, end)
    print " ---- ---------                                -------------  -------------"

def confirm_erase_bank(mk5, args, bank, vsn):
    print_dir_list(mk5, args, bank, vsn)
    sys.stdout.write("Are you sure that you want to erase %s in bank %s ? (Y or N)  " % (vsn, bank))
    continue_reply = sys.stdin.readline()
    return continue_reply[0] in ["Y", "y"]

def get_banks_to_erase(mk5, args):
    """
    Ask the user which banks to erase on mark5 mk5 (of type Mark5).
    Returns a list of banks.
    """
    banks_reply = mk5.send_query("bank_set?")

    if banks_reply[2] == "-": # active bank
        print "Nothing mounted"
        return []

    banks = []
    if confirm_erase_bank(mk5, args, banks_reply[2], banks_reply[3]):
        banks.append(banks_reply[2])
    if (banks_reply[4] != "-") and confirm_erase_bank(mk5, args, banks_reply[4], banks_reply[5]): # inactive bank
        banks.append(banks_reply[4])
    
    return banks

class Erase_Results(object):
    def __init__(self):
        self.duration = None
        self.messages = {}
        self.min_data_rate = None
        self.max_data_rate = None
        self.stat_thresholds = None

def erase(mk5, args, bank):
    """
    Perform an erase of the given bank (A or B) on mk5 (of type Mark5), 
    using arguments args (object with members the arguments given)
    Returns an object with members:
     -duration
     -messages ({(drive_number, serial) : [bin stats]})
     -min_data_rate
     -max_data_rate
     -stat_thresholds
    """

    set_bank(mk5, args, bank)
    
    results = Erase_Results()
    if args.condition:
        results.stat_thresholds = [ 0.001125 * 2**i for i in xrange(7) ]
        mk5.send_query("start_stats=%s" % " : ".join(map(lambda x: "%.6fs" % x, results.stat_thresholds)))

    print "Bank", bank
    dir_info = mk5.send_query("dir_info?")
    pack_size = int(dir_info[4])
    mk5.send_query("protect=off")
    then = time.time()
    if args.condition:
        mk5.send_query("reset=condition")
        try:
            if args.debug:
                prev_time = time.time()
                prev_byte = None
                pass_name = "Read"
            while True:
                transfer = mk5.send_query("tstat=")
                if transfer[3] == "no_transfer":
                    break

                if args.debug:
                    if mk5.type == "mark5A":
                        position = mk5.send_query("position?")
                    else:
                        position = mk5.send_query("pointers?")
                    byte = int(position[2]) * 4

                    now = time.time()

                    data_rate_text = ""
                    if prev_byte != None:
                        if byte > prev_byte:
                            pass_name = "Write"
                        else:
                            data_rate = (prev_byte - byte)/(now - prev_time)
                            if (results.min_data_rate == None) or (data_rate < results.min_data_rate):
                                results.min_data_rate = data_rate
                            if (results.max_data_rate == None) or (data_rate > results.max_data_rate):
                                results.max_data_rate = data_rate

                            data_rate_text = " at %.0fMbps" % (data_rate * 8 / 1000**2)
                    
                    if args.gigabyte:
                        bytes_text = "%13.7fGB" % to_gb(byte)
                    else:
                        bytes_text = "%d bytes" % byte

                    print "Bank %s %s cycle progress: %s to go (%d%%)%s" % (bank, pass_name, bytes_text, 100*byte/pack_size, data_rate_text)

                    prev_byte = byte
                    prev_time = now
                    time.sleep(args.debug_time)
        except:
            print "Exception during conditioning, trying to abort, exception:", sys.exc_info()[1]
            # try to stop the conditioning
            mk5.send_query("reset=abort")
            raise
    else:
        mk5.send_query("reset=erase")

    serials = mk5.send_query("disk_serial?")
    stats = mk5.send_query("get_stats?")
    start_drive = int(stats[2])
    while True:
        drive = int(stats[2])
        results.messages[(drive, serials[drive + 2])] = " : ".join(stats[3:11])
        stats = mk5.send_query("get_stats?")
        drive = int(stats[2])
        if int(stats[2]) == start_drive:
            break

    results.duration = time.time() - then
    return results

def erase_test(mk5, args, bank):
    """
    Just for debugging purposes
    """
    ret = Erase_Results()
    ret.duration = 2 * 60 * 60
    ret.messages = { (disk, "disk%d" % disk) : range(9) for disk in xrange(8) }
    ret.min_data_rate = 255e6
    ret.max_data_rate = 257e6
    ret.stat_thresholds = range(7)
    return ret
                   
if __name__ == "__main__":
    parser = generate_parser()
    args = parser.parse_args()

    if args.test:
        print "WARNING in test mode"
        erase = erase_test

    mk5 = Mark5(args.address, args.port)
    
    banks = get_banks_to_erase(mk5, args)
    if len(banks) == 0:
        print "Nothing to erase"
        sys.exit()
    for bank in banks:
        erase_results = erase(mk5, args, bank)

        for ((drive, serial), message) in sorted(erase_results.messages.items()):
            print "%d, %s: %s" % (drive, serial, message)
        if args.condition:
            pack_size = int(mk5.send_query("dir_info?")[4])
            print "Conditioning %.1f Gbytes in Bank %s took %d secs ie. %.1f mins" % (pack_size/1000000000, bank, erase_results.duration, (erase_results.duration)/60)
            to_mbps = lambda x: x * 8 / 1000**2
            print "Minimum data rate %.0fMbps, maximum data rate %.0fMbps" % (to_mbps(erase_results.min_data_rate), to_mbps(erase_results.max_data_rate))
