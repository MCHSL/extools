#include "core.h"
#include "../dmdism/disassembly.h"
#include "../debug_server/debug_server.h"

trvh cheap_hypotenuse(Value* args, unsigned int argcount)
{
	return { 0x2A, (int)sqrt((args[0].valuef - args[2].valuef) * (args[0].valuef - args[2].valuef) + (args[1].valuef - args[3].valuef) * (args[1].valuef - args[3].valuef)) };
}

trvh measure_get_variable(Value* args, unsigned int argcount)
{
	int name_string_id = Core::GetString("name");
	int type = args[0].type;
	int value = args[0].value;
	//long long duration = 0;
	//for (int j = 0; j < 1000; j++)
	//{
		//auto t1 = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 10; i++)
	{
		GetVariable(args[0].type, args[0].value, name_string_id);
	}
	//auto t2 = std::chrono::high_resolution_clock::now();
	//auto duration2 = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
	//duration += duration2;
//}

//Core::Alert(std::to_string(duration/1000).c_str());
	return { 0, 0 };
}

trvh show_profiles(Value* args, unsigned int argcount)
{
	unsigned long long dm = Core::get_proc("/proc/measure_dm").profile()->total.microseconds;
	unsigned long long native = Core::get_proc("/proc/measure_native").profile()->total.microseconds;
	std::string woo = "DM proc took " + std::to_string(dm) + " microseconds while native took " + std::to_string(native) + " microseconds.";
	Core::Alert(woo.c_str());
	return { 0, 0 };
}

void cheap_hypotenuse_opcode(ExecutionContext* ctx) //for testing purposes, remove later
{
	//Core::Alert("Calculating hypotenuse");
	const float Ax = Core::get_stack_value(4).valuef;
	const float Ay = Core::get_stack_value(3).valuef;
	const float Bx = Core::get_stack_value(2).valuef;
	const float By = Core::get_stack_value(1).valuef;
	Core::stack_pop(4);
	Core::stack_push({ 0x2A, (int)std::sqrt((Ax - Bx) * (Ax - Bx) + (Ay - By) * (Ay - By)) });
}

trvh update_light_objects;

LONG WINAPI DumpThingy(_EXCEPTION_POINTERS* ExceptionInfo)
{
	return EXCEPTION_CONTINUE_SEARCH;
}

void init_testing()
{
	Core::enable_profiling();
	debugger_initialize();
	debugger_connect();
	//Core::get_proc("/datum/explosion/New").extended_profile();
	//Core::get_proc("/client/verb/test_reentry").extended_profile();
	//Core::get_proc("/client/verb/test_extended_profiling").extended_profile();
	//extended_profiling_procs[.id] = true;
	//Core::get_proc("/proc/cheap_hypotenuse_hook").hook(cheap_hypotenuse);
	//Core::get_proc("/proc/measure_get_variable").hook(measure_get_variable);
	//Core::get_proc("/proc/laugh").hook(show_profiles);

	/*int hypotenuse_opcode = Core::register_opcode("CHEAP_HYPOTENUSE", cheap_hypotenuse_opcode);
	Core::Proc hypotenuse_bench = Core::get_proc("/proc/bench_cheap_hypotenuse_native");
	Disassembly dis = hypotenuse_bench.disassemble();
	for (Instruction& instr : dis)
	{
		if (instr == Bytecode::CALLGLOB)
		{
			instr = Instruction(hypotenuse_opcode);
			break;
		}
	}
	hypotenuse_bench.assemble(dis);*/
}

void run_tests()
{

}