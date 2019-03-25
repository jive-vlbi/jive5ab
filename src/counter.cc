#include <counter.h>

#if defined(GDBDEBUG) && GDBDEBUG>=2
real_counter_type::real_counter_type()          {}
real_counter_type::real_counter_type(int)       {}
real_counter_type::real_counter_type(uint64_t)  {}
real_counter_type::real_counter_type(int64_t)   {}

real_counter_type::operator bool( void ) const {
    return false;
}

real_counter_type::operator double( void ) const {
    return 0;
}

real_counter_type::operator int64_t( void ) const {
    return 0;
}
real_counter_type::operator uint64_t( void ) const {
    return 0;
}

real_counter_type const& real_counter_type::operator++( void ) {
    return *this;
}

real_counter_type const& real_counter_type::operator++( int ) {
    return *this;
}

std::ostream& operator<<(std::ostream& os, real_counter_type const&) {
    return os << "*.**";
}

real_counter_type operator-(real_counter_type const&, real_counter_type const&) {
    return real_counter_type();
}
#endif
