#ifndef SCAN_LABEL_H
#define SCAN_LABEL_H

#include <ezexcept.h>
#include <ioboard.h> // scan labels are hardware dependent

namespace scan_label {

    DECLARE_EZEXCEPT(Scan_Label_Exception)

    // what is valid scan label depends on the hardware
    void initialize(const ioboard_type::iobflags_type& hardware);

    enum validate_version { command,  // basic version of the scan label, as given by the user
                            extended, // the scan label can be automatically extended under certain conditions, use this version 
                            file_name // file names might contain extra information, this will assume the directory and extension have been stripped
    };

    struct Split_Result {
        Split_Result(const std::string& experiment, 
                     const std::string& station, 
                     const std::string& scan,
                     const std::string& rest);
        std::string experiment;
        std::string station;
        std::string scan;
        std::string rest;
    };
    
    // split the label in experiment, station, scan name and an remainder
    Split_Result split(validate_version version, const std::string& label);
    
    // forms a valid scan label according to Section 6 (5 for Mark5C) 
    // of the Mark5 command sets
    // empty string means no override
    std::string create_scan_label(validate_version version,
                                  const std::string& label_in,
                                  const std::string& experiment_override = "",
                                  const std::string& station_override = "");
                                  
}
    
#endif
