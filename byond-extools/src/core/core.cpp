#include "core.h"
#include "find_functions.h"
#include "../extended_profiling/extended_profiling.h"
#include "socket/socket.h"
#include "../datum_socket/datum_socket.h"
#include <fstream>
#include <unordered_set>
#include <chrono>

ExecutionContext** Core::current_execution_context_ptr;
ExecutionContext** Core::parent_context_ptr_hack;
MiscEntry** Core::misc_entry_table;

RawDatum*** Core::datum_pointer_table;
unsigned int* Core::datum_pointer_table_length;

int ByondVersion;
int ByondBuild;
unsigned int* Core::some_flags_including_profile;
unsigned int Core::extended_profiling_insanely_hacky_check_if_its_a_new_call_or_resume;

//std::vector<bool> Core::codecov_executed_procs;

std::map<unsigned int, opcode_handler> Core::opcode_handlers;
std::map<std::string, unsigned int> Core::name_to_opcode;
unsigned int next_opcode_id = 0x1337;
bool Core::initialized = false;
unsigned int* Core::name_table_id_ptr = nullptr;
unsigned int* Core::name_table = nullptr;
Value* Core::global_var_table = nullptr;
std::unordered_map<std::string, Value*> Core::global_direct_cache;

TableHolder2* Core::obj_table = nullptr;
TableHolder2* Core::datum_table = nullptr;
TableHolder2* Core::list_table = nullptr;
TableHolder2* Core::mob_table = nullptr;
SuspendedProcList* Core::suspended_proc_list = nullptr;

Core::ManagedString::ManagedString(unsigned int id) : string_id(id)
{
	string_entry = GetStringTableEntry(string_id);
	IncRefCount(DataType::STRING, string_id);
}

Core::ManagedString::ManagedString(std::string str)
{
	string_id = GetStringId(str);
	string_entry = GetStringTableEntry(string_id); //leaving it like this for now, not feeling like reversing the new structure when we have refcount functions
	IncRefCount(DataType::STRING, string_id);
}

Core::ManagedString::ManagedString(const ManagedString& other)
{
	string_id = other.string_id;
	string_entry = other.string_entry;
	IncRefCount(DataType::STRING, string_id);
}

Core::ManagedString::~ManagedString()
{
	DecRefCount(DataType::STRING, string_id);
}

Core::ManagedString Core::GetManagedString(std::string str)
{
	return ManagedString(str);
}

Core::ResumableProc::ResumableProc(const ResumableProc& other)
{
	proc = other.proc;
}

Core::ResumableProc Core::ResumableProc::FromCurrent()
{
	auto ctx = Core::get_context();
	ctx->current_opcode++;
	auto ret = ResumableProc(Suspend(ctx, 0));
	ctx->current_opcode--;
	return ret;
}

void Core::ResumableProc::resume()
{
	if (!proc)
	{
		return;
	}
	proc->time_to_resume = 1;
	StartTiming(proc);
	proc = nullptr;
}

Core::ResumableProc Core::SuspendCurrentProc()
{
	return ResumableProc::FromCurrent();
}

bool Core::initialize()
{
	if (initialized)
	{
		return true;
	}
	initialized = verify_compat() && find_functions() && populate_proc_list() && hook_custom_opcodes();
	//Core::codecov_executed_procs.resize(Core::get_all_procs().size());
	return initialized;
}

void Core::Alert(const std::string& what) {
#ifdef _WIN32
	MessageBoxA(NULL, what.c_str(), "Ouch!", MB_OK);
#else
	printf("Ouch!: %s\n", what.c_str());
#endif
}

void Core::Alert(int what)
{
	Alert(std::to_string(what));
}

unsigned int Core::GetStringId(std::string str, bool increment_refcount) {
	switch (ByondVersion) {
	case 512:
		{
			int idx = GetStringTableIndex(str.c_str(), 0, 1);
			if (increment_refcount)
			{
				String* str = GetStringTableEntry(idx);
				str->refcount++;
			}
			return idx; //this could cause memory problems with a lot of long strings but otherwise they get garbage collected after first use.
		}
		case 513:
			return GetStringTableIndexUTF8(str.c_str(), 0xFFFFFFFF, 0, 1);
		default: break;
	}
	return 0;
}

std::string Core::GetStringFromId(unsigned int id)
{
	return GetStringTableEntry(id)->stringData;
}

RawDatum* Core::GetDatumPointerById(unsigned int id)
{
	if (id >= *datum_pointer_table_length) return nullptr;
	return (*datum_pointer_table)[id];
}

std::uint32_t Core::register_opcode(std::string name, opcode_handler handler)
{
	std::uint32_t next_opcode = next_opcode_id++;
	opcode_handlers[next_opcode] = handler;
	name_to_opcode[name] = next_opcode;
	return next_opcode;
}

ExecutionContext* Core::get_context()
{
	return *current_execution_context_ptr;
}

ExecutionContext* Core::_get_parent_context()
{
	return *parent_context_ptr_hack;
}

Value Core::get_stack_value(unsigned int which)
{
	return (*Core::current_execution_context_ptr)->stack[(*Core::current_execution_context_ptr)->stack_size - which - 1];
}

void Core::stack_pop(unsigned int how_many)
{
	(*Core::current_execution_context_ptr)->stack_size -= how_many;
}

void Core::stack_push(Value val)
{
	(*Core::current_execution_context_ptr)->stack_size++;
	(*Core::current_execution_context_ptr)->stack[(*Core::current_execution_context_ptr)->stack_size-1] = val;
}

bool Core::enable_profiling()
{
	*some_flags_including_profile |= FLAG_PROFILE;
	return true;
}

bool Core::disable_profiling()
{
	*some_flags_including_profile &= ~FLAG_PROFILE;
	return true;
}

std::string Core::type_to_text(unsigned int type)
{
	return GetStringFromId(GetTypeById(type)->path);
}

Value Core::get_turf(int x, int y, int z)
{
	return GetTurf(x-1, y-1, z-1);
}

std::string Core::stringify(Value val)
{
	return GetStringFromId(ToString(val.type, val.value));
}

/*extern "C" __declspec(dllexport) const char* dump_codecov(int a, const char** b)
{
	std::ofstream o("codecov.txt");
	unsigned int called = 0;
	unsigned int actual_count = 0;
	for (int i = 0; i < Core::codecov_executed_procs.size(); i++)
	{
		Core::Proc& p = Core::get_proc(i);
		if (!p.name.empty() && p.name.back() != ')')
		{
			o << p.name << ": " << Core::codecov_executed_procs[i] << "\n";
			if (Core::codecov_executed_procs[i]) called++;
			actual_count++;
		}
	}
	o << "Coverage: " << (called / (float)actual_count) * 100.0f << "% (" << called << "/" << actual_count << ")\n";
	return "";
}*/

std::uint32_t Core::get_socket_from_client(unsigned int id)
{
	int str = (int)GetSocketHandleStruct(id);
	return ((Hellspawn*)(str - 0x74))->handle;
}

Value* locate_global_by_name(std::string name)
{
	unsigned int varname = Core::GetStringId(name);
	TableHolderThingy* tht = GetTableHolderThingyById(*Core::name_table_id_ptr);
	int id;
	for (id = 0; id < tht->length; id++) // add binary search here
	{
		if (Core::name_table[tht->elements[id]] == varname)
		{
			break;
		}
	}
	return &Core::global_var_table[tht->elements[id]];
}

void Core::global_direct_set(std::string name, Value val)
{
	if (auto ptr = global_direct_cache.find(name); ptr != global_direct_cache.end())
	{
		*ptr->second = val;
	}
	Value* var = locate_global_by_name(name);
	*var = val;
	global_direct_cache[name] = var;
}

Value Core::global_direct_get(std::string name)
{
	if (auto ptr = global_direct_cache.find(name); ptr != global_direct_cache.end())
	{
		return *ptr->second;
	}
	Value* var = locate_global_by_name(name);
	global_direct_cache[name] = var;
	return *var;
}


void Core::disconnect_client(unsigned int id)
{
#ifdef _WIN32
	closesocket(get_socket_from_client(id));
#else
	close(get_socket_from_client(id));
#endif
	DisconnectClient1(id, 1, 1);
	DisconnectClient2(id);
}

void Core::alert_dd(std::string msg)
{
	msg += "\n";
	PrintToDD(msg.c_str());
}

void Core::cleanup()
{
	Core::remove_all_hooks();
	Core::opcode_handlers.clear();
	Core::destroy_proc_list();
	procs_to_profile.clear();
	proc_hooks.clear();
	global_direct_cache.clear();
	clean_sockets();
	Core::initialized = false; // add proper modularization already
}
