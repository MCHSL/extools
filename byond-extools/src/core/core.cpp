#include "core.h"
#include "find_functions.h"
#include "../extended_profiling/extended_profiling.h"
#include "socket/socket.h"
#include <fstream>
#include <unordered_set>
#include <chrono>

ExecutionContext** Core::current_execution_context_ptr;
ExecutionContext** Core::parent_context_ptr_hack;
ProcSetupEntry** Core::proc_setup_table;

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

Core::ManagedString::ManagedString(unsigned int id) : string_id(id)
{
	string_entry = GetStringTableEntry(string_id);
	string_entry->refcount++;
}

Core::ManagedString::ManagedString(std::string str)
{
	string_id = GetStringId(str);
	string_entry = GetStringTableEntry(string_id);
	string_entry->refcount++;
}

Core::ManagedString::ManagedString(const ManagedString& other)
{
	string_id = other.string_id;
	string_entry = other.string_entry;
	string_entry->refcount++;
}

Core::ManagedString::~ManagedString()
{
	string_entry->refcount--;
}

Core::ManagedString Core::GetManagedString(std::string str)
{
	return ManagedString(str);
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
			return GetStringTableIndexUTF8(str.c_str(), 0, 0, 1);
		default: break;
	}
	return 0;
}

std::string Core::GetStringFromId(unsigned int id)
{
	return GetStringTableEntry(id)->stringData;
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

static const char* good = Core::SUCCESS;
static const char* bad = Core::FAIL;

extern "C" EXPORT const char* enable_profiling(int n_args, const char** args)
{
	Core::initialize() && Core::enable_profiling();
	return good;
}

extern "C" EXPORT const char* disable_profiling(int n_args, const char** args)
{
	Core::initialize() && Core::disable_profiling();
	return good;
}

extern "C" EXPORT const char* core_initialize(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		Core::Alert("Core init failed!");
		return bad;
	}
	//optimizer_initialize();
#ifdef _WIN32 // i ain't fixing this awful Linux situation anytime soon
	//extended_profiling_initialize();
#endif
	return good;
}

void init_testing();
void run_tests();
extern "C" EXPORT const char* run_tests(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return bad;
	}
	init_testing();
	run_tests();
	return good;
}

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
	if (global_direct_cache.find(name) != global_direct_cache.end())
	{
		*global_direct_cache[name] = val;
		return;
	}
	Value* var = locate_global_by_name(name);
	*var = val;
	global_direct_cache[name] = var;
}

Value Core::global_direct_get(std::string name)
{
	if (global_direct_cache.find(name) != global_direct_cache.end())
	{
		return *global_direct_cache[name];
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
	Core::initialized = false; // add proper modularization already
}

extern "C" EXPORT const char* cleanup(int n_args, const char** args)
{
	Core::cleanup();
	return good;
}

std::unordered_set<std::string> blacklist;
std::unordered_set<std::string> whitelist;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_packets;
Core::Proc* ban_callback;

bool flood_topic_filter(BSocket* socket, int socket_id)
{
	std::string addr = socket->addr();
	if (blacklist.find(addr) != blacklist.end())
	{
		Core::disconnect_client(socket_id);
		return false;
	}
	if (addr == "127.0.0.1") //this can be optimized further but whatever
	{
		return true;
	}
	if (whitelist.find(addr) != whitelist.end())
	{
		return true;
	}
	auto now = std::chrono::steady_clock::now();
	if (last_packets.find(addr) == last_packets.end())
	{
		last_packets[addr] = now;
		return true;
	}
	auto last = last_packets[addr];
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() < 50)
	{
		Core::alert_dd("Blacklisting " + addr + " for this session.");
		blacklist.emplace(addr);
		last_packets.erase(addr);
		Core::disconnect_client(socket_id);
		if (ban_callback)
		{
			ban_callback->call({ addr });
		}
		return false;
	}
	last_packets[addr] = now;
	return true;
}

void read_filter_config(std::string filename, std::unordered_set<std::string>& set)
{
	std::ifstream conf("config/extools/" + filename);
	if (!conf.is_open())
	{
		Core::alert_dd("Failed to open config/extools/" + filename);
		return;
	}
	std::string line;
	while (std::getline(conf, line))
	{
		line.erase(line.find_last_not_of(" \t") + 1);
		line.erase(0, line.find_first_not_of(" \t"));
		Core::alert_dd("Read " + line + " from " + filename);
		set.emplace(line);
	}
}

extern "C" EXPORT const char* install_flood_topic_filter(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return bad;
	}
	ban_callback = nullptr;
	if (n_args == 1)
	{
		ban_callback = Core::try_get_proc(args[0]);
	}
	Core::alert_dd("Installing flood topic filter");
	whitelist.clear();
	blacklist.clear();
	last_packets.clear();
	read_filter_config("blacklist.txt", blacklist);
	read_filter_config("whitelist.txt", whitelist);
	Core::set_topic_filter(flood_topic_filter);
	return good;
}