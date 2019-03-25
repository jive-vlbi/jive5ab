#include <mk5command/spill2net.h>

// Change settings in object returned by open_file(), according
// to desired props for _this_ function, mainly, we want to hold
// up reading until user really starts the transfer; thus we
// want '.run == false'
fdreaderargs* open_file_wrap(std::string filename, runtime* r) {
    struct stat   f_stat;
    fdreaderargs* actfdr = open_file(filename+",r", r);

     // Fill in '.end' with the size of the file
    ASSERT2_ZERO( ::stat(filename.c_str(), &f_stat), SCINFO(" - " << filename));
    EZASSERT2((f_stat.st_mode&S_IFREG)==S_IFREG, cmdexception, EZINFO(filename << " not a regular file"));
    
    actfdr->run                       = false;
    actfdr->end                       = f_stat.st_size;
    actfdr->allow_variable_block_size = true;

    return actfdr;
}

// Force instantation of the templates - only for debugging compile
#ifdef GDBDEBUG
std::string  (*s2n5a)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5a>;
std::string  (*s2n5b)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5b>;
std::string  (*s2n5c)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5c>;
std::string  (*s2n5g)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<0>;
#endif
