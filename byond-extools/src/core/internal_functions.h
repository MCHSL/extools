#pragma once

#include "byond_structures.h"

typedef trvh(__cdecl *CallGlobalProcPtr)(char usr_type, int usr_value, int proc_type, unsigned int proc_id, int const_0, char src_type, int src_value, Value* argList, unsigned int argListLen, int const_0_2, int const_0_3);
typedef Value(*Text2PathPtr)(unsigned int text);
#ifdef _WIN32
typedef unsigned int(*GetStringTableIndexPtr)(const char* string, int handleEscapes, int duplicateString);
#else
typedef unsigned int(*GetStringTableIndexPtr)(const char* string, int handleEscapes, int duplicateString) __attribute__((regparm(3)));
#endif
typedef int(*GetByondVersionPtr)();
typedef int(*GetByondBuildPtr)();
typedef void(*SetVariablePtr)(int datumType, int datumId, unsigned int varNameId, Value newvalue);
typedef trvh(*GetVariablePtr)(int datumType, int datumId, unsigned int varNameId);
typedef Value(*CallProcPtr)(int unk1, int unk2, unsigned int proc_type, unsigned int proc_name, unsigned char datumType, unsigned int datumId, Value* argList, unsigned int argListLen, int unk4, int unk5);
typedef IDArrayEntry* (*GetIDArrayEntryPtr)(unsigned int index);
typedef int(*ThrowDMErrorPtr)(const char* msg);
typedef ProcArrayEntry* (*GetProcArrayEntryPtr)(unsigned int index);
typedef List* (*GetListArrayEntryPtr)(unsigned int index);
typedef void(*AppendToContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
typedef void(*RemoveFromContainerPtr)(unsigned char containerType, int containerValue, unsigned char valueType, int newValue);
typedef String* (*GetStringTableEntryPtr)(int stringId);
typedef unsigned int(*Path2TextPtr)(unsigned int pathType, unsigned int pathValue);
typedef Type* (*GetTypeTableIndexPtr)(unsigned int typeIndex);
typedef unsigned int* (*MobTableIndexToGlobalTableIndexPtr)(unsigned int mobTypeIndex);
typedef Value(*GetAssocElementPtr)(unsigned int listType, unsigned int listId, unsigned int keyType, unsigned int keyValue);
typedef void(*SetAssocElementPtr)(unsigned int listType, unsigned int listId, unsigned int keyType, unsigned int keyValue, unsigned int valueType, unsigned int valueValue);
typedef unsigned int(*CreateListPtr)(unsigned int reserveSize);
typedef Value(*NewPtr)(Value* type, Value* args, unsigned int num_args, int unknown);
//typedef void(*TempBreakpoint)();
typedef void(*CrashProcPtr)(char* error, int argument);
//typedef SuspendedProc* (*ResumeIn)(ExecutionContext* ctx, float deciseconds);
typedef void(*SendMapsPtr)(void);
#ifdef _WIN32
typedef SuspendedProc* (*SuspendPtr)(ExecutionContext* ctx, int unknown);
typedef void(*StartTimingPtr)(SuspendedProc*);
#else
typedef SuspendedProc* (*SuspendPtr)(ExecutionContext* ctx) __attribute__((regparm(3)));
typedef void(*StartTimingPtr)(SuspendedProc*) __attribute__((regparm(3)));
#endif
typedef ProfileInfo* (*GetProfileInfoPtr)(unsigned int proc_id);

extern CrashProcPtr CrashProc;
extern StartTimingPtr StartTiming;
extern SuspendPtr Suspend;
extern SetVariablePtr SetVariable;
extern GetVariablePtr GetVariable;
extern GetStringTableIndexPtr GetStringTableIndex;
extern GetProcArrayEntryPtr GetProcArrayEntry;
extern GetStringTableEntryPtr GetStringTableEntry;
extern GetByondVersionPtr GetByondVersion;
extern GetByondBuildPtr GetByondBuild;
extern CallGlobalProcPtr CallGlobalProc;
extern GetProfileInfoPtr GetProfileInfo;
