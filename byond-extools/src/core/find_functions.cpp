#include "find_functions.h"

#ifdef _WIN32
#define BYONDCORE "byondcore.dll"
#else
#define BYONDCORE "libbyond"
#endif

#define FIND_OR_DIE(name, sig) name = (name##Ptr)Pocket::Sigscan::FindPattern(BYONDCORE, sig); if(!name) { Core::Alert("Failed to locate " #name); return false; }
#ifdef _WIN32
#define IMPORT_OR_DIE(name, sig) name = (name##Ptr)GetProcAddress(LoadLibraryA(BYONDCORE), sig); if(!name) { Core::Alert("Failed to locate " #name " via " #sig); return false; }
#else
#define IMPORT_OR_DIE(name, sig) name = (name##Ptr)dlsym(dlopen(BYONDCORE, 0), sig); if(!name) { Core::Alert("Failed to locate " #name " via " #sig); return false; }
#endif

bool Core::find_functions()
{	
#ifdef _WIN32
	IMPORT_OR_DIE(GetByondVersion, "?GetByondVersion@ByondLib@@QAEJXZ");
	IMPORT_OR_DIE(GetByondBuild, "?GetByondBuild@ByondLib@@QAEJXZ");
	ByondVersion = GetByondVersion();
	ByondBuild = GetByondBuild();
	FIND_OR_DIE(Suspend, "55 8B EC 53 56 57 8B 7D 08 57 E8 ?? ?? ?? ?? 8B 1F 8B F0 8A 4F 63 83 C4 04 8B 56 18 88 4A 63 8B 4B 20 89 4E 20 8B 43 24 89 46 24 8B 45 0C C6 47 63 00 C7 43 ?? ?? ?? ?? ?? C7 43 ?? ?? ?? ?? ?? 8B 4E 18 89 41 04 F6 43 04 10");
	FIND_OR_DIE(StartTiming, "55 8B EC 56 8B 75 08 56 80 4E 04 04 E8 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 83 C4 04 3B ?? ?? ?? ?? ?? 73 0A A1 ?? ?? ?? ?? 8B 04 88 EB 02 33 C0 3B F0");
	FIND_OR_DIE(SetVariable, "55 8B EC 8B 4D 08 0F B6 C1 48 57 8B 7D 10 83 F8 53 0F ?? ?? ?? ?? ?? 0F B6 80 ?? ?? ?? ?? FF 24 85 ?? ?? ?? ?? FF 75 18 FF 75 14 57 FF 75 0C E8 ?? ?? ?? ?? 83 C4 10 5F 5D C3");
	FIND_OR_DIE(GetProcArrayEntry, "55 8B EC 8B 45 08 3B 05 ?? ?? ?? ?? 72 04 33 C0 5D C3 8D 0C C0 A1 ?? ?? ?? ?? 8D 04 88 5D C3");
	FIND_OR_DIE(GetStringTableEntry, "55 8B EC 8B 4D 08 3B 0D ?? ?? ?? ?? 73 10 A1");
	switch(ByondVersion) {
		case 512:
			FIND_OR_DIE(CrashProc, "55 8B EC 6A FF 68 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 50 A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 ?? ?? ?? ?? ?? ?? A1 ?? ?? ?? ?? A8 01 75 2D 83 C8 01 A3 ?? ?? ?? ?? B9 ?? ?? ?? ?? C7 45 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 04 C7 45 ?? ?? ?? ?? ?? B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8D 45 0C B9 ?? ?? ?? ?? 50 FF 75 08 E8 ?? ?? ?? ?? B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? FF 35 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 04 8B 4D F4 ?? ?? ?? ?? ?? ?? ?? 59 8B E5 5D C3");
			FIND_OR_DIE(GetVariable, "55 8B EC 8B 4D 08 0F B6 C1 48 83 F8 53 0F 87 F1 00 00 00 0F B6 80 ?? ?? ?? ?? FF 24 85 ?? ?? ?? ?? FF 75 10 FF 75 0C E8 ?? ?? ?? ?? 83 C4 08 5D C3");
			FIND_OR_DIE(GetStringTableIndex, "55 8B EC 8B 45 08 83 EC 18 53 8B 1D ?? ?? ?? ?? 56 57 85 C0 75 ?? 68 ?? ?? ?? ?? FF D3 83 C4 04 C6 45 10 00 80 7D 0C 00 89 45 E8 74 ?? 8D 45 10 50 8D 45 E8 50");
			break;
		case 513:
			FIND_OR_DIE(CrashProc, ""); // oh god
			FIND_OR_DIE(GetVariable, "55 8B EC 8B 4D ?? 0F B6 C1 48 83 F8 ?? 0F 87 ?? ?? ?? ?? 0F B6 80 ?? ?? ?? ?? FF 24 85 ?? ?? ?? ?? FF 75 ?? FF 75 ?? E8 ?? ?? ?? ??");
			FIND_OR_DIE(GetStringTableIndex, "55 8B EC 8B 45 ?? 83 EC ?? 53 56 8B 35 ?? ?? ?? ??");
			break;
		default: break;
	}
	current_execution_context_ptr = *(ExecutionContext * **)Pocket::Sigscan::FindPattern(BYONDCORE, "A1 ?? ?? ?? ?? 8D ?? ?? ?? ?? ?? 83 C4 08 89 48 28 8D ?? ?? ?? ?? ?? 89 48 2C 83 3D ?? ?? ?? ?? ?? 74 25 8B 00 FF 30 E8 ?? ?? ?? ?? 83 C4 04 FF 30 E8 ?? ?? ?? ?? 83 C4 04 FF 30 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 08 66 ?? ?? ?? ?? ?? ?? A1 ?? ?? ?? ?? 75 28 A8 02 75 24 E8 ?? ?? ?? ?? 85 C0 75 09 50", 1);
	proc_setup_table = **(ProcSetupEntry * ***)Pocket::Sigscan::FindPattern(BYONDCORE, "A1 ?? ?? ?? ?? FF 34 B8 FF D6 47 83 C4 04 3B ?? ?? ?? ?? ?? 72 EA FF 35 ?? ?? ?? ?? FF D6 33 FF 83 C4 04 39 ?? ?? ?? ?? ?? 76 1E", 1);
#else
	IMPORT_OR_DIE(GetByondVersion, "_ZN8ByondLib15GetByondVersionEv");
	IMPORT_OR_DIE(GetByondBuild, "_ZN8ByondLib13GetByondBuildEv");
	ByondVersion = GetByondVersion();
	ByondBuild = GetByondBuild();
	FIND_OR_DIE(CrashProc, 	"55 89 E5 53 83 EC ?? 80 3D ?? ?? ?? ?? ?? 75 ?? C7 04 24 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 ?? C7 04 24 ?? ?? ?? ?? 8D 5D ?? E8 ?? ?? ?? ?? 8B 45 ?? 89 5C 24 ?? C7 04 24 ?? ?? ?? ?? 89 44 24 ?? E8 ?? ?? ?? ?? C7 04 24 ?? ?? ?? ??");
	FIND_OR_DIE(Suspend, "55 89 E5 57 31 FF 56 89 C6 53 83 EC ?? 89 F0"); //regparm3
	FIND_OR_DIE(StartTiming, "55 89 E5 83 EC ?? 85 C0 89 5D ?? 89 C3 89 75 ?? 89 7D ?? 74 ?? 8B 50 ??"); //regparm3
	FIND_OR_DIE(SetVariable, "55 89 E5 ?? EC ?? ?? ?? ?? 89 75 ?? 8B 55 ?? 8B 75 ??");
	FIND_OR_DIE(GetVariable, "55 89 E5 81 EC ?? ?? ?? ?? 8B 55 ?? 89 5D ?? 8B 5D ?? 89 75 ?? 8B 75 ??");
	FIND_OR_DIE(GetProcArrayEntry, "55 31 C0 89 E5 8B 55 ?? 39 15 ?? ?? ?? ?? 76 ?? 8D 04 D2");
	FIND_OR_DIE(GetStringTableEntry, "55 89 E5 83 EC ?? 8B 45 ?? 39 05 ?? ?? ?? ??");
	switch(ByondVersion) {
		case 512:
			FIND_OR_DIE(GetStringTableIndex, "55 89 E5 57 56 53 89 D3 83 EC ?? 85 C0"); // regparm3
			break;
		case 513:
			FIND_OR_DIE(GetStringTableIndex, "55 8B EC 8B 45 ? 83 EC ? 53 56 8B 35 ? ? ? ?"); // regparm3
			break;
		default: break;
	}
	current_execution_context_ptr = *(ExecutionContext * **)Pocket::Sigscan::FindPattern(BYONDCORE, "A1 ?? ?? ?? ?? 8D 7D ?? 89 78 ??", 1);
	proc_setup_table = **(ProcSetupEntry * ***)Pocket::Sigscan::FindPattern(BYONDCORE, "A1 ?? ?? ?? ?? 8B 04 98 85 C0 74 ?? 89 04 24 E8 ?? ?? ?? ?? 8B 15 ?? ?? ?? ??", 1);
#endif
	return true;
}