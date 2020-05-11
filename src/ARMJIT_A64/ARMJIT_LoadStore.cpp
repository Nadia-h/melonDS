#include "ARMJIT_Compiler.h"

#include "../Config.h"

using namespace Arm64Gen;

namespace ARMJIT
{

bool Compiler::Comp_MemLoadLiteral(int size, bool signExtend, int rd, u32 addr)
{
    u32 translatedAddr = Num == 0 ? TranslateAddr9(addr) : TranslateAddr7(addr);

    int invalidLiteralIdx = InvalidLiterals.Find(translatedAddr);
    if (invalidLiteralIdx != -1)
    {
        InvalidLiterals.Remove(invalidLiteralIdx);
        return false;
    }

    Comp_AddCycles_CDI();

    u32 val;
    // make sure arm7 bios is accessible
    u32 tmpR15 = CurCPU->R[15];
    CurCPU->R[15] = R15;
    if (size == 32)
    {
        CurCPU->DataRead32(addr & ~0x3, &val);
        val = ROR(val, (addr & 0x3) << 3);
    }
    else if (size == 16)
    {
        CurCPU->DataRead16(addr & ~0x1, &val);
        if (signExtend)
            val = ((s32)val << 16) >> 16;
    }
    else
    {
        CurCPU->DataRead8(addr, &val);
        if (signExtend)
            val = ((s32)val << 24) >> 24;
    }
    CurCPU->R[15] = tmpR15;

    MOVI2R(MapReg(rd), val);

    if (Thumb || CurInstr.Cond() == 0xE)
        RegCache.PutLiteral(rd, val);
    
    return true;
}

void Compiler::Comp_MemAccess(int rd, int rn, Op2 offset, int size, int flags)
{
    u32 addressMask = ~0;
    if (size == 32)
        addressMask = ~3;
    if (size == 16)
        addressMask = ~1;

    if (Config::JIT_LiteralOptimisations && rn == 15 && rd != 15 && offset.IsImm && !(flags & (memop_Post|memop_Store|memop_Writeback)))
    {
        u32 addr = R15 + offset.Imm * ((flags & memop_SubtractOffset) ? -1 : 1);
        
        if (Comp_MemLoadLiteral(size, flags & memop_SignExtend, rd, addr))
            return;
    }
    
    if (flags & memop_Store)
        Comp_AddCycles_CD();
    else
        Comp_AddCycles_CDI();

    ARM64Reg rdMapped = MapReg(rd);
    ARM64Reg rnMapped = MapReg(rn);

    if (Thumb && rn == 15)
    {
        ANDI2R(W3, rnMapped, ~2);
        rnMapped = W3;
    }

    ARM64Reg finalAddr = W0;
    if (flags & memop_Post)
    {
        finalAddr = rnMapped;
        MOV(W0, rnMapped);
    }

    if (!offset.IsImm)
        Comp_RegShiftImm(offset.Reg.ShiftType, offset.Reg.ShiftAmount, false, offset, W2);
    // offset might has become an immediate
    if (offset.IsImm)
    {
        if (offset.Imm)
        {
            if (flags & memop_SubtractOffset)
                SUB(finalAddr, rnMapped, offset.Imm);
            else
                ADD(finalAddr, rnMapped, offset.Imm);
        }
        else if (finalAddr != rnMapped)
            MOV(finalAddr, rnMapped);
    }
    else
    {
        if (offset.Reg.ShiftType == ST_ROR)
        {
            ROR_(W0, offset.Reg.Rm, offset.Reg.ShiftAmount);
            offset = Op2(W0);
        }

        if (flags & memop_SubtractOffset)
            SUB(finalAddr, rnMapped, offset.Reg.Rm, offset.ToArithOption());
        else
            ADD(finalAddr, rnMapped, offset.Reg.Rm, offset.ToArithOption());
    }

    if (!(flags & memop_Post) && (flags & memop_Writeback))
        MOV(rnMapped, W0);

    bool addrIsStatic = Config::JIT_LiteralOptimisations
            && RegCache.IsLiteral(rn) && offset.IsImm && !(flags & (memop_Writeback|memop_Post));
    u32 staticAddress;
    if (addrIsStatic)
        staticAddress = RegCache.LiteralValues[rn] + offset.Imm * ((flags & memop_SubtractOffset) ? -1 : 1);

    int expectedTarget = Num == 0
            ? ClassifyAddress9(addrIsStatic ? staticAddress : CurInstr.DataRegion) 
            : ClassifyAddress7(addrIsStatic ? staticAddress : CurInstr.DataRegion);
    if (CurInstr.Cond() < 0xE && !addrIsStatic)
        expectedTarget = memregion_Other;

    bool compileFastPath = false, compileSlowPath = true;
    bool targetNeverCode = false;

    switch (expectedTarget)
    {
    case memregion_IO9:
    case memregion_IO7:
        targetNeverCode = true;
    case memregion_MainRAM:
    case memregion_DTCM:
    case memregion_WRAM7:
    case memregion_SWRAM9:
    case memregion_SWRAM7:
    case memregion_VWRAM:
        compileFastPath = true;
        break;
    case memregion_Wifi:
        targetNeverCode = true;
        compileFastPath = size >= 16;
        break;
    case memregion_VRAM:
        compileFastPath = !(flags & memop_Store) || size >= 16;
    case memregion_BIOS9:
        targetNeverCode = true;
        compileFastPath = !(flags & memop_Store);
        break;
    default: break;
    }
    
    if (addrIsStatic)
    {
        if (!(flags & memop_Store) || targetNeverCode)
            compileSlowPath = false;

        MOVI2R(W0, staticAddress);
    }

    if (addrIsStatic && !compileFastPath)
    {
        compileFastPath = false;
        compileSlowPath = true;
    }

    ARM64Reg addressOffsetReg = W0;

    if (compileFastPath)
    {
        FixupBranch slowPath;
        if (compileSlowPath)
        {
            LSR(W1, W0, 9);
            MOVP2R(X2, Num == 0 ? MemoryStatus9 : MemoryStatus7);
            LDRB(W1, X2, X1);
            if (!(flags & memop_Store))
                ANDI2R(W1, W1, ~0x80);
            CMP(W1, expectedTarget);
            slowPath = B(CC_NEQ);
        }

        if (expectedTarget == memregion_MainRAM
            || expectedTarget == memregion_BIOS9
            || expectedTarget == memregion_WRAM7)
        {
            u8* data;
            u32 mask;
            if (expectedTarget == memregion_MainRAM)
            {
                data = NDS::MainRAM;
                mask = MAIN_RAM_SIZE - 1;
            }
            else if (expectedTarget == memregion_BIOS9)
            {
                data = NDS::ARM9BIOS;
                mask = 0xFFF;
            }
            else
            {
                data = NDS::ARM7WRAM;
                mask = 0xFFFF;
            }

            ANDI2R(W1, W0, mask & addressMask);
            MOVP2R(X2, data);

            if (flags & memop_Store)
                STRGeneric(size, rdMapped, X1, X2);
            else
                LDRGeneric(size, flags & memop_SignExtend, rdMapped, X1, X2);
        }
        else if (expectedTarget == memregion_SWRAM9 || expectedTarget == memregion_SWRAM7)
        {
            NDS::MemRegion* region = expectedTarget == memregion_SWRAM9
                ? &NDS::SWRAM_ARM9 : &NDS::SWRAM_ARM7;

            MOVP2R(X3, region);
            LDR(INDEX_UNSIGNED, W2, X3, offsetof(NDS::MemRegion, Mask));
            LDR(INDEX_UNSIGNED, X3, X3, offsetof(NDS::MemRegion, Mem));
            if (size > 8)
            {
                ANDI2R(W1, W0, addressMask);
                AND(W1, W1, W2);
            }
            else
            {
                AND(W1, W0, W2);
            }

            if (flags & memop_Store)
                STRGeneric(size, rdMapped, X1, X3);
            else
                LDRGeneric(size, flags & memop_SignExtend, rdMapped, X1, X3);
        }
        else if (expectedTarget == memregion_DTCM)
        {
            LDR(INDEX_UNSIGNED, W1, RCPU, offsetof(ARMv5, DTCMBase));
            SUB(W1, W0, W1);
            ANDI2R(W1, W1, 0x3FFF & addressMask);
            ADDI2R(W1, W1, offsetof(ARMv5, DTCM), W2);

            if (flags & memop_Store)
                STRGeneric(size, rdMapped, RCPU, W1);
            else
                LDRGeneric(size, flags & memop_SignExtend, rdMapped, RCPU, W1);
        }
        else
        {
            u32 maskedDataRegion;
            if (addrIsStatic)
            {
                maskedDataRegion = staticAddress;
            }
            else
            {
                maskedDataRegion = CurInstr.DataRegion;
                if (Num == 0)
                    maskedDataRegion &= ~0xFFFFFF;
                else
                    maskedDataRegion &= ~0x7FFFFF;
            }

            void* func = GetFuncForAddr(CurCPU, maskedDataRegion, flags & memop_Store, size);

            if (flags & memop_Store)
            {
                MOV(W1, rdMapped);

                QuickCallFunction(X2, (void(*)())func);
            }
            else
            {
                if (!addrIsStatic)
                {
                    MOV(rdMapped, W0);
                    if (size > 8)
                        ANDI2R(W0, W0, addressMask);
                }

                QuickCallFunction(X2, (void(*)())func);

                if (!addrIsStatic)
                {
                    addressOffsetReg = W1;
                    MOV(W1, rdMapped);
                }

                if (size == 32)
                    MOV(rdMapped, W0);
                else if (flags & memop_SignExtend)
                    SBFM(rdMapped, W0, 0, size - 1);
                else
                    UBFX(rdMapped, W0, 0, size);
            }
        }

        if (size == 32 && !(flags & memop_Store))
        {
            if (addrIsStatic)
            {
                if (staticAddress & 0x3)
                    ROR_(rdMapped, rdMapped, (staticAddress & 0x3) * 8);
            }
            else
            {
                ANDI2R(addressOffsetReg, addressOffsetReg, 0x3);
                LSL(addressOffsetReg, addressOffsetReg, 3);
                RORV(rdMapped, rdMapped, addressOffsetReg);
            }
        }

        if (compileSlowPath)
        {
            SwitchToFarCode();
            VeneersLeft.Add(std::make_tuple(slowPath, (u8*)GetRXPtr()));
        }
    }

    if (compileSlowPath)
    {
        if (Num == 0)
        {
            MOV(X1, RCPU);
            if (flags & memop_Store)
            {
                MOV(W2, rdMapped);
                switch (size)
                {
                case 32: QuickCallFunction(X3, SlowWrite9<u32>); break;
                case 16: QuickCallFunction(X3, SlowWrite9<u16>); break;
                case 8: QuickCallFunction(X3, SlowWrite9<u8>); break;
                }
            }
            else
            {
                switch (size)
                {
                case 32: QuickCallFunction(X3, SlowRead9<u32>); break;
                case 16: QuickCallFunction(X3, SlowRead9<u16>); break;
                case 8: QuickCallFunction(X3, SlowRead9<u8>); break;
                }
            }
        }
        else
        {
            if (flags & memop_Store)
            {
                MOV(W1, rdMapped);
                switch (size)
                {
                case 32: QuickCallFunction(X3, SlowWrite7<u32>); break;
                case 16: QuickCallFunction(X3, SlowWrite7<u16>); break;
                case 8: QuickCallFunction(X3, SlowWrite7<u8>); break;
                }
            }
            else
            {
                switch (size)
                {
                case 32: QuickCallFunction(X3, SlowRead7<u32>); break;
                case 16: QuickCallFunction(X3, SlowRead7<u16>); break;
                case 8: QuickCallFunction(X3, SlowRead7<u8>); break;
                }
            }
        }

        if (!(flags & memop_Store))
        {
            if (size == 32)
                MOV(rdMapped, W0);
            else if (flags & memop_SignExtend)
                SBFM(rdMapped, W0, 0, size - 1);
            else
                UBFX(rdMapped, W0, 0, size);
        }
    }

    if (compileFastPath && compileSlowPath)
    {
        FixupBranch ret = B();
        SwitchToNearCode();
        SetJumpTarget(ret);
    }

    if (CurInstr.Info.Branches())
    {
        if (size < 32)
            printf("LDR size < 32 branching?\n");
        Comp_JumpTo(rdMapped, Num == 0, false);
    }
}

void Compiler::A_Comp_MemWB()
{
    Op2 offset;
    if (CurInstr.Instr & (1 << 25))
        offset = Op2(MapReg(CurInstr.A_Reg(0)), (ShiftType)((CurInstr.Instr >> 5) & 0x3), (CurInstr.Instr >> 7) & 0x1F);
    else
        offset = Op2(CurInstr.Instr & 0xFFF);

    bool load = CurInstr.Instr & (1 << 20);
    bool byte = CurInstr.Instr & (1 << 22);

    int flags = 0;
    if (!load)
        flags |= memop_Store;
    if (!(CurInstr.Instr & (1 << 24)))
        flags |= memop_Post;
    if (CurInstr.Instr & (1 << 21))
        flags |= memop_Writeback;
    if (!(CurInstr.Instr & (1 << 23)))
        flags |= memop_SubtractOffset;

    Comp_MemAccess(CurInstr.A_Reg(12), CurInstr.A_Reg(16), offset, byte ? 8 : 32, flags);
}

void Compiler::A_Comp_MemHD()
{
    bool load = CurInstr.Instr & (1 << 20);
    bool signExtend;
    int op = (CurInstr.Instr >> 5) & 0x3;
    int size;
    
    if (load)
    {
        signExtend = op >= 2;
        size = op == 2 ? 8 : 16;
    }
    else
    {
        size = 16;
        signExtend = false;
    }

    Op2 offset;
    if (CurInstr.Instr & (1 << 22))
        offset = Op2((CurInstr.Instr & 0xF) | ((CurInstr.Instr >> 4) & 0xF0));
    else
        offset = Op2(MapReg(CurInstr.A_Reg(0)));
    
    int flags = 0;
    if (signExtend)
        flags |= memop_SignExtend;
    if (!load)
        flags |= memop_Store;
    if (!(CurInstr.Instr & (1 << 24)))
        flags |= memop_Post;
    if (!(CurInstr.Instr & (1 << 23)))
        flags |= memop_SubtractOffset;
    if (CurInstr.Instr & (1 << 21))
        flags |= memop_Writeback;

    Comp_MemAccess(CurInstr.A_Reg(12), CurInstr.A_Reg(16), offset, size, flags);
}

void Compiler::T_Comp_MemReg()
{
    int op = (CurInstr.Instr >> 10) & 0x3;
    bool load = op & 0x2;
    bool byte = op & 0x1;

    Comp_MemAccess(CurInstr.T_Reg(0), CurInstr.T_Reg(3), 
        Op2(MapReg(CurInstr.T_Reg(6))), byte ? 8 : 32, load ? 0 : memop_Store);
}

void Compiler::T_Comp_MemImm()
{
    int op = (CurInstr.Instr >> 11) & 0x3;
    bool load = op & 0x1;
    bool byte = op & 0x2;
    u32 offset = ((CurInstr.Instr >> 6) & 0x1F) * (byte ? 1 : 4);

    Comp_MemAccess(CurInstr.T_Reg(0), CurInstr.T_Reg(3), Op2(offset), 
        byte ? 8 : 32, load ? 0 : memop_Store);
}

void Compiler::T_Comp_MemRegHalf()
{
    int op = (CurInstr.Instr >> 10) & 0x3;
    bool load = op != 0;
    int size = op != 1 ? 16 : 8;
    bool signExtend = op & 1;

    int flags = 0;
    if (signExtend)
        flags |= memop_SignExtend;
    if (!load)
        flags |= memop_Store;

    Comp_MemAccess(CurInstr.T_Reg(0), CurInstr.T_Reg(3), Op2(MapReg(CurInstr.T_Reg(6))),
        size, flags);
}

void Compiler::T_Comp_MemImmHalf()
{
    u32 offset = (CurInstr.Instr >> 5) & 0x3E;
    bool load = CurInstr.Instr & (1 << 11);

    Comp_MemAccess(CurInstr.T_Reg(0), CurInstr.T_Reg(3), Op2(offset), 16,
        load ? 0 : memop_Store);
}

void Compiler::T_Comp_LoadPCRel()
{
    u32 offset = ((CurInstr.Instr & 0xFF) << 2);
    u32 addr = (R15 & ~0x2) + offset;

    if (!Config::JIT_LiteralOptimisations || !Comp_MemLoadLiteral(32, false, CurInstr.T_Reg(8), addr))
        Comp_MemAccess(CurInstr.T_Reg(8), 15, Op2(offset), 32, 0);
}

void Compiler::T_Comp_MemSPRel()
{
    u32 offset = (CurInstr.Instr & 0xFF) * 4;
    bool load = CurInstr.Instr & (1 << 11);

    Comp_MemAccess(CurInstr.T_Reg(8), 13, Op2(offset), 32, load ? 0 : memop_Store);
}

s32 Compiler::Comp_MemAccessBlock(int rn, BitSet16 regs, bool store, bool preinc, bool decrement, bool usermode)
{
    IrregularCycles = true;

    int regsCount = regs.Count();

    if (regsCount == 0)
        return 0; // actually not the right behaviour TODO: fix me

    if (regsCount == 1 && !usermode)
    {
        int flags = 0;
        if (store)
            flags |= memop_Store;
        if (decrement)
            flags |= memop_SubtractOffset;
        Op2 offset = preinc ? Op2(4) : Op2(0);

        Comp_MemAccess(*regs.begin(), rn, offset, 32, flags);

        return decrement ? -4 : 4;
    }

    if (store)
        Comp_AddCycles_CD();
    else
        Comp_AddCycles_CDI();

    int expectedTarget = Num == 0
        ? ClassifyAddress9(CurInstr.DataRegion)
        : ClassifyAddress7(CurInstr.DataRegion);
    if (usermode || CurInstr.Cond() < 0xE)
        expectedTarget = memregion_Other;

    bool compileFastPath = false;

    if (decrement)
    {
        SUB(W0, MapReg(rn), regsCount * 4);
        preinc ^= true;
    }
    else
        MOV(W0, MapReg(rn));

    switch (expectedTarget)
    {
    case memregion_DTCM:
    case memregion_MainRAM:
    case memregion_SWRAM9:
    case memregion_SWRAM7:
    case memregion_WRAM7:
        compileFastPath = true;
        break;
    default:
        break;
    }

    if (compileFastPath)
    {
        LSR(W1, W0, 9);

        MOVP2R(X2, Num == 0 ? MemoryStatus9 : MemoryStatus7);
        LDRB(W1, X2, X1);
        if (!store)
            ANDI2R(W1, W1, ~0x80);
        CMP(W1, expectedTarget);
        FixupBranch slowPath = B(CC_NEQ);

        if (expectedTarget == memregion_DTCM)
        {
            LDR(INDEX_UNSIGNED, W2, RCPU, offsetof(ARMv5, DTCMBase));
            ADDI2R(X1, RCPU, offsetof(ARMv5, DTCM), X3);
            SUB(W0, W0, W2);
            ANDI2R(W0, W0, 0x3FFF & ~3);
        }
        else if (expectedTarget == memregion_WRAM7)
        {
            MOVP2R(X1, NDS::ARM7WRAM);
            ANDI2R(W0, W0, 0xFFFF & ~3);
            ADD(X1, X1, X0);
        }
        else if (expectedTarget == memregion_MainRAM)
        {
            MOVP2R(X1, NDS::MainRAM);
            ANDI2R(W0, W0, (MAIN_RAM_SIZE - 1) & ~3);
            ADD(X1, X0, X1);
        }
        else
        {
            NDS::MemRegion* region = expectedTarget == memregion_SWRAM9
                ? &NDS::SWRAM_ARM9 : &NDS::SWRAM_ARM7;

            MOVP2R(X2, region);
            LDR(INDEX_UNSIGNED, W1, X2, offsetof(NDS::MemRegion, Mask));
            LDR(INDEX_UNSIGNED, X2, X2, offsetof(NDS::MemRegion, Mem));
            ANDI2R(W0, W0, ~3);
            AND(W0, W0, W1);
            ADD(X1, X2, X0);
        }

        u32 offset = preinc ? 4 : 0;

        BitSet16::Iterator it = regs.begin();
        while (it != regs.end())
        {
            BitSet16::Iterator nextReg = it;
            nextReg++;

            if (nextReg != regs.end())
            {
                ARM64Reg first = W3, second = W4;
                if (RegCache.LoadedRegs & (1 << *it))
                    first = MapReg(*it);
                if (RegCache.LoadedRegs & (1 << *nextReg))
                    second = MapReg(*nextReg);

                if (store)
                {
                    if (first == W3)
                        LoadReg(*it, first);
                    if (second == W4)
                        LoadReg(*nextReg, second);

                    STP(INDEX_SIGNED, first, second, X1, offset);
                }
                else
                {
                    LDP(INDEX_SIGNED, first, second, X1, offset);
                
                    if (first == W3)
                        SaveReg(*it, first);
                    if (second == W4)
                        SaveReg(*nextReg, second);
                }

                it++;
                offset += 8;
            }
            else
            {
                ARM64Reg first = W3;
                if (RegCache.LoadedRegs & (1 << *it))
                    first = MapReg(*it);

                if (first == W3 && store)
                    LoadReg(*it, first);

                if (store)
                    STR(INDEX_UNSIGNED, first, X1, offset);
                else
                    LDR(INDEX_UNSIGNED, first, X1, offset);

                if (first == W3 && !store)
                    SaveReg(*it, first);

                offset += 4;
            }

            it++;
        }

        SwitchToFarCode();
        VeneersLeft.Add(std::make_tuple(slowPath, (u8*)GetRXPtr()));
    }

    int i = 0;

    SUB(SP, SP, ((regsCount + 1) & ~1) * 8);
    if (store)
    {
        if (usermode && (regs & BitSet16(0x7f00)))
            UBFX(W5, RCPSR, 0, 5);

        BitSet16::Iterator it = regs.begin();
        while (it != regs.end())
        {
            BitSet16::Iterator nextReg = it;
            nextReg++;

            int reg = *it;

            if (usermode && reg >= 8 && reg < 15)
            {
                if (RegCache.LoadedRegs & (1 << reg))
                    MOV(W3, MapReg(reg));
                else
                    LoadReg(reg, W3);
                MOVI2R(W1, reg - 8);
                BL(ReadBanked);
                STR(INDEX_UNSIGNED, W3, SP, i * 8);
            }
            else if (!usermode && nextReg != regs.end())
            {
                ARM64Reg first = W3, second = W4;

                if (RegCache.LoadedRegs & (1 << reg))
                    first = MapReg(reg);
                else
                    LoadReg(reg, W3);

                if (RegCache.LoadedRegs & (1 << *nextReg))
                    second = MapReg(*nextReg);
                else
                    LoadReg(*nextReg, W4);

                STP(INDEX_SIGNED, EncodeRegTo64(first), EncodeRegTo64(second), SP, i * 8);

                i++;
                it++;
            }
            else if (RegCache.LoadedRegs & (1 << reg))
            {
                STR(INDEX_UNSIGNED, MapReg(reg), SP, i * 8);
            }
            else
            {
                LoadReg(reg, W3);
                STR(INDEX_UNSIGNED, W3, SP, i * 8);
            }
            i++;
            it++;
        }
    }

    ADD(X1, SP, 0);
    MOVI2R(W2, regsCount);

    if (Num == 0)
    {
        MOV(X3, RCPU);
        switch (preinc * 2 | store)
        {
        case 0: QuickCallFunction(X4, SlowBlockTransfer9<false, false>); break;
        case 1: QuickCallFunction(X4, SlowBlockTransfer9<false, true>); break;
        case 2: QuickCallFunction(X4, SlowBlockTransfer9<true, false>); break;
        case 3: QuickCallFunction(X4, SlowBlockTransfer9<true, true>); break;
        }
    }
    else
    {
        switch (preinc * 2 | store)
        {
        case 0: QuickCallFunction(X4, SlowBlockTransfer7<false, false>); break;
        case 1: QuickCallFunction(X4, SlowBlockTransfer7<false, true>); break;
        case 2: QuickCallFunction(X4, SlowBlockTransfer7<true, false>); break;
        case 3: QuickCallFunction(X4, SlowBlockTransfer7<true, true>); break;
        }
    }

    if (!store)
    {
        if (usermode && !regs[15] && (regs & BitSet16(0x7f00)))
            UBFX(W5, RCPSR, 0, 5);

        BitSet16::Iterator it = regs.begin();
        while (it != regs.end())
        {
            BitSet16::Iterator nextReg = it;
            nextReg++;

            int reg = *it;

            if (usermode && !regs[15] && reg >= 8 && reg < 15)
            {
                LDR(INDEX_UNSIGNED, W3, SP, i * 8);
                MOVI2R(W1, reg - 8);
                BL(WriteBanked);
                FixupBranch alreadyWritten = CBNZ(W4);
                if (RegCache.LoadedRegs & (1 << reg))
                    MOV(MapReg(reg), W3);
                else
                    SaveReg(reg, W3);
                SetJumpTarget(alreadyWritten);
            }
            else if (!usermode && nextReg != regs.end())
            {
                ARM64Reg first = W3, second = W4;
                
                if (RegCache.LoadedRegs & (1 << reg))
                    first = MapReg(reg);
                if (RegCache.LoadedRegs & (1 << *nextReg))
                    second = MapReg(*nextReg);

                LDP(INDEX_SIGNED, EncodeRegTo64(first), EncodeRegTo64(second), SP, i * 8);

                if (first == W3)
                    SaveReg(reg, W3);
                if (second == W4)
                    SaveReg(*nextReg, W4);

                it++;
                i++;
            }
            else if (RegCache.LoadedRegs & (1 << reg))
            {
                ARM64Reg mapped = MapReg(reg);
                LDR(INDEX_UNSIGNED, mapped, SP, i * 8);
            }
            else
            {
                LDR(INDEX_UNSIGNED, W3, SP, i * 8);
                SaveReg(reg, W3);
            }

            it++;
            i++;
        }
    }
    ADD(SP, SP, ((regsCount + 1) & ~1) * 8);

    if (compileFastPath)
    {
        FixupBranch ret = B();
        SwitchToNearCode();
        SetJumpTarget(ret);
    }

    if (!store && regs[15])
    {
        ARM64Reg mapped = MapReg(15);
        Comp_JumpTo(mapped, Num == 0, usermode);
    }

    return regsCount * 4 * (decrement ? -1 : 1);
}

void Compiler::A_Comp_LDM_STM()
{
    BitSet16 regs(CurInstr.Instr & 0xFFFF);

    bool load = CurInstr.Instr & (1 << 20);
    bool pre = CurInstr.Instr & (1 << 24);
    bool add = CurInstr.Instr & (1 << 23);
    bool writeback = CurInstr.Instr & (1 << 21);
    bool usermode = CurInstr.Instr & (1 << 22);

    ARM64Reg rn = MapReg(CurInstr.A_Reg(16));

    s32 offset = Comp_MemAccessBlock(CurInstr.A_Reg(16), regs, !load, pre, !add, usermode);

    if (load && writeback && regs[CurInstr.A_Reg(16)])
        writeback = Num == 0
            ? (!(regs & ~BitSet16(1 << CurInstr.A_Reg(16)))) || (regs & ~BitSet16((2 << CurInstr.A_Reg(16)) - 1))
            : false;
    if (writeback)
    {
        if (offset > 0)
            ADD(rn, rn, offset);
        else
            SUB(rn, rn, -offset);
    }
}

void Compiler::T_Comp_PUSH_POP()
{
    bool load = CurInstr.Instr & (1 << 11);
    BitSet16 regs(CurInstr.Instr & 0xFF);
    if (CurInstr.Instr & (1 << 8))
    {
        if (load)
            regs[15] = true;
        else
            regs[14] = true;
    }

    ARM64Reg sp = MapReg(13);
    s32 offset = Comp_MemAccessBlock(13, regs, !load, !load, !load, false);

    if (offset > 0)
            ADD(sp, sp, offset);
        else
            SUB(sp, sp, -offset);
}

void Compiler::T_Comp_LDMIA_STMIA()
{
    BitSet16 regs(CurInstr.Instr & 0xFF);
    ARM64Reg rb = MapReg(CurInstr.T_Reg(8));
    bool load = CurInstr.Instr & (1 << 11);
    u32 regsCount = regs.Count();
    
    s32 offset = Comp_MemAccessBlock(CurInstr.T_Reg(8), regs, !load, false, false, false);

    if (!load || !regs[CurInstr.T_Reg(8)])
    {
        if (offset > 0)
            ADD(rb, rb, offset);
        else
            SUB(rb, rb, -offset);
    }
}

}