#!/usr/bin/python

import argparse
import socket
import types
import time
import sys
import calendar
import math
import copy
import itertools
import random

class TestError(RuntimeError):
    def __init__(self, msg):
        RuntimeError.__init__(self, msg)

def vex2time(text):
    x = copy.copy(text)
    # strptime doesn't do floating point seconds, so do that first
    assert(x[-1] == "s")
    x = x[:-1]
    index = x.find('.')
    partial = 0
    if index >= 0:
        partial = float(x[index:])
        x = x[:index]
    return calendar.timegm(time.strptime(x, "%Yy%jd%Hh%Mm%S")) + partial

def time2vex(t):
    s = time.gmtime(t)
    return time.strftime("%Yy%jd%Hh%Mm%S", s) + ("%0.4fs" % (t % 1))[1:] # append partial seconds

def julian_date(year, month, day, hour, minute, second):
    return 367*year - (7*(year+((month+9)/12))/4) + (275*month/9) + day + 1721013.5 - math.copysign(0.5, (100*year)+month-190002.5) + hour/24.0 + minute/(60.0*24.0) + second/(3600.0*24.0)

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

def build_check_function(check):
    check_type = type(check)
# unhandled types: 'BooleanType', 'BufferType', 'BuiltinMethodType', 'ClassType', 'CodeType', 'ComplexType', 'DictProxyType', 'DictType', 'DictionaryType', 'EllipsisType', 'FileType', 'FrameType', 'GeneratorType', 'GetSetDescriptorType', 'InstanceType', 'ListType', 'MemberDescriptorType', 'MethodType', 'ModuleType', 'NoneType', 'NotImplementedType', 'ObjectType', 'SliceType', 'StringTypes', 'TracebackType', 'TupleType', 'TypeType', 'UnboundMethodType', 'UnicodeType', 'XRangeType'
    if check_type in [types.StringType, types.FloatType, types.IntType, types.LongType]: # plain "==" check
        def inner(x):
            if check_type(x) != check:
                raise TestError("%s != %s" % (x, str(check)))
        return inner
    elif check_type in [types.BuiltinFunctionType, types.LambdaType, types.FunctionType]:
        return check
    else:
        raise RuntimeError("Unsupported type " + repr(check_type) )

def dont_care(x):
    return True

def in_range(start, end):
    def inner(x):
        if not(start <= float(x) <= end):
            raise TestError("%s not in range [%f, %f]" % (x, start, end))
    return inner

def at_least(threshold):
    def inner(x):
        if (float(x) < threshold):
            raise TestError("%s under threshold %f" % (x, threshold))
    return inner

def any_of(elements):
    def inner(x):
        if x not in map(str, elements):
            raise TestError("%s is not any of %s", repr(elements))
    return inner

def remove_units(unit, func):
    def inner(x):
        build_check_function(func)(x[:x.rfind(unit)])
    return inner

def around_time(center_time, max_error):
    def inner(x):
        t = vex2time(x)
        if not(center_time - max_error <= t <= center_time + max_error):
            time_struct = time.gmtime(center_time)
            raise TestError("%s not within %s%fs +- %f" % (x, time.strftime("%Yy%jd%Hh%Mm", time_struct), time_struct.tm_sec + (center_time % 1), max_error))
    return inner

def get_value(into):
    def inner(x):
        into.value = float(x)
    return inner

def get_string(into):
    def inner(x):
        into.text = x
    return inner

class Getter(object):
    pass

class Mark5(object):
    def __init__(self, address, port):
        self.address = (address, port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.settimeout(300)
        self.socket.connect(self.address)

        self.type = self.check_type()
        assert(self.type in ["mark5A", "mark5b"])
        self.runtimes = self.check_runtimes()
        assert(self.runtimes > 0)
    
    def check_type(self):
        return split_reply(self.send_query("dts_id?"))[2]

    def check_runtimes(self):
        return int(split_reply(self.send_query("runtime?"))[3])

    def send_query(self, query):
        self.socket.send(query + "\n\r")
        now = time.time()
        time_struct = time.gmtime(now)
        print ""
        print "%s%fs" % (time.strftime("%Hh%Mm", time_struct), (time_struct.tm_sec + now % 1)), "send to       %s:" % self.socket.getpeername()[0], query
        reply = self.socket.recv(1024)
        now = time.time()
        time_struct = time.gmtime(now)
        print "%s%fs" % (time.strftime("%Hh%Mm", time_struct), (time_struct.tm_sec + now % 1)), "received from %s:" % self.socket.getpeername()[0], reply
        return reply

    def verify(self, query, checks):
        split = split_reply(self.send_query(query))
        assert(len(split) == len(checks))
        for (r, c) in zip(split, checks):
            f = build_check_function(c)
            f(r)

    def verify_multi_expectations(self, query, expectations):
        split = split_reply(self.send_query(query))
        errors = []
        for checks in expectations:
            try:
                assert(len(split) == len(checks))
                for (r, c) in zip(split, checks):
                    f = build_check_function(c)
                    f(r)
                return # all tests succeeded
            except (AssertionError, TestError), e:
                errors.append(e)
        raise TestError("All possible expectations failed for query {query}, errors:\n {errors}".format(query = query, errors = "\n".join(map(str,errors))))

if __name__ == "__main__":

    parser = argparse.ArgumentParser(description = "Perform tests on a Mark5 unit, verifying that all response are within expected bounds.\n\nRequires two scratch empty (safety measure) disk packs mounted.\n\nSupported machines are Mark5A and Mark5B in dimino mode.\n\nFor Mark5A, this test assumes that data of Mark4 data format is being input at the VLBA connectors (64 tracks, 16MHz rate and current UTC timestamps). For the dimino this test requires a clock signal at AltA.\n\n For eVLBI, the destination machine to test eVLBI is expected to be a Mark5A and have an empty scratch disk mounted.")
    

    parser.add_argument("-a", "--address", required = True, help = "address of the machine to test")
    parser.add_argument("-p", "--port", default = 2620, type = int, help = "command port to send queries to")
    parser.add_argument("-e", "--evlbi", default = None, help = "address of machine to test eVLBI with")
    
    args = parser.parse_args()

    mk5 = Mark5(args.address, args.port)

    def execute(query, expectation, target = mk5):
        target.verify(query, expectation)

    def execute_multi_expectations(query, expectations, target = mk5):
        target.verify_multi_expectations(query, expectations)
    
    def start_record(scan_name):
        if mk5.type == "mark5b":
            # as we have a crappy 1pps, need to resync dot for every record
            execute("dot_set=:force", ["!dot_set", 1, dont_care])
            time.sleep(1) # give the dot_set time to wait for the 1pps
        execute("record=on:%s" % scan_name, ["!record", 0])

    execute("error?", ["!error", 0, dont_care, dont_care, dont_care]) # clear errors

    bank_set_time = 3 # 3s should be enough to switch
    execute("bank_set=B", ["!bank_set", any_of([0, 1])])
    time.sleep(bank_set_time) 
    execute("dir_info?", ["!dir_info", 0, 0, 0, at_least(100e9)]) # empty disk, size at least 100GB
    execute("protect=off", ["!protect", 0])
    execute("bank_set=A", ["!bank_set", 1])
    time.sleep(bank_set_time)
    execute("bank_set?", ["!bank_set", 0, "A", dont_care, "B", dont_care])
    time.sleep(bank_set_time)
    execute("dir_info?", ["!dir_info", 0, 0, 0, at_least(100e9)]) # empty disk, size at least 100GB
    execute("protect=off", ["!protect", 0])
    execute("status?", ["!status", 0, "0x02300001"])

    if mk5.type == "mark5A":
        execute("OS_rev1?", ["!os_rev1", 0, dont_care])
        execute("OS_rev2?", ["!os_rev2", 0, dont_care])
        execute("SS_rev1?", ["!ss_rev1", 0] + 11 * [dont_care])
        execute("SS_rev2?", ["!ss_rev2", 0] + 11 * [dont_care])
    else:
        execute("OS_rev?", ["!os_rev", 0, dont_care])
        execute("SS_rev?", ["!ss_rev", 0] + 19 * [dont_care])
        

    # prepare recording
    if mk5.type == "mark5A":
        execute("mode=mark4:64", ["!mode", 0])
        execute("mode?", ["!mode", 0, "mark4", 64, "mark4", 64, dont_care, dont_care])
        execute("play_rate=data:16", ["!play_rate", 0])
    else:
        execute("1pps_source=altA", ["!1pps_source", 0])
        execute("clock_set=32:int:32", ["!clock_set", 0])
        time.sleep(1) # give the clock_set time to wait for the 1pps
        execute("dot_set=:force", ["!dot_set", 1, dont_care])
        time.sleep(1) # give the dot_set time to wait for the 1pps
        execute("mode=ext:0xffffffff:1", ["!mode", 0])
    
    # start first recording
    execute("start_stats=0.1:0.2:0.3:0.4:0.5:0.6:0.7", ["!start_stats", 0])
    execute("start_stats?", ["!start_stats", 0, "0.1s", "0.2s", "0.3s", "0.4s", "0.5s", "0.6s", "0.7s"])
    scan_name1 = "test_ts_scan1"
    start_record(scan_name1)
    scan1_start_time = time.time()
    time.sleep(1)
    execute("record?", ["!record", 0, "on", 1, scan_name1])
    if mk5.type == "mark5b":
        time.sleep(1) # need at least 1s to have a guaranteed record start
        execute("DOT?", ["!dot", 0, around_time(time.time(), 1), dont_care, "FHG_on", dont_care, dont_care])
    execute("status?", ["!status", 0, "0x02300059"])
    time.sleep(2)
    scan1_end_time = time.time()
    execute("record=off", ["!record", 0])
    execute("record?", ["!record", 0, "off"])

    # check recording
    if type == "mark5A":
        recorded_bytes = map(lambda x: x *  (scan1_end_time - scan1_start_time) * 128e6, [0.95, 1.05])
    else:
        recorded_bytes = map(lambda x: x * 128e6, [int(scan1_end_time - scan1_start_time) - 1, int(scan1_end_time - scan1_start_time) + 1])
    execute("dir_info?", ["!dir_info", 0, 1, in_range(recorded_bytes[0], recorded_bytes[1]), dont_care])
    blocks_written = map(lambda x: x / (64*1024) / 8, recorded_bytes) # 64K blocks, 8 disks
    execute("get_stats?", ["!get_stats", 0, dont_care, in_range(blocks_written[0], blocks_written[1])] + 8 * [0])
    if mk5.type == "mark5A":
        execute("data_check?", ["!data_check", 0, "mark4", 64, around_time(scan1_start_time, 1), dont_care, "0.00125s", 160000, dont_care])
        execute("track_set=4 : 105", ["!track_set", 0])
        execute("track_check?", ["!track_check", 0, "mark4", 64, around_time(scan1_start_time, 1), in_range(0, 64 * 2500), "0.00125s", 16, 4, dont_care])
    else:
        time_struct = time.gmtime(scan1_start_time)
        julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
        execute("data_check?", ["!data_check", 0, "ext", around_time(int(scan1_start_time) + 1.5, 1), int(julian % 1000), 0, "7.8125e-05s", 1024, in_range(0, 10016), dont_care])

    # do another bit of recording, at half the data rate
    if mk5.type == "mark5A":
        execute("mode=mark4:32", ["!mode", 0])
        scan_name2 = "__scan2"
    else:
        execute("mode=ext:0xffffffff:2", ["!mode", 0])
        scan_name2 = "EXP_STN_scan2"
        
    start_record("scan2")
    scan2_start_time = time.time()
    time.sleep(5)
    scan2_end_time = time.time()
    execute("record=off", ["!record", 0])


    execute("disk_state?", ["!disk_state", 0, "A", "Recorded", "B", dont_care])
    duration = scan2_end_time - scan2_start_time
    if mk5.type == "mark5A":
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "mark4", 32, around_time(scan2_start_time, 1), remove_units("s", in_range(duration -1, duration + 1)), "16Mbps", in_range(-1e6, 1e6)])
    else:
        time_struct = time.gmtime(scan2_start_time)
        julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "-", int(julian % 1000), around_time(int(scan2_start_time) + 1.5, 1), remove_units("s", in_range(int(duration), int(duration) + 2)), "512Mbps", 0])

    # scan set options: scan name, inc, dec, next, absolute time, relative time, relative byte position
    execute("scan_set=%s" % scan_name2, ["!scan_set", 0])
    reported_start_time = Getter()
    reported_duration = Getter()
    if mk5.type == "mark5A":
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "mark4", 32, get_string(reported_start_time), remove_units("s", get_value(reported_duration)), "16Mbps", in_range(-1e6, 1e6)])
    else:
        time_struct = time.gmtime(scan2_start_time)
        julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "-", int(julian % 1000), get_string(reported_start_time), remove_units("s", get_value(reported_duration)), "512Mbps", 0])
    
    epsilon = 0.0013 # frame duration rounded up

    reported_start_time = vex2time(reported_start_time.text)
    reported_duration = reported_duration.value
    
    execute("scan_set=dec", ["!scan_set", 0])
    execute("scan_set=inc:+1s:-1s", ["!scan_set", 0])

    def scan_check():
        # expect to be at scan2, but now 1s later and 2s shorter
        if mk5.type == "mark5A":
            execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "mark4", 32, around_time(reported_start_time + 1, epsilon), remove_units("s", in_range(reported_duration - 2 - epsilon, reported_duration - 2 + epsilon)), "16Mbps", in_range(-1e6, 1e6)])
        else:
            time_struct = time.gmtime(scan2_start_time)
            julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
            execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "-", int(julian % 1000), around_time(reported_start_time + 1, epsilon), remove_units("s", in_range(reported_duration - 2 - epsilon, reported_duration - 2 + epsilon)), "512Mbps", 0])

    scan_check()

    # same as previous check, but now use bytes
    bytes_per_sec = 64000000 # 512Mbps => 64MBps
    if mk5.type == "mark5b":
        bytes_per_sec = bytes_per_sec * 626 / 625 # header overhead
    execute("scan_set=next:%d:-%d" %(bytes_per_sec, bytes_per_sec), ["!scan_set", 0])
    scan_check()
        
    execute("scan_set=2:%s:%s" % (time2vex(reported_start_time + 1), time2vex(reported_start_time + 2)), ["!scan_set", 0])
    if mk5.type == "mark5A":
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "mark4", 32, around_time(reported_start_time + 1, epsilon), remove_units("s", in_range(1 - epsilon, 1 + epsilon)), "16Mbps", in_range(-1e6, 1e6)])
    else:
        time_struct = time.gmtime(scan2_start_time)
        julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
        execute("scan_check?", ["!scan_check", 0, 2, scan_name2, "-", int(julian % 1000), around_time(reported_start_time + 1, epsilon), remove_units("s", in_range(1 - epsilon, 1 + epsilon)), "512Mbps", 0])

    
    # disk2file2disk
    execute("disk_state_mask=1:0:1", ["!disk_state_mask", 0, 1, 0, 1])
    execute("disk_state_mask?", ["!disk_state_mask", 0, 1, 0, 1])
    execute("scan_set=%s" % scan_name1, ["!scan_set", 0])
    filename = "/tmp/d1sk2f1l3.test"
    filesize = 1024e6 / 8 * 3 # 3s recording at 1Gbps
    execute("disk2file= %s : : +%d : w" % (filename, filesize), ["!disk2file", 1])
    execute("disk2file?", ["!disk2file", 0, "active", filename, 0, in_range(0, filesize), filesize, "w"])
    time.sleep(filesize * 8 / 128e6) # should be able to write at 128Mbps
    execute("disk2file?", ["!disk2file", 0, "inactive", filename])
    execute("bank_set=B", ["!bank_set", 1])
    time.sleep(bank_set_time)
    execute("file2disk= %s : 8 : 0 : %s" % (filename, scan_name1), ["!file2disk", 1])
    execute("file2disk?", ["!file2disk", 0, "active", filename, 8, in_range(8, filesize), 0, 1, scan_name1])
    time.sleep(filesize * 8 / 128e6) # should be able to read at 128Mbps
    execute("file2disk?", ["!file2disk", 0, "inactive"])
    execute("dir_info?", ["!dir_info", 0, 1, filesize - 8, dont_care]) 
    execute("scan_set=1", ["!scan_set", 0])
    if mk5.type == "mark5A":
        execute("scan_check?", ["!scan_check", 0, 1, scan_name1, "mark4", "64", around_time(scan1_start_time, 1), remove_units("s", in_range(2, 3)), "16Mbps", in_range(-1e5, 1e5)])
    else:
        time_struct = time.gmtime(scan1_start_time)
        julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
        execute("scan_check?", ["!scan_check", 0, 1, scan_name1, "-", int(julian % 1000), around_time(int(scan1_start_time) + 1.5, 1), remove_units("s", in_range(2, 4)), "1024Mbps", 0])

    execute("disk_state?", ["!disk_state", 0, "B", "Recorded", "A", "Recorded"])

    # test protect
    execute("protect=on", ["!protect", 0])
    execute("record=on:fail", ["!record", 4, dont_care])
    execute("vsn=fail", ["!vsn", 6, dont_care])
    execute("protect=off", ["!protect", 0])

    # test unerasing, only for mark5A for now, as amazon cards seem to have a bug
    if mk5.type == "mark5A":
        execute("reset=erase", ["!reset", 0])
        execute("recover=2", ["!recover", 0, 2])

    min_recovered_bytes = 0.9 * filesize # according to XLR documentation, some data might not be recovered, some threshold picked here, according to a few test runs, but nothing is guaranteed in recovery actually
        
    execute("dir_info?", ["!dir_info", 0, 1, in_range(min_recovered_bytes, filesize), dont_care]) 

    # simple tests for leftover commands, most for correlator use
    if mk5.type == "mark5A":
        execute("task_ID=42", ["!task_id", 0])
        execute("task_ID?", ["!task_id", 0, 42])
        execute("disk_state_mask=1:1:1", ["!disk_state_mask", 0, 1, 1, 1])
        execute("play=on:0", ["!play", 0])
        execute("play=off", ["!play", 0])
        execute("disk_state?", ["!disk_state", 0, "B", "Played", "A", "Recorded"])
        execute("scan_set=1", ["!scan_set", 0])
        execute("mode=mark4:64", ["!mode", 0])
        execute("mode?", ["!mode", 0, "mark4", 64, "mark4", 64, dont_care, dont_care])
        execute("play_rate=data:16", ["!play_rate", 0])
        execute("scan_play=on", ["!scan_play", 0])
        execute("skip=40", ["!skip", 0])
        execute("skip?", ["!skip", 0, 40])
        time.sleep(5)
        execute("scan_play?", ["!scan_play", 0, "halted"])
        execute("scan_play=off", ["!scan_play", 0])
        execute("bank_info?", ["!bank_info", 0, "B", at_least(90e9), "A", at_least(90e9)])
        execute("play=off:0", ["!play", 6, "inactive"]) 
        execute("position?", ["!position", 0, in_range(min_recovered_bytes, filesize), 0])
        execute("rtime?", ["!rtime", 0, remove_units("s", at_least(60*60)), remove_units("GB", at_least(90)), remove_units("%", at_least(90)), "mark4", 64, "16MHz", "1024Mbps"])
    else:
        execute("pointers?", ["!pointers", 0, in_range(min_recovered_bytes, filesize), 0, in_range(min_recovered_bytes, filesize)])
        execute("rtime?", ["!rtime", 0, remove_units("s", at_least(60*60)), remove_units("GB", at_least(90)), remove_units("%", at_least(90)), "ext", "0xffffffff", 2, remove_units("Mbps", in_range(512, math.ceil(512.0 * 10016 /10000)))])
        pass
        
    execute("bank_switch=on", ["!bank_switch", 0])
    execute("bank_switch=off", ["!bank_switch", 0])
    execute("disk_model?", ["!disk_model", 0] + 8 * [dont_care])
    execute("disk_serial?", ["!disk_serial", 0] + 8 * [dont_care])
    execute("disk_size?", ["!disk_size", 0] + 8 * [at_least(10e9)])

    execute("replaced_blks?", ["!replaced_blks", 0] + 9 * [0])
    
    # check eVLBI (if a remote mark5 is given)
    if args.evlbi:
        remote = Mark5(args.evlbi, args.port)

        execute("dir_info?", ["!dir_info", 0, 0, 0, at_least(100e9)], remote) # empty disk, size at least 100GB
        execute("protect=off", ["!protect", 0], remote)

        source_transfers = ["file2net", "disk2net"]
        if mk5.runtimes == 1:
            source_transfers.append("in2net")
        else:
            source_transfers.append("mem2net")
        
        destination_transfers = ["net2out", "net2file", "net2disk"]
        max_data_rate = dict(zip(source_transfers, [128e6, 512e6, 256e6]) +
                             zip(destination_transfers, [1024e6, 128e6, 512e6]))
        setup_procedures = {
            "in2net" : [lambda: execute("in2net=connect:%s" % remote.address[0], ["!in2net", 0]),
                        lambda: time.sleep(1),
                        lambda: execute("in2net=on", ["!in2net", 0])],
            "file2net" : [lambda: execute("file2net=connect:%s:%s" % (remote.address[0], filename), ["!file2net", 0]),
                          lambda: time.sleep(1),
                          lambda: execute("file2net=on", ["!file2net", 0])],
            "disk2net" : [lambda: execute("scan_set=1", ["!scan_set", 0]),
                          lambda: execute("disk2net=connect:%s" % remote.address[0], ["!disk2net", 0]),
                          lambda: time.sleep(1),
                          lambda: execute("disk2net=on", ["!disk2net", 0])],
            "mem2net" : [lambda: execute("in2mem=on", ["!in2mem", 0]),
                         lambda: execute("runtime=1", ["!runtime", 0, 1]),
                         lambda: execute("in2net=connect:%s" % remote.address[0], ["!in2net", 0]),
                         lambda: time.sleep(1),
                         lambda: execute("in2net=on", ["!in2net", 0])],
            "net2out" : [lambda: execute("net2out=open", ["!net2out", 0], remote)],
            "net2file" : [lambda: execute("net2file=open:/tmp/net2file,w", ["!net2file", 0], remote),
                          lambda: time.sleep(10)], # opening might take some time, if it is a big file (as we request it to be cleared)
            "net2disk" : [lambda: execute("net2disk=open:net2disk", ["!net2disk", 0], remote)]
            }
        # in case of tcp, the remote side might have closed down the transfer itself if it detected the shutdown of the socket
        stop_procedures = {
             "in2net" : [lambda: execute("in2net=disconnect", ["!in2net", 0])],
             "file2net" : [lambda: execute_multi_expectations("file2net=disconnect", [["!file2net", 1], ["!file2net", 6, dont_care]]),
                           lambda: time.sleep(5)],
             "disk2net" : [lambda: execute_multi_expectations("disk2net=disconnect", [["!disk2net", 1], ["!disk2net", 6, dont_care]]),
                           lambda: time.sleep(5)],
             "mem2net" : [lambda: execute("in2net=disconnect", ["!in2net", 0]),
                          lambda: execute("runtime=0", ["!runtime", 0, 0]),
                          lambda: execute("in2mem=off", ["!in2mem", 0])],
             "net2out" : [lambda: execute_multi_expectations("net2out=close", [["!net2out", 0], ["!net2out", 6, dont_care]], remote)],
             "net2file" : [lambda: execute_multi_expectations("net2file=close", [["!net2file", 0], ["!net2file", 6, dont_care]], remote)],
             "net2disk" : [lambda: execute_multi_expectations("net2disk=close", [["!net2disk", 0], ["!net2disk", 6, dont_care]], remote)]
            }
        check_procedures = {} # TO DO
            
        # for now, just setup one data rate transfer FIX: trackmask
        if mk5.type == "mark5A":
            execute("mode=mark4:16", ["!mode", 0])
            execute("play_rate=data:16", ["!play_rate", 0])
            execute("mode=mark4:16", ["!mode", 0], remote)
        else:
            execute("1pps_source=altA", ["!1pps_source", 0])
            execute("clock_set=32:int:32", ["!clock_set", 0])
            execute("dot_set=:force", ["!dot_set", 1, dont_care])
            time.sleep(1) # give the dot_set time to wait for the 1pps
            execute("mode=ext:0xffff0000:2", ["!mode", 0])
            execute("mode=mark5a+2:16", ["!mode", 0], remote)
        
        execute("play_rate=data:16", ["!play_rate", 0], remote)
        execute("mtu=9000", ["!mtu", 0], remote)
        for r in reversed(xrange(mk5.runtimes)):
            execute("runtime=%d" % r, ["!runtime", 0, r])
            execute("mtu=9000", ["!mtu", 0])
            execute("ipd=70", ["!ipd", 0])

        # execute all combos of transfers
        sdp = list(itertools.product(source_transfers, destination_transfers, ["udp", "tcp"]))
        random.shuffle(sdp)
        for source, destination, protocol in sdp:
            for target in [mk5, remote]:
                for r in reversed(xrange(target.runtimes)):
                    execute("runtime=%d" % r, ["!runtime", 0, r], target)
                    execute("net_protocol=%s:128k:128k" % protocol, ["!net_protocol", 0], target)

            for f in setup_procedures[destination] + setup_procedures[source]:
                f()
            # some processes seem to have a bit of a slow start (especially disk2net and net2disk), take that out of the equation
            time.sleep(1)
            bytes_before = Getter()
            now = time.time()
            execute("tstat=", ["!tstat", 0, in_range(now - 1, now + 1), destination, dont_care, get_value(bytes_before), dont_care, dont_care, "FIFOLength", dont_care], remote)
            
            before = time.time()
            time.sleep(2)
            after = time.time()
            least_expected_bytes = 0.9 * (after - before) / 8 * min(max_data_rate[source], max_data_rate[destination]) + bytes_before.value
            # the transfer might have stopped automatically
            execute_multi_expectations("tstat=", [["!tstat", 0, in_range(after - 1, after + 1), source, dont_care, at_least(least_expected_bytes), dont_care, at_least(least_expected_bytes), "FIFOLength", dont_care], ["!tstat", 0, dont_care, "no_transfer"]])
            if protocol == "udp":
                # if udp, we might loose some data, take that into account (especially at startup it seems)
                total = Getter()
                lost = Getter()
                execute("evlbi=%t:%l", ["!evlbi", 0, get_value(total), get_value(lost)], remote)
                assert( total.value > lost.value )
                least_expected_bytes *= lost.value / (lost.value + total.value)
            execute_multi_expectations("tstat=", [["!tstat", 0, in_range(after - 1, after + 1), destination, dont_care, at_least(least_expected_bytes), dont_care, at_least(least_expected_bytes), "FIFOLength", dont_care], ["!tstat", 0, dont_care, "no_transfer"]], remote)
            for f in stop_procedures[source] + stop_procedures[destination]:
                f()
            execute("tstat=", ["!tstat", 0, dont_care, "no_transfer"])
            execute("tstat=", ["!tstat", 0, dont_care, "no_transfer"], remote)

        # clean up remote disk
        execute("protect=off", ["!protect", 0], remote)
        execute("reset=erase", ["!reset", 0], remote)
        execute("dir_info?", ["!dir_info", 0, 0, 0, dont_care], remote)

        

    # check all recording modes
    if mk5.type == "mark5A":
        # make a list with argument to mode = <mode> : <submode> and play_rate=data:<rate>
        modes = list(itertools.product( ["mark4"], ["8", "16", "32", "64"], [16] )) + list(itertools.product(["st"], ["mark4"], [16])) + list(itertools.product(["tvg"], ["32"], [16])) # thought i could do a bit more, but cant set the data rate, as that is given by the data coming on the connectors
        random.shuffle(modes)
        for (scan_number, (mode, submode, rate)) in enumerate(modes):
            execute("mode=%s:%s" % (mode, submode), ["!mode", 0])
            execute("play_rate=%s:%d" % ("clock" if mode=="tvg" else "data", rate), ["!play_rate", 0])
            scan_name = "exp_st_scan-%s-%s-%d" % (mode, submode, rate)
            start_record(scan_name)
            start = time.time()
            time.sleep(10) # required to get a scan long enough to check for the lowest data rate
            end = time.time()
            execute("record=off", ["!record", 0])
            duration = end - start
            if mode == "tvg":
                execute("scan_check?", ["!scan_check", 0, scan_number + 2, scan_name, mode, 0, 1000000, 1000000])
            else:
                execute("scan_check?", ["!scan_check", 0, scan_number + 2, scan_name, mode, submode, around_time(start, 1), remove_units("s", in_range(duration -1, duration + 1)), "%dMbps" % rate, in_range(-1e6, 1e6)])
            
    else:
        for (scan_number, (mask_bits, decimation)) in enumerate(itertools.product([1, 2, 4, 8, 16, 32], [1, 2, 4, 8, 16])):
            scan_name = "exp_st_scan-%d-%d" % (mask_bits, decimation)
            execute("mode=ext:0x%x:%d" % ((2**(mask_bits) - 1), decimation), ["!mode", 0])
            start_record(scan_name)
            start = time.time()
            time.sleep(10) # required to get a scan long enough to check for the lowest data rate
            end = time.time()
            execute("record=off", ["!record", 0])
            duration = int(end - start)
            time_struct = time.gmtime(start)
            julian = julian_date(time_struct.tm_year, time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec)
            execute("scan_check?", ["!scan_check", 0, scan_number + 2, scan_name, "-", int(julian % 1000), around_time(int(start) + 1.5, 1), remove_units("s", in_range(duration, duration + 2)), "%dMbps" % (32 * mask_bits / decimation), 0])
                



    # finish by cleaning the disk
    execute("protect=off", ["!protect", 0])
    execute("reset=erase", ["!reset", 0])
    execute("dir_info?", ["!dir_info", 0, 0, 0, dont_care])
    execute("bank_set=A", ["!bank_set", 1])
    time.sleep(bank_set_time)
    execute("protect=off", ["!protect", 0])
    execute("reset=erase", ["!reset", 0])
    execute("dir_info?", ["!dir_info", 0, 0, 0, dont_care])
    execute("disk_state?", ["!disk_state", 0, "A", "Erased", "B", "Erased"])
