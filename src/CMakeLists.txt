set(PROG jive5ab)

# Prepare the version.cc source containing info about the build
configure_file(version.cc.in version.cc)

set(JIVE5AB_SRC
./bin.cc
./block.cc
./blockpool.cc
./boyer_moore.cc
./busywait.cc
./byteorder.cc
./chain.cc
./chainstats.cc
./constraints.cc
./counter.cc
./data_check.cc
./dayconversion.cc
./dosyscall.cc
./dotzooi.cc
./dynamic_channel_extractor.cc
./errorqueue.cc
./evlbidebug.cc
./getsok.cc
./getsok_udt.cc
./headersearch.cc
./hex.cc
./highrestime.cc
./interchain.cc
./interchainfns.cc
./ioboard.cc
./jit.cc
./libvbs.cc
./mk5_exception.cc
./mk5command/ackperiod.cc
./mk5command/bankinfoset.cc
./mk5command/bankswitch.cc
./mk5command/bufsize.cc
./mk5command/clockset.cc
./mk5command/constraints.cc
./mk5command/datastream.cc
./mk5command/data_check_5a.cc
./mk5command/data_check_dim.cc
./mk5command/debug.cc
./mk5command/debuglevel.cc
./mk5command/diag.cc
./mk5command/dirinfo.cc
./mk5command/dirinfo_vbs.cc
./mk5command/disk2file.cc
./mk5command/disk2file_vbs.cc
./mk5command/disk2net.cc
./mk5command/disk2net_vbs.cc
./mk5command/disk2out.cc
./mk5command/disk_info.cc
./mk5command/diskfill2file.cc
./mk5command/diskstatemask.cc
./mk5command/dot.cc
./mk5command/dot_set.cc
./mk5command/dtsid.cc
./mk5command/error.cc
./mk5command/evlbi.cc
./mk5command/file2check.cc
./mk5command/file2disk.cc
./mk5command/file2mem.cc
./mk5command/fill2out.cc
./mk5command/get_stats.cc
./mk5command/group_def.cc
./mk5command/in2disk.cc
./mk5command/in2net.cc
./mk5command/in2netsupport.cc
./mk5command/interpacketdelay.cc
./mk5command/itcp_id.cc
./mk5command/layout.cc
./mk5command/led.cc
./mk5command/mem2file.cc
./mk5command/mem2net.cc
./mk5command/mem2sfxc.cc
./mk5command/mem2time.cc
./mk5command/memstat.cc
./mk5command/mk5.cc
./mk5command/mk5a_clock.cc
./mk5command/mk5a_mode.cc
./mk5command/mk5bdim_cascade.cc
./mk5command/mk5bdim_mode.cc
./mk5command/mk5bdom_mode.cc
./mk5command/mk5c_fill_pattern.cc
./mk5command/mk5c_packet.cc
./mk5command/mk5c_playrate_clockset.cc
./mk5command/mount.cc
./mk5command/mtu.cc
./mk5command/net2check.cc
./mk5command/net2file.cc
./mk5command/net2mem.cc
./mk5command/net2out.cc
./mk5command/net2sfxc.cc
./mk5command/net2vbs.cc
./mk5command/net_port.cc
./mk5command/net_protocol.cc
./mk5command/nop.cc
./mk5command/os_rev.cc
./mk5command/personality.cc
./mk5command/playrate.cc
./mk5command/position.cc
./mk5command/pps.cc
./mk5command/pps_source.cc
./mk5command/protect.cc
./mk5command/recover.cc
./mk5command/replaced_blks.cc
./mk5command/reset.cc
./mk5command/reset_vbs.cc
./mk5command/rtime_5a.cc
./mk5command/rtime_5c.cc
./mk5command/rtime_dim.cc
./mk5command/rtime_vbs.cc
./mk5command/scan_check_5a.cc
./mk5command/scan_check_dim.cc
./mk5command/scan_check_vbs.cc
./mk5command/scan_set.cc
./mk5command/scan_set_vbs.cc
./mk5command/scandir.cc
./mk5command/set_disks.cc
./mk5command/skip.cc
./mk5command/spill2net.cc
./mk5command/ssrev.cc
./mk5command/start_stats.cc
./mk5command/status.cc
./mk5command/taskid.cc
./mk5command/track_set.cc
./mk5command/trackmask.cc
./mk5command/transfermode.cc
./mk5command/tstat.cc
./mk5command/tvr.cc
./mk5command/vbs2net.cc
./mk5command/version.cc
./mk5command/vsn.cc
./mk5command.cc
./mk6info.cc
./mountpoint.cc
./mutex_locker.cc
./netparms.cc
./playpointer.cc
./registerstuff.cc
./regular_expression.cc
./rotzooi.cc
./runtime.cc
./scan.cc
./scan_label.cc
./sciprint.cc
./sfxc_binary_command.cc
./splitstuff.cc
./streamutil.cc
./stringutil.cc
./test.cc
./threadfns/kvmap.cc
./threadfns/multisend.cc
./threadfns.cc
./threadutil.cc
./timewrap.cc
./timezooi.cc
./trackmask.cc
./transfermode.cc
./userdir.cc
./userdir_layout.cc
./variable_type.cc
./xlrdevice.cc
./sse_dechannelizer-${B2B}.S
${CMAKE_CURRENT_BINARY_DIR}/version.cc
${ETRANSFER_SOURCES})

#message("Building ${ACTUAL_JIVE5AB}")

add_executable(jive5ab ${JIVE5AB_SRC})
set_property(TARGET jive5ab PROPERTY POSITION_INDEPENDENT_CODE TRUE)

# On Linux add -lrt for clock_gettime
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
        target_link_libraries(jive5ab PUBLIC rt)
    else()
        target_link_libraries(jive5ab rt)
    endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
endif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")


if (NOT CMAKE_VERSION VERSION_LESS 2.8.12)
    target_include_directories(jive5ab PRIVATE ${CMAKE_SOURCE_DIR}/src ${SSAPI_INCLUDE_DIR} ${CMAKE_SOURCE_DIR} ${ETRANSFER_SOURCE_DIR})
    target_compile_definitions(jive5ab PRIVATE ${INSANITY_DEFS})
    target_compile_options(jive5ab PRIVATE     ${INSANITY_FLAGS})
    target_link_libraries(jive5ab PRIVATE udt5ab ${SSAPI_LIB} ${SSAPI_WDAPI})
else()
    # OLD fucking cmake doesn't propagate include directories between
    # dependents. Thanks guys!
    include_directories(${CMAKE_SOURCE_DIR}/src ${SSAPI_INCLUDE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libudt5ab ${ETRANSFER_SOURCE_DIR})
    # Godverdomme kutcmake!
    set(INSANITY_DEFS_STR ";${INSANITY_DEFS}")
    string(REPLACE ";" " -D" INSANITY_DEFS_STR  "${INSANITY_DEFS_STR}")
    string(REPLACE ";" " " INSANITY_FLAGS_STR "${INSANITY_FLAGS}")
    add_definitions(${INSANITY_DEFS_STR})
    set_target_properties(jive5ab PROPERTIES COMPILE_FLAGS ${INSANITY_FLAGS_STR})
    target_link_libraries(jive5ab udt5ab ${SSAPI_LIB} ${SSAPI_WDAPI})
endif (NOT CMAKE_VERSION VERSION_LESS 2.8.12)

install(TARGETS jive5ab DESTINATION bin)
