// The core: an in-order five-stage RV64I pipeline with machine mode, written
// as docs/hls-coding-standard.md asks -- one loop at II=1 whose body is one
// clock cycle, explicit stage registers, the stages evaluated write-back
// first, one stall decision before any stage register updates, forwarding by
// comparing register indices.
//
// A template of the generator: what varies by configuration comes from
// config.hpp (cfg::), never from a constant here.
//
// Precision. Instructions that read or write CSRs or change the privilege
// state -- CSR accesses, ecall, ebreak, mret, wfi, fence.i, and anything
// illegal -- are serialised: each waits in ID until the stages ahead are
// empty, executes alone in EX, and refetches what follows it. Faults found in
// EX (a misaligned target, a misaligned or out-of-range access) trap there;
// what is older is already past EX and cannot fault. Every step reaches WB in
// program order and is reported there.
#include "core.hpp"

namespace {

enum Op : unsigned char {
	OP_NOP, OP_ALU, OP_LUI, OP_AUIPC, OP_JAL, OP_JALR, OP_BRANCH, OP_LOAD, OP_STORE, OP_FENCE,
	// serialised:
	OP_CSR, OP_ECALL, OP_EBREAK, OP_MRET, OP_WFI, OP_FENCEI, OP_ILLEGAL, OP_FETCHFAULT,
};

inline bool serial(unsigned char op) { return op >= OP_CSR; }

// Exception causes.
constexpr unsigned C_MISALIGNED_FETCH = 0, C_FETCH_ACCESS = 1, C_ILLEGAL = 2, C_BREAKPOINT = 3,
                   C_MISALIGNED_LOAD = 4, C_LOAD_ACCESS = 5, C_MISALIGNED_STORE = 6, C_STORE_ACCESS = 7,
                   C_ECALL_M = 11;

// mstatus, machine mode only: MIE and MPIE are writable, MPP is always M.
constexpr unsigned long long MSTATUS_MIE = 1ull << 3, MSTATUS_MPIE = 1ull << 7, MSTATUS_MPP = 3ull << 11;

struct IfId {
	bool valid;
	u64 pc;
	ap_uint<32> insn;
	bool fault;              // the fetch was outside memory
};

struct IdEx {
	bool valid;
	u64 pc;
	ap_uint<32> insn;
	bool compressed;
	unsigned char op;
	ap_uint<5> rs1, rs2, rd;
	u64 imm;
	ap_uint<3> f3;
	bool alt;                // insn[30]: SUB, SRA
	bool word;               // the *W forms: 32-bit, sign-extended
	bool imm_b;              // the ALU's second operand is the immediate
	bool wb;
	u64 rs1v, rs2v;          // read in ID, forwarded in EX
};

struct ExMem {
	bool valid;
	retire_t r;              // the record, filled in as the step goes
	bool load;
	ap_uint<3> f3;
	bool count;              // counts towards minstret
};

struct MemWb {
	bool valid;
	retire_t r;
	bool count;
};

inline u64 sext(u64 v, int bits)
{
	const int shift = 64 - bits;
	return (u64)(((ap_int<64>)(v << shift)) >> shift);
}

inline u64 alu(ap_uint<3> f3, bool alt, bool word, u64 a, u64 b)
{
	if (word) {
		const ap_uint<32> a32 = a.range(31, 0), b32 = b.range(31, 0);
		const ap_uint<5> sh = b.range(4, 0);
		ap_uint<32> r = 0;
		switch (f3) {
		case 0: r = alt ? (ap_uint<32>)(a32 - b32) : (ap_uint<32>)(a32 + b32); break;
		case 1: r = a32 << sh; break;
		case 5: r = alt ? (ap_uint<32>)((ap_int<32>)a32 >> sh) : (ap_uint<32>)(a32 >> sh); break;
		default: break;
		}
		return sext((u64)r, 32);
	}
	const ap_uint<6> sh = b.range(5, 0);
	switch (f3) {
	case 0: return alt ? (u64)(a - b) : (u64)(a + b);
	case 1: return a << sh;
	case 2: return (ap_int<64>)a < (ap_int<64>)b ? 1 : 0;
	case 3: return a < b ? 1 : 0;
	case 4: return a ^ b;
	case 5: return alt ? (u64)((ap_int<64>)a >> sh) : (u64)(a >> sh);
	case 6: return a | b;
	default: return a & b;
	}
}

inline bool branch_taken(ap_uint<3> f3, u64 a, u64 b)
{
	switch (f3) {
	case 0: return a == b;
	case 1: return a != b;
	case 4: return (ap_int<64>)a < (ap_int<64>)b;
	case 5: return (ap_int<64>)a >= (ap_int<64>)b;
	case 6: return a < b;
	case 7: return a >= b;
	default: return false;
	}
}

inline bool in_memory(u64 addr, unsigned size)
{
	return addr >= cfg::MEM_BASE && addr + size <= cfg::MEM_BASE + 8ull * cfg::MEM_WORDS;
}

// Decode: what an instruction is, and what is illegal on this core.
IdEx decode(const IfId &f, const u64 rf[32])
{
	const ap_uint<32> in = f.insn;
	const ap_uint<7> opc = in.range(6, 0);
	const ap_uint<5> rd = in.range(11, 7), rs1 = in.range(19, 15), rs2 = in.range(24, 20);
	const ap_uint<3> f3 = in.range(14, 12);
	const ap_uint<7> f7 = in.range(31, 25);
	const ap_uint<6> f6 = in.range(31, 26);
	const u64 imm_i = sext((u64)in.range(31, 20), 12);
	const u64 imm_s = sext((u64)(in.range(31, 25), in.range(11, 7)), 12);
	// Single bits are ap_uint<1> in a concatenation: on a const ap_uint, in[31]
	// is a plain bool, and (bool, x) is C++'s own comma operator, which drops
	// the bit without a word -- the branch offsets lost their sign that way.
	const ap_uint<1> b31 = in[31], b20 = in[20], b7 = in[7];
	const u64 imm_b = sext((u64)(b31, b7, in.range(30, 25), in.range(11, 8), ap_uint<1>(0)), 13);
	const u64 imm_u = sext((u64)(in.range(31, 12), ap_uint<12>(0)), 32);
	const u64 imm_j = sext((u64)(b31, in.range(19, 12), b20, in.range(30, 21), ap_uint<1>(0)), 21);

	IdEx d = {};
	d.valid = true;
	d.pc = f.pc;
	d.insn = in;
	d.rs1 = rs1;
	d.rs2 = rs2;
	d.rd = rd;
	d.f3 = f3;
	d.alt = in[30];
	d.rs1v = rf[rs1];
	d.rs2v = rf[rs2];
	d.op = OP_ILLEGAL;
	if (f.fault) { d.op = OP_FETCHFAULT; return d; }
	// No C: a 16-bit encoding is illegal, and names its 16 bits.
	if (in.range(1, 0) != 3) { d.compressed = true; d.insn = in.range(15, 0); return d; }

	switch (opc) {
	case 0x37: d.op = OP_LUI; d.imm = imm_u; d.wb = true; break;
	case 0x17: d.op = OP_AUIPC; d.imm = imm_u; d.wb = true; break;
	case 0x6F: d.op = OP_JAL; d.imm = imm_j; d.wb = true; break;
	case 0x67: if (f3 == 0) { d.op = OP_JALR; d.imm = imm_i; d.wb = true; } break;
	case 0x63: if (f3 != 2 && f3 != 3) { d.op = OP_BRANCH; d.imm = imm_b; } break;
	case 0x03: if (f3 != 7) { d.op = OP_LOAD; d.imm = imm_i; d.wb = true; } break;
	case 0x23: if (f3 <= 3) { d.op = OP_STORE; d.imm = imm_s; } break;
	case 0x13:   // OP-IMM: the shifts take a 6-bit shamt, SRAI with insn[30]
		if (f3 == 1 ? f6 == 0 : f3 == 5 ? (f6 == 0 || f6 == 0x10) : true) {
			d.op = OP_ALU; d.imm = imm_i; d.imm_b = true; d.wb = true;
			if (f3 != 5) d.alt = false;
		}
		break;
	case 0x1B:   // OP-IMM-32
		if (f3 == 0 || (f3 == 1 && f7 == 0) || (f3 == 5 && (f7 == 0 || f7 == 0x20))) {
			d.op = OP_ALU; d.imm = imm_i; d.imm_b = true; d.word = true; d.wb = true;
			if (f3 != 5) d.alt = false;
		}
		break;
	case 0x33:   // OP: funct7 0, or 0x20 for SUB and SRA
		if (f7 == 0 || (f7 == 0x20 && (f3 == 0 || f3 == 5))) { d.op = OP_ALU; d.wb = true; }
		break;
	case 0x3B:   // OP-32
		if ((f3 == 0 && (f7 == 0 || f7 == 0x20)) || (f3 == 1 && f7 == 0) || (f3 == 5 && (f7 == 0 || f7 == 0x20))) {
			d.op = OP_ALU; d.word = true; d.wb = true;
		}
		break;
	case 0x0F:   // MISC-MEM: fence, and fence.i (Zifencei)
		if (f3 == 0) d.op = OP_FENCE;
		else if (f3 == 1) d.op = OP_FENCEI;
		break;
	case 0x73:   // SYSTEM
		if (f3 == 0) {
			if (in == 0x00000073) d.op = OP_ECALL;
			else if (in == 0x00100073) d.op = OP_EBREAK;
			else if (in == 0x30200073) d.op = OP_MRET;
			else if (in == 0x10500073) d.op = OP_WFI;
		} else if (f3 != 4) {
			d.op = OP_CSR; d.wb = true;
		}
		break;
	default: break;
	}
	return d;
}

} // namespace

void core_top(byte_t m0[cfg::MEM_WORDS], byte_t m1[cfg::MEM_WORDS], byte_t m2[cfg::MEM_WORDS],
              byte_t m3[cfg::MEM_WORDS], byte_t m4[cfg::MEM_WORDS], byte_t m5[cfg::MEM_WORDS],
              byte_t m6[cfg::MEM_WORDS], byte_t m7[cfg::MEM_WORDS], u64 tohost, u64 max_cycles,
              hls::stream<retire_t> &retire, u64 &cycles)
{
#pragma HLS INTERFACE mode=bram port=m0 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m1 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m2 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m3 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m4 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m5 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m6 storage_type=ram_t2p
#pragma HLS INTERFACE mode=bram port=m7 storage_type=ram_t2p
	u64 rf[32];
#pragma HLS ARRAY_PARTITION variable=rf type=complete
	for (int i = 0; i < 32; i++) rf[i] = 0;

	// Machine-mode CSRs.
	u64 mstatus = 0, mie = 0, mtvec = 0, mscratch = 0, mepc = 0, mcause = 0, mtval = 0;
	u64 mcycle = 0, minstret = 0, mtime = 0, tick_phase = 0, mcountinhibit = 0;
	// tselect with no triggers: it holds what was written and reads back the
	// inverse, so no index ever selects a trigger (tdata1-3 do not exist).
	u64 tselect = 0;

	IfId ifid = {};
	IdEx idex = {};
	ExMem exmem = {};
	MemWb memwb = {};
	u64 pc = cfg::MEM_BASE;
	bool done = false;
	u64 cycle = 0;

	for (cycle = 0; cycle < max_cycles && !done; cycle++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=m0 type=inter false
#pragma HLS DEPENDENCE variable=m1 type=inter false
#pragma HLS DEPENDENCE variable=m2 type=inter false
#pragma HLS DEPENDENCE variable=m3 type=inter false
#pragma HLS DEPENDENCE variable=m4 type=inter false
#pragma HLS DEPENDENCE variable=m5 type=inter false
#pragma HLS DEPENDENCE variable=m6 type=inter false
#pragma HLS DEPENDENCE variable=m7 type=inter false
		bool minstret_written = false, mcycle_written = false;

		// ---- WB: first, so ID below reads what is written this cycle ----
		if (memwb.valid) {
			if (memwb.r.rd_we && memwb.r.rd != 0) rf[memwb.r.rd] = memwb.r.rd_val;
			if (memwb.count) minstret++;
			retire.write(memwb.r);
			if (memwb.r.st && memwb.r.st_addr == tohost) done = true;
		}

		// ---- MEM ----
		MemWb mw_next = {};
		if (exmem.valid) {
			retire_t r = exmem.r;
			const u64 off = r.st ? r.st_addr - cfg::MEM_BASE : r.rd_val - cfg::MEM_BASE;   // the address, for a load
			const unsigned w = (unsigned)(off >> 3);
			const unsigned lane = (unsigned)(off & 7);
			if (exmem.load) {
				const u64 word = (u64)(m7[w], m6[w], m5[w], m4[w], m3[w], m2[w], m1[w], m0[w]);
				const u64 v = word >> (lane * 8);
				switch (exmem.f3) {
				case 0: r.rd_val = sext(v.range(7, 0), 8); break;
				case 1: r.rd_val = sext(v.range(15, 0), 16); break;
				case 2: r.rd_val = sext(v.range(31, 0), 32); break;
				case 4: r.rd_val = v.range(7, 0); break;
				case 5: r.rd_val = v.range(15, 0); break;
				case 6: r.rd_val = v.range(31, 0); break;
				default: r.rd_val = v; break;
				}
			}
			// One access a cycle on the memory's data port: a load or a
			// store, never both -- so they are one if/else, which HLS can
			// put on one port. Fetch has the other (ram_t2p).
			else if (r.st) {
				const u64 d = r.st_val << (lane * 8);
				const ap_uint<8> be = (ap_uint<8>)(((1u << r.st_size) - 1) << lane);
				if (be[0]) m0[w] = d.range(7, 0);
				if (be[1]) m1[w] = d.range(15, 8);
				if (be[2]) m2[w] = d.range(23, 16);
				if (be[3]) m3[w] = d.range(31, 24);
				if (be[4]) m4[w] = d.range(39, 32);
				if (be[5]) m5[w] = d.range(47, 40);
				if (be[6]) m6[w] = d.range(55, 48);
				if (be[7]) m7[w] = d.range(63, 56);
			}
			mw_next.valid = true;
			mw_next.r = r;
			mw_next.count = exmem.count;
		}

		// ---- EX, with forwarding from MEM and WB ----
		ExMem em_next = {};
		bool redirect = false;
		u64 target = 0;
		if (idex.valid) {
			u64 a = idex.rs1v, b = idex.rs2v;
			if (memwb.valid && memwb.r.rd_we && memwb.r.rd != 0 && memwb.r.rd == idex.rs1) a = memwb.r.rd_val;
			if (memwb.valid && memwb.r.rd_we && memwb.r.rd != 0 && memwb.r.rd == idex.rs2) b = memwb.r.rd_val;
			if (exmem.valid && !exmem.load && exmem.r.rd_we && exmem.r.rd != 0 && exmem.r.rd == idex.rs1) a = exmem.r.rd_val;
			if (exmem.valid && !exmem.load && exmem.r.rd_we && exmem.r.rd != 0 && exmem.r.rd == idex.rs2) b = exmem.r.rd_val;

			retire_t r = {};
			r.pc = idex.pc;
			r.insn = idex.insn;
			r.compressed = idex.compressed;
			r.cycle = cycle;
			r.rd = idex.rd;
			bool trap = false, count = true;
			unsigned cause = 0;
			u64 tval = 0;
			const u64 next = idex.pc + 4;

			switch (idex.op) {
			case OP_ALU: r.rd_we = true; r.rd_val = alu(idex.f3, idex.alt, idex.word, a, idex.imm_b ? idex.imm : b); break;
			case OP_LUI: r.rd_we = true; r.rd_val = idex.imm; break;
			case OP_AUIPC: r.rd_we = true; r.rd_val = idex.pc + idex.imm; break;
			case OP_JAL: case OP_JALR: case OP_BRANCH: {
				const u64 t = idex.op == OP_JAL ? (u64)(idex.pc + idex.imm)
				            : idex.op == OP_JALR ? (u64)((a + idex.imm) & ~(u64)1) : (u64)(idex.pc + idex.imm);
				const bool taken = idex.op != OP_BRANCH || branch_taken(idex.f3, a, b);
				if (taken && (t & 3) != 0) { trap = true; cause = C_MISALIGNED_FETCH; tval = t; break; }
				if (idex.op != OP_BRANCH) { r.rd_we = true; r.rd_val = next; }
				if (taken) { redirect = true; target = t; }
				break;
			}
			case OP_LOAD: case OP_STORE: {
				const u64 addr = a + idex.imm;
				const unsigned size = 1u << (unsigned)idex.f3.range(1, 0);
				const bool load = idex.op == OP_LOAD;
				if (cfg::MISALIGNED_TRAP && (addr & (size - 1)) != 0) {
					trap = true; cause = load ? C_MISALIGNED_LOAD : C_MISALIGNED_STORE; tval = addr;
				} else if (!in_memory(addr, size)) {
					trap = true; cause = load ? C_LOAD_ACCESS : C_STORE_ACCESS; tval = addr;
				} else if (load) {
					r.rd_we = true;
					r.rd_val = addr;   // the address, until MEM replaces it with the value
				} else {
					r.st = true;
					r.st_addr = addr;
					r.st_size = size;
					r.st_val = size == 8 ? b : (u64)(b & ((1ull << (8 * size)) - 1));
				}
				break;
			}
			case OP_FENCE: break;
			case OP_FENCEI: case OP_WFI: redirect = true; target = next; break;
			case OP_ECALL: trap = true; cause = C_ECALL_M; break;
			case OP_EBREAK: trap = true; cause = C_BREAKPOINT; tval = idex.pc; break;
			case OP_ILLEGAL: trap = true; cause = C_ILLEGAL; tval = idex.insn; break;
			case OP_FETCHFAULT: trap = true; cause = C_FETCH_ACCESS; tval = idex.pc; r.no_insn = true; break;
			case OP_MRET: {
				u64 s = mstatus;
				s = (s & MSTATUS_MPIE) ? (u64)(s | MSTATUS_MIE) : (u64)(s & ~(u64)MSTATUS_MIE);
				s = s | MSTATUS_MPIE | MSTATUS_MPP;   // MPP: the lowest mode there is, M
				mstatus = s;
				r.csr_we = true; r.csr = 0x300; r.csr_val = s;
				redirect = true; target = mepc & ~(u64)3;
				break;
			}
			case OP_CSR: {
				const ap_uint<12> csr = idex.insn.range(31, 20);
				const bool imm = idex.f3[2];
				const u64 src = imm ? (u64)idex.rs1 : a;
				const ap_uint<2> kind = idex.f3.range(1, 0);   // 1 write, 2 set, 3 clear
				const bool writes = kind == 1 || idex.rs1 != 0;
				bool exists = true;
				u64 old = 0;
				switch (csr) {
				case 0x300: old = mstatus; break;
				case 0x301: old = cfg::MISA; break;
				case 0x304: old = mie; break;
				case 0x305: old = mtvec; break;
				case 0x340: old = mscratch; break;
				case 0x341: old = mepc & ~(u64)3; break;
				case 0x342: old = mcause; break;
				case 0x343: old = mtval; break;
				case 0x344: old = mtime >= 0 ? (u64)(1ull << 7) : (u64)0; break;   // MTIP: mtimecmp is 0, with no CLINT yet
				case 0x320: old = mcountinhibit; break;
				case 0xB00: case 0xC00: old = mcycle; break;
				case 0xB02: case 0xC02: old = minstret; break;
				case 0xC01: old = mtime; break;
				case 0xF11: case 0xF12: case 0xF13: case 0xF14: case 0xF15: old = 0; break;
				case 0x7A0: old = ~tselect; break;
				default: exists = false; break;
				}
				if (!exists || (writes && csr.range(11, 10) == 3)) {
					trap = true; cause = C_ILLEGAL; tval = idex.insn;
					break;
				}
				r.rd_we = true;
				r.rd_val = old;
				if (writes) {
					u64 v = kind == 1 ? src : kind == 2 ? (u64)(old | src) : (u64)(old & ~src);
					switch (csr) {
					case 0x300: v = (v & (MSTATUS_MIE | MSTATUS_MPIE)) | MSTATUS_MPP; mstatus = v; break;
					case 0x301: v = cfg::MISA; break;
					case 0x304: v = v & 0x888; mie = v; break;   // MSIE, MTIE, MEIE
					case 0x305: if ((v & 3) >= 2) v = (v & ~(u64)3) | (mtvec & 3); mtvec = v; break;
					case 0x340: mscratch = v; break;
					case 0x341: mepc = v; break;
					case 0x342: mcause = v; break;
					case 0x343: mtval = v; break;
					case 0x344: v = old; break;   // nothing in mip is writable on this core
					case 0x320: v = v & 0x5; mcountinhibit = v; break;   // CY, IR
					case 0xB00: mcycle = v; mcycle_written = true; break;
					case 0xB02: minstret = v; minstret_written = true; count = false; break;
					case 0x7A0: tselect = v; break;
					default: break;
					}
					r.csr_we = true; r.csr = csr; r.csr_val = v;
				}
				redirect = true; target = next;
				break;
			}
			default: break;
			}

			if (trap) {
				// Enter the trap: machine mode, mtvec's base (exceptions go
				// there in either mode).
				r.trap = true;
				r.rd_we = false;
				r.st = false;
				r.csr_we = false;
				r.cause = cause;
				r.tval = tval;
				mepc = idex.pc;
				mcause = cause;
				mtval = tval;
				u64 s = mstatus;
				s = (s & MSTATUS_MIE) ? (u64)(s | MSTATUS_MPIE) : (u64)(s & ~(u64)MSTATUS_MPIE);
				s = (s & ~(u64)MSTATUS_MIE) | MSTATUS_MPP;
				mstatus = s;
				r.mstatus = s;
				count = false;
				redirect = true;
				target = mtvec & ~(u64)3;
			}
			em_next.valid = true;
			em_next.r = r;
			em_next.load = idex.op == OP_LOAD && !trap;
			em_next.f3 = idex.f3;
			em_next.count = count;
		}

		// ---- ID: decode, and decide whether to stall ----
		IdEx ie_next = {};
		bool stall = false;
		if (ifid.valid && !redirect) {
			const IdEx d = decode(ifid, rf);
			const bool uses_rs2 = d.op == OP_BRANCH || d.op == OP_STORE || (d.op == OP_ALU && !d.imm_b);
			// A load in EX whose result this instruction needs: wait a cycle.
			const bool load_use = idex.valid && idex.op == OP_LOAD && idex.rd != 0
			                    && (idex.rd == d.rs1 || (uses_rs2 && idex.rd == d.rs2));
			// A serialised instruction waits for the stages ahead to empty.
			const bool drain = serial(d.op) && (idex.valid || exmem.valid || memwb.valid);
			stall = load_use || drain;
			if (!stall) ie_next = d;
		}

		// ---- IF ----
		IfId if_next = ifid;
		if (redirect) {
			if_next.valid = false;
			pc = target;
		} else if (!stall) {
			if_next.valid = true;
			if_next.pc = pc;
			if_next.fault = !in_memory(pc, 4);
			if (!if_next.fault) {
				const unsigned w = (unsigned)((pc - cfg::MEM_BASE) >> 3);
				if_next.insn = (pc & 4) ? (ap_uint<32>)(m7[w], m6[w], m5[w], m4[w])
				                        : (ap_uint<32>)(m3[w], m2[w], m1[w], m0[w]);
			}
			// Nothing to fetch after a fetch fault: it traps, and the trap
			// redirects.
			pc = pc + 4;
		}

		// ---- the counters, as the cycle ends ----
		if (mcycle_written) mcycle = mcycle + 1;
		else if (!(mcountinhibit & 1)) mcycle = mcycle + 1;
		(void)minstret_written;
		if (++tick_phase == cfg::CYCLES_PER_TICK) {
			tick_phase = 0;
			mtime = mtime + 1;
		}

		memwb = mw_next;
		exmem = em_next;
		idex = stall ? IdEx{} : ie_next;
		ifid = if_next;
	}
	cycles = cycle;
}
