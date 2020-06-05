#include "core.h"
#include "../dmdism/disassembly.h"
#include "../debug_server/debug_server.h"
#include "../optimizer/optimizer.h"
#include "../crash_guard/crash_guard.h"
#include "../extended_profiling/normal_profiling.h"
#include "../extended_profiling/memory_profiling.h"

#include <fstream>

trvh cheap_hypotenuse(Value* args, unsigned int argcount)
{
	return { DataType::NUMBER, (int)sqrt((args[0].valuef - args[2].valuef) * (args[0].valuef - args[2].valuef) + (args[1].valuef - args[3].valuef) * (args[1].valuef - args[3].valuef)) };
}

trvh measure_get_variable(Value* args, unsigned int argcount)
{
	int name_string_id = Core::GetStringId("name");
	DataType type = args[0].type;
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
	return Value::Null();
}

trvh show_profiles(Value* args, unsigned int argcount)
{
	unsigned long long dm = Core::get_proc("/proc/measure_dm").profile()->total.microseconds;
	unsigned long long native = Core::get_proc("/proc/measure_native").profile()->total.microseconds;
	std::string woo = "DM proc took " + std::to_string(dm) + " microseconds while native took " + std::to_string(native) + " microseconds.";
	Core::Alert(woo);
	return Value::Null();
}

void cheap_hypotenuse_opcode(ExecutionContext* ctx) //for testing purposes, remove later
{
	//Core::Alert("Calculating hypotenuse");
	const float Ax = Core::get_stack_value(4).valuef;
	const float Ay = Core::get_stack_value(3).valuef;
	const float Bx = Core::get_stack_value(2).valuef;
	const float By = Core::get_stack_value(1).valuef;
	Core::stack_pop(4);
	Core::stack_push({ DataType::NUMBER, (int)std::sqrt((Ax - Bx) * (Ax - Bx) + (Ay - By) * (Ay - By)) });
}



unsigned char _number_op_localm_localn_store_localx[] =
{
	0x55,							// PUSH EBP
	0x8B, 0xEC,						// MOV EBP, ESP
	0x8B, 0x45, 0x08,				// MOV EAX, DWORD PTR SS : [EBP + 0x8]
	0x8B, 0x40, 0x38,				// MOV EAX, DWORD PTR DS : [EAX + 0x38]
	0xF3, 0x0F, 0x10, 0x40, 0x0C,	// MOVSS XMM0, DWORD PTR DS : [EAX + 0xC]
	0xF3, 0x0F, 0x58, 0x40, 0x04,	// ADDSS XMM0, DWORD PTR DS : [EAX + 0x4]
	0xF3, 0x0F, 0x11, 0x40, 0x14,	// MOVSS DWORD PTR DS : [EAX + 0x14], XMM0
	0x5D,							// POP EBP
	0xC3,							// RET
};

template<typename T>
class ext_vector : std::vector<T>
{
public:
	template<typename... Ts>
	void extend(Ts... rest)
	{
		(push_back(rest), ...);
	}
};

#ifdef _WIN32
opcode_handler _generate_number_op_localm_localn_store_localx(unsigned char op, int localm, int localn, int localx)
{
	unsigned char* func = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!func)
	{
		return nullptr;
	}
	std::memcpy(func, _number_op_localm_localn_store_localx, 26);
	func[16] = op;
	func[13] = 4 + 8 * localm;
	func[18] = 4 + 8 * localn;
	func[23] = 4 + 8 * localx;
	DWORD old_prot;
	VirtualProtect(func, 64, PAGE_EXECUTE_READ, &old_prot);
	return (opcode_handler)func;
}
#endif

extern "C" EXPORT void add_subvars_of_locals(ExecutionContext* ctx)
{
	Value a = ctx->local_variables[0];
	Value b = ctx->local_variables[1];
	ctx->local_variables[2].valuef = GetVariable(a.type, a.value, 0x33).valuef + GetVariable((int)b.type, b.value, 0x33).valuef;
}

trvh toggle_verb_hidden(unsigned int argcount, Value* args, Value src)
{
	Core::get_proc("/client/verb/hidden").proc_table_entry->procFlags = 4;
	return Value::Null();
}

trvh test_invoke(unsigned int argcount, Value* args, Value src)
{
	return Value(DataType::STRING, args[0]);
}

void init_testing()
{
	//Value a(5.0f);
	//Value b = a + 5.0f;
	//b += 1.0f;
	//Core::Alert(std::to_string(b));
	//ManagedValue a = Value::World().get("name");
	//dump_full_obj_mem_usage();
	//Core::Alert("end func");
	//Core::global_direct_set("internal_tick_usage", "AYYLMAO");
	//Core::Alert(Core::global_direct_get("internal_tick_usage"));
	//Core::Alert(Core::GetStringFromId(0x86));
	//Core::get_proc("/proc/get_string_by_id").hook(test_invoke);
	//Core::Alert(Core::get_proc("/client/verb/hidden").proc_table_entry->procFlags);
	//Core::Alert(Core::get_proc("/client/verb/nothidden").proc_table_entry->procFlags);
	//Core::get_proc("/client/verb/toggle_hidden_verb").hook(toggle_verb_hidden);
	//Core::get_proc("/client/verb/hidden").proc_table_entry->procFlags = 4;
	//initialize_profiler_access();
	//enable_crash_guard();
	//optimizer_initialize();
	//Core::Alert(Core::stringify({ 0x0C, 0x00 }));
	//Core::Proc p = "/proc/pickdism";
	//std::ofstream o("out.txt");
	//for (Instruction& i : p.disassemble())
	//{
	//	o << i.offset() << "\t\t\t" << i.bytes_str() << "\t\t\t" << i.opcode().mnemonic() << "\n";
	//}
	/*Core::Proc p = "/proc/bench_intrinsic_add";
	Disassembly d = p.disassemble();
	Core::Proc intrinsic_add = "/proc/__intrinsic_add_locals";
	std::vector<std::uint32_t>* new_bytecode = new std::vector<std::uint32_t>;
	for (int i = 0; i < d.size(); i++)
	{
		if (d.at(i) == CALLGLOB && d.at(i).bytes().at(2) == intrinsic_add.id)
		{
			Instruction first_arg = d.at(i - 2);
			Instruction second_arg = d.at(i - 1);
			Instruction destination = d.at(i + 1);
			new_bytecode->resize(new_bytecode->size() - 6);
			int intrinsic_op = Core::register_opcode("INTRINSIC_ADD_NUMBERS_"+std::to_string(rand()), _generate_number_op_localm_localn_store_localx(ADD, first_arg.bytes().at(2), second_arg.bytes().at(2), destination.bytes().at(2)));
			new_bytecode->push_back(intrinsic_op);
			i += 1;
		}
		else
		{
			for (unsigned int& byte : d.at(i).bytes())
			{
				new_bytecode->push_back(byte);
			}
		}
	}
	for (int i = 0; i < 10; i++)
	{
		new_bytecode->push_back(0x00);
	}
	p.set_bytecode(new_bytecode);
	std::ofstream o("out.txt");
	for (Instruction& i : Core::get_proc("/proc/bench_dm_add").disassemble())
	{
		o << i.offset() << "\t\t\t" << i.bytes_str() << "\t\t\t" << i.opcode().mnemonic() << "\n";
	}*//*
	bool find_unknowns = false;
	if (find_unknowns)
	{
		std::ofstream log("unknown_opcodes.txt");
		for (Core::Proc& p : Core::get_all_procs())
		{
			if (!p.name.empty() && p.name.back() == ')')
			{
				continue;
			}
			Disassembly d = p.disassemble();
			for (Instruction& i : d)
			{
				if (i == UNK)
				{
					log << "Unknown instruction in " + p.name + "\n";
					break;
				}
			}
		}
		log.close();
	}*/

	//debugger_connect();
	//Core::get_proc("/datum/explosion/New").extended_profile();
	//Core::get_proc("/client/verb/test_reentry").extended_profile();
	//Core::get_proc("/client/verb/test_extended_profiling").extended_profile();
	//extended_profiling_procs[.id] = true;
	//Core::get_proc("/proc/cheap_hypotenuse_hook").hook(cheap_hypotenuse);
	//Core::get_proc("/proc/measure_get_variable").hook(measure_get_variable);
	//Core::get_proc("/proc/laugh").hook(show_profiles);

	/*int hypotenuse_opcode = Core::register_opcode("CHEAP_HYPOTENUSE", cheap_hypotenuse_opcode);
	Core::Proc& hypotenuse_bench = Core::get_proc("/proc/bench_cheap_hypotenuse_native");
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

extern "C" EXPORT const char* run_tests(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}
	init_testing();
	run_tests();
	return Core::SUCCESS;
}
