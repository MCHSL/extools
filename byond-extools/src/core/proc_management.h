#pragma once
#include "core.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

typedef trvh(*ProcHook)(unsigned int args_len, Value* args, Value src);

class Disassembly;

namespace Core
{
	class Proc
	{
		Proc(const Proc&) = delete;
		Proc& operator=(const Proc&) = delete;

		Proc() {}
		friend bool populate_proc_list();

	public:
		Proc(Proc&& rhs) noexcept = default;
		Proc& operator=(Proc&& rhs) noexcept = default;

		std::string raw_path;
		std::string name;
		std::string simple_name;
		std::uint32_t id = 0;
		unsigned int override_id = 0;

		ProcArrayEntry* proc_table_entry = nullptr;
		ProcSetupEntry* setup_entry_bytecode = nullptr;
		ProcSetupEntry* setup_entry_varcount = nullptr;

		std::uint16_t bytecode_idx = 0;
		std::uint16_t varcount_idx = 0;

		std::uint32_t* original_bytecode_ptr = nullptr;
		std::vector<std::uint32_t> bytecode;

		void set_bytecode(std::vector<std::uint32_t>&& new_bytecode);
		std::uint32_t* get_bytecode();
		std::uint16_t get_bytecode_length();
		void reset_bytecode();

		std::uint16_t get_local_varcount();
		Disassembly disassemble();
		void assemble(Disassembly disasm);

		ProfileInfo* profile() const;
		void extended_profile();
		void hook(ProcHook hook_func);
		Value call(std::vector<Value> arguments, Value usr = Value::Null());

		bool operator<(const Proc& rhs) const
		{
			return id < rhs.id;
		}

		bool operator==(const Proc& rhs) const
		{
			return id == rhs.id;
		}
	};

	Proc& get_proc(std::string name, unsigned int override_id=0);
	Proc& get_proc(unsigned int id);
	Proc& get_proc(ExecutionContext* ctx);
	Proc* try_get_proc(std::string name, unsigned int override_id=0);
	std::vector<Proc>& get_all_procs();

	bool populate_proc_list();
	void destroy_proc_list();
	Disassembly disassemble_raw(std::vector<int> bytecode);
}

extern std::unordered_map<unsigned int, ProcHook> proc_hooks;
extern std::unordered_map<unsigned int, bool> extended_profiling_procs;

extern std::vector<bool> codecov_executed_procs;
