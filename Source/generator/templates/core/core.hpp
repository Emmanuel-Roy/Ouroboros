// The core's interface: its retirement record and its top-level function.
// A template of the generator (Source/generator/): instantiated with the
// configuration in config.hpp, which the generator writes beside it.
#pragma once
#include "config.hpp"
#include <ap_int.h>
#include <hls_stream.h>

typedef ap_uint<64> u64;
typedef ap_uint<8> byte_t;

// One retired step: an instruction that completed, or a trap. What DoomV
// checks the core against, one record at a time (doomv_lockstep.h's
// doomv_ls_record, which the testbench builds from this).
struct retire_t {
	u64 pc;
	ap_uint<32> insn;
	ap_uint<1> compressed;   // insn is a 16-bit encoding
	u64 cycle;               // the cycle the step executed in: the record's stamp (-cycle-clock)
	ap_uint<1> trap;         // an exception, not a completed instruction
	ap_uint<1> no_insn;      // a fetch fault: there is no instruction to name
	ap_uint<1> rd_we;
	ap_uint<5> rd;
	u64 rd_val;
	ap_uint<1> csr_we;       // a CSR the instruction wrote (mstatus for mret), and its new value
	ap_uint<12> csr;
	u64 csr_val;
	ap_uint<1> st;           // a store: physical address, bytes, value
	u64 st_addr;
	ap_uint<4> st_size;
	u64 st_val;
	u64 cause, tval;         // a trap's
	u64 mstatus;             // a trap's new mstatus
};

// The core, run from reset until it stores to `tohost` or `max_cycles` pass,
// on the test memory m0..m7 (byte lanes of 8-byte words from cfg::MEM_BASE),
// each retired step written to `retire`.
void core_top(byte_t m0[cfg::MEM_WORDS], byte_t m1[cfg::MEM_WORDS], byte_t m2[cfg::MEM_WORDS],
              byte_t m3[cfg::MEM_WORDS], byte_t m4[cfg::MEM_WORDS], byte_t m5[cfg::MEM_WORDS],
              byte_t m6[cfg::MEM_WORDS], byte_t m7[cfg::MEM_WORDS], u64 tohost, u64 max_cycles,
              hls::stream<retire_t> &retire, u64 &cycles);
