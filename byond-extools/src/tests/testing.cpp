#include "testing.h"

std::condition_variable Tests::breakpoint_hit;
std::mutex Tests::breakpoint_mutex;
Value *Tests::local_var;
bool Tests::bp_hit;

void Tests::Breakpoint(ExecutionContext *ctx)
{
    bp_hit = true;
    auto lock = std::unique_lock<std::mutex>(breakpoint_mutex);
    auto bp = get_breakpoint(ctx->constants->proc_id, ctx->current_opcode);
	std::swap(ctx->bytecode[bp->offset], bp->replaced_opcode);
    if (ctx->local_var_count > 0)
        local_var = &ctx->local_variables[0];
    breakpoint_hit.notify_all();
    ctx->current_opcode--;
}

void Tests::Thread(int promise_id)
{
    const char *res = strdup(Run().c_str());
    SetVariable(0x21, promise_id, TFFI::result_string_id, {0x06, (int)Core::GetString(res)});
    SetVariable(0x21, promise_id, TFFI::completed_string_id, {0x2A, 1});
    float internal_id = GetVariable(0x21, promise_id, TFFI::internal_id_string_id).valuef;
    while (true)
    {
        if (TFFI::suspended_procs.find(internal_id) != TFFI::suspended_procs.end())
        {
            break;
        }
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    TFFI::suspended_procs[internal_id]->time_to_resume = 1;
    TFFI::suspended_procs.erase(internal_id);
}

std::string Tests::Run()
{
    nlohmann::json test_data = {
        {"success", false},
        {"opcode", TEST_INACTIVE},
        {"disassembly", TEST_INACTIVE},
        {"offset", TEST_INACTIVE},
        {"breakpoint", TEST_INACTIVE},
        {"locals", TEST_INACTIVE}};
    std::uint32_t breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", Breakpoint);
    if (!breakpoint_opcode)
    {
        printf("[Testing] Failed to register DEBUG_BREAKPOINT opcode!\n");
        test_data["opcode"] = TEST_FAILURE;
        return test_data.dump();
    }
    test_data["opcode"] = TEST_PASSED;
    Core::Proc testing_proc = Core::get_proc("/proc/testing");
    if (!testing_proc)
    {
        printf("[Testing] Didn't find /proc/testing.\n");
        test_data["disassembly"] = TEST_FAILURE;
        return test_data.dump();
    }
    test_data["disassembly"] = TEST_PASSED;
    printf("[Testing] Found /proc/testing (proc id %d)\n", testing_proc.id);
    Disassembly dis = testing_proc.disassemble();
    test_data["disassembly"] = TEST_PASSED;
    std::uint16_t bp_offset = 0;
    for (Instruction i : dis.instructions)
    {
        if (i.comment() == "\"Breakpoint goes here.\"")
        {
            bp_offset = i.offset();
            break;
        }
    }
    if (bp_offset == 0)
    {
        printf("[Testing] Failed to find \"Breakpoint goes here.\" in /proc/testing.\n");
        test_data["offset"] = TEST_FAILURE;
        return test_data.dump();
    }
    test_data["offset"] = TEST_PASSED;
    set_breakpoint(testing_proc, bp_offset);
    std::unique_lock<std::mutex> lk(breakpoint_mutex);
    bool status = breakpoint_hit.wait_for(lk, std::chrono::seconds(15), [] {
        return bp_hit;
    });
    if (!status)
    {
        printf("[Testing] Breakpoint test timed out!\n", local_var);
        test_data["breakpoint"] = TEST_FAILURE;
    }
    else
    {
        test_data["breakpoint"] = TEST_PASSED;
    }
    if (!local_var)
    {
        printf("[Testing] Local variable not set!\n");
        test_data["locals"] = TEST_FAILURE;
    }
    else if (local_var->valuef != 9.0f && local_var->value != 9)
    {
        printf("[Testing] Local Variable #1 was %ff instead of 9.0f!\n", local_var->valuef);
        test_data["locals"] = TEST_FAILURE;
    }
    else
    {
        test_data["locals"] = TEST_PASSED;
    }
    return test_data.dump();
}