// The core's testbench: a template of the generator, the same for every
// configuration. It keeps DoomV's harness contract
// (Tools/Verification/DoomV/tools/verification/vitis_dut.py):
//
//   DOOMV_LS_ELF     the program: loaded into the test memory, run from reset
//   DOOMV_LS_ARGS    DoomV's arguments for the same machine (doomv_ls_open_line)
//   DOOMV_LS_LIMIT   at most this many instructions
//
// Every record the core retires goes to DoomV (doomv_lockstep.h), which
// checks it strictly; the run passes only if every one matches. The
// program's own end -- a store to its tohost symbol -- ends the run.
#include "core.hpp"
#include "doomv_lockstep.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

byte_t mem[8][cfg::MEM_WORDS];

struct Elf {
	std::vector<uint8_t> data;
	uint64_t entry = 0;
	template <class T> T at(size_t off) const
	{
		T v{};
		if (off + sizeof(T) <= data.size()) std::memcpy(&v, data.data() + off, sizeof(T));
		return v;
	}
};

bool load_elf(const char *path, Elf &elf, uint64_t &tohost, std::string &why)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { why = std::string("cannot read ") + path; return false; }
	elf.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	if (elf.data.size() < 64 || std::memcmp(elf.data.data(), "\x7f" "ELF", 4) != 0 || elf.data[4] != 2) {
		why = "not a 64-bit ELF";
		return false;
	}
	elf.entry = elf.at<uint64_t>(0x18);
	const uint64_t phoff = elf.at<uint64_t>(0x20), shoff = elf.at<uint64_t>(0x28);
	const uint16_t phentsize = elf.at<uint16_t>(0x36), phnum = elf.at<uint16_t>(0x38);
	const uint16_t shentsize = elf.at<uint16_t>(0x3A), shnum = elf.at<uint16_t>(0x3C);
	// The loadable segments, into the test memory.
	for (unsigned i = 0; i < phnum; i++) {
		const size_t ph = phoff + (size_t)i * phentsize;
		if (elf.at<uint32_t>(ph) != 1) continue;   // PT_LOAD
		const uint64_t off = elf.at<uint64_t>(ph + 8), paddr = elf.at<uint64_t>(ph + 24);
		const uint64_t filesz = elf.at<uint64_t>(ph + 32), memsz = elf.at<uint64_t>(ph + 40);
		for (uint64_t k = 0; k < memsz; k++) {
			const uint64_t a = paddr + k;
			if (a < cfg::MEM_BASE || a >= cfg::MEM_BASE + 8ull * cfg::MEM_WORDS) {
				why = "the program does not fit the test memory";
				return false;
			}
			const uint64_t o = a - cfg::MEM_BASE;
			mem[o & 7][o >> 3] = k < filesz ? elf.data[off + k] : 0;
		}
	}
	// tohost, from the symbol table.
	tohost = 0;
	for (unsigned i = 0; i < shnum; i++) {
		const size_t sh = shoff + (size_t)i * shentsize;
		if (elf.at<uint32_t>(sh + 4) != 2) continue;   // SHT_SYMTAB
		const uint64_t symoff = elf.at<uint64_t>(sh + 24), symsize = elf.at<uint64_t>(sh + 32);
		const uint32_t link = elf.at<uint32_t>(sh + 40);
		const uint64_t stroff = elf.at<uint64_t>(shoff + (size_t)link * shentsize + 24);
		for (uint64_t s = 0; s + 24 <= symsize; s += 24) {
			const uint32_t name = elf.at<uint32_t>(symoff + s);
			const char *n = (const char *)elf.data.data() + stroff + name;
			if (stroff + name < elf.data.size() && std::strcmp(n, "tohost") == 0)
				tohost = elf.at<uint64_t>(symoff + s + 8);
		}
	}
	return true;
}

} // namespace

int main()
{
	const char *elf_path = std::getenv("DOOMV_LS_ELF");
	const char *args = std::getenv("DOOMV_LS_ARGS");
	const char *limit_s = std::getenv("DOOMV_LS_LIMIT");
	if (!elf_path || !args) {
		std::printf("core_tb: set DOOMV_LS_ELF and DOOMV_LS_ARGS (DoomV's harness contract)\n");
		return 2;
	}
	Elf elf;
	uint64_t tohost = 0;
	std::string why;
	if (!load_elf(elf_path, elf, tohost, why)) {
		std::printf("core_tb: %s\n", why.c_str());
		return 2;
	}
	if (elf.entry != cfg::MEM_BASE) {
		std::printf("core_tb: the program starts at %#llx, the core at %#llx\n",
		            (unsigned long long)elf.entry, (unsigned long long)cfg::MEM_BASE);
		return 2;
	}

	// The core, from reset. A cycle bound well past the instruction limit:
	// the pipeline's stalls and flushes cost a few cycles an instruction.
	const uint64_t limit = limit_s ? std::strtoull(limit_s, nullptr, 0) : 2000000;
	hls::stream<retire_t> retire("retire");
	u64 cycles = 0;
	core_top(mem[0], mem[1], mem[2], mem[3], mem[4], mem[5], mem[6], mem[7], tohost, limit * 8, retire, cycles);

	// What it retired, to DoomV, a record at a time.
	doomv_ls *ls = doomv_ls_open_line(args);
	if (!ls) {
		std::printf("core_tb: DoomV: %s\n", doomv_ls_open_error());
		return 2;
	}
	int status = DOOMV_LS_MATCH;
	uint64_t records = 0;
	while (!retire.empty() && status == DOOMV_LS_MATCH && records < limit) {
		const retire_t r = retire.read();
		records++;
		doomv_ls_write writes[2];
		doomv_ls_write trap_writes[4];
		doomv_ls_store store{};
		doomv_ls_record rec{};
		rec.priv = 3;
		rec.cycle = (uint64_t)r.cycle;
		rec.flags = DOOMV_LS_HAS_CYCLE;
		rec.pc = (uint64_t)r.pc;
		rec.insn = (uint32_t)r.insn;
		rec.insn_len = r.compressed ? 2 : 4;
		if (!r.no_insn) rec.flags |= DOOMV_LS_HAS_INSN;
		if (r.trap) {
			rec.kind = DOOMV_LS_EXCEPTION;
			rec.cause = (uint64_t)r.cause;
			rec.epc = (uint64_t)r.pc;
			rec.tval = (uint64_t)r.tval;
			rec.flags |= DOOMV_LS_HAS_EPC | DOOMV_LS_HAS_TVAL;
			trap_writes[0] = {'c', 0x300, (uint64_t)r.mstatus, nullptr};
			trap_writes[1] = {'c', 0x341, (uint64_t)r.pc, nullptr};
			trap_writes[2] = {'c', 0x342, (uint64_t)r.cause, nullptr};
			trap_writes[3] = {'c', 0x343, (uint64_t)r.tval, nullptr};
			rec.trap_writes = trap_writes;
			rec.n_trap_writes = 4;
		} else {
			rec.kind = DOOMV_LS_COMMIT;
			unsigned n = 0;
			if (r.rd_we && r.rd != 0) writes[n++] = {'x', (uint16_t)r.rd, (uint64_t)r.rd_val, nullptr};
			if (r.csr_we) writes[n++] = {'c', (uint16_t)r.csr, (uint64_t)r.csr_val, nullptr};
			rec.writes = writes;
			rec.n_writes = n;
			if (r.st) {
				store.paddr = (uint64_t)r.st_addr;
				store.size = (uint32_t)r.st_size;
				store.value = (uint64_t)r.st_val;
				rec.stores = &store;
				rec.n_stores = 1;
			}
		}
		status = doomv_ls_step(ls, &rec);
	}
	// The program's own end: it wrote tohost, and DoomV stopped there.
	if (status == DOOMV_LS_STOPPED && doomv_ls_tohost(ls)) status = DOOMV_LS_MATCH;
	const uint64_t matched = doomv_ls_matched(ls);
	if (status == DOOMV_LS_MATCH)
		std::printf("core_tb: %llu records matched DoomV in %llu cycles\n", (unsigned long long)matched,
		            (unsigned long long)(uint64_t)cycles);
	else
		std::printf("core_tb: status %d after %llu matching records\n%s\n", status, (unsigned long long)matched,
		            doomv_ls_message(ls));
	std::fflush(stdout);
	doomv_ls_close(ls);
	return status == DOOMV_LS_MATCH ? 0 : 1;
}
