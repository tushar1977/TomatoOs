global gdt_flush
global tss_flush
global reload_CS
global reloadSegments
gdt_flush:
    cli
    lgdt [rdi]
    jmp reloadSegments
    sti
    ret
reloadSegments:
    push 0x08
    lea rax, [rel reload_CS]  
    push rax                
    retfq     
reload_CS:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
global tss_flush
tss_flush:
    mov ax, 0x28
    ltr ax
    ret
