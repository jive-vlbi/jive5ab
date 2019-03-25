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
import re

# parse jive5ab version number string into a number
# such that we can easily compare
def parse_version(txt):
    # jive5ab versions are X.Y[.Z[(.-)gunk]]
    # find all the sequences that are made solely out of digits -
    # the actual parts of the version number.
    # Then convert to int.
    # Then start multiplying by 10000 for the first version digit and
    # by x/100 for each next digit and add them up.
    return reduce(lambda (vsn, factor), x: (vsn + x*factor, factor/100.0),
                  map(int, re.findall(r"[0-9]+", txt)),
                  (0.0, 10000))[0]

def split_reply(reply):
    end_index = reply.rfind(';')
    if end_index != -1:
        reply = reply[:end_index]
    separator_index = reply.find('=')
    if separator_index == -1:
        separator_index = reply.find('?')
        if separator_index == -1:
            return [reply]

    return map(str.strip, [reply[0:separator_index]] + reply[separator_index+1:].split(': '))

class Mark5(object):
    def __init__(self, address, port, timeout):
        self.connect_point = (address, port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(self.connect_point)
        except:
            raise RuntimeError, "Failed to connect to {0}".format(self.connect_point)
    
        self.type = self.check_type()
        assert (self.type in ["Mark5A", "mark5A", "mark5b", "Mark5C", "StreamStor"])

    def check_type(self):
        return self.send_query("dts_id?")[2]

    def send_query(self, query, acceptable=["0", "1"]):
        self.socket.send(query + "\n\r")
        now = time.time()
        time_struct = time.gmtime(now)
        orgreply = reply = self.socket.recv(1024)
        now = time.time()
        time_struct = time.gmtime(now)
        reply = split_reply(reply)
        if not reply[1] in acceptable:
            raise RuntimeError, "Unacceptable reply '{0}' for command '{1}'".format(orgreply, query)
        return reply


if __name__ == "__main__":
    print "DirList %s\n        (c) H. Verkouter/B. Eldering" % "$Id$"

    parser = argparse.ArgumentParser(description = "Retrieve DirList from disk module.")
    
    parser.add_argument("-a", "--address", default = "localhost", help = "Mark5 IP or host name")
    parser.add_argument("-p", "--port", default = 2620, type = int, help = "port to send queries to")
    parser.add_argument("-b", "--bank", default = '', type = str, help = "request DirList of specific bank")
    parser.add_argument("-g", "--gigabyte", action="store_true", help = "show values in units of GB (10^9 bytes)")
    parser.add_argument("-s", "--showtime", action="store_true", help = "query and report start time and duration for each scan (using 'scan_check?', so this will increase execution time considerably)")
    parser.add_argument("-t", "--timeout", default = 5, type = int, help = "maximum time DirList will wait on reply") 

    args = parser.parse_args()

    mk5 = Mark5(args.address, args.port, args.timeout)

    # retrieve the jive5ab version we're talking to, if we are
    version_s = mk5.send_query("version?", ["0", "7"])
    version   = 0
    if int(version_s[1])==0:
        # ok, remote side recognized the version command
        if 'jive5a' in version_s[2]:
            # wonderful! we're talking to a version of jive5ab!
            # extract the actual version number
            version = parse_version( version_s[3] )
    # starting from 2.6.0 jive5ab supports command/reply silencing
    # per connection
    supports_echo = (version >= parse_version("2.6.0"))

    # In case a specific bank is requested, we must
    # store the current active bank, if any
    prevBank = None
    # HV 08/Nov/2016 The Mk5 could also be in non-bank mode
    #                Old jive5ab (pre 2.8 (official release)) would return
    #                    !bank_info? 6 : not in bank mode ;"
    #                    0           1   2
    #                2.8+ will return
    #                    !bank_info? 0 : nb ;"
    #                    0           1   2
    #                if in non-bank mode
    #                Success reply is:
    #                    !bank_info? 0 : <active bank> : <vsn>|- : <inactive bank> : <vsn>|- ;
    #                    0           1   2
    bankinfo = map(str.upper, mk5.send_query("bank_info?", ["0","6"]))
    actbank  = None if bankinfo[1]=="6" or bankinfo[2]=="NB" else bankinfo[2]
    if args.bank:
        args.bank = args.bank.upper()
        # if actbank == 'nb' we're not in bank mode and *thus* we
        # cannot honour switching to a particular bank!
        if actbank is None:
            raise RuntimeError, "Target system is not in bank mode (requested bank={0})".format( args.bank )

        # only need to switch bank if current bank != requested
        if actbank!=args.bank:
            # let's attempt to switch bank. Therefore the only acceptable
            # return value for bank_set=<args.bank> is "1". "0" will be 
            # returned if the requested bank is not active and no bank switch
            # actually can happen ....
            reply = mk5.send_query("bank_set=%s" % args.bank, ["1"])
            # ok the bank switch was initiated, wait for completion
            # we should be getting error 6 ("busy") whilst the switch
            # is in progress so only "0" and "6" are acceptable at this point
            while True:
                time.sleep(1)
                reply = mk5.send_query("bank_set?", ["0", "6"])
                if reply[1]=="0":
                    break
            # verify it's a different bank than we started with
            if actbank==reply[2]:
                reply = mk5.send_query("error?")
                raise RuntimeError, "Could not switch to bank %s [%s]" % (args.bank,reply[3])

            # only need to save previous bank if an active bank is set
            if actbank != "-":
                prevBank = actbank

    # Allright, inquire the bank
    vsn      = mk5.send_query("bank_set?")[3] if actbank is not None else mk5.send_query("vsn?")[2]
    # jive5ab pre 2.8 doesn't like dir_info? on non-bank mode ("!dir_info? 6 : not in bank mode ;")
    # so first attempt dir_info? and fall back trying to use the undocumented "scandir?"/"scandir=" query/command
    dirinfo  = mk5.send_query("dir_info?", ["0", "6"])
    if dirinfo[1]=="6":
        # ok, didn't like it, try scandir?
        #   !scandir? 0 : <nscan> : <scan label> : <start byte> : <length> ;
        #   0         1   2         3              4              5
        dirinfo = mk5.send_query("scandir?")
        nscan   = int( dirinfo[2] )
        recptr  = -1 
    else:
        nscan    = int( dirinfo[2] )
        recptr   = int( dirinfo[3] )

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
    if not args.showtime:
        print "   n' scan name                                   start byte       %8s" % e_value
        print " ---- ---------                                -------------  -------------"
    else:
        fmt += " %23s %10s"
        print "   n' scan name                                   start byte       %8s              start time   duration" % e_value
        print " ---- ---------                                -------------  ------------- ----------------------- ----------"

    # This generates a lot of (debug) output on the jive5ab console
    # shut it down if we can
    if supports_echo:
        mk5.send_query("echo=off", ["0"])

    for i in xrange(1,nscan+1):
        # set current scan
        mk5.send_query("scan_set=%d" % i)
        scan = mk5.send_query("scan_set?")
        # assert sanity!
        assert(int(scan[2]) == i)
        (start, end) = strt_end(scan[4], scan[5])
        if not args.showtime:
            print  fmt % (i, scan[3], start, end)
        else:
            try:
                check = mk5.send_query("scan_check?", ["0"])
                # sanity check
                assert((scan[2] == check[2]) and (scan[3] == check[3]))
                # if mode is st : [mark4|vlba], the replies we are looking
                # for is found at an increased index
                index_inc = 1 if (scan[4] == "st") else 0
                start_time = check[6 + index_inc]
                duration = check[7 + index_inc]
                print fmt % (i, scan[3], start, end, start_time, duration)
            except Exception as e:
                print fmt % (i, scan[3], start, end, "?", "?")


    # switch echoing back on for this connection
    if supports_echo:
        mk5.send_query("echo=on", ["0"])

    # if there was a previous active bank, put it back
    if prevBank:
        mk5.send_query("bank_set=%s" % prevBank)

