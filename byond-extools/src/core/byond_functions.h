#pragma once

#include "byond_structures.h"

struct variadic_arg_hack
{
	char data[1024];
};

struct BSocket;
struct DungBuilder;

#ifndef _WIN32
#define __thiscall __attribute__((thiscall))
#endif

typedef trvh(REGPARM3 *CallGlobalProcPtr)(char usr_type, int usr_value, int proc_type, unsigned int proc_id, int const_0, DataType src_type, int src_value, Value* argList, unsigned int argListLen, int const_0_2, int const_0_3);
typedef Value(*Text2PathPtr)(unsigned int text);
#ifdef _WIN32
typedef unsigned int(*GetStringTableIndexPtr)(const char* string, int handleEscapes, int duplicateString);
typedef unsigned int(*GetStringTableIndexUTF8Ptr)(const char* string, int utf8, int handleEscapes, int duplicateString);
#else
typedef unsigned int(REGPARM3 *GetStringTableIndexPtr)(const char* string, int handleEscapes, int duplicateString);
typedef unsigned int(REGPARM3 *GetStringTableIndexUTF8Ptr)(const char* string, int utf8, int handleEscapes, int duplicateString);
#endif
typedef int(*GetByondVersionPtr)();
typedef int(*GetByondBuildPtr)();
typedef void(*SetVariablePtr)(int datumType, int datumId, unsigned int varNameId, Value newvalue);
typedef trvh(*GetVariablePtr)(int datumType, int datumId, unsigned int varNameId);
#ifdef _WIN32
typedef trvh(*CallProcByNamePtr)(char usrType, char usrValue, unsigned int proc_type, unsigned int proc_name, unsigned char datumType, unsigned int datumId, Value* argList, unsigned int argListLen, int unk4, int unk5);
#else
typedef trvh(REGPARM3 *CallProcByNamePtr)(char usrType, char usrValue, unsigned int proc_type, unsigned int proc_name, unsigned char datumType, unsigned int datumId, Value* argList, unsigned int argListLen, int unk4, int unk5);
#endif
typedef IDArrayEntry* (*GetIDArrayEntryPtr)(unsigned int index);
typedef int(*ThrowDMErrorPtr)(const char* msg);
typedef ProcArrayEntry* (*GetProcArrayEntryPtr)(unsigned int index);
#ifdef _WIN32
typedef RawList* (*GetListPointerByIdPtr)(unsigned int index);
typedef void(*AppendToContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
typedef void(*RemoveFromContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
#else
typedef RawList* (REGPARM3 *GetListPointerByIdPtr)(unsigned int index);
typedef void(__attribute__((regparm(2))) *AppendToContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
typedef void(__attribute__((regparm(2))) *RemoveFromContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
#endif
typedef String* (*GetStringTableEntryPtr)(int stringId);
typedef unsigned int(*Path2TextPtr)(unsigned int pathType, unsigned int pathValue);
typedef Type* (*GetTypeByIdPtr)(unsigned int typeIndex);
typedef unsigned int* (*MobTableIndexToGlobalTableIndexPtr)(unsigned int mobTypeIndex);
#ifdef _WIN32
typedef trvh(*GetAssocElementPtr)(unsigned int listType, unsigned int listId, unsigned int keyType, unsigned int keyValue);
#else
typedef trvh(REGPARM3 *GetAssocElementPtr)(unsigned int listType, unsigned int listId, unsigned int keyType, unsigned int keyValue);
#endif
typedef void(*SetAssocElementPtr)(unsigned int listType, unsigned int listId, unsigned int keyType, unsigned int keyValue, unsigned int valueType, unsigned int valueValue);
typedef unsigned int(*CreateListPtr)(unsigned int reserveSize);
typedef trvh(*NewPtr)(Value* type, Value* args, unsigned int num_args, int unknown);
//typedef void(*TempBreakpoint)();
typedef void(*CrashProcPtr)(char* error, variadic_arg_hack hack);
//typedef SuspendedProc* (*ResumeIn)(ExecutionContext* ctx, float deciseconds);
typedef void(*SendMapsPtr)(void);
#ifdef _WIN32
typedef SuspendedProc* (*SuspendPtr)(ExecutionContext* ctx, int unknown);
typedef void(*StartTimingPtr)(SuspendedProc*);
#else
typedef SuspendedProc* (REGPARM3 *SuspendPtr)(ExecutionContext* ctx, int unknown);
typedef void(REGPARM3 *StartTimingPtr)(SuspendedProc*);
#endif
typedef ProfileInfo* (*GetProfileInfoPtr)(unsigned int proc_id);
#ifdef _WIN32
typedef void(*CreateContextPtr)(ProcConstants* constants, ExecutionContext* new_ctx);
typedef void(*ProcCleanupPtr)(ExecutionContext* thing_that_just_executed); //this one is hooked to help with extended profiling
#else
typedef void(REGPARM3 *CreateContextPtr)(void* unknown, ExecutionContext* new_ctx);
typedef void(REGPARM3 *ProcCleanupPtr)(ExecutionContext* thing_that_just_executed);
#endif
typedef void(*RuntimePtr)(char* error); //Do not call this, it relies on some global state
typedef trvh(*GetTurfPtr)(int x, int y, int z);
typedef unsigned int(*LengthPtr)(int type, int value);
typedef bool(*IsInContainerPtr)(int keyType, int keyValue, int cntType, int cntId);
typedef unsigned int(*ToStringPtr)(int type, int value);
typedef bool(*TopicFloodCheckPtr)(int socket_id);
typedef void(*PrintToDDPtr)(const char* msg);
typedef BSocket*(*GetBSocketPtr)(unsigned int id);
typedef void(*DisconnectClient1Ptr)(unsigned int id, int unknown, bool suggest_reconnect); //this and the below function must be called in tandem
typedef void(*DisconnectClient2Ptr)(unsigned int id);
typedef Hellspawn* (*GetSocketHandleStructPtr)(unsigned int id);
typedef Value(*GetGlobalByNamePtr)(unsigned int name_id);
typedef TableHolderThingy*(*GetTableHolderThingyByIdPtr)(unsigned int id);
typedef void(*IncRefCountPtr)(int type, int value);
typedef void(*DecRefCountPtr)(int type, int value);
typedef void(*DelDatumPtr)(unsigned int id);
typedef const char* (__thiscall *StdDefDMPtr)(DungBuilder* this_);

extern CrashProcPtr CrashProc;
extern StartTimingPtr StartTiming;
extern SuspendPtr Suspend;
extern SetVariablePtr SetVariable;
extern GetVariablePtr GetVariable;
extern GetStringTableIndexPtr GetStringTableIndex;
extern GetStringTableIndexUTF8Ptr GetStringTableIndexUTF8;
extern GetProcArrayEntryPtr GetProcArrayEntry;
extern GetStringTableEntryPtr GetStringTableEntry;
extern GetByondVersionPtr GetByondVersion;
extern GetByondBuildPtr GetByondBuild;
extern CallGlobalProcPtr CallGlobalProc;
extern GetProfileInfoPtr GetProfileInfo;
extern ProcCleanupPtr ProcCleanup;
extern CreateContextPtr CreateContext;
extern GetTypeByIdPtr GetTypeById;
extern MobTableIndexToGlobalTableIndexPtr MobTableIndexToGlobalTableIndex;
extern RuntimePtr Runtime;
extern GetTurfPtr GetTurf;
extern AppendToContainerPtr AppendToContainer;
extern GetAssocElementPtr GetAssocElement;
extern GetListPointerByIdPtr GetListPointerById;
extern SetAssocElementPtr SetAssocElement;
extern CreateListPtr CreateList;
extern LengthPtr Length;
extern IsInContainerPtr IsInContainer;
extern ToStringPtr ToString;
extern TopicFloodCheckPtr TopicFloodCheck;
extern PrintToDDPtr PrintToDD;
extern GetBSocketPtr GetBSocket;
extern DisconnectClient1Ptr DisconnectClient1;
extern DisconnectClient2Ptr DisconnectClient2;
extern GetSocketHandleStructPtr GetSocketHandleStruct;
extern CallProcByNamePtr CallProcByName;
extern SendMapsPtr SendMaps;
extern GetGlobalByNamePtr GetGlobalByName;
extern GetTableHolderThingyByIdPtr GetTableHolderThingyById;
extern IncRefCountPtr IncRefCount;
extern DecRefCountPtr DecRefCount;
extern StdDefDMPtr StdDefDM;
extern DelDatumPtr DelDatum;
