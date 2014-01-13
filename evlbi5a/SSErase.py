#!/usr/bin/python

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
        assert (self.type in ["mark5A", "mark5b"]), "Failed to recognize Mark5 type '%s'" % self.type

    def check_type(self):
        return self.send_query("dts_id?")[2]

    def send_query(self, query):
        self.socket.send(query + "\n\r")
        now = time.time()
        time_struct = time.gmtime(now)
        #print ""
        #print "%s%fs" % (time.strftime("%Hh%Mm", time_struct), (time_struct.tm_sec + now % 1)), "send to       %s:" % self.socket.getpeername()[0], query
        reply = self.socket.recv(1024)
        now = time.time()
        time_struct = time.gmtime(now)
        #print "%s%fs" % (time.strftime("%Hh%Mm", time_struct), (time_struct.tm_sec + now % 1)), "received from %s:" % self.socket.getpeername()[0], reply
        split = split_reply(reply)
        assert split[1] in ["0", "1"], "Query ('%s') execution failed, reply: '%s'" % (query, reply) # all command send in this program require succesful completion
        return split

if __name__ == "__main__":

    parser = argparse.ArgumentParser(description = "Erase disk(s) mounted in the target machine. Apply conditioning while erasing if requested.")
    

    parser.add_argument("-a", "--address", default = "localhost", help = "Mark5 IP or host name")
    parser.add_argument("-p", "--port", default = 2620, type = int, help = "port to send queries to")
    parser.add_argument("-c", "--condition", default = 0, type = int, help = "apply conditioning to the disk (1: apply, default 0)")
    parser.add_argument("-d", "--debug", default = 0, type = int, help = "print progress of conditioning (1: print, default 0)")
    
    args = parser.parse_args()

    mk5 = Mark5(args.address, args.port)

    banks_reply = mk5.send_query("bank_set?")

    if banks_reply[2] == "-": # active bank
        print "Nothing mounted"
        sys.exit(0)

    banks = [banks_reply[2]]
    if banks_reply[4] != "-": # inactive bank
        banks.append(banks_reply[4])

    if len(banks) == 1:
        print "Are you sure that you want to erase the disks in bank " + banks[0] + "? (Y or N)  ",
    else:
        print "Are you sure that you want to erase the disks in both banks? (Y or N)  ",

    continue_reply = sys.stdin.read(1)

    if continue_reply not in ["Y", "y"]:
        print "Not erasing"
        sys.exit(0)

    mk5.send_query("start_stats=0.001125s : 0.00225s : 0.0045s : 0.009s : 0.018s : 0.036s : 0.072s")

    for bank in banks:
        mk5.send_query("bank_set=%s" % bank)
        print "Bank", bank
        dir_info = mk5.send_query("dir_info?")
        pack_size = int(dir_info[4])
        mk5.send_query("protect=off")
        then = time.time()
        if args.condition:
            mk5.send_query("reset=condition")
            try:
                pass_name = "Write"
                last_byte = pack_size*1.1
                while True:
                    if mk5.type == "mark5A":
                        position = mk5.send_query("position?")
                    else:
                        position = mk5.send_query("pointers?")
                    byte = int(position[2]) * 4

                    transfer = mk5.send_query("tstat=")
                    if transfer[3] == "no_transfer":
                        break

                    if byte > last_byte:
                        pass_name = "Read"

                    if args.debug:
                        print "%s cycle progress: %d bytes to go (%d%%)" % (pass_name, byte, 100*byte/pack_size)
                        
                    last_byte = byte
                    time.sleep(3)
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
        messages = {}
        while True:
            drive = int(stats[2])
            messages[(drive, serials[drive + 2])] = " : ".join(stats[3:11])
            stats = mk5.send_query("get_stats?")
            drive = int(stats[2])
            if int(stats[2]) == start_drive:
                break

        for ((drive, serial), message) in sorted(messages.items()):
            print "%d, %s: %s" % (drive, serial, message)

    now = time.time()
    if args.condition:
        print "Conditioning %.1f Gbytes in Bank %s took %d secs ie. %.1f mins" % (pack_size/1000000000, bank, now-then, (now-then)/60)
