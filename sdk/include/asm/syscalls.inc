
#ifdef _M_ARM

#include <ksarm.h>

    GBLA SyscallId

SyscallId SETA 0

	MACRO
	STUB_K $Name
	LCLS ZwFuncName
	LCLS ZwFuncEndName
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$ZwFuncEndName
    MEND

	MACRO
	STUB_U $Name
	LCLS NtFuncName
	LCLS NtFuncEndName
	LCLS ZwFuncName
	LCLS ZwFuncEndName
NtFuncName    SETS "Nt$Name"
NtFuncEndName SETS "$NtFuncName":CC:"_end"
ZwFuncName    SETS "Zw$Name"
ZwFuncEndName SETS "$ZwFuncName":CC:"_end"
	ALIGN 2
	EXPORT $NtFuncName [FUNC]
$NtFuncName
	EXPORT $ZwFuncName [FUNC]
$ZwFuncName
	ROUT
    mov r12, #SyscallId
    svc #1
    bx lr
$NtFuncEndName
$ZwFuncEndName
    MEND

#else

#include <asm.inc>

SyscallId = 0

#ifdef _M_IX86
#define KUSER_SHARED_SYSCALL HEX(7ffe0300)
#define KGDT_R0_CODE 8
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    StackBytes = 4 * ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    mov edx, KUSER_SHARED_SYSCALL
    call dword ptr [edx]
    ret StackBytes
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    StackBytes = 4 * &ArgCount
    FPO 0, 0, 0, 0, 0, FRAME_FPO
    mov eax, SyscallId
    lea edx, [esp + 4]
    pushfd
    push KGDT_R0_CODE
    call _KiSystemService
    ret StackBytes
ENDM
#elif defined(_M_AMD64)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    .ENDPROLOG
    mov eax, SyscallId
    mov r10, rcx
    syscall
    ret
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    .ENDPROLOG
    EXTERN Nt&Name:PROC
    lea rax, Nt&Name[rip]
    mov r10, ArgCount * 8
    jmp KiZwSystemService
ENDM
#elif defined(_M_PPC)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    stwu 1,-16(1)
    mflr 0
    stw  0,0(1)
    li   0, SyscallId
    sc
    lwz 0,0(1)
    mtlr 0
    addi 1,1,16
    blr
ENDM
#define STUBCODE_K STUBCODE_U
#elif defined(_M_MIPS)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    li $8, KUSER_SHARED_SYSCALL
    lw $8,0($8)
    j $8
    nop
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    j KiSystemService
    nop
ENDM
#elif defined(_M_ARM64) || defined(_ARM64_)
MACRO(STUBCODE_U, Name, SyscallId, ArgCount)
    mov w8, #SyscallId
    svc #0
    ret
ENDM
MACRO(STUBCODE_K, Name, SyscallId, ArgCount)
    EXTERN Nt&Name
    adrp x16, Nt&Name
    add x16, x16, #:lo12:Nt&Name
    mov x9, #ArgCount * 8
    b KiZwSystemService
ENDM
// ARM64-specific macros to avoid .pdata generation for syscall stubs
#ifdef _M_ARM64
MACRO(ARM64_MAKE_LABEL, Name, StackBytes)
    PUBLIC &Name
    &Name:
ENDM
MACRO(ARM64_STUB_U, Name, ArgCount)
    ARM64_MAKE_LABEL Zw&Name, %ArgCount * 8
    ARM64_MAKE_LABEL Nt&Name, %ArgCount * 8
    STUBCODE_U Name, SyscallId, %ArgCount
    SyscallId = SyscallId + 1
ENDM
MACRO(ARM64_STUB_K, Name, ArgCount)
    ARM64_MAKE_LABEL Zw&Name, %ArgCount * 8
    STUBCODE_K Name, SyscallId, %ArgCount
    SyscallId = SyscallId + 1
ENDM
#endif
#else
#error unsupported architecture
#endif

#ifdef _M_IX86
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    _&Name&@&StackBytes:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC _&Name&@&StackBytes
    .PROC _&Name&@&StackBytes
ENDM
#else
MACRO(MAKE_LABEL, Name, StackBytes)
    PUBLIC &Name
    &Name:
ENDM
MACRO(START_PROC, Name, StackBytes)
    PUBLIC &Name
    .PROC &Name
ENDM
#endif

#ifdef _M_ARM64
// Use ARM64-specific stubs that don't generate .pdata
#define STUB_U ARM64_STUB_U
#define STUB_K ARM64_STUB_K
#else
MACRO(STUB_U, Name, ArgCount)
    MAKE_LABEL Zw&Name, %ArgCount * 4
    START_PROC Nt&Name, %ArgCount * 4
    STUBCODE_U Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM

MACRO(STUB_K, Name, ArgCount)
    START_PROC Zw&Name, %ArgCount * 4
    STUBCODE_K Name, SyscallId, %ArgCount
    .ENDP
    SyscallId = SyscallId + 1
ENDM
#endif

#endif
