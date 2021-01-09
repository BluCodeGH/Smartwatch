#ifndef ntp_h
#define ntp_h

#include <time.h>

class ntpClass {
public:
    ntpClass();
    static int sock;
    time_t getTime(char *server);
    time_t getTime(const char* server);
};

extern ntpClass ntp;

#endif // _NtpClientLib_h
