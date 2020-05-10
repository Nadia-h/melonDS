#include "ARMJIT_Compiler.h"

#include "../ARMJIT_Internal.h"
#include "../ARMInterpreter.h"
#include "../Config.h"

#ifdef __SWITCH__
#include "../switch/compat_switch.h"

extern char __start__;
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <malloc.h>

using namespace Arm64Gen;

extern "C" void ARM_Ret();

namespace ARMJIT
{

/*

    Recompiling classic ARM to ARMv8 code is at the same time
    easier and trickier than compiling to a less related architecture
    like x64. At one hand you can translate a lot of instructions directly.
    But at the same time, there are a ton of exceptions, like for
    example ADD and SUB can't have a RORed second operand on ARMv8.
 */

template <>
const ARM64Reg RegisterCache<Compiler, ARM64Reg>::NativeRegAllocOrder[] =
    {W19, W20, W21, W22, W23, W24, W25, W26};
template <>
const int RegisterCache<Compiler, ARM64Reg>::NativeRegsAvailable = 8;

const int JitMemSize = 16 * 1024 * 1024;
#ifndef __SWITCH__
u8 JitMem[JitMemSize];
#endif

void Compiler::MovePC()
{
    ADD(MapReg(15), MapReg(15), Thumb ? 2 : 4);
}

Compiler::Compiler()
{
#ifdef __SWITCH__
    JitRWBase = memalign(0x1000, JitMemSize);

    JitRXStart = (u8*)&__start__ - JitMemSize - 0x1000;
    JitRWStart = virtmemReserve(JitMemSize);
    MemoryInfo info = {0};
    u32 pageInfo = {0};
    int i = 0;
    while (JitRXStart != NULL)
    {
        svcQueryMemory(&info, &pageInfo, (u64)JitRXStart);
        if (info.type != MemType_Unmapped)
            JitRXStart = (void*)((u8*)info.addr - JitMemSize - 0x1000);
        else
            break;
        if (i++ > 8)
        {
            printf("couldn't find unmapped place for jit memory\n");
            JitRXStart = NULL;
        }
    }

    assert(JitRXStart != NULL);

    bool succeded = R_SUCCEEDED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize));
    assert(succeded);
    succeded = R_SUCCEEDED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize, Perm_Rx));
    assert(succeded);
    succeded = R_SUCCEEDED(svcMapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize));
    assert(succeded);

    SetCodeBase((u8*)JitRWStart, (u8*)JitRXStart);
    JitMemUseableSize = JitMemSize;
    Reset();
#else
    u64 pageSize = sysconf(_SC_PAGE_SIZE);
    u8* pageAligned = (u8*)(((u64)JitMem & ~(pageSize - 1)) + pageSize);
    u64 alignedSize = (((u64)JitMem + sizeof(JitMem)) & ~(pageSize - 1)) - (u64)pageAligned;
    mprotect(pageAligned, alignedSize, PROT_EXEC | PROT_READ | PROT_WRITE);

    SetCodeBase(pageAligned, pageAligned);
    JitMemUseableSize = alignedSize;
    Reset();
#endif

    for (int i = 0; i < 3; i++)
    {
        JumpToFuncs9[i] = Gen_JumpTo9(i);
        JumpToFuncs7[i] = Gen_JumpTo7(i);
    }

    /*
        W5 - mode
        W1 - reg num
        W3 - in/out value of reg
    */
    {
        ReadBanked = GetRXPtr();

        ADD(X2, RCPU, X1, ArithOption(X1, ST_LSL, 2));
        CMP(W5, 0x11);
        FixupBranch fiq = B(CC_EQ);
        SUBS(W1, W1, 13 - 8);
        ADD(X2, RCPU, X1, ArithOption(X1, ST_LSL, 2));
        FixupBranch notEverything = B(CC_LT);
        CMP(W5, 0x12);
        FixupBranch irq = B(CC_EQ);
        CMP(W5, 0x13);
        FixupBranch svc = B(CC_EQ);
        CMP(W5, 0x17);
        FixupBranch abt = B(CC_EQ);
        CMP(W5, 0x1B);
        FixupBranch und = B(CC_EQ);
        SetJumpTarget(notEverything);
        RET();

        SetJumpTarget(fiq);
        LDR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_FIQ));
        RET();
        SetJumpTarget(irq);
        LDR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_IRQ));
        RET();
        SetJumpTarget(svc);
        LDR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_SVC));
        RET();
        SetJumpTarget(abt);
        LDR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_ABT));
        RET();
        SetJumpTarget(und);
        LDR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_UND));
        RET();
    }
    {
        WriteBanked = GetRXPtr();

        ADD(X2, RCPU, X1, ArithOption(X1, ST_LSL, 2));
        CMP(W5, 0x11);
        FixupBranch fiq = B(CC_EQ);
        SUBS(W1, W1, 13 - 8);
        ADD(X2, RCPU, X1, ArithOption(X1, ST_LSL, 2));
        FixupBranch notEverything = B(CC_LT);
        CMP(W5, 0x12);
        FixupBranch irq = B(CC_EQ);
        CMP(W5, 0x13);
        FixupBranch svc = B(CC_EQ);
        CMP(W5, 0x17);
        FixupBranch abt = B(CC_EQ);
        CMP(W5, 0x1B);
        FixupBranch und = B(CC_EQ);
        SetJumpTarget(notEverything);
        MOVI2R(W4, 0);
        RET();

        SetJumpTarget(fiq);
        STR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_FIQ));
        MOVI2R(W4, 1);
        RET();
        SetJumpTarget(irq);
        STR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_IRQ));
        MOVI2R(W4, 1);
        RET();
        SetJumpTarget(svc);
        STR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_SVC));
        MOVI2R(W4, 1);
        RET();
        SetJumpTarget(abt);
        STR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_ABT));
        MOVI2R(W4, 1);
        RET();
        SetJumpTarget(und);
        STR(INDEX_UNSIGNED, W3, X2, offsetof(ARM, R_UND));
        MOVI2R(W4, 1);
        RET();
    }

    {
        CPSRDirty = true;
        BranchStub[0] = GetRXPtr();
        SaveCPSR();
        MOV(X0, RCPU);
        QuickCallFunction(X1, ARMJIT::LinkBlock<0>);
        LoadCPSR();
        QuickTailCall(X0, ARM_Ret);

        CPSRDirty = true;
        BranchStub[1] = GetRXPtr();
        SaveCPSR();
        MOV(X0, RCPU);
        QuickCallFunction(X1, ARMJIT::LinkBlock<1>);
        LoadCPSR();
        QuickTailCall(X0, ARM_Ret);
    }

    //FlushIcache();

    JitMemUseableSize -= GetCodeOffset();
    SetCodeBase((u8*)GetRWPtr(), (u8*)GetRXPtr());

    NearStart = 0;
    FarStart = NearStart + 12 * 1024 * 1024;
    NearSize = FarStart - NearStart;
    FarSize = JitMemUseableSize - FarStart;
}

Compiler::~Compiler()
{
#ifdef __SWITCH__
    if (JitRWStart != NULL)
    {
        bool succeded = R_SUCCEEDED(svcUnmapProcessMemory(JitRWStart, envGetOwnProcessHandle(), (u64)JitRXStart, JitMemSize));
        assert(succeded);
        virtmemFree(JitRWStart, JitMemSize);
        succeded = R_SUCCEEDED(svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)JitRXStart, (u64)JitRWBase, JitMemSize));
        assert(succeded);
        free(JitRWBase);
    }
#endif
}

void Compiler::LoadReg(int reg, ARM64Reg nativeReg)
{
    if (reg == 15)
        MOVI2R(nativeReg, R15);
    else
        LDR(INDEX_UNSIGNED, nativeReg, RCPU, offsetof(ARM, R[reg]));
}

void Compiler::SaveReg(int reg, ARM64Reg nativeReg)
{
    STR(INDEX_UNSIGNED, nativeReg, RCPU, offsetof(ARM, R[reg]));
}

void Compiler::LoadCPSR()
{
    assert(!CPSRDirty);
    LDR(INDEX_UNSIGNED, RCPSR, RCPU, offsetof(ARM, CPSR));
}

void Compiler::SaveCPSR(bool markClean)
{
    if (CPSRDirty)
    {
        STR(INDEX_UNSIGNED, RCPSR, RCPU, offsetof(ARM, CPSR));
        CPSRDirty = CPSRDirty && !markClean;
    }
}

FixupBranch Compiler::CheckCondition(u32 cond)
{
    if (cond >= 0x8)
    {
        LSR(W1, RCPSR, 28);
        MOVI2R(W2, 1);
        LSLV(W2, W2, W1);
        ANDI2R(W2, W2, ARM::ConditionTable[cond], W3);

        return CBZ(W2);
    }
    else
    {
        u8 bit = (28 + ((~(cond >> 1) & 1) << 1 | (cond >> 2 & 1) ^ (cond >> 1 & 1)));

        if (cond & 1)
            return TBNZ(RCPSR, bit);
        else
            return TBZ(RCPSR, bit);
    }
}

#define F(x) &Compiler::A_Comp_##x
const Compiler::CompileFunc A_Comp[ARMInstrInfo::ak_Count] =
{
    // AND
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // EOR
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // SUB
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // RSB
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // ADD
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // ADC
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // SBC
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // RSC
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // ORR
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // MOV
    F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp),
    F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp),
    // BIC
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp), F(ALUTriOp),
    // MVN
    F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp),
    F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp), F(ALUMovOp),
    // TST
    F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp),
    // TEQ
    F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp),
    // CMP
    F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp),
    // CMN
    F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp), F(ALUCmpOp),
    // Mul
    F(Mul), F(Mul), F(Mul_Long), F(Mul_Long), F(Mul_Long), F(Mul_Long), NULL, NULL, NULL, NULL, NULL, 
    // ARMv5 exclusives
    F(Clz), NULL, NULL, NULL, NULL, 
    
    // STR
    F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB),
    // STRB
    F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB),
    // LDR
    F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB),
    // LDRB
    F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB), F(MemWB),
    // STRH
    F(MemHD), F(MemHD), F(MemHD), F(MemHD),
    // LDRD
    NULL, NULL, NULL, NULL,
    // STRD
    NULL, NULL, NULL, NULL,
    // LDRH
    F(MemHD), F(MemHD), F(MemHD), F(MemHD),
    // LDRSB
    F(MemHD), F(MemHD), F(MemHD), F(MemHD),
    // LDRSH
    F(MemHD), F(MemHD), F(MemHD), F(MemHD),
    // Swap
    NULL, NULL,
    // LDM, STM
    F(LDM_STM), F(LDM_STM),
    // Branch
    F(BranchImm), F(BranchImm), F(BranchImm), F(BranchXchangeReg), F(BranchXchangeReg),
    // Special
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    &Compiler::Nop
};
#undef F
#define F(x) &Compiler::T_Comp_##x
const Compiler::CompileFunc T_Comp[ARMInstrInfo::tk_Count] =
{
    // Shift imm
    F(ShiftImm), F(ShiftImm), F(ShiftImm),
    // Add/sub tri operand
    F(AddSub_), F(AddSub_), F(AddSub_), F(AddSub_),
    // 8 bit imm
    F(ALUImm8), F(ALUImm8), F(ALUImm8), F(ALUImm8),
    // ALU
    F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU),
    F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU), F(ALU),
    // ALU hi reg
    F(ALU_HiReg), F(ALU_HiReg), F(ALU_HiReg),
    // PC/SP relative ops
    F(RelAddr), F(RelAddr), F(AddSP),
    // LDR PC rel
    F(LoadPCRel),
    // LDR/STR reg offset
    F(MemReg), F(MemReg), F(MemReg), F(MemReg),
    // LDR/STR sign extended, half
    F(MemRegHalf), F(MemRegHalf), F(MemRegHalf), F(MemRegHalf),
    // LDR/STR imm offset
    F(MemImm), F(MemImm), F(MemImm), F(MemImm),
    // LDR/STR half imm offset
    F(MemImmHalf), F(MemImmHalf),
    // LDR/STR sp rel
    F(MemSPRel), F(MemSPRel),
    // PUSH/POP
    F(PUSH_POP), F(PUSH_POP),
    // LDMIA, STMIA
    F(LDMIA_STMIA), F(LDMIA_STMIA),
    // Branch
    F(BCOND), F(BranchXchangeReg), F(BranchXchangeReg), F(B), F(BL_LONG_1), F(BL_LONG_2),
    // Unk, SVC
    NULL, NULL,
    F(BL_Merged)
};

bool Compiler::CanCompile(bool thumb, u16 kind)
{
    return (thumb ? T_Comp[kind] : A_Comp[kind]) != NULL;
}

void Compiler::Comp_BranchSpecialBehaviour(bool taken)
{
    if (taken && CurInstr.BranchFlags & branch_IdleBranch)
    {
        MOVI2R(W0, 1);
        STRB(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, IdleLoop));
    }

    if ((CurInstr.BranchFlags & branch_FollowCondNotTaken && taken)
        || (CurInstr.BranchFlags & branch_FollowCondTaken && !taken))
    {
        RegCache.PrepareExit();

        if (Config::JIT_BrancheOptimisations == 2 && !(CurInstr.BranchFlags & branch_IdleBranch)
            && (!taken || (CurInstr.BranchFlags & branch_StaticTarget)))
        {
            SUBS(RCycles, RCycles, ConstantCycles);
            FixupBranch ret = B(CC_MI);
            LDR(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, StopExecution));
            FixupBranch ret2 = CBNZ(W0);

            ptrdiff_t rewritePart = GetCodeOffset();
            HINT(HINT_NOP);

            MOVI2R(W1, rewritePart);
            B(BranchStub[Num]);

            SetJumpTarget(ret);
            SetJumpTarget(ret2);
        }
        else
        {
            SUB(RCycles, RCycles, ConstantCycles);
        }
        QuickTailCall(X0, ARM_Ret);
    }
}

void Compiler::SwitchToFarCode()
{
    NearCode = GetCodeOffset();
    SetCodePtrUnsafe(FarCode);
}

void Compiler::SwitchToNearCode()
{
    FarCode = GetCodeOffset();
    SetCodePtrUnsafe(NearCode);
}

JitBlockEntry Compiler::CompileBlock(u32 translatedAddr, ARM* cpu, bool thumb, FetchedInstr instrs[], int instrsCount)
{
    if (FarSize - (FarCode - FarStart) < 1024 * 4)
    {
        printf("JIT far memory full, resetting...\n");
        ResetBlockCache();
    }
    if (NearSize - (GetCodeOffset() - NearStart) < 1024 * 16)
    {
        printf("JIT near memory full, resetting...\n");
        ResetBlockCache();
    }

    JitBlockEntry res = (JitBlockEntry)GetRXPtr();

    Thumb = thumb;
    Num = cpu->Num;
    CurCPU = cpu;
    ConstantCycles = 0;
    RegCache = RegisterCache<Compiler, ARM64Reg>(this, instrs, instrsCount, true);
    CPSRDirty = Config::JIT_BrancheOptimisations == 2;

    for (int i = 0; i < instrsCount; i++)
    {
        CurInstr = instrs[i];
        R15 = CurInstr.Addr + (Thumb ? 4 : 8);
        CodeRegion = R15 >> 24;

        CompileFunc comp = Thumb
            ? T_Comp[CurInstr.Info.Kind]
            : A_Comp[CurInstr.Info.Kind];

        Exit = i == (instrsCount - 1) || (CurInstr.BranchFlags & branch_FollowCondNotTaken);

        //printf("%x instr %x regs: r%x w%x n%x flags: %x %x %x\n", R15, CurInstr.Instr, CurInstr.Info.SrcRegs, CurInstr.Info.DstRegs, CurInstr.Info.ReadFlags, CurInstr.Info.NotStrictlyNeeded, CurInstr.Info.WriteFlags, CurInstr.SetFlags);

        bool isConditional = Thumb ? CurInstr.Info.Kind == ARMInstrInfo::tk_BCOND : CurInstr.Cond() < 0xE;
        if (comp == NULL || (CurInstr.BranchFlags & branch_FollowCondTaken) || (i == instrsCount - 1 && (!CurInstr.Info.Branches() || isConditional)))
        {
            MOVI2R(W0, R15);
            STR(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, R[15]));
            if (comp == NULL)
            {
                MOVI2R(W0, CurInstr.Instr);
                STR(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, CurInstr));
            }
            if (Num == 0)
            {
                MOVI2R(W0, (s32)CurInstr.CodeCycles);
                STR(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, CodeCycles));
            }
        }

        if (comp == NULL)
        {
            SaveCPSR();
            RegCache.Flush();

            STR(INDEX_UNSIGNED, RCycles, RCPU, offsetof(ARM, Cycles));
        }
        else
            RegCache.Prepare(Thumb, i);

        if (Thumb)
        {
            if (comp == NULL)
            {
                MOV(X0, RCPU);
                QuickCallFunction(X1, InterpretTHUMB[CurInstr.Info.Kind]);
            }
            else
                (this->*comp)();
        }
        else
        {
            u32 cond = CurInstr.Cond();
            if (CurInstr.Info.Kind == ARMInstrInfo::ak_BLX_IMM)
            {
                if (comp)
                    (this->*comp)();
                else
                {
                    MOV(X0, RCPU);
                    QuickCallFunction(X1, ARMInterpreter::A_BLX_IMM);
                }
            }
            else if (cond == 0xF)
                Comp_AddCycles_C();
            else
            {
                IrregularCycles = false;

                FixupBranch skipExecute;
                if (cond < 0xE)
                    skipExecute = CheckCondition(cond);

                if (comp == NULL)
                {
                    MOV(X0, RCPU);
                    QuickCallFunction(X1, InterpretARM[CurInstr.Info.Kind]);
                }
                else
                {
                    (this->*comp)();
                }

                Comp_BranchSpecialBehaviour(true);

                if (cond < 0xE)
                {
                    if (IrregularCycles || (CurInstr.BranchFlags & branch_FollowCondTaken))
                    {
                        FixupBranch skipNop = B();
                        SetJumpTarget(skipExecute);

                        Comp_AddCycles_C();

                        Comp_BranchSpecialBehaviour(false);

                        SetJumpTarget(skipNop);
                    }
                    else
                        SetJumpTarget(skipExecute);
                }

            }
        }

        if (comp == NULL)
        {
            LoadCPSR();
            LDR(INDEX_UNSIGNED, RCycles, RCPU, offsetof(ARM, Cycles));
        }
    }

    RegCache.Flush();

    if (Config::JIT_BrancheOptimisations == 2
        && !(instrs[instrsCount - 1].BranchFlags & branch_IdleBranch)
        && (!instrs[instrsCount - 1].Info.Branches()
        || instrs[instrsCount - 1].BranchFlags & branch_FollowCondNotTaken
        || (instrs[instrsCount - 1].BranchFlags & branch_FollowCondTaken && instrs[instrsCount - 1].BranchFlags & branch_StaticTarget)))
    {
        SUBS(RCycles, RCycles, ConstantCycles);
        FixupBranch ret = B(CC_MI);
        LDR(INDEX_UNSIGNED, W0, RCPU, offsetof(ARM, StopExecution));
        FixupBranch ret2 = CBNZ(W0);

        ptrdiff_t rewritePart = GetCodeOffset();
        HINT(HINT_NOP);

        MOVI2R(W1, rewritePart);
        B(BranchStub[Num]);

        SetJumpTarget(ret);
        SetJumpTarget(ret2);
    }
    else
    {
        SUB(RCycles, RCycles, ConstantCycles);
    }
    QuickTailCall(X0, ARM_Ret);

    for (int i = 0; i < VeneersLeft.Length; i++)
    {
        SetJumpTarget(std::get<0>(VeneersLeft[i]));
        B(std::get<1>(VeneersLeft[i]));
    }
    VeneersLeft.Clear();

    FlushIcache();
    FlushIcacheSection(GetRXBase() + FarLastFlushStart, GetRXBase() + FarCode);
    FarLastFlushStart = FarCode;

    return res;
}

void Compiler::Reset()
{
    SetCodePtr(0);
    NearCode = NearStart;
    FarCode = FarStart;

    const u32 brk_0 = 0xD4200000;

    for (int i = 0; i < JitMemUseableSize / 4; i++)
        *(((u32*)GetRWPtr()) + i) = brk_0;
}

void Compiler::LinkBlock(u32 offset, JitBlockEntry entry)
{
    ptrdiff_t curOffset = GetCodeOffset();
    SetCodePtrUnsafe(offset);
    u8* flushStart = (u8*)GetRXPtr();
    B((void*)entry);
    u8* flushEnd = (u8*)GetRXPtr();
    SetCodePtrUnsafe(curOffset);
    
    FlushIcacheSection(flushStart, flushEnd);
}

void Compiler::UnlinkBlock(u32 offset)
{
    ptrdiff_t curOffset = GetCodeOffset();
    SetCodePtrUnsafe(offset);
    u8* flushStart = (u8*)GetRXPtr();
    HINT(HINT_NOP);
    u8* flushEnd = (u8*)GetRXPtr();
    SetCodePtrUnsafe(curOffset);
    
    FlushIcacheSection(flushStart, flushEnd);
}

void Compiler::Comp_AddCycles_C(bool nonConst)
{
    s32 cycles = Num ?
        NDS::ARM7MemTimings[CurInstr.CodeCycles][Thumb ? 1 : 3]
        : ((R15 & 0x2) ? 0 : CurInstr.CodeCycles);

    if (!nonConst && !CurInstr.Info.Branches())
        ConstantCycles += cycles;
    else
        SUB(RCycles, RCycles, cycles);
}

void Compiler::Comp_AddCycles_CI(u32 numI)
{
    s32 cycles = (Num ?
        NDS::ARM7MemTimings[CurInstr.CodeCycles][Thumb ? 0 : 2]
        : ((R15 & 0x2) ? 0 : CurInstr.CodeCycles)) + numI;

    if (Thumb || CurInstr.Cond() >= 0xE)
        ConstantCycles += cycles;
    else
        SUB(RCycles, RCycles, cycles);
}

void Compiler::Comp_AddCycles_CI(u32 c, ARM64Reg numI, ArithOption shift)
{
    s32 cycles = (Num ?
        NDS::ARM7MemTimings[CurInstr.CodeCycles][Thumb ? 0 : 2]
        : ((R15 & 0x2) ? 0 : CurInstr.CodeCycles)) + c;

    SUB(RCycles, RCycles, numI, shift);
    if (Thumb || CurInstr.Cond() >= 0xE)
        ConstantCycles += c;
    else
        SUB(RCycles, RCycles, cycles);
}

void Compiler::Comp_AddCycles_CDI()
{
    if (Num == 0)
        Comp_AddCycles_CD();
    else
    {
        IrregularCycles = true;

        s32 cycles;

        s32 numC = NDS::ARM7MemTimings[CurInstr.CodeCycles][Thumb ? 0 : 2];
        s32 numD = CurInstr.DataCycles;

        if ((CurInstr.DataRegion >> 24) == 0x02) // mainRAM
        {
            if (CodeRegion == 0x02)
                cycles = numC + numD;
            else
            {
                numC++;
                cycles = std::max(numC + numD - 3, std::max(numC, numD));
            }
        }
        else if (CodeRegion == 0x02)
        {
            numD++;
            cycles = std::max(numC + numD - 3, std::max(numC, numD));
        }
        else
        {
            cycles = numC + numD + 1;
        }
        
        if (!Thumb && CurInstr.Cond() < 0xE)
            SUB(RCycles, RCycles, cycles);
        else
            ConstantCycles += cycles;
    }
}

void Compiler::Comp_AddCycles_CD()
{
    u32 cycles = 0;
    if (Num == 0)
    {
        s32 numC = (R15 & 0x2) ? 0 : CurInstr.CodeCycles;
        s32 numD = CurInstr.DataCycles;

        //if (DataRegion != CodeRegion)
            cycles = std::max(numC + numD - 6, std::max(numC, numD));

        IrregularCycles = cycles != numC;
    }
    else
    {
        s32 numC = NDS::ARM7MemTimings[CurInstr.CodeCycles][Thumb ? 0 : 2];
        s32 numD = CurInstr.DataCycles;

        if ((CurInstr.DataRegion >> 24) == 0x02)
        {
            if (CodeRegion == 0x02)
                cycles += numC + numD;
            else
                cycles += std::max(numC + numD - 3, std::max(numC, numD));
        }
        else if (CodeRegion == 0x02)
        {
            cycles += std::max(numC + numD - 3, std::max(numC, numD));
        }
        else
        {
            cycles += numC + numD;
        }

        IrregularCycles = true;
    }

    if ((!Thumb && CurInstr.Cond() < 0xE) && IrregularCycles)
        SUB(RCycles, RCycles, cycles);
    else
        ConstantCycles += cycles;
}

}