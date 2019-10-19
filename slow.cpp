#include <string>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#ifdef LINUX
#include <unistd.h>
#endif
#ifdef WINDOWS
#include <windows.h>
#endif

void mySleep(int sleepMs)
{
#ifdef LINUX
    usleep(sleepMs * 1000);   // usleep takes sleep time in us (1 millionth of a second)
#endif
#ifdef WINDOWS
    Sleep(sleepMs);
#endif
}

extern "C" EXPORT const char* slow_concat(int n_args, const char** args)
{
    std::string out;
	int i = 0;
    while (args[i] != 0) {
        out.append(args[i]);
        mySleep(750);
    }
    return out.c_str();
}