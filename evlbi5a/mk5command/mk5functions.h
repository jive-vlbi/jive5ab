// Prototypes of all available Mark5 command function implementations
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
#ifndef JIVE5AB_MK5FUNCTIONS_H
#define JIVE5AB_MK5FUNCTIONS_H

#include <string>
#include <vector>
#include <runtime.h>

std::string bankinfoset_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string disk_state_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string disk_state_mask_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string bank_switch_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string dir_info_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string disk2net_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string disk2out_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string task_id_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string constraints_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string fill2out_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string net2out_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string net2file_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string file2check_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string file2disk_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string disk2file_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string file2mem_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string diskfill2file_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string net2check_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string net2sfxc_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string net2mem_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mem2file_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mem2net_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mem2sfxc_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mem2time_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mk5a_clock_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string in2disk_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string pps_source_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mtu_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string net_port_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string tstat_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string memstat_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string evlbi_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string reset_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string mk5bdim_mode_fn( bool qry, const std::vector<std::string>& args, runtime& rte);
std::string mk5bdim_cascade_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mk5a_mode_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mk5bdom_mode_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string mk5c_fill_pattern_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string playrate_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string clock_set_fn(bool qry, const std::vector<std::string>& args, runtime& rte );
std::string mk5c_playrate_clockset_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string mk5c_packet_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string net_protocol_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string status_fn(bool q, const std::vector<std::string>&, runtime& rte);
std::string debug_fn( bool q, const std::vector<std::string>& args, runtime& rte );
std::string diag_fn(bool qry, const std::vector<std::string>& args, runtime& rte);
std::string debuglevel_fn(bool qry, const std::vector<std::string>& args, runtime&);
std::string interpacketdelay_fn( bool qry, const std::vector<std::string>& args, runtime& rte );
std::string skip_fn( bool q, const std::vector<std::string>& args, runtime& rte );
std::string led_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string dtsid_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string ssrev_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string scandir_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string pps_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string dot_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string trackmask_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string version_fn(bool q, const std::vector<std::string>& args, runtime& );
std::string bufsize_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string dot_set_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string disk_info_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string position_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string os_rev_fn(bool q, const std::vector<std::string>& args, runtime&);
std::string start_stats_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string get_stats_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string replaced_blks_fn(bool q, const std::vector<std::string>& args, runtime& XLRCODE(rte) );
std::string vsn_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string data_check_5a_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string data_check_dim_fn(bool q, const std::vector<std::string>& args, runtime& rte );
std::string scan_check_5a_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string scan_check_dim_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string scan_set_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string error_fn(bool q, const std::vector<std::string>& args, runtime& );
std::string recover_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string protect_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string rtime_5a_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string rtime_dim_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string track_set_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string tvr_fn(bool q, const std::vector<std::string>& args, runtime& rte);
std::string itcp_id_fn(bool q,  const std::vector<std::string>& args, runtime& rte);
std::string layout_fn(bool q,  const std::vector<std::string>& args, runtime& rte);
std::string nop_fn(bool q, const std::vector<std::string>& args, runtime&);
std::string personality_fn(bool q, const std::vector<std::string>& args, runtime&);

std::string transfermode_fn(bool q, const std::vector<std::string>& args, runtime&);



// These functions are templates. They're templated on the actual mark5
// type to deal with the Mark5 I/O board in case of "in2*" or "spin2*"
#include <mk5command/in2net.h>
#include <mk5command/spill2net.h>




#endif
