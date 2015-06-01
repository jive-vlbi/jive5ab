// building of the command maps
// Copyright (C) 2007-2013 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
//
// HV: 28-Oct-2013 - All implementations have now been moved to
//                   mk5command/*.cc
// * generic Mk5 commands [Mk5 hardware agnostic]
// * generic jive5a commands [ipd, pdr, tstat, mtu, ...]
// * specializations for
//      - Mk5A
//      - Mk5B flavour agnostic but Mk5B specific
//      - Mk5B/DIM
//      - Mk5B/DOM
// * commandmaps which define which of the commands
//   are allowed for which Mk5 flavour.
//   Currently there's 3 commandmaps:
//      - Mk5A
//      - Mk5B/DIM
//      - Mk5B/DOM
// * Utility functions for Mk5's
//   (eg programming Mk5B/DIM input section for recording:
//    is shared between dim2net and in2disk)
#include <mk5command.h>
#include <mk5command/mk5functions.h>

using std::make_pair;

//
//    HERE we build the actual command-maps
//
const mk5commandmap_type& make_mk5a_commandmap( bool buffering ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev1", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev2", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev1", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev2", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("position", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("track_set", track_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("track_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("rtime", rtime_5a_fn)).second );
    


    // in2net + in2fork [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("in2net", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5a>)).second );
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("record", &in2net_fn<mark5a>)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2mem", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2memfork", &in2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );

    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2fork", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2out", fill2out_fn)).second );

    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("play_rate", playrate_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5a_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<mark5a>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<mark5a>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );
#if 0
    // Not official mk5 commands but handy sometimes anyway :)
    insres = mk5commands.insert( make_pair("dbg", debug_fn) );
    if( !insres.second )
        throw cmdexception("Failed to insert command dbg into commandmap");

#endif
#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif
    ASSERT_COND( mk5.insert(make_pair("clock", mk5a_clock_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("transfermode", transfermode_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("layout", layout_fn)).second );
    return mk5;
}

// Build the Mk5B DIM commandmap
const mk5commandmap_type& make_dim_commandmap( bool buffering ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("rtime", rtime_dim_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("tvr", tvr_fn)).second );

    // in2net + in2fork [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("in2net",  &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5b>)).second );
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("record", &in2net_fn<mark5b>)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2mem", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2memfork", &in2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );

    // sekrit functions ;) Mk5B/DIM is not supposed to be able to record to
    // disk/output ... but the h/w can do it all the same :)
    // net2out + net2disk [same function, different behaviour]
    ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2fork", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // disk2*
    ASSERT_COND( mk5.insert(make_pair("play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_play", disk2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );

    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("clock_set", clock_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("1pps_source", pps_source_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pps", pps_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot", dot_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_set", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dot_inc", dot_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdim_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("cascade", mk5bdim_cascade_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("skip", skip_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<mark5b>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<mark5b>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );

#if 0
    mk5commands.insert( make_pair("getlength", getlength_fn) );
    mk5commands.insert( make_pair("erase", erase_fn) );
#endif

    ASSERT_COND( mk5.insert(make_pair("layout", layout_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("transfermode", transfermode_fn)).second );

    return mk5;
}

const mk5commandmap_type& make_dom_commandmap( bool ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("led", led_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    // We must be able to sort of set the trackbitrate. Support both 
    // play_rate= and clock_set (since we do "mode= mark4|vlba" and
    // "mode=ext:<bitstreammask>")
    ASSERT_COND( mk5.insert(make_pair("play_rate", mk5c_playrate_clockset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("clock_set", mk5c_playrate_clockset_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("replaced_blks", replaced_blks_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2out", fill2out_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    // file2*
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );

    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<0>)).second );
    // Mk5B/DOM has an I/O board but can't read from it
    //ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<0>)).second );
    //ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<0>)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("layout", layout_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("transfermode", transfermode_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_5a_fn)).second );
    return mk5;
}

// The Mark5C command map
const mk5commandmap_type& make_mk5c_commandmap( bool buffering ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ss_rev", ssrev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scandir", scandir_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_info", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_set", bankinfoset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state", disk_state_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_state_mask", disk_state_mask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bank_switch", bank_switch_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dir_info", dir_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_model", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_serial", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk_size", disk_info_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("memstat", memstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("position", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("pointers", position_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("start_stats", start_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("get_stats", get_stats_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("vsn", vsn_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("data_check", data_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_dim_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("recover", recover_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("protect", protect_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("reset", reset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("rtime", rtime_5c_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("personality", personality_fn)).second );

    // We must be able to sort of set the trackbitrate. Support both 
    // play_rate= and clock_set (since we do "mode= mark4|vlba" and
    // "mode=ext:<bitstreammask>")
    ASSERT_COND( mk5.insert(make_pair("play_rate", mk5c_playrate_clockset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("clock_set", mk5c_playrate_clockset_fn)).second );


    // 5C specific
    ASSERT_COND( mk5.insert(make_pair("packet", mk5c_packet_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill_pattern", mk5c_fill_pattern_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // disk2*
    ASSERT_COND( mk5.insert(make_pair("disk2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file", disk2file_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );

    // net2*
    //ASSERT_COND( mk5.insert(make_pair("net2out", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2disk", net2out_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    // in2*
    ASSERT_COND( mk5.insert(make_pair("in2net",  &in2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2fork", &in2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2file", &in2net_fn<mark5c>)).second );
    if ( buffering ) {
        ASSERT_COND( mk5.insert(make_pair("record", &in2net_fn<mark5c>)).second );
    }
    else {
        ASSERT_COND( mk5.insert(make_pair("record", in2disk_fn)).second );
    }
    ASSERT_COND( mk5.insert(make_pair("in2mem", &in2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("in2memfork", &in2net_fn<mark5c>)).second );

    // mem2*
    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    
    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2net", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spid2file", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2net", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("spin2file", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<mark5c>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<mark5c>)).second );


    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("file2disk", file2disk_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );
    
    ASSERT_COND( mk5.insert(make_pair("layout", layout_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("debug", debug_fn)).second );

    // The same daughterboard register backdoor that Chet Ruszczyk has in 
    // "drs"
    ASSERT_COND( mk5.insert(make_pair("diag", diag_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("transfermode", transfermode_fn)).second );

    return mk5;
}

const mk5commandmap_type& make_generic_commandmap( bool ) {
    static mk5commandmap_type mk5 = mk5commandmap_type();

    if( mk5.size() )
        return mk5;

    // generic
    ASSERT_COND( mk5.insert(make_pair("dts_id", dtsid_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("os_rev", os_rev_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("error", error_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("status", status_fn)).second );
    // task_id could be useful on generic. Need ROT broadcasts for time
    ASSERT_COND( mk5.insert(make_pair("task_id", task_id_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("constraints", constraints_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("tstat", tstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("memstat", memstat_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("dbglev", debuglevel_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mode", mk5bdom_mode_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("evlbi", evlbi_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("bufsize", bufsize_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("version", version_fn)).second );
    // Data check could be useful if we could let it read from mem or file
    //ASSERT_COND( mk5.insert(make_pair("data_check", data_check_5a_fn)).second );
    //ASSERT_COND( mk5.insert(make_pair("scan_check", scan_check_5a_fn)).second );
    // Maybe use 'scan_set' to set source for data_check/scan_check?
    //ASSERT_COND( mk5.insert(make_pair("scan_set", scan_set_fn)).second );
    //
    // We must be able to sort of set the trackbitrate. Support both 
    // play_rate= and clock_set (since we do "mode= mark4|vlba" and
    // "mode=ext:<bitstreammask>")
    ASSERT_COND( mk5.insert(make_pair("play_rate", mk5c_playrate_clockset_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("clock_set", mk5c_playrate_clockset_fn)).second );

    // network stuff
    ASSERT_COND( mk5.insert(make_pair("net_protocol", net_protocol_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net_port", net_port_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mtu", mtu_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("ipd", interpacketdelay_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("trackmask", trackmask_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("itcp_id", itcp_id_fn)).second );

    // fill2*
    ASSERT_COND( mk5.insert(make_pair("fill2net", disk2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2file", diskfill2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("fill2vbs", net2vbs_fn)).second );

    // net2*
    ASSERT_COND( mk5.insert(make_pair("net2file", net2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2check", net2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxc", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2sfxcfork", net2sfxc_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2mem", net2mem_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("mem2sfxc", mem2sfxc_fn)).second );
    
    // Dechannelizing/cornerturning to the network or file
    ASSERT_COND( mk5.insert(make_pair("spill2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spill2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("spif2file", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2net", &spill2net_fn<0>)).second );
    ASSERT_COND( mk5.insert(make_pair("splet2file", &spill2net_fn<0>)).second );


    ASSERT_COND( mk5.insert(make_pair("file2check", file2check_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2mem", file2mem_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file2net", disk2net_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("mem2file",  mem2file_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2net",  mem2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("mem2time",  mem2time_fn)).second );

    // vlbi streamer
    ASSERT_COND( mk5.insert(make_pair("vbs2net",  vbs2net_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("net2vbs",  net2vbs_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("record",  net2vbs_fn)).second );

    // Mark6-like
    ASSERT_COND( mk5.insert(make_pair("group_def",  group_def_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("set_disks",  set_disks_fn)).second );

    ASSERT_COND( mk5.insert(make_pair("transfermode", transfermode_fn)).second );

    // Very useful Mark5-like interface to FlexBuf recordings
    //ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_5a_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("file_check",  scan_check_vbs_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_check",  scan_check_vbs_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("scan_set",    scan_set_vbs_fn)).second );
    ASSERT_COND( mk5.insert(make_pair("disk2file",   disk2file_vbs_fn)).second );


    return mk5;
}

