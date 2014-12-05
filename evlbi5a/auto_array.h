#ifndef EVLBI5A_AUTO_ARRAY_H
#define EVLBI5A_AUTO_ARRAY_H

#include <sys/types.h>

template <typename T>
struct auto_array {
    public:
        typedef T element_type;

        auto_array() :
            m_ptr( 0 )
        {}

        auto_array(T* const p):
            m_ptr( p )
        {}

        auto_array(const auto_array<T>& other):
            m_ptr( other.m_ptr )
        { other.m_ptr = 0; }

        auto_array<T>& operator=(const auto_array<T>& other) {
            if( this!=&other ) {
                m_ptr       = other.m_ptr;
                other.m_ptr = 0;
            }
            return *this;
        }

        T& operator[](size_t idx) {
            return m_ptr[idx];
        }
        T const& operator[](size_t idx) const {
            return m_ptr[idx];
        }

        ~auto_array() {
            delete [] m_ptr;
        }

    private:
        element_type*   m_ptr;
};


#endif
