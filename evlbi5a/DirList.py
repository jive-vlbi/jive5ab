#!/usr/bin/env python
# Requests DirList from localhost or remote Mark5 

# see http://pypi.python.org/pypi/argparse for installation instructions of argparse (for python 2.6 or lower, 2.7 and up have it by default)
import argparse
import socket
import time
import sys
import math
import copy
import itertools

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
        assert (self.type in ["mark5A", "mark5b"])

    def check_type(self):
        return self.send_query("dts_id?")[2]

    def send_query(self, query):
        self.socket.send(query + "\n\r")
        now = time.time()
        time_struct = time.gmtime(now)
        reply = self.socket.recv(1024)
        now = time.time()
        time_struct = time.gmtime(now)
        reply = split_reply(reply)
        assert reply[1] in ["0", "1"] # all command send in this program require succesful completion
        return reply


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description = "Retrieve DirList from disk module.")
    
    parser.add_argument("-a", "--address", default = "localhost", help = "Mark5 IP or host name")
    parser.add_argument("-p", "--port", default = 2620, type = int, help = "port to send queries to")
    parser.add_argument("-b", "--bank", default = '', type = str, help = "request DirList of specific bank")
    parser.add_argument("-g", "--gigabyte", action="store_true", help = "show values in units of GB (10^9 bytes)")
    
    args = parser.parse_args()

    mk5 = Mark5(args.address, args.port)

    print "DirList %s (c) H. Verkouter/B. Eldering" % "$Id$"

    # In case a specific bank is requested, we must
    # store the current active bank, if any
    prevBank = None
    if args.bank:
        actbank = mk5.send_query("bank_info?")[2]
        # only need to switch bank if current bank != requested
        if actbank!=args.bank:
            # only need to save previous bank if an active bank is set
            if actbank != "-":
                prevBank = actbank
            reply = mk5.send_query("bank_set=%s" % args.bank)
            # this was a bank switch so we must give it some time
            time.sleep(2)

    # Allright, inquire the bank
    vsn      = mk5.send_query("bank_set?")[3]
    dirinfo  = mk5.send_query("dir_info?")
    nscan    = int(dirinfo[2])
    recptr   = int(dirinfo[3])

    # if user specified "-g" (for Gigabytes) we list the start + length, not start + end
    # as well as translate to 10^9 bytes
    e_value  = "end byte"
    fmt      = "%5d %-40s %13s  %13s"
    strt_end = lambda x1, x2: (x1, x2)
    to_gb    = lambda x: float(x)/1.0e9
    if args.gigabyte:
        e_value  = "length"
        fmt      = "%5d %-40s %13.7f  %13.7f"
        strt_end = lambda x1, x2: (to_gb(x1), to_gb(int(x2)-int(x1)))
    print "  nscans %d, recpnt %d, VSN <%s>" % (nscan, recptr, vsn)
    print "   n' scan name                                   start byte       %8s" % e_value
    print " ---- ---------                                -------------  -------------"
    for i in xrange(1,nscan+1):
        # set current scan
        mk5.send_query("scan_set=%d" % i)
        scan = mk5.send_query("scan_set?")
        # assert sanity!
        assert(int(scan[2]) == i)
        (start, end) = strt_end(scan[4], scan[5])
        print  fmt % (i, scan[3], start, end)

    # if there was a previous active bank, put it back
    if prevBank:
        mk5.send_query("bank_set=%s" % prevBank)

