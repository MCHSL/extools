#include "core.h"
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <fstream>

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
		return Core::FAIL;
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
	return Core::SUCCESS;
}
