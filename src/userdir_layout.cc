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

// SDK8 DRIVEINFO has the smallest fields thus S_DRIVEINFO is at least large
// enough to hold the fields of SDK8_DRIVEINFO.
// Specifically we're talking about the Capacity field here.
void SDK8_DRIVEINFO::get( S_DRIVEINFO& out ) const {
    memcpy( out.Model, Model, sizeof(Model) );
    memcpy( out.Serial, Serial, sizeof(Serial) );
    memcpy( out.Revision, Revision, sizeof(Revision) );
    out.Capacity = Capacity;
    out.SMARTCapable = SMARTCapable;
    out.SMARTState = SMARTState;
}

void SDK8_DRIVEINFO::set( S_DRIVEINFO const& in ) {
    memcpy( Model, in.Model, sizeof(Model) );
    memcpy( Serial, in.Serial, sizeof(Serial) );
    memcpy( Revision, in.Revision, sizeof(Revision) );
    // SDK8 has a limit of 1TB per disk that it can store
    // S_DEVINFO.Capacity is reported in #-of-512byte 'pages'
    // and it seems that the limit of SDK8 is having at most
    // 2^31 of these pages it can count
    EZASSERT2( (uint64_t)in.Capacity<=(((uint64_t)1)<<31) , userdirexception,
               EZINFO("cannot store disk capacity " << in.Capacity << " in a SDK8 formatted user directory [~1TB/disk max]") );
    Capacity     = in.Capacity;
    SMARTCapable = in.SMARTCapable;
    SMARTState   = in.SMARTState;
}

void SDK9_DRIVEINFO_wrong::get( S_DRIVEINFO& out ) const {
    memcpy( out.Model, Model, sizeof(Model) );
    memcpy( out.Serial, Serial, sizeof(Serial) );
    memcpy( out.Revision, Revision, sizeof(Revision) );
    EZASSERT2( sizeof(out.Capacity)>=sizeof(this->Capacity), userdirexception,
               EZINFO("disk capacity field in disk pack's user directory is larger than in current SDK") );
    out.Capacity = Capacity;
    out.SMARTCapable = SMARTCapable;
    out.SMARTState = SMARTState;
}

// SDK9 DRIVEINFO's Capacity is the larger of them all so that all S_DEVINFO
// structs' Capacity fields values will fit easily
void SDK9_DRIVEINFO_wrong::set( S_DRIVEINFO const& in ) {
    memcpy( Model, in.Model, sizeof(Model) );
    memcpy( Serial, in.Serial, sizeof(Serial) );
    memcpy( Revision, in.Revision, sizeof(Revision) );
    Capacity = in.Capacity;
    SMARTCapable = in.SMARTCapable;
    SMARTState = in.SMARTState;
}


void SDK9_DRIVEINFO::get( S_DRIVEINFO& out ) const {
    memcpy( out.Model, Model, sizeof(Model) );
    memcpy( out.Serial, Serial, sizeof(Serial) );
    memcpy( out.Revision, Revision, sizeof(Revision) );
    EZASSERT2( sizeof(out.Capacity)>=sizeof(this->Capacity), userdirexception,
               EZINFO("disk capacity field in disk pack's user directory is larger than in current SDK") );
    out.Capacity = Capacity;
    out.SMARTCapable = SMARTCapable;
    out.SMARTState = SMARTState;
}

void SDK9_DRIVEINFO::set( S_DRIVEINFO const& in ) {
    memcpy( Model, in.Model, sizeof(Model) );
    memcpy( Serial, in.Serial, sizeof(Serial) );
    memcpy( Revision, in.Revision, sizeof(Revision) );
    Capacity = in.Capacity;
    SMARTCapable = in.SMARTCapable;
    SMARTState = in.SMARTState;
}

