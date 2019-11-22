#include "../core/core.h"
#include "../debug_server/debug_server.h"
#include "../dmdism/disassembly.h"
#include "../core/json.hpp"
#include "../tffi/tffi.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

#define TEST_FAILURE                    "test failed"
#define TEST_PASSED                     "test passed"
#define TEST_INACTIVE                   "test not ran"

namespace Tests
{   
    extern std::condition_variable breakpoint_hit;
    extern std::mutex breakpoint_mutex;
    extern Value* local_var;
    extern bool bp_hit;
    std::string Run();
    void Thread(int promise_id);
    void Breakpoint(ExecutionContext* ctx);
};