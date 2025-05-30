global divideException
global debugException
global breakpointException
global overflowException
global boundRangeExceededException
global invalidOpcodeException
global deviceNotAvaliableException
global doubleFaultException
global coprocessorSegmentOverrunException
global invalidTSSException
global segmentNotPresentException
global stackSegmentFaultException
global generalProtectionFaultException
global pageFaultException
global floatingPointException
global alignmentCheckException
global machineCheckException
global simdFloatingPointException
global virtualisationException
global irq1
global idt_flush
idt_flush:
    cli                 ; Disable interrupts
    lidt [rdi]         ; Load the new IDT from the address in RDI
    sti                 ; Re-enable interrupts
    ret                 ; Return

divideException:
    push 0
    push 0
    jmp baseHandler

debugException:
    push 0
    push 1
    jmp baseHandler

breakpointException:
    push 0
    push 3
    jmp baseHandler

overflowException:
    push 0
    push 4
    jmp baseHandler

boundRangeExceededException:
    push 0
    push 5
    jmp baseHandler

invalidOpcodeException:
    push 0
    push 6
    jmp baseHandler

deviceNotAvaliableException:
    push 0
    push 7
    jmp baseHandler

doubleFaultException:
    push 8
    jmp baseHandler

coprocessorSegmentOverrunException:
    push 0
    push 9
    jmp baseHandler

invalidTSSException:
    push 10
    jmp baseHandler

segmentNotPresentException:
    push 11
    jmp baseHandler

stackSegmentFaultException:
    push 12
    jmp baseHandler

generalProtectionFaultException:
    push 13
    jmp baseHandler

pageFaultException:
    push 14
    jmp baseHandler

floatingPointException:
    push 0
    push 16
    jmp baseHandler

alignmentCheckException:
    push 17
    jmp baseHandler

machineCheckException:
    push 0
    push 18
    jmp baseHandler

simdFloatingPointException:
    push 0
    push 19
    jmp baseHandler

virtualisationException:
    push 0
    push 20
    jmp baseHandler
irq1:
    push 0
    push 33
    jmp baseHandler

extern exception_handler
align 8
baseHandler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp
    call exception_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq
