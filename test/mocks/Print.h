#pragma once
#include <stddef.h>

// Minimal Print base used by SerialTransport. On target this is Arduino's Print.
// Here it's a no-op sink — SerialTransport tests only care that write() is called.
class Print {
public:
    virtual size_t print(const char*) { return 0; }
    virtual size_t println(const char*) { return 0; }
    virtual ~Print() = default;
};
