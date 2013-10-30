#include <mk5command/in2net.h>

// Force instantation of the templates - only for debugging compile
#ifdef GDBDEBUG
std::string  (*i2n5a)(bool, const std::vector<std::string>&, runtime&) = &in2net_fn<mark5a>;
std::string  (*i2n5b)(bool, const std::vector<std::string>&, runtime&) = &in2net_fn<mark5b>;
std::string  (*i2n5c)(bool, const std::vector<std::string>&, runtime&) = &in2net_fn<mark5c>;
#endif
