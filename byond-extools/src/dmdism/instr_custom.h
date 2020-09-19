class Instruction;
class Context;
class Disassembler;

void dis_custom_output_format(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_call(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_callglob(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_pushval(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_switch(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_dbg_file(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_dbg_lineno(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_isinlist(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_call_global_arglist(Instruction* instruction, Context* context, Disassembler* dism);
void dis_custom_pick_switch(Instruction* instruction, Context* context, Disassembler* dism);
