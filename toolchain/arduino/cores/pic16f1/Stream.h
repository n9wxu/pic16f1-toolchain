// Stream: bytes in, with Print for bytes out.
#ifndef Stream_h
#define Stream_h

#include "Print.h"

class Stream : public Print {
    unsigned long timeout = 1000;
    int timedRead();
    int timedPeek();

public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;

    // How long the functions below wait for a byte, in milliseconds.
    void setTimeout(unsigned long ms) { timeout = ms; }
    size_t readBytes(char *buffer, size_t length);
    size_t readBytes(uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }
    size_t readBytesUntil(char terminator, char *buffer, size_t length);
    // The next number in the input, skipping what comes before it; 0 if
    // none comes in time.
    long parseInt();
};

#endif
