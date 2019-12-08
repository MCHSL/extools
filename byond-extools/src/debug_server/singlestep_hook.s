.intel_syntax
.global _singlestep_hook
_singlestep_hook:
    push eax
    mov edx, _on_singlestep
    call edx
    pop eax
    MOVZX ECX, WORD PTR DS : [EAX + 0x14]
    MOV EDI, DWORD PTR DS : [EAX + 0x10]
    ret
