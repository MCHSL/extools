#include "core.h"
#include "../dmdism/disassembly.h"
#include "../debug_server/debug_server.h"
#include "../optimizer/optimizer.h"
#include "../optimizer/jit.h"
#include "../crash_guard/crash_guard.h"
#include "../extended_profiling/normal_profiling.h"
#include "../extended_profiling/memory_profiling.h"

#include <fstream>

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

trvh toggle_verb_hidden(unsigned int argcount, Value* args, Value src)
{
	Core::get_proc("/client/verb/hidden").proc_table_entry->procFlags = 4;
	return Value::Null();
}

trvh test_invoke(unsigned int argcount, Value* args, Value src)
{
	ProcConstants* fake_ass_sus = new ProcConstants();
	fake_ass_sus->time_to_resume = 0x69;
	fake_ass_sus->proc_id = -1;
	AddSleeperToList(fake_ass_sus);
	return Value::Null();
}

void init_testing()
{
	//Core::Alert(Value(DataType::NUMBER, 5.0f).valuef);
	// jit_compile(Core::get_proc("/proc/jit"));
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
	/*Core::Proc& p = Core::get_proc("/proc/disme");
	std::ofstream o("out.txt");
	for (Instruction& i : p.disassemble())
	{
		o << std::hex << i.offset() << std::dec << "\t\t\t" << i.bytes_str() << "\t\t\t" << i.opcode().mnemonic() << "\n";
	}*/
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
