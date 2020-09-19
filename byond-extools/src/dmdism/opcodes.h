#pragma once

#include <unordered_map>
#include "../core/byond_constants.h"

enum class Bytecode : uint32_t
{
    UNK = 0xFFFFFFFF,
#define I(NUMBER, NAME, DIS) \
    NAME = NUMBER,
#include "opcodes_table.inl"
#undef I
};

enum class AccessModifier : std::uint32_t
{
	SRC = 0xFFCE,
	ARGS = 0xFFCF,
	DOT = 0xFFD0,
	CACHE = 0xFFD8,
	ARG = 0xFFD9,
	LOCAL = 0xFFDA,
	GLOBAL = 0xFFDB,
	SUBVAR = 0xFFDC,

	SRC_PROC_SPEC = 0xFFDD,
	SRC_PROC = 0xFFDE,
	PROC = 0xFFDF,
	PROC_NO_RET = 0xFFE0,

	WORLD = 0xFFE5,
	NULL_ = 0xFFE6,
};

const std::unordered_map<AccessModifier, std::string> modifier_names = {
	{AccessModifier::SRC, "SRC"},
	{AccessModifier::DOT, "DOT" },
	{AccessModifier::ARG, "ARG" },
	{AccessModifier::ARGS, "ARGS"},
	{AccessModifier::LOCAL, "LOCAL"},
	{AccessModifier::GLOBAL, "GLOBAL"},
	{AccessModifier::SUBVAR, "SUBVAR"},
	{AccessModifier::CACHE, "CACHE"},

	{AccessModifier::SRC_PROC_SPEC, "SRC_PROC_SPEC"},
	{AccessModifier::SRC_PROC, "SRC_PROC"},
	{AccessModifier::PROC, "PROC"},
	{AccessModifier::PROC_NO_RET, "PROC_NO_RET"},

	{AccessModifier::WORLD, "WORLD"},
	{AccessModifier::NULL_, "NULL"},
};

const std::unordered_map<DataType, std::string> datatype_names = {
	{ DataType::NULL_D, "NULL" },
	{ DataType::TURF, "TURF" },
	{ DataType::OBJ, "OBJ" },
	{ DataType::MOB, "MOB" },
	{ DataType::AREA, "AREA" },
	{ DataType::CLIENT, "CLIENT" },
	{ DataType::STRING, "STRING" },
	{ DataType::MOB_TYPEPATH, "MOB_TYPEPATH" },
	{ DataType::OBJ_TYPEPATH, "OBJ_TYPEPATH" },
	{ DataType::TURF_TYPEPATH, "TURF_TYPEPATH" },
	{ DataType::AREA_TYPEPATH, "AREA_TYPEPATH" },
	{ DataType::RESOURCE, "RESOURCE"},
	{ DataType::IMAGE, "IMAGE" },
	{ DataType::WORLD_D, "WORLD" },
	{ DataType::DATUM, "DATUM" },
	{ DataType::SAVEFILE, "SAVEFILE" },
	{ DataType::LIST_TYPEPATH, "LIST_TYPEPATH" },
	{ DataType::NUMBER, "NUMBER" },
	{ DataType::CLIENT_TYPEPATH, "CLIENT_TYPEPATH" },
	{ DataType::LIST, "LIST" },
	{ DataType::LIST_ARGS, "LIST_ARGS" },
	{ DataType::LIST_VERBS, "LIST_VERBS" },
	{ DataType::LIST_CONTENTS, "LIST_CONTENTS" },
	{ DataType::DATUM_TYPEPATH, "DATUM_TYPEPATH" },
	{ DataType::LIST_TURF_CONTENTS, "LIST_TURF_CONTENTS" },
};

const char* const get_mnemonic(Bytecode bytecode);

class Instruction;
class Context;
class Disassembler;
typedef void (*DisassembleCallback)(Instruction*, Context*, Disassembler*);

DisassembleCallback get_disassemble_callback(std::uint32_t opcode);
