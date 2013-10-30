#include <mk5command/spill2net.h>

// Force instantation of the templates - only for debugging compile
#ifdef GDBDEBUG
std::string  (*s2n5a)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5a>;
std::string  (*s2n5b)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5b>;
std::string  (*s2n5c)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<mark5c>;
std::string  (*s2n5g)(bool, const std::vector<std::string>&, runtime&) = &spill2net_fn<0>;
#endif
