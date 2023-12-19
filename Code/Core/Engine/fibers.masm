.code

save_to_fiber proc
    ; Save all of our registers into the fiber input ptr.

    mov    r8, qword ptr [rsp]
    mov    qword ptr [rcx + 8*0], r8

    lea    r9, [rsp]
    mov    qword ptr [rcx + 8*1], r9

    mov      qword ptr [rcx + 8*2], rbx
    mov      qword ptr [rcx + 8*3], rbp
    mov      qword ptr [rcx + 8*4], r12
    mov      qword ptr [rcx + 8*5], r13
    mov      qword ptr [rcx + 8*6], r14
    mov      qword ptr [rcx + 8*7], r15
    mov      qword ptr [rcx + 8*8], rdi
    mov      qword ptr [rcx + 8*9], rsi
    movups xmmword ptr [rcx + 060h], xmm6
    movups xmmword ptr [rcx + 070h], xmm7
    movups xmmword ptr [rcx + 080h], xmm8
    movups xmmword ptr [rcx + 090h], xmm9
    movups xmmword ptr [rcx + 0A0h], xmm10
    movups xmmword ptr [rcx + 0B0h], xmm11
    movups xmmword ptr [rcx + 0C0h], xmm12
    movups xmmword ptr [rcx + 0D0h], xmm13
    movups xmmword ptr [rcx + 0E0h], xmm14
    movups xmmword ptr [rcx + 0F0h], xmm15

    ; Save the TIB data to the fiber struct
    mov    r10, gs:[030h]

    ; Stack limit/stack low
    mov    rax, qword ptr [r10 + 010h]
    mov      qword ptr [rcx + 0100h], rax

    ; Stack base/stack high
    mov    rax, qword ptr [r10 + 08h]
    mov      qword ptr [rcx + 0108h], rax

    ; Fiber local storage
    mov    rax, qword ptr [r10 + 020h]
    mov      qword ptr [rcx + 0110h], rax

    ; Deallocation stack
    mov    rax, qword ptr [r10 + 01478h]
    mov      qword ptr [rcx + 0118h], rax

    ; Set yielded to true
    mov    byte ptr [rcx + 58h], 1h

    ; Restore our old rsp
    mov    rsp, qword ptr [rdx - 8h]

    ; Restore the registers we saved on the stack
    ; in restore_fiber
    mov    rbx,     qword ptr [rsp + 8*0]
    mov    rbp,     qword ptr [rsp + 8*1]
    mov    r12,     qword ptr [rsp + 8*2]
    mov    r13,     qword ptr [rsp + 8*3]
    mov    r14,     qword ptr [rsp + 8*4]
    mov    r15,     qword ptr [rsp + 8*5]
    mov    rdi,     qword ptr [rsp + 8*6]
    mov    rsi,     qword ptr [rsp + 8*7]
    movups xmm6,  xmmword ptr [rsp + 040h]
    movups xmm7,  xmmword ptr [rsp + 050h]
    movups xmm8,  xmmword ptr [rsp + 060h]
    movups xmm9,  xmmword ptr [rsp + 070h]
    movups xmm10, xmmword ptr [rsp + 080h]
    movups xmm11, xmmword ptr [rsp + 090h]
    movups xmm12, xmmword ptr [rsp + 0A0h]
    movups xmm13, xmmword ptr [rsp + 0B0h]
    movups xmm14, xmmword ptr [rsp + 0C0h]
    movups xmm15, xmmword ptr [rsp + 0D0h]

    ; TIB data
    mov    r10, gs:[030h]
    ; Fiber local storage
    mov    rax,     qword ptr [rsp + 0E0h]
    mov    qword ptr [r10 + 020h], rax
    ; Stack limit/stack low
    mov    rax,     qword ptr [rsp + 0E8h]
    mov    qword ptr [r10 + 010h], rax
    ; Stack base/stack high
    mov    rax,     qword ptr [rsp + 0F0h]
    mov    qword ptr [r10 + 08h], rax
    ; Deallocation stack
    mov    rax,     qword ptr [rsp + 0F8h]
    mov    qword ptr [r10 + 01478h], rax

    ; Pop everything off the stack since we don't
    ; need them anymore.
    add    rsp, 0100h

    ; At this point, this is the old stack
    ; so the return address _should_ be correct
    ; and ret will return us to the original call
    ; point of restore_fiber
    ret

save_to_fiber endp

resume_fiber proc
    ; Save current registers
    sub    rsp, 0100h

    mov      qword ptr [rsp + 8*0], rbx
    mov      qword ptr [rsp + 8*1], rbp
    mov      qword ptr [rsp + 8*2], r12
    mov      qword ptr [rsp + 8*3], r13
    mov      qword ptr [rsp + 8*4], r14
    mov      qword ptr [rsp + 8*5], r15
    mov      qword ptr [rsp + 8*6], rdi
    mov      qword ptr [rsp + 8*7], rsi

    movups xmmword ptr [rsp + 040h], xmm6
    movups xmmword ptr [rsp + 050h], xmm7
    movups xmmword ptr [rsp + 060h], xmm8
    movups xmmword ptr [rsp + 070h], xmm9
    movups xmmword ptr [rsp + 080h], xmm10
    movups xmmword ptr [rsp + 090h], xmm11
    movups xmmword ptr [rsp + 0A0h], xmm12
    movups xmmword ptr [rsp + 0B0h], xmm13
    movups xmmword ptr [rsp + 0C0h], xmm14
    movups xmmword ptr [rsp + 0D0h], xmm15

    ; TIB data
    mov    r10, gs:[030h]
    ; Fiber local storage
    mov    rax, qword ptr [r10 + 020h]
    mov      qword ptr [rsp + 0E0h], rax

    ; Stack limit/stack low
    mov    rax, qword ptr [r10 + 010h]
    mov      qword ptr [rsp + 0E8h], rax

    ; Stack base/stack high
    mov    rax, qword ptr [r10 + 08h]
    mov      qword ptr [rsp + 0F0h], rax

    ; Deallocation stack
    mov    rax, qword ptr [r10 + 01478h]
    mov      qword ptr [rsp + 0F8h], rax


    ; Write our new rsp into the fiber's stack so that
    ; when it unwinds it can resume at the correct location
    mov    qword ptr [rdx - 8h], rsp

    ; Change rsp to our new stack for the fiber.
    mov    rsp, qword ptr [rcx + 8]
    mov    byte ptr [rcx + 58h], 0h

    ; Restore the fiber registers
    mov    rbx,     qword ptr [rcx + 8*2]
    mov    rbp,     qword ptr [rcx + 8*3]
    mov    r12,     qword ptr [rcx + 8*4]
    mov    r13,     qword ptr [rcx + 8*5]
    mov    r14,     qword ptr [rcx + 8*6]
    mov    r15,     qword ptr [rcx + 8*7]
    mov    rdi,     qword ptr [rcx + 8*8]
    mov    rsi,     qword ptr [rcx + 8*9]
    movups xmm6,  xmmword ptr [rcx + 060h]
    movups xmm7,  xmmword ptr [rcx + 070h]
    movups xmm8,  xmmword ptr [rcx + 080h]
    movups xmm9,  xmmword ptr [rcx + 090h]
    movups xmm10, xmmword ptr [rcx + 0A0h]
    movups xmm11, xmmword ptr [rcx + 0B0h]
    movups xmm12, xmmword ptr [rcx + 0C0h]
    movups xmm13, xmmword ptr [rcx + 0D0h]
    movups xmm14, xmmword ptr [rcx + 0E0h]
    movups xmm15, xmmword ptr [rcx + 0F0h]

    ; Restore the TIB data from the fiber struct
    mov    r10, gs:[030h]
    ; Stack limit/stack low
    mov    rax,     qword ptr [rcx + 0100h]
    mov    qword ptr [r10 + 010h], rax

    ; Stack base/stack high
    mov    rax,     qword ptr [rcx + 0108h]
    mov    qword ptr [r10 + 08h], rax

    ; Fiber local storage
    mov    rax,     qword ptr [rcx + 0110h]
    mov    qword ptr [r10 + 020h], rax

    ; Deallocation stack
    mov    rax,     qword ptr [rcx + 0118h]
    mov    qword ptr [r10 + 01478h], rax

    ret
resume_fiber endp

launch_fiber proc
    ; Save current registers
    sub    rsp, 0100h

    ; Save current registers into _real_ rsp
    mov      qword ptr [rsp + 8*0], rbx
    mov      qword ptr [rsp + 8*1], rbp
    mov      qword ptr [rsp + 8*2], r12
    mov      qword ptr [rsp + 8*3], r13
    mov      qword ptr [rsp + 8*4], r14
    mov      qword ptr [rsp + 8*5], r15
    mov      qword ptr [rsp + 8*6], rdi
    mov      qword ptr [rsp + 8*7], rsi
    movups xmmword ptr [rsp + 040h], xmm6
    movups xmmword ptr [rsp + 050h], xmm7
    movups xmmword ptr [rsp + 060h], xmm8
    movups xmmword ptr [rsp + 070h], xmm9
    movups xmmword ptr [rsp + 080h], xmm10
    movups xmmword ptr [rsp + 090h], xmm11
    movups xmmword ptr [rsp + 0A0h], xmm12
    movups xmmword ptr [rsp + 0B0h], xmm13
    movups xmmword ptr [rsp + 0C0h], xmm14
    movups xmmword ptr [rsp + 0D0h], xmm15

    ; The TIB data also needs to be saved
    mov    r10, gs:[030h]
    ; Fiber local storage
    mov    rax, qword ptr [r10 + 020h]
    mov      qword ptr [rsp + 0E0h], rax
    ; Stack limit/stack low
    mov    rax, qword ptr [r10 + 010h]
    mov      qword ptr [rsp + 0E8h], rax
    ; Stack base/stack high
    mov    rax, qword ptr [r10 + 08h]
    mov      qword ptr [rsp + 0F0h], rax
    ; Deallocation stack
    mov    rax, qword ptr [r10 + 01478h]
    mov      qword ptr [rsp + 0F8h], rax

    ; Set yield param to 0
    mov    byte ptr [rcx + 58h], 0h

    ; Get relevant info from fiber and put
    ; in registers.
    mov    r8, qword ptr [rcx + 8*0]
    mov    r9, rsp

    ; Change rsp to our new stack for the fiber.
    mov    rsp, qword ptr [rcx + 8*1]
    ; We need 8 bytes for the old rsp and 32 more bytes
    ; for the mandatory shadow space. Then, in order to
    ; Adhere to x64's 16 byte rsp alignment requirement,
    ; there are an additional 8 bytes added, in total
    ; 48 bytes, or 0x30
    sub    rsp, 30h
    ; Put the old rsp at the highest address
    mov    [rsp + 28h], r9

    mov    rbx,     qword ptr [rcx + 8*2]
    mov    rbp,     qword ptr [rcx + 8*3]
    mov    r12,     qword ptr [rcx + 8*4]
    mov    r13,     qword ptr [rcx + 8*5]
    mov    r14,     qword ptr [rcx + 8*6]
    mov    r15,     qword ptr [rcx + 8*7]
    mov    rdi,     qword ptr [rcx + 8*8]
    mov    rsi,     qword ptr [rcx + 8*9]

    movups xmm6,  xmmword ptr [rcx + 060h]
    movups xmm7,  xmmword ptr [rcx + 070h]
    movups xmm8,  xmmword ptr [rcx + 080h]
    movups xmm9,  xmmword ptr [rcx + 090h]
    movups xmm10, xmmword ptr [rcx + 0A0h]
    movups xmm11, xmmword ptr [rcx + 0B0h]
    movups xmm12, xmmword ptr [rcx + 0C0h]
    movups xmm13, xmmword ptr [rcx + 0D0h]
    movups xmm14, xmmword ptr [rcx + 0E0h]
    movups xmm15, xmmword ptr [rcx + 0F0h]

    ; Restore TIB data from fiber struct
    mov    r10, gs:[030h]
    ; Stack limit/stack low
    mov    rax,     qword ptr [rcx + 0100h]
    mov    qword ptr [r10 + 010h], rax
    ; Stack base/stack high
    mov    rax,     qword ptr [rcx + 0108h]
    mov    qword ptr [r10 + 08h], rax
    ; Fiber local storage
    mov    rax,     qword ptr [rcx + 0110h]
    mov    qword ptr [r10 + 020h], rax
    ; Deallocation stack
    mov    rax,     qword ptr [rcx + 0118h]
    mov    qword ptr [r10 + 01478h], rax

    ; The rcx parameter can now be set
    mov    rcx, qword ptr [rcx + 8*10]

    ; Indirect call to our actual entry-point
    call   r8

    ; Restore our old rsp
    mov    rsp, [rsp + 28h]

    ; Restore the registers we saved on the stack
    ; in restore_fiber
    mov    rbx,     qword ptr [rsp + 8*0]
    mov    rbp,     qword ptr [rsp + 8*1]
    mov    r12,     qword ptr [rsp + 8*2]
    mov    r13,     qword ptr [rsp + 8*3]
    mov    r14,     qword ptr [rsp + 8*4]
    mov    r15,     qword ptr [rsp + 8*5]
    mov    rdi,     qword ptr [rsp + 8*6]
    mov    rsi,     qword ptr [rsp + 8*7]

    movups xmm6,  xmmword ptr [rsp + 040h]
    movups xmm7,  xmmword ptr [rsp + 050h]
    movups xmm8,  xmmword ptr [rsp + 060h]
    movups xmm9,  xmmword ptr [rsp + 070h]
    movups xmm10, xmmword ptr [rsp + 080h]
    movups xmm11, xmmword ptr [rsp + 090h]
    movups xmm12, xmmword ptr [rsp + 0A0h]
    movups xmm13, xmmword ptr [rsp + 0B0h]
    movups xmm14, xmmword ptr [rsp + 0C0h]
    movups xmm15, xmmword ptr [rsp + 0D0h]

    ; TIB data
    mov    r10, gs:[030h]
    ; Fiber local storage
    mov    rax,     qword ptr [rsp + 0E0h]
    mov    qword ptr [r10 + 020h], rax
    ; Stack limit/stack low
    mov    rax,     qword ptr [rsp + 0E8h]
    mov    qword ptr [r10 + 010h], rax
    ; Stack base/stack high
    mov    rax,     qword ptr [rsp + 0F0h]
    mov    qword ptr [r10 + 08h], rax
    ; Deallocation stack
    mov    rax,     qword ptr [rsp + 0F8h]
    mov    qword ptr [r10 + 01478h], rax

    ; Pop everything off the stack since we don't
    ; need them anymore.
    add    rsp, 0100h

    ; At this point, this is the old stack
    ; so the return address _should_ be correct
    ; and ret will return us to the original call
    ; point of restore_fiber
    ret

launch_fiber endp

get_rsp proc
    mov   rax, rsp
    ret
get_rsp endp
end