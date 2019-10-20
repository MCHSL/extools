#include <string>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#ifdef _WIN32
#include <windows.h>
#include <synchapi.h>
#else
#include <unistd.h>
#endif

void mySleep(int sleepMs)
{
#ifdef _WIN32
    Sleep(sleepMs);
#else
    usleep(sleepMs * 1000);
#endif
}

char ret[256];

extern "C" EXPORT const char* slow_concat(int n_args, const char** args)
{
    std::string s;
    for (int i = 0; i < n_args; i++)
    {
        mySleep(500);
        s += args[i];
    }
    strcpy(ret, s.c_str());
    return ret;
}