// Exceptions?
#ifndef LIBUDT11_UDT_EXCEPTION_H
#define LIBUDT11_UDT_EXCEPTION_H

namespace libudt11 {
    namespace exception {
        extern const int ERROR;

        class CUDTException {
            public:
                CUDTException(int , int , int) {}
                CUDTException(const CUDTException& /*e*/) {}
                CUDTException& operator=(CUDTException const& /*other*/) {
                    return *this;
                }
                virtual const char* getErrorMessage() const {
                    return "";
                };
                virtual int getErrorCode() const {
                    return 0;
                }
                virtual void clear() {}
                virtual ~CUDTException() {}
        };

        CUDTException& getlasterror( void );
    } // exceptions
} // libudt11

#endif
