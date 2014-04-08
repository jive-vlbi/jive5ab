#include <scan_label.h>
#include <regular_expression.h>
#include <mk5_exception.h>

#include <map>

using namespace std;

const string match_whole_string(string in) {
    return "^" + in + "$";
}

string default_experiment;
string default_station;

struct Scan_Regular_Expressions {
    Scan_Regular_Expressions(const string& exp,
                             const string& st,
                             const string& sc,
                             const string& l,
                             const string& e_l,
                             const string& f_l) :
        experiment(exp),
        station(st),
        scan(sc),
        label(l),
        extended_label(e_l),
        file_label(f_l)
    {}

    const Regular_Expression experiment;
    const Regular_Expression station;
    const Regular_Expression scan;
    const Regular_Expression label;
    const Regular_Expression extended_label;
    const Regular_Expression file_label;

private:
    Scan_Regular_Expressions();
};

countedpointer<Scan_Regular_Expressions> regexs;

scan_label::Split_Result split_implementation(const string& label, const Regular_Expression& regex) {
    matchresult matches = regex.matches(label);
    if ( !matches ) {
        THROW_EZEXCEPT(scan_label::Scan_Label_Exception, "failed to split label '" << label << "'");
    }
    
    // the fourth group is optional
    string remainder;
    try {
        remainder = matches.group(4);
    }
    catch (Regular_Expression_Exception) {
        remainder = "";
    }

    return scan_label::Split_Result(matches.group(1), 
                                    matches.group(2), 
                                    matches.group(3),
                                    remainder);
}

namespace scan_label {
    DEFINE_EZEXCEPT(Scan_Label_Exception)

    void initialize(const ioboard_type::iobflags_type& hardware) {
        // for now just allow one call to initialize
        EZASSERT2(!regexs, Scan_Label_Exception, EZINFO("scan label regexs already initialized"));

        if (hardware & ioboard_type::mk5a_flag) {
            default_experiment = "";
            default_station = "";
        }
        else if (hardware & ioboard_type::mk5b_flag) {
            default_experiment = "EXP";
            default_station = "STN";
        }
        else {
            default_experiment = "EXP";
            default_station = "ST";
        }

        // all regular expression pattern are build from a pattern for
        // experiment, station and scan name
        string experiment_pattern;
        string station_pattern;
        string scan_name_pattern;
        string extended_scan_name_pattern;
        if (hardware & ioboard_type::mk5a_flag) {
            experiment_pattern = "([A-Za-z0-9]{0,16})";
            scan_name_pattern = "([A-Za-z0-9\\-]{0,16})";
            extended_scan_name_pattern = scan_name_pattern;
        }
        else {
            experiment_pattern = "([A-Za-z0-9]{0,8})";
            scan_name_pattern = "([A-Za-z0-9\\+\\.\\-]{0,31})";
            extended_scan_name_pattern = "([A-Za-z0-9\\+\\.\\-]{0,32})";
        }
        station_pattern = experiment_pattern;

        // build the patterns for scan labels
        string label_pattern = experiment_pattern + "_" + station_pattern + "_" + scan_name_pattern;
        string extended_label_pattern = experiment_pattern + "_" + station_pattern + "_" + extended_scan_name_pattern;
        string file_name_pattern;
        if (hardware & ioboard_type::mk5a_flag) {
            file_name_pattern = label_pattern + "(_([0-9]{13}|[0-9]{9}|[0-9]{6,7})(\\.[0-9]*)+(_[A-Za-z]{2}=[^_]*)*)?"; // scan label may be followed by a date field (6,7,9 or 13 digits), which be followed a number of auxilarry information fields in the format 'cc=ppp' where cc is 2 characters and ppp is free format
        }
        else if (hardware & ioboard_type::mk5b_flag) {
            file_name_pattern = label_pattern + "(_bm=0x[0-9a-f]+)?"; // scan label may be followed by a bitstream definition
        }
        else {
            file_name_pattern = label_pattern;
        }

        // compile the patterns and store the regex
        regexs = countedpointer<Scan_Regular_Expressions>
            (new Scan_Regular_Expressions(match_whole_string(experiment_pattern),
                                          match_whole_string(station_pattern),
                                          match_whole_string(scan_name_pattern),
                                          match_whole_string(label_pattern),
                                          match_whole_string(extended_label_pattern),
                                          match_whole_string(file_name_pattern)));
    }
    
    Split_Result::Split_Result(const string& e,
                               const string& s, 
                               const string& n,
                               const string& r) :
        experiment(e), station(s), scan(n), rest(r) {}

    Split_Result split(validate_version version, const string& label) {
        EZASSERT2(regexs, Scan_Label_Exception, EZINFO("scan label regexs not initialized"));
        switch(version) {
        case command:
            return split_implementation(label, regexs->label);
        case extended:
            return split_implementation(label, regexs->extended_label);
        case file_name:
            return split_implementation(label, regexs->file_label);
        default:
            THROW_EZEXCEPT(Scan_Label_Exception, "unknown validate version: " << version);
        }        
    }

    string create_scan_label(validate_version version, 
                             const string& label_in,
                             const string& experiment_override,
                             const string& station_override) {
        EZASSERT2(regexs, Scan_Label_Exception, EZINFO("scan label regexs not initialized"));
        // check overrides, the empty defaults are valid
        if ( !regexs->experiment.matches(experiment_override) ) {
            THROW_EZEXCEPT(Error_Code_6_Exception,
                           "experiment label '" << experiment_override << "' is not valid");
        }
        if ( !regexs->station.matches(station_override) ) {
            THROW_EZEXCEPT(Error_Code_6_Exception,
                           "station label '" << station_override << "' is not valid");
        }

        stringstream out;
        if ( experiment_override.empty() && station_override.empty() ) {
            // try to interpret the given label as a Sec. 6 documented label
            try {
                Split_Result res = split(version, label_in);
                out << res.experiment << "_" 
                    << res.station << "_" 
                    << res.scan;
                return out.str();
            }
            catch (Scan_Label_Exception) {
                // fall through to interpreting it as a scan name
            }
        }
        // either we have override(s) given, or the label failed to be parsed
        // as a complete Sec. 6 label
        // so label_in should be a pure scan name
        if ( !regexs->scan.matches(label_in) ) {
            THROW_EZEXCEPT(Error_Code_6_Exception,
                           "scan name '" << label_in << "' is not valid");
        }
        out << ( experiment_override.empty() ? 
                 default_experiment :
                 experiment_override )
            << "_"
            << ( station_override.empty() ? 
                 default_station :
                 station_override )
            << "_"
            << label_in;
        return out.str();
    }
                                  
}
