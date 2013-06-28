#include <userdir_layout.h>
#include <boost/mpl/assert.hpp>//#include <static_check.h>

// the exception
DEFINE_EZEXCEPT(userdirexception)
void EnhancedDirectoryHeader::clear() {
    directory_version = 1;
    status = 0;
    vsn[0] = '\0';
    companion_vsn[0] = '\0';
    continued_to_vsn[0] = '\0';
    memset( &spare[0], 0, sizeof(spare) );
}

void EnhancedDirectoryEntry::clear() {
    data_type = 0;
    scan_number = 0;
    frame_length = 0;
    station_code[0] = '\0';
    scan_name[0] = '\0';
    experiment[0] = '\0';
    start_byte = 0;
    stop_byte = 0;
    memset(&first_time_tag[0], 0, sizeof(first_time_tag));
    first_frame_number = 0;
    byte_offset = 0;
    track_bitstream_data_rate_mbps = 0;
    track_bitstream_mask = 0;
    memset(&spare[0], 0, sizeof(spare));
}

void SDK8_DRIVEINFO::get( S_DRIVEINFO& out ) const {
#if WDAPIVER>999
    memcpy( out.Model, Model, sizeof(Model) );
    memcpy( out.Serial, Serial, sizeof(Serial) );
    memcpy( out.Revision, Revision, sizeof(Revision) );
    out.Capacity = Capacity;
    out.SMARTCapable = SMARTCapable;
    out.SMARTState = SMARTState;
#else
    //STATIC_CHECK( sizeof(*this) == sizeof(out), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size )
    BOOST_MPL_ASSERT_MSG( sizeof(*this) == sizeof(out), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size, (SDK8_DRIVEINFO, S_DRIVEINFO) );
    memcpy( &out, this, sizeof(*this) );
#endif
}

void SDK8_DRIVEINFO::set( S_DRIVEINFO& in ) {
#if WDAPIVER>999
    memcpy( Model, in.Model, sizeof(Model) );
    memcpy( Serial, in.Serial, sizeof(Serial) );
    memcpy( Revision, in.Revision, sizeof(Revision) );
    if ( in.Capacity > std::numeric_limits<uint32_t>::max() ) {
        THROW_EZEXCEPT( userdirexception, "cannot store disk packs of size " << in.Capacity << " in a SDK8 formatted user directory" );
    }
    Capacity = in.Capacity;
    SMARTCapable = in.SMARTCapable;
    SMARTState = in.SMARTState;
#else
    //STATIC_CHECK( sizeof(*this) == sizeof(in), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size )
    BOOST_MPL_ASSERT_MSG( sizeof(*this) == sizeof(in), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size, (SDK8_DRIVEINFO, S_DRIVEINFO) );
    memcpy( this, &in, sizeof(*this) );
#endif
}

void SDK9_DRIVEINFO::get( S_DRIVEINFO& out ) const {
#if WDAPIVER>999
    //STATIC_CHECK( sizeof(*this) == sizeof(out), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size )
    BOOST_MPL_ASSERT_MSG( sizeof(*this) == sizeof(out), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size, (SDK9_DRIVEINFO, S_DRIVEINFO) );
    memcpy( &out, this, sizeof(*this) );
#else
    memcpy( out.Model, Model, sizeof(Model) );
    memcpy( out.Serial, Serial, sizeof(Serial) );
    memcpy( out.Revision, Revision, sizeof(Revision) );
    if ( Capacity > std::numeric_limits<uint32_t>::max() ) {
        THROW_EZEXCEPT( userdirexception, "current SDK cannot handle disk pack sizes as strored in the user directory, stored size: " << Capacity );
    }
    out.Capacity = Capacity;
    out.SMARTCapable = SMARTCapable;
    out.SMARTState = SMARTState;
#endif
}

void SDK9_DRIVEINFO::set( S_DRIVEINFO& in ) {
#if WDAPIVER>999
    //STATIC_CHECK( sizeof(*this) == sizeof(in), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size )
    BOOST_MPL_ASSERT_MSG( sizeof(*this) == sizeof(in), current_SDK_DRIVEINFO_struct_layout_has_unexpected_size, (SDK9_DRIVEINFO, S_DRIVEINFO) );
    memcpy( this, &in, sizeof(*this) );
#else
    memcpy( Model, in.Model, sizeof(Model) );
    memcpy( Serial, in.Serial, sizeof(Serial) );
    memcpy( Revision, in.Revision, sizeof(Revision) );
    Capacity = in.Capacity;
    SMARTCapable = in.SMARTCapable;
    SMARTState = in.SMARTState;
#endif
}
