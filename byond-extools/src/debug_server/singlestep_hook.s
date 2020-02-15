.intel_syntax noprefix  
.global _singlestep_hook
_singlestep_hook:

    // If you modify this, modify the inline assembly in debug_server.cpp as well.
    push eax
    mov edx, offset _on_singlestep
    call edx
    pop eax
    MOVZX ECX, WORD PTR DS : [EAX + 0x14]
    MOV EDI, DWORD PTR DS : [EAX + 0x10]
    ret
