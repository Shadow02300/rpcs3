#pragma once

#include "Emu/Cell/PPUOpcodes.h"
#include "Emu/Memory/Memory.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/SysCalls/SysCalls.h"
#include "rpcs3.h"

#define START_OPCODES_GROUP(x) /*x*/
#define END_OPCODES_GROUP(x) /*x*/

#ifdef _DEBUG
#define HLE_CALL_DEBUG
#endif

static u64 rotate_mask[64][64];
void InitRotateMask()
{
	static bool inited = false;
	if(inited) return;

	for(u32 mb=0; mb<64; mb++) for(u32 me=0; me<64; me++)
	{
		const u64 mask = ((u64)-1 >> mb) ^ ((me >= 63) ? 0 : (u64)-1 >> (me + 1));	
		rotate_mask[mb][me] = mb > me ? ~mask : mask;
	}

	inited = true;
}

/*
u32 rotl32(const u32 x, const u8 n) const { return (x << n) | (x >> (32 - n)); }
u32 rotr32(const u32 x, const u8 n) const { return (x >> n) | (x << (32 - n)); }
u64 rotl64(const u64 x, const u8 n) const { return (x << n) | (x >> (64 - n)); }
u64 rotr64(const u64 x, const u8 n) const { return (x >> n) | (x << (64 - n)); }
*/

#define rotl32 _rotl
#define rotr32 _rotr
#define rotl64 _rotl64
#define rotr64 _rotr64

class PPU_Interpreter : public PPU_Opcodes
{
private:
	PPUThread& CPU;

public:
	PPU_Interpreter(PPUThread& cpu) : CPU(cpu)
	{
		InitRotateMask();
	}

private:
	virtual void Exit() {}

	virtual void SysCall()
	{
		CPU.GPR[3] = SysCallsManager.DoSyscall(CPU.GPR[11], CPU);

		if((s32)CPU.GPR[3] < 0)
			ConLog.Warning("SysCall[%lld] done with code [0x%x]! #pc: 0x%llx", CPU.GPR[11], (u32)CPU.GPR[3], CPU.PC);
#ifdef HLE_CALL_DEBUG
		else ConLog.Write("SysCall[%lld] done with code [0x%llx]! #pc: 0x%llx", CPU.GPR[11], CPU.GPR[3], CPU.PC);
#endif
	}

	virtual void NOP()
	{
		//__asm nop
	}

	bool CheckCondition(OP_REG bo, OP_REG bi)
	{
		const u8 bo0 = (bo & 0x10) ? 1 : 0;
		const u8 bo1 = (bo & 0x08) ? 1 : 0;
		const u8 bo2 = (bo & 0x04) ? 1 : 0;
		const u8 bo3 = (bo & 0x02) ? 1 : 0;
		const u8 bo4 = (bo & 0x01) ? 1 : 0;

		if(!bo2) --CPU.CTR;

		const u8 ctr_ok = bo2 | ((CPU.CTR != 0) ^ bo3);
		const u8 cond_ok = bo0 | (CPU.IsCR(bi) ^ (~bo1 & 0x1));

		//if(bo1) CPU.SetCR(bi, bo4 ? 1 : 0);
		//if(bo1) return !bo4;

		//ConLog.Write("bo0: 0x%x, bo1: 0x%x, bo2: 0x%x, bo3: 0x%x", bo0, bo1, bo2, bo3);

		return ctr_ok && cond_ok;
	}

	u64& GetRegBySPR(OP_REG spr)
	{
		const u32 n = (spr >> 5) | ((spr & 0x1f) << 5);

		switch(n)
		{
		case 0x001: return CPU.XER.XER;
		case 0x008: return CPU.LR;
		case 0x009: return CPU.CTR;
		}

		UNK(wxString::Format("GetRegBySPR error: Unknown spr %d!", n));
		return CPU.XER.XER;
	}
	
	virtual void TDI(OP_REG to, OP_REG ra, OP_sIMM simm16)
	{
		s64 a = CPU.GPR[ra];

		if( (a < (s64)simm16  && (to & 0x10)) ||
			(a > (s64)simm16  && (to & 0x8))  ||
			(a == (s64)simm16 && (to & 0x4))  ||
			((u64)a < (u64)simm16 && (to & 0x2)) ||
			((u64)a > (u64)simm16 && (to & 0x1)) )
		{
			UNK(wxString::Format("Trap! (tdi %x, r%d, %x)", to, ra, simm16), false);
		}
	}

	virtual void TWI(OP_REG to, OP_REG ra, OP_sIMM simm16)
	{
		s32 a = CPU.GPR[ra];

		if( (a < simm16  && (to & 0x10)) ||
			(a > simm16  && (to & 0x8))  ||
			(a == simm16 && (to & 0x4))  ||
			((u32)a < (u32)simm16 && (to & 0x2)) ||
			((u32)a > (u32)simm16 && (to & 0x1)) )
		{
			UNK(wxString::Format("Trap! (twi %x, r%d, %x)", to, ra, simm16), false);
		}
	}

	START_OPCODES_GROUP(G_04)
		virtual void VXOR(OP_REG vrd, OP_REG vra, OP_REG vrb)
		{
			CPU.VPR[vrd] = CPU.VPR[vra] ^ CPU.VPR[vrb];
		}
	END_OPCODES_GROUP(G_04);

	virtual void MULLI(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		CPU.GPR[rt] = (s64)CPU.GPR[ra] * simm16;
	}
	virtual void SUBFIC(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		s64 RA = CPU.GPR[ra];
		CPU.GPR[rt] = (s64)simm16 - RA;
		CPU.XER.CA = RA <= simm16;
	}
	virtual void CMPLI(OP_REG crfd, OP_REG l, OP_REG ra, OP_uIMM uimm16)
	{
		CPU.UpdateCRn<u64>(crfd, l ? CPU.GPR[ra] : (u32)CPU.GPR[ra], uimm16);
	}
	virtual void CMPI(OP_REG crfd, OP_REG l, OP_REG ra, OP_sIMM simm16)
	{
		CPU.UpdateCRn<s64>(crfd, l ? CPU.GPR[ra] : (s32)CPU.GPR[ra], simm16);
	}
	virtual void ADDIC(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		const u64 RA = CPU.GPR[ra];
		CPU.GPR[rt] = RA + simm16;
		CPU.XER.CA = CPU.IsCarry(RA, simm16);
	}
	virtual void ADDIC_(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		const u64 RA = CPU.GPR[ra];
		CPU.GPR[rt] = RA + simm16;
		CPU.XER.CA = CPU.IsCarry(RA, simm16);
		CPU.UpdateCR0<s64>(CPU.GPR[rt]);
	}
	virtual void ADDI(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		CPU.GPR[rt] = ra ? ((s64)CPU.GPR[ra] + simm16) : simm16;
	}
	virtual void ADDIS(OP_REG rt, OP_REG ra, OP_sIMM simm16)
	{
		CPU.GPR[rt] = ra ? ((s64)CPU.GPR[ra] + (simm16 << 16)) : (simm16 << 16);
	}
	virtual void BC(OP_REG bo, OP_REG bi, OP_sIMM bd, OP_REG aa, OP_REG lk)
	{
		if(!CheckCondition(bo, bi)) return;
		CPU.SetBranch(branchTarget((aa ? 0 : CPU.PC), bd));
		if(lk) CPU.LR = CPU.PC + 4;
	}
	virtual void SC(const s32 sc_code)
	{
		switch(sc_code)
		{
		case 0x1: UNK(wxString::Format("HyperCall %d", CPU.GPR[0])); break;
		case 0x2: SysCall(); break;
		case 0x22: UNK("HyperCall LV1"); break;
		default: UNK(wxString::Format("Unknown sc: %x", sc_code));
		}
	}
	virtual void B(OP_sIMM ll, OP_REG aa, OP_REG lk)
	{
		CPU.SetBranch(branchTarget(aa ? 0 : CPU.PC, ll));
		if(lk) CPU.LR = CPU.PC + 4;
	}
	
	START_OPCODES_GROUP(G_13)
		virtual void MCRF(OP_REG crfd, OP_REG crfs)
		{
			CPU.SetCR(crfd, CPU.GetCR(crfs));
		}
		virtual void BCLR(OP_REG bo, OP_REG bi, OP_REG bh, OP_REG lk)
		{
			if(!CheckCondition(bo, bi)) return;
			CPU.SetBranch(branchTarget(0, CPU.LR));
			if(lk) CPU.LR = CPU.PC + 4;
		}
		virtual void CRNOR(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = 1 ^ (CPU.IsCR(ba) | CPU.IsCR(bb));
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CRANDC(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = CPU.IsCR(ba) & (1 ^ CPU.IsCR(bb));
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void ISYNC()
		{
		}
		virtual void CRXOR(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = CPU.IsCR(ba) ^ CPU.IsCR(bb);
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CRNAND(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = 1 ^ (CPU.IsCR(ba) & CPU.IsCR(bb));
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CRAND(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = CPU.IsCR(ba) & CPU.IsCR(bb);
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CREQV(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = 1 ^ (CPU.IsCR(ba) ^ CPU.IsCR(bb));
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CRORC(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = CPU.IsCR(ba) | (1 ^ CPU.IsCR(bb));
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void CROR(OP_REG bt, OP_REG ba, OP_REG bb)
		{
			const u8 v = CPU.IsCR(ba) | CPU.IsCR(bb);
			CPU.SetCRBit2(bt, v & 0x1);
		}
		virtual void BCCTR(OP_REG bo, OP_REG bi, OP_REG bh, OP_REG lk)
		{
			if(bo & 0x10 || CPU.IsCR(bi) == (bo & 0x8))
			{
				CPU.SetBranch(branchTarget(0, CPU.CTR & ~0x3));
				if(lk) CPU.LR = CPU.PC + 4;
			}
		}
		virtual void BCTR()
		{
			CPU.SetBranch(branchTarget(0, CPU.CTR & ~0x3));
		}
		virtual void BCTRL()
		{
			CPU.SetBranch(branchTarget(0, CPU.CTR & ~0x3));
			CPU.LR = CPU.PC + 4;
		}
	END_OPCODES_GROUP(G_13);
	
	virtual void RLWIMI(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG mb, OP_REG me, bool rc)
	{
		const u32 mask = rotate_mask[32 + mb][32 + me];
		CPU.GPR[ra] = (CPU.GPR[ra] & ~mask) | (rotl32(CPU.GPR[rs], sh) & mask);
		if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
	}
	virtual void RLWINM(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG mb, OP_REG me, bool rc)
	{
		CPU.GPR[ra] = rotl32(CPU.GPR[rs], sh) & rotate_mask[32 + mb][32 + me];
		if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
	}
	virtual void RLWNM(OP_REG ra, OP_REG rs, OP_REG rb, OP_REG mb, OP_REG me, bool rc)
	{
		CPU.GPR[ra] = rotl32(CPU.GPR[rs], CPU.GPR[rb] & 0x1f) & rotate_mask[32 + mb][32 + me];
		if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
	}
	virtual void ORI(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] | uimm16;
	}
	virtual void ORIS(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] | (uimm16 << 16);
	}
	virtual void XORI(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] ^ uimm16;
	}
	virtual void XORIS(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] ^ (uimm16 << 16);
	}
	virtual void ANDI_(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] & uimm16;
		CPU.UpdateCR0<s64>(CPU.GPR[ra]);
	}
	virtual void ANDIS_(OP_REG ra, OP_REG rs, OP_uIMM uimm16)
	{
		CPU.GPR[ra] = CPU.GPR[rs] & (uimm16 << 16);
		CPU.UpdateCR0<s64>(CPU.GPR[ra]);
	}

	START_OPCODES_GROUP(G_1e)
		virtual void RLDICL(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG mb, bool rc)
		{
			CPU.GPR[ra] = rotl64(CPU.GPR[rs], sh) & rotate_mask[mb][63];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void RLDICR(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG me, bool rc)
		{
			CPU.GPR[ra] = rotl64(CPU.GPR[rs], sh) & rotate_mask[0][me];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void RLDIC(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG mb, bool rc)
		{
			CPU.GPR[ra] = rotl64(CPU.GPR[rs], sh) & rotate_mask[mb][63-sh];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void RLDIMI(OP_REG ra, OP_REG rs, OP_REG sh, OP_REG mb, bool rc)
		{
			const u64 mask = rotate_mask[mb][63-sh];
			CPU.GPR[ra] = (CPU.GPR[ra] & ~mask) | (rotl64(CPU.GPR[rs], sh) & mask);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
	END_OPCODES_GROUP(G_1e);
	
	START_OPCODES_GROUP(G_1f)
		virtual void CMP(OP_REG crfd, OP_REG l, OP_REG ra, OP_REG rb)
		{
			CPU.UpdateCRn<s64>(crfd, l ? CPU.GPR[ra] : (s32)CPU.GPR[ra],  l ? CPU.GPR[rb] : (s32)CPU.GPR[rb]);
		}
		virtual void TW(OP_uIMM to, OP_REG ra, OP_REG rb)
		{
			s32 a = CPU.GPR[ra];
			s32 b = CPU.GPR[rb];

			if( (a < b  && (to & 0x10)) ||
				(a > b  && (to & 0x8))  ||
				(a == b && (to & 0x4))  ||
				((u32)a < (u32)b && (to & 0x2)) ||
				((u32)a > (u32)b && (to & 0x1)) )
			{
				UNK(wxString::Format("Trap! (tw %x, r%d, r%d)", to, ra, rb), false);
			}
		}
		virtual void LVEBX(OP_REG vd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			CPU.VPR[vd].Clear();
			CPU.VPR[vd].b(addr & 0xf) = Memory.Read8(addr);
		}
		virtual void SUBFC(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];
			CPU.GPR[rd] = RB - RA;
			CPU.XER.CA = CPU.IsCarry(RA, RB);
			if(oe) UNK("subfco");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void ADDC(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];
			CPU.GPR[rd] = RA + RB;
			CPU.XER.CA = RA <= RB;
			if(oe) UNK("addco");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void MULHDU(OP_REG rd, OP_REG ra, OP_REG rb, bool rc)
		{
			const u64 RA = CPU.GPR[ra];
			const u64 RB = CPU.GPR[rb];

			u128 RD;

			u64& lo =  (u64&)((u32*)&RD)[0];
			u64& mid = (u64&)((u32*)&RD)[1];
			u64& hi =  (u64&)((u32*)&RD)[2];

			const u64 a0 = ((u32*)&RA)[0];
			const u64 a1 = ((u32*)&RA)[1];
			const u64 b0 = ((u32*)&RB)[0];
			const u64 b1 = ((u32*)&RB)[1];

			lo = a0 * b0;
			hi = a1 * b1;

			mid += (a0 + a1) * (b0 + b1) - (lo + hi);

			CPU.GPR[rd] = RD._u64[1];

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void MULHWU(OP_REG rd, OP_REG ra, OP_REG rb, bool rc)
		{
			CPU.GPR[rd] = (CPU.GPR[ra] * CPU.GPR[rb]) >> 32;
			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[rd]);
		}
		virtual void MFOCRF(OP_REG a, OP_REG fxm, OP_REG rt)
		{
			/*
			if(a)
			{
				u32 n = 0, count = 0;
				for(u32 i=0; i<8; ++i)
				{
					if(fxm & (1 << i))
					{
						n = i;
						count++;
					}
				}

				if(count == 1)
				{
					//RT[32+4*n : 32+4*n+3] = CR[4*n : 4*n+3];
					CPU.GPR[rt] = (u64)CPU.GetCR(n) << (n * 4);
				}
				else CPU.GPR[rt] = 0;
			}
			else
			{
			*/
				CPU.GPR[rt] = CPU.CR.CR;
			//}
		}
		virtual void LWARX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			CPU.reserve_addr = addr;
			CPU.reserve = true;
			CPU.GPR[rd] = Memory.Read32(addr);
		}
		virtual void LDX(OP_REG ra, OP_REG rs, OP_REG rb)
		{
			CPU.GPR[ra] = Memory.Read64(rs ? CPU.GPR[rs] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void LWZX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			CPU.GPR[rd] = Memory.Read32(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void SLW(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			const u32 n = CPU.GPR[rb] & 0x1f;
			const u32 r = rotl32((u32)CPU.GPR[rs], n);
			const u32 m = (CPU.GPR[rb] & 0x20) ? 0 : rotate_mask[32][63 - n];

			CPU.GPR[ra] = r & m;

			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
		}
		virtual void CNTLZW(OP_REG ra, OP_REG rs, bool rc)
		{
			u32 i;
			for(i=0; i < 32; i++)
			{
				if(CPU.GPR[rs] & (0x80000000 >> i)) break;
			}

			CPU.GPR[ra] = i;

			if(rc) CPU.UpdateCR0<u32>(CPU.GPR[ra]);
		}
		virtual void SLD(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rb] & 0x40 ? 0 : CPU.GPR[rs] << (CPU.GPR[rb] & 0x3f);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void AND(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rs] & CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void CMPL(OP_REG crfd, OP_REG l, OP_REG ra, OP_REG rb)
		{
			CPU.UpdateCRn<u64>(crfd, l ? CPU.GPR[ra] : (u32)CPU.GPR[ra], l ? CPU.GPR[rb] : (u32)CPU.GPR[rb]);
		}
		virtual void LVEHX(OP_REG vd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = (ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]) & ~1ULL;
			CPU.VPR[vd].Clear();
			CPU.VPR[vd].h((addr & 0xf) >> 1) = Memory.Read16(addr);
		}
		virtual void SUBF(OP_REG rt, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			CPU.GPR[rt] = CPU.GPR[rb] - CPU.GPR[ra];
			if(oe) UNK("subfo");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		virtual void DCBST(OP_REG ra, OP_REG rb)
		{
			//UNK("dcbst", false);
		}
		virtual void CNTLZD(OP_REG ra, OP_REG rs, bool rc)
		{
			u32 i = 0;
			
			for(u64 mask = 1ULL << 63; i < 64; i++, mask >>= 1)
			{
				if(CPU.GPR[rs] & mask) break;
			}

			CPU.GPR[ra] = i;
			if(rc) CPU.UpdateCR0<u64>(CPU.GPR[ra]);
		}
		virtual void ANDC(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rs] & ~CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void LVEWX(OP_REG vd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = (ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]) & ~3ULL;
			CPU.VPR[vd].Clear();
			CPU.VPR[vd].w((addr & 0xf) >> 2) = Memory.Read32(addr);
		}
		virtual void MULHD(OP_REG rt, OP_REG ra, OP_REG rb, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];

			u128 RT;

			s64& lo =  (s64&)((s32*)&RT)[0];
			s64& mid = (s64&)((s32*)&RT)[1];
			s64& hi =  (s64&)((s32*)&RT)[2];

			const s64 a0 = ((s32*)&RA)[0];
			const s64 a1 = ((s32*)&RA)[1];
			const s64 b0 = ((s32*)&RB)[0];
			const s64 b1 = ((s32*)&RB)[1];

			lo = a0 * b0;
			hi = a1 * b1;

			mid += (a0 + a1) * (b0 + b1) - (lo + hi);

			CPU.GPR[rt] = RT._u64[1];

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		virtual void MULHW(OP_REG rd, OP_REG ra, OP_REG rb, bool rc)
		{
			CPU.GPR[rd] = (s64)(s32)((CPU.GPR[ra] * CPU.GPR[rb]) >> 32);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void LDARX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			CPU.reserve_addr = addr;
			CPU.reserve = true;
			CPU.GPR[rd] = Memory.Read64(addr);
		}
		virtual void DCBF(OP_REG ra, OP_REG rb)
		{
			//UNK("dcbf", false);
		}
		virtual void LBZX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			CPU.GPR[rd] = Memory.Read8(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void LVX(OP_REG vrd, OP_REG ra, OP_REG rb)
		{
			CPU.VPR[vrd] = Memory.Read128(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void NEG(OP_REG rt, OP_REG ra, OP_REG oe, bool rc)
		{
			CPU.GPR[rt] = 0-CPU.GPR[ra];
			if(oe) UNK("nego");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		virtual void LBZUX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			//if(ra == 0 || ra == rd) throw "Bad instruction [LBZUX]";

			const u64 addr = CPU.GPR[ra] + CPU.GPR[rb];
			CPU.GPR[rd] = Memory.Read8(addr);
			CPU.GPR[ra] = addr;
		}
		virtual void NOR(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = ~(CPU.GPR[rs] | CPU.GPR[rb]);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void SUBFE(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];
			CPU.GPR[ra] = ~RA + RB + CPU.XER.CA;
			CPU.XER.CA = ((u64)~RA + CPU.XER.CA > ~(u64)RB) | ((RA == 0) & CPU.XER.CA);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
			if(oe) UNK("subfeo");
		}
		virtual void ADDE(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];
			CPU.GPR[rd] = RA + RB + CPU.XER.CA;
			CPU.XER.CA = ((u64)RA + CPU.XER.CA > ~(u64)RB) | ((RA == -1) & CPU.XER.CA);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
			if(oe) UNK("addeo");
		}
		virtual void MTOCRF(OP_REG fxm, OP_REG rs)
		{
			u32 n = 0, count = 0;
			for(u32 i=0; i<8; ++i)
			{
				if(fxm & (1 << i))
				{
					n = i;
					count++;
				}
			}

			if(count == 1)
			{
				//CR[4*n : 4*n+3] = RS[32+4*n : 32+4*n+3];
				CPU.SetCR(n, (CPU.GPR[rs] >> (4*n)) & 0xf);
			}
			else CPU.CR.CR = 0;
		}
		virtual void STDX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			Memory.Write64((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), CPU.GPR[rs]);
		}
		virtual void STWCX_(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			CPU.SetCR(0, CPU.XER.SO ? CR_SO : 0);
			if(!CPU.reserve) return;

			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];

			if(addr == CPU.reserve_addr)
			{
				Memory.Write32(addr, CPU.GPR[rs]);
				CPU.SetCR_EQ(0, true);
			}
			else
			{
				static const bool u = 0;
				if(u) Memory.Write32(addr, CPU.GPR[rs]);
				CPU.SetCR_EQ(0, u);
				CPU.reserve = false;
			}
		}
		virtual void STWX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			Memory.Write32((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), CPU.GPR[rs]);
		}
		virtual void STDUX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			const u64 addr = CPU.GPR[ra] + CPU.GPR[rb];
			Memory.Write64(addr, CPU.GPR[rs]);
			CPU.GPR[ra] = addr;
		}
		virtual void ADDZE(OP_REG rd, OP_REG ra, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			CPU.GPR[rd] = RA + CPU.XER.CA;

			CPU.XER.CA = CPU.IsCarry(RA, CPU.XER.CA);
			if(oe) ConLog.Warning("addzeo");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void STDCX_(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			CPU.SetCR(0, CPU.XER.SO ? CR_SO : 0);
			if(!CPU.reserve) return;

			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];

			if(addr == CPU.reserve_addr)
			{
				Memory.Write64(addr, CPU.GPR[rs]);
				CPU.SetCR_EQ(0, true);
			}
			else
			{
				static const bool u = 0;
				if(u) Memory.Write64(addr, CPU.GPR[rs]);
				CPU.SetCR_EQ(0, u);
				CPU.reserve = false;
			}
		}
		virtual void STBX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			Memory.Write8((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), CPU.GPR[rs]);
		}
		virtual void STVX(OP_REG vrd, OP_REG ra, OP_REG rb)
		{
			Memory.Write128((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), CPU.VPR[vrd]);
		}
		virtual void MULLD(OP_REG rt, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			CPU.GPR[rt] = CPU.GPR[ra] * CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
			if(oe) UNK("mulldo");
		}
		virtual void ADDME(OP_REG rd, OP_REG ra, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			CPU.GPR[rd] = RA + CPU.XER.CA - 1;
			CPU.XER.CA |= RA != 0;

			if(oe) UNK("addmeo");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void MULLW(OP_REG rt, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			CPU.GPR[rt] = (s64)(s32)((s32)CPU.GPR[ra] * (s32)CPU.GPR[rb]);
			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[rt]);
			if(oe) UNK("mullwo");
		}
		virtual void DCBTST(OP_REG th, OP_REG ra, OP_REG rb)
		{
			//UNK("dcbtst", false);
		}
		/*0x108*///DOZ
		virtual void ADD(OP_REG rt, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const u64 RA = CPU.GPR[ra];
			const u64 RB = CPU.GPR[rb];
			CPU.GPR[rt] = RA + RB;
			CPU.XER.CA = CPU.IsCarry(RA, RB);
			if(oe) UNK("addo");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		virtual void DCBT(OP_REG ra, OP_REG rb, OP_REG th)
		{
			//UNK("dcbt", false);
		}
		virtual void LHZX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			CPU.GPR[rd] = Memory.Read16(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void EQV(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = ~(CPU.GPR[rs] ^ CPU.GPR[rb]);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void ECIWX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			//HACK!
			CPU.GPR[rd] = Memory.Read32(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void DIV(OP_REG rt, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			if(CPU.GPR[rb] == 0) return;
			if(oe) UNK("divo");
			CPU.GPR[rt] = CPU.GPR[ra] / CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		virtual void LHZUX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			CPU.GPR[rd] = Memory.Read16(addr);
			CPU.GPR[ra] = addr;
		}
		virtual void XOR(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rs] ^ CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void MFSPR(OP_REG rd, OP_REG spr)
		{
			CPU.GPR[rd] = GetRegBySPR(spr);
		}
		virtual void LHAX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			CPU.GPR[rd] = (s64)(s16)Memory.Read16(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
		}
		virtual void ABS(OP_REG rt, OP_REG ra, OP_REG oe, bool rc)
		{
			CPU.GPR[rt] = abs((s64)CPU.GPR[ra]);
			if(oe) UNK("abso");
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rt]);
		}
		/*0x16b*///DIVS
		virtual void MFTB(OP_REG rt, OP_REG spr)
		{
			const u32 n = (spr >> 5) | ((spr & 0x1f) << 5);

			switch(n)
			{
			case 268: CPU.GPR[rt] = CPU.TB /*& 0xffffffff*/; break;
			case 269: CPU.GPR[rt] = CPU.TB >> 32; break;
			default: UNK(wxString::Format("mftb r%d, %d", rt, spr)); break;
			}
		}
		virtual void LHAUX(OP_REG rd, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			CPU.GPR[rd] = (s64)(s16)Memory.Read16(addr);
			CPU.GPR[ra] = addr;
		}
		virtual void STHX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			const u64 addr = ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb];
			Memory.Write16(addr, CPU.GPR[rs]);
		}
		virtual void ORC(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rs] | ~CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void ECOWX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			//HACK!
			Memory.Write32((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), CPU.GPR[rs]);
		}
		virtual void OR(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rs] | CPU.GPR[rb];
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void DIVDU(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const u64 RA = CPU.GPR[ra];
			const u64 RB = CPU.GPR[rb];

			if(RB == 0)
			{
				if(oe) UNK("divduo");
				CPU.GPR[rd] = 0;
			}
			else
			{
				CPU.GPR[rd] = RA / RB;
			}

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void DIVWU(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const u32 RA = CPU.GPR[ra];
			const u32 RB = CPU.GPR[rb];

			if(RB == 0)
			{
				if(oe) UNK("divwuo");
				CPU.GPR[rd] = 0;
			}
			else
			{
				CPU.GPR[rd] = RA / RB;
			}

			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[rd]);
		}
		virtual void MTSPR(OP_REG spr, OP_REG rs)
		{
			GetRegBySPR(spr) = CPU.GPR[rs];
		}
		/*0x1d6*///DCBI
		virtual void DIVD(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s64 RA = CPU.GPR[ra];
			const s64 RB = CPU.GPR[rb];

			if (RB == 0 || ((u64)RA == 0x8000000000000000 && RB == -1))
			{
				if(oe) UNK("divdo");
				CPU.GPR[rd] = (((u64)RA & 0x8000000000000000) && RB == 0) ? -1 : 0;
			}
			else
			{
				CPU.GPR[rd] = RA / RB;
			}

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[rd]);
		}
		virtual void DIVW(OP_REG rd, OP_REG ra, OP_REG rb, OP_REG oe, bool rc)
		{
			const s32 RA = CPU.GPR[ra];
			const s32 RB = CPU.GPR[rb];

			if (RB == 0 || ((u32)RA == 0x80000000 && RB == -1))
			{
				if(oe) UNK("divwo");
				CPU.GPR[rd] = (((u32)RA & 0x80000000) && RB == 0) ? -1 : 0;
			}
			else
			{
				CPU.GPR[rd] = (s64)(RA / RB);
			}

			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[rd]);
		}
		virtual void LFSX(OP_REG frd, OP_REG ra, OP_REG rb)
		{
			u64 v = Memory.Read64(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]);
			CPU.FPR[frd] = *(double*)&v;
		}
		virtual void SRW(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rb] & 0x20 ? 0 : (u32)CPU.GPR[rs] >> (CPU.GPR[rb] & 0x1f);
			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
		}
		virtual void SRD(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			CPU.GPR[ra] = CPU.GPR[rb] & 0x40 ? 0 : CPU.GPR[rs] >> (CPU.GPR[rb] & 0x3f);
			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		/*0x237*///LFSUX
		virtual void SYNC(OP_REG l)
		{
		}
		/*0x257*///LFDX
		/*0x28a*///LDUX
		/*0x277*///LFDUX
		virtual void STFSX(OP_REG rs, OP_REG ra, OP_REG rb)
		{
			float v = (float)CPU.FPR[rs];
			Memory.Write32((ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb]), *(u32*)&v);
		}
		/*0x316*///LHBRX
		virtual void SRAW(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{    
			s32 RS = CPU.GPR[rs];
			s32 RB = CPU.GPR[rb];
			CPU.GPR[ra] = RS >> RB;
			CPU.XER.CA = (RS < 0) & ((CPU.GPR[ra] << RB) != RS);

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void SRAD(OP_REG ra, OP_REG rs, OP_REG rb, bool rc)
		{
			s64 RS = CPU.GPR[rs];
			s64 RB = CPU.GPR[rb];
			CPU.GPR[ra] = RS >> RB;
			CPU.XER.CA = (RS < 0) & ((CPU.GPR[ra] << RB) != RS);

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void SRAWI(OP_REG ra, OP_REG rs, OP_REG sh, bool rc)
		{
			s32 RS = CPU.GPR[rs];
			CPU.GPR[ra] = RS >> sh;
			CPU.XER.CA = (RS < 0) & ((CPU.GPR[ra] << sh) != RS);

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void SRADI1(OP_REG ra, OP_REG rs, OP_REG sh, bool rc)
		{
			s64 RS = CPU.GPR[rs];
			CPU.GPR[ra] = RS >> sh;
			CPU.XER.CA = (RS < 0) & ((CPU.GPR[ra] << sh) != RS);

			if(rc) CPU.UpdateCR0<s64>(CPU.GPR[ra]);
		}
		virtual void SRADI2(OP_REG ra, OP_REG rs, OP_REG sh, bool rc)
		{
			SRADI1(ra, rs, sh, rc);
		}
		virtual void EIEIO()
		{
		}
		virtual void EXTSH(OP_REG ra, OP_REG rs, bool rc)
		{
			CPU.GPR[ra] = (s64)(s16)CPU.GPR[rs];
			if(rc) CPU.UpdateCR0<s16>(CPU.GPR[ra]);
		}
		virtual void EXTSB(OP_REG ra, OP_REG rs, bool rc)
		{
			CPU.GPR[ra] = (s64)(s8)CPU.GPR[rs];
			if(rc) CPU.UpdateCR0<s8>(CPU.GPR[ra]);
		}
		virtual void STFIWX(OP_REG frs, OP_REG ra, OP_REG rb)
		{
			Memory.Write32(ra ? CPU.GPR[ra] + CPU.GPR[rb] : CPU.GPR[rb], FPRdouble::To32(CPU.FPR[frs]));
		}
		virtual void EXTSW(OP_REG ra, OP_REG rs, bool rc)
		{
			CPU.GPR[ra] = (s64)(s32)CPU.GPR[rs];
			if(rc) CPU.UpdateCR0<s32>(CPU.GPR[ra]);
		}
		/*0x3d6*///ICBI
		virtual void DCBZ(OP_REG ra, OP_REG rs)
		{
			//UNK("dcbz", false);
		}
	END_OPCODES_GROUP(G_1f);

	virtual void LWZ(OP_REG rt, OP_REG ra, OP_sIMM d)
	{
		CPU.GPR[rt] = Memory.Read32(ra ? CPU.GPR[ra] + d : d);
	}
	virtual void LWZU(OP_REG rt, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		CPU.GPR[rt] = Memory.Read32(addr);
		CPU.GPR[ra] = addr;
	}
	virtual void LBZ(OP_REG rt, OP_REG ra, OP_sIMM d)
	{
		CPU.GPR[rt] = Memory.Read8(ra ? CPU.GPR[ra] + d : d);
	}
	virtual void LBZU(OP_REG rt, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		CPU.GPR[rt] = Memory.Read8(addr);
		CPU.GPR[ra] = addr;
	}
	virtual void STW(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		Memory.Write32(ra ? CPU.GPR[ra] + d : d, CPU.GPR[rs]);
	}
	virtual void STWU(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		Memory.Write32(addr, CPU.GPR[rs]);
		CPU.GPR[ra] = addr;
	}
	virtual void STB(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		Memory.Write8(ra ? CPU.GPR[ra] + d : d, CPU.GPR[rs]);
	}
	virtual void STBU(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		Memory.Write8(addr, CPU.GPR[rs]);
		CPU.GPR[ra] = addr;
	}
	virtual void LHZ(OP_REG rd, OP_REG ra, OP_sIMM d)
	{
		CPU.GPR[rd] = Memory.Read16(ra ? CPU.GPR[ra] + d : d);
	}
	virtual void LHZU(OP_REG rd, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		CPU.GPR[rd] = Memory.Read16(addr);
		CPU.GPR[ra] = addr;
	}
	virtual void STH(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		Memory.Write16(ra ? CPU.GPR[ra] + d : d, CPU.GPR[rs]);
	}
	virtual void STHU(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		Memory.Write16(addr, CPU.GPR[rs]);
		CPU.GPR[ra] = addr;
	}
	virtual void LMW(OP_REG rd, OP_REG ra, OP_sIMM d)
	{
		u64 addr = ra ? CPU.GPR[ra] + d : d;
		for(u32 i=rd; i<32; ++i, addr += 4)
		{
			CPU.GPR[i] = Memory.Read32(addr);
		}
	}
	virtual void STMW(OP_REG rs, OP_REG ra, OP_sIMM d)
	{
		u64 addr = ra ? CPU.GPR[ra] + d : d;
		for(u32 i=rs; i<32; ++i, addr += 4)
		{
			Memory.Write32(addr, CPU.GPR[i]);
		}
	}
	virtual void LFS(OP_REG frt, OP_REG ra, OP_sIMM d)
	{
		const u32 v = Memory.Read32(ra ? CPU.GPR[ra] + d : d);
		CPU.FPR[frt] = *(float*)&v;
	}
	virtual void LFSU(OP_REG frt, OP_REG ra, OP_sIMM ds)
	{
		const u64 addr = CPU.GPR[ra] + ds;
		const u32 v = Memory.Read32(addr);
		CPU.FPR[frt] = *(float*)&v;
		CPU.GPR[ra] = addr;
	}
	virtual void LFD(OP_REG frt, OP_REG ra, OP_sIMM d)
	{
		const u64 v = Memory.Read64(ra ? CPU.GPR[ra] + d : d);
		CPU.FPR[frt] = *(double*)&v;
	}
	virtual void LFDU(OP_REG frt, OP_REG ra, OP_sIMM ds)
	{
		const u64 addr = CPU.GPR[ra] + ds;
		const u64 v = Memory.Read64(addr);
		CPU.FPR[frt] = *(double*)&v;
		CPU.GPR[ra] = addr;
	}
	virtual void STFS(OP_REG frs, OP_REG ra, OP_sIMM d)
	{
		Memory.Write32(ra ? CPU.GPR[ra] + d : d, FPRdouble::To32(CPU.FPR[frs]));
	}
	virtual void STFSU(OP_REG frs, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		Memory.Write32(addr, FPRdouble::To32(CPU.FPR[frs]));
		CPU.GPR[ra] = addr;
	}
	virtual void STFD(OP_REG frs, OP_REG ra, OP_sIMM d)
	{
		Memory.Write64(ra ? CPU.GPR[ra] + d : d, *(u64*)&CPU.FPR[frs]);
	}
	virtual void STFDU(OP_REG frs, OP_REG ra, OP_sIMM d)
	{
		const u64 addr = CPU.GPR[ra] + d;
		Memory.Write64(addr, *(u64*)&CPU.FPR[frs]);
		CPU.GPR[ra] = addr;
	}
	
	START_OPCODES_GROUP(G_3a)
		virtual void LD(OP_REG rt, OP_REG ra, OP_sIMM ds)
		{
			CPU.GPR[rt] = Memory.Read64(ra ? CPU.GPR[ra] + ds : ds);
		}
		virtual void LDU(OP_REG rt, OP_REG ra, OP_sIMM ds)
		{
			//if(ra == 0 || rt == ra) return;
			const u64 addr = CPU.GPR[ra] + ds;
			CPU.GPR[rt] = Memory.Read64(addr);
			CPU.GPR[ra] = addr;
		}
	END_OPCODES_GROUP(G_3a);

	START_OPCODES_GROUP(G_3b)
		virtual void FDIVS(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			//if(CPU.FPR[frb] == 0.0) return;
			CPU.FPR[frt] = (float)(CPU.FPR[fra] / CPU.FPR[frb]);
			if(rc) ConLog.Warning("fdivs.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FSUBS(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(CPU.FPR[fra] - CPU.FPR[frb]);
			if(rc) ConLog.Warning("fsubs.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FADDS(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(CPU.FPR[fra] + CPU.FPR[frb]);
			if(rc) ConLog.Warning("fadds.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FSQRTS(OP_REG frt, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = sqrt((float)CPU.FPR[frb]);
			if(rc) ConLog.Warning("fsqrts.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FRES(OP_REG frt, OP_REG frb, bool rc)
		{
			//if(CPU.FPR[frb] == 0.0) return;
			CPU.FPR[frt] = (float)(1.0f/CPU.FPR[frb]);
			if(rc) ConLog.Warning("fres.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMULS(OP_REG frt, OP_REG fra, OP_REG frc, bool rc)
		{
			CPU.FPR[frt] = (float)(CPU.FPR[fra] * CPU.FPR[frc]);
			if(rc) ConLog.Warning("fmuls.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMADDS(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(CPU.FPR[fra] * CPU.FPR[frc] + CPU.FPR[frb]);
			if(rc) ConLog.Warning("fmadds.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMSUBS(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(CPU.FPR[fra] * CPU.FPR[frc] - CPU.FPR[frb]);
			if(rc) ConLog.Warning("fmsubs.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FNMSUBS(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(-(CPU.FPR[fra] * CPU.FPR[frc] - CPU.FPR[frb]));
			if(rc) ConLog.Warning("fnmsubs.");////CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FNMADDS(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)(-(CPU.FPR[fra] * CPU.FPR[frc] + CPU.FPR[frb]));
			if(rc) ConLog.Warning("fnmadds.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
	END_OPCODES_GROUP(G_3b);
	
	START_OPCODES_GROUP(G_3e)
		virtual void STD(OP_REG rs, OP_REG ra, OP_sIMM d)
		{
			Memory.Write64(ra ? CPU.GPR[ra] + d : d, CPU.GPR[rs]);
		}
		virtual void STDU(OP_REG rs, OP_REG ra, OP_sIMM ds)
		{
			//if(ra == 0 || rs == ra) return;
			const u64 addr = CPU.GPR[ra] + ds;
			Memory.Write64(addr, CPU.GPR[rs]);
			CPU.GPR[ra] = addr;
		}
	END_OPCODES_GROUP(G_3e);

	START_OPCODES_GROUP(G_3f)
		virtual void MTFSB1(OP_REG bt, bool rc)
		{
			UNK("mtfsb1");
		}
		virtual void MCRFS(OP_REG bf, OP_REG bfa)
		{
			UNK("mcrfs");
		}
		virtual void MTFSB0(OP_REG bt, bool rc)
		{
			UNK("mtfsb0");
		}
		virtual void MTFSFI(OP_REG crfd, OP_REG i, bool rc)
		{
			UNK("mtfsfi");
		}
		virtual void MFFS(OP_REG frt, bool rc)
		{
			UNK("mffs");
		}
		virtual void MTFSF(OP_REG flm, OP_REG frb, bool rc)
		{
			UNK("mtfsf");
		}
		virtual void FCMPU(OP_REG crfd, OP_REG fra, OP_REG frb)
		{
			if((CPU.FPSCR.FPRF = FPRdouble::Cmp(CPU.FPR[fra], CPU.FPR[frb])) == 1)
			{
				if(FPRdouble::IsSNaN(CPU.FPR[fra]) || FPRdouble::IsSNaN(CPU.FPR[frb]))
				{
					CPU.SetFPSCRException(FPSCR_VXSNAN);
				}
			}

			CPU.SetCR(crfd, CPU.FPSCR.FPRF);
		}
		virtual void FRSP(OP_REG frt, OP_REG frb, bool rc)
		{
			const double b = CPU.FPR[frb];
			const double r = (double)(float)b;
			CPU.FPSCR.FR = fabs(r) > fabs(b);
			CPU.FPSCR.FI = b != r;
			//CPU.FPSCR.FPRF
			CPU.FPR[frt] = r;
		}
		virtual void FCTIW(OP_REG frt, OP_REG frb, bool rc)
		{
			if(CPU.FPR[frb] > (double)0x7fffffff)
			{
				CPU.SetFPSCRException(FPSCR_VXCVI);
				CPU.FPSCR.FR = 0;
				CPU.SetFPSCR_FI(0);

				*(u64*)&CPU.FPR[frt] = 0x7fffffff;
			}
			else if(CPU.FPR[frb] < -(double)0x80000000)
			{
				CPU.SetFPSCRException(FPSCR_VXCVI);
				CPU.FPSCR.FR = 0;
				CPU.SetFPSCR_FI(0);

				*(u64*)&CPU.FPR[frt] = 0x80000000;
			}
			else
			{
				s32 i = 0;
				switch(CPU.FPSCR.RN)
				{
					case FPSCR_RN_NEAR:
					{
						const double FRB_T = CPU.FPR[frb] + 0.5;
						i = (s32)FRB_T;
						if(FRB_T - i < 0 || (FRB_T - i == 0 && CPU.FPR[frb] > 0)) i--;
					}
					break;

					case FPSCR_RN_ZERO:
					{
						i = (s32)CPU.FPR[frb];
					}
					break;

					case FPSCR_RN_PINF:
					{
						i = (s32)CPU.FPR[frb];
						if(CPU.FPR[frb] - i > 0) i++;
					}
					break;

					case FPSCR_RN_MINF:
					{
						i = (s32)CPU.FPR[frb];
						if(CPU.FPR[frb] - i < 0) i--;
					}
					break;
                }

				*(u64*)&CPU.FPR[frt] = (u32)i;
				const double di = i;

				if(di == CPU.FPR[frb])
				{
					CPU.SetFPSCR_FI(0);
					CPU.FPSCR.FR = 0;
				}
				else
				{
					CPU.SetFPSCR_FI(1);
					CPU.FPSCR.FR = fabs(di) > fabs(CPU.FPR[frb]);
				}
			}

			*(u64*)&CPU.FPR[frt] |= 0xfff8000000000000ULL;

			if(*(u64*)&CPU.FPR[frt] == 0 && ((*(u64*)&CPU.FPR[frt]) & DOUBLE_SIGN))
			{
				*(u64*)&CPU.FPR[frt] |= 0x100000000ULL;
			}

			if(rc) ConLog.Warning("fctiw.");
		}
		virtual void FCTIWZ(OP_REG frt, OP_REG frb, bool rc)
		{
			if(CPU.FPR[frb] > (double)0x7fffffff)
			{
				CPU.SetFPSCRException(FPSCR_VXCVI);
				CPU.FPSCR.FR = 0;
				CPU.SetFPSCR_FI(0);

				*(u64*)&CPU.FPR[frt] = 0x7fffffff;
			}
			else if(CPU.FPR[frb] < -(double)0x80000000)
			{
				CPU.SetFPSCRException(FPSCR_VXCVI);
				CPU.FPSCR.FR = 0;
				CPU.SetFPSCR_FI(0);

				*(u64*)&CPU.FPR[frt] = 0x80000000;
			}
			else
			{
				const s32 iFRB = (s32)CPU.FPR[frb];
				const double dFRB = iFRB;

				if(iFRB == dFRB)
				{
					CPU.SetFPSCR_FI(0);
					CPU.FPSCR.FR = 0;
				}
				else
				{
					CPU.SetFPSCR_FI(1);
					CPU.FPSCR.FR = fabs(dFRB) > fabs(CPU.FPR[frb]);
				}

				*(u64*)&CPU.FPR[frt] = (u32)iFRB;
			}

			*(u64*)&CPU.FPR[frt] |= 0xfff8000000000000ULL;

			if(*(u64*)&CPU.FPR[frt] == 0 && ((*(u64*)&CPU.FPR[frt]) & DOUBLE_SIGN))
			{
				*(u64*)&CPU.FPR[frt] |= 0x100000000ULL;
			}

			if(rc) ConLog.Warning("fctiwz.");
		}
		virtual void FDIV(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			//if(CPU.FPR[frb] == 0.0) return;
			CPU.FPR[frt] = CPU.FPR[fra] / CPU.FPR[frb];
			if(rc) ConLog.Warning("fdiv.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FSUB(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] - CPU.FPR[frb];
			if(rc) ConLog.Warning("fsub.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FADD(OP_REG frt, OP_REG fra, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] + CPU.FPR[frb];
			if(rc) ConLog.Warning("fadd.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FSQRT(OP_REG frt, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = sqrt(CPU.FPR[frb]);
			if(rc) ConLog.Warning("fsqrt.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FSEL(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] < 0.0 ? CPU.FPR[frc] : CPU.FPR[frb];
			if(rc) ConLog.Warning("fsel.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMUL(OP_REG frt, OP_REG fra, OP_REG frc, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] * CPU.FPR[frc];
			CPU.FPSCR.FI = 0;
			CPU.FPSCR.FR = 0;
			if(rc) ConLog.Warning("fmul.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FRSQRTE(OP_REG frt, OP_REG frb, bool rc)
		{
			UNK("frsqrte");
		}
		virtual void FMSUB(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] * CPU.FPR[frc] - CPU.FPR[frb];
			if(rc) ConLog.Warning("fmsub.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMADD(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[fra] * CPU.FPR[frc] + CPU.FPR[frb];
			if(rc) ConLog.Warning("fmadd.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FNMSUB(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = -(CPU.FPR[fra] * CPU.FPR[frc] - CPU.FPR[frb]);
			if(rc) ConLog.Warning("fnmsub.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FNMADD(OP_REG frt, OP_REG fra, OP_REG frc, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = -(CPU.FPR[fra] * CPU.FPR[frc] + CPU.FPR[frb]);
			if(rc) ConLog.Warning("fnmadd.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FCMPO(OP_REG crfd, OP_REG fra, OP_REG frb)
		{
			if((CPU.FPSCR.FPRF = FPRdouble::Cmp(CPU.FPR[fra], CPU.FPR[frb])) == 1)
			{
				if(FPRdouble::IsSNaN(CPU.FPR[fra]) || FPRdouble::IsSNaN(CPU.FPR[frb]))
				{
					CPU.SetFPSCRException(FPSCR_VXSNAN);
					if(!CPU.FPSCR.VE) CPU.SetFPSCRException(FPSCR_VXVC);
				}
				else if(FPRdouble::IsQNaN(CPU.FPR[fra]) || FPRdouble::IsQNaN(CPU.FPR[frb]))
				{
					CPU.SetFPSCRException(FPSCR_VXVC);
				}

				CPU.FPSCR.FX = 1;
			}

			CPU.SetCR(crfd, CPU.FPSCR.FPRF);
		}
		virtual void FNEG(OP_REG frt, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = -CPU.FPR[frb];
			if(rc) ConLog.Warning("fneg.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FMR(OP_REG frt, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = CPU.FPR[frb];
			if(rc) ConLog.Warning("fmr.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FNABS(OP_REG frt, OP_REG frb, bool rc)
		{
			*(u64*)&CPU.FPR[frt] = *(u64*)&CPU.FPR[frb] | 0x8000000000000000ULL;
			if(rc) ConLog.Warning("fnabs.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FABS(OP_REG frd, OP_REG frb, bool rc)
		{
			CPU.FPR[frd] = fabs(CPU.FPR[frb]);
			if(rc) ConLog.Warning("fabs.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
		virtual void FCTID(OP_REG frt, OP_REG frb, bool rc)
		{
			UNK("fctid");
		}
		virtual void FCTIDZ(OP_REG frt, OP_REG frb, bool rc)
		{
			UNK("fctidz");
		}
		virtual void FCFID(OP_REG frt, OP_REG frb, bool rc)
		{
			CPU.FPR[frt] = (float)CPU.FPR[frb];
			if(rc) ConLog.Warning("fcfid.");//CPU.UpdateCR1(CPU.FPR[frt]);
		}
	END_OPCODES_GROUP(G_3f);

	bool IsVecGcode(const u32 gcode)
	{
		switch(gcode)
		{
		case 0x47: case 0x67: case 0x7:
		case 0x26: case 0xe7: case 0x207:
		case 0xc7: case 0x6:
			return true;
		}

		return false;
	}

	virtual void UNK(const s32 code, const s32 opcode, const s32 gcode)
	{
		UNK(wxString::Format("Unknown/Illegal opcode! (0x%08x : 0x%x : 0x%x)", code, opcode, gcode),
			opcode != 0x4 && !(opcode == 0x1f && IsVecGcode(gcode)));
	}

	void UNK(const wxString& err, bool pause = true)
	{
		ConLog.Error(err + wxString::Format(" #pc: 0x%llx", CPU.PC));

		if(!pause) return;

		Emu.Pause();

		for(uint i=0; i<32; ++i) ConLog.Write("r%d = 0x%llx", i, CPU.GPR[i]);
		for(uint i=0; i<32; ++i) ConLog.Write("f%d = %llf", i, CPU.FPR[i]);
		for(uint i=0; i<32; ++i) ConLog.Write("v%d = 0x%s", i, CPU.VPR[i].ToString());
		ConLog.Write("CR = 0x%08x", CPU.CR);
		ConLog.Write("LR = 0x%llx", CPU.LR);
		ConLog.Write("CTR = 0x%llx", CPU.CTR);
		ConLog.Write("XER = 0x%llx", CPU.XER);
	}
};

#undef START_OPCODES_GROUP
#undef END_OPCODES_GROUP