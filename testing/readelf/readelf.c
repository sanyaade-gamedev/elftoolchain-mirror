/*-
 * Copyright (c) 2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#ifndef LIBELF_AR
#include <archive.h>
#include <archive_entry.h>
#endif	/* ! LIBELF_AR */

#include "config.h"

/*
 * readelf(1) options.
 */
#define	RE_AA	0x00000001
#define	RE_C	0x00000002
#define	RE_DD	0x00000004
#define	RE_D	0x00000008
#define	RE_G	0x00000010
#define	RE_H	0x00000020
#define	RE_II	0x00000040
#define	RE_I	0x00000080
#define	RE_L	0x00000100
#define	RE_NN	0x00000200
#define	RE_N	0x00000400
#define	RE_P	0x00000800
#define	RE_R	0x00001000
#define	RE_SS	0x00002000
#define	RE_S	0x00004000
#define	RE_T	0x00008000
#define	RE_U	0x00010000
#define	RE_VV	0x00020000
#define	RE_WW	0x00040000
#define	RE_W	0x00080000
#define	RE_X	0x00100000

/*
 * readelf(1) run control flags.
 */
#define	DISPLAY_FILENAME	0x0001

/*
 * Internal data structure for sections.
 */
struct section {
	const char	*name;		/* section name */
	Elf_Scn		*scn;		/* section scn */
	uint64_t	 off;		/* section offset */
	uint64_t	 sz;		/* section size */
	uint64_t	 entsize;	/* section entsize */
	uint64_t	 align;		/* section alignment */
	uint64_t	 type;		/* section type */
	uint64_t	 flags;		/* section flags */
	uint64_t	 addr;		/* section virtual addr */
	uint32_t	 link;		/* section link ndx */
	uint32_t	 info;		/* section info ndx */
};

struct dumpop {
	size_t		 sn;		/* section index */
#define HEX_DUMP	0x0001
	int		 op;		/* dump op type */
	STAILQ_ENTRY(dumpop) dumpop_list;
};

/*
 * Structure encapsulates the global data for readelf(1).
 */
struct readelf {
	const char	 *filename;	/* current processing file. */
	int		  options;	/* command line options. */
	int		  flags;	/* run control flags. */
	Elf		 *elf;		/* underlying ELF descriptor. */
	GElf_Ehdr	  ehdr;		/* ELF header. */
	int		  ec;		/* ELF class. */
	size_t		  shnum;	/* #sections. */
	struct section	 *vd_s;		/* Verdef section. */
	struct section	 *vn_s;		/* Verneed section. */
	struct section	 *vs_s;		/* Versym section. */
	uint16_t	 *vs;		/* Versym array. */
	int		  vs_sz;	/* Versym array size. */
	const char	**vname;	/* Version name array. */
	int		  vname_sz;	/* Size version name array. */
	struct section	 *sl;		/* list of sections. */
	STAILQ_HEAD(, dumpop) v_dumpop; /* list of dump ops. */
};

struct elf_define {
	const char	*name;
	int		 value;
};

enum options
{
	OPTION_DEBUG_DUMP
};

static struct option longopts[] = {
	{"all", no_argument, NULL, 'a'},
	{"arch-specific", no_argument, NULL, 'A'},
	{"archive-index", no_argument, NULL, 'c'},
	{"debug-dump", optional_argument, NULL, OPTION_DEBUG_DUMP},
	{"dynamic", no_argument, NULL, 'd'},
	{"file-header", no_argument, NULL, 'h'},
	{"full-section-name", no_argument, NULL, 'N'},
	{"headers", no_argument, NULL, 'e'},
	{"help", no_argument, 0, 'H'},
	{"hex-dump", required_argument, NULL, 'x'},
	{"histogram", no_argument, NULL, 'I'},
	{"notes", no_argument, NULL, 'n'},
	{"program-headers", no_argument, NULL, 'l'},
	{"relocs", no_argument, NULL, 'r'},
	{"sections", no_argument, NULL, 'S'},
	{"section-headers", no_argument, NULL, 'S'},
	{"section-groups", no_argument, NULL, 'g'},
	{"section-details", no_argument, NULL, 't'},
	{"segments", no_argument, NULL, 'l'},
	{"string-dump", required_argument, NULL, 'p'},
	{"symbols", no_argument, NULL, 's'},
	{"syms", no_argument, NULL, 's'},
	{"unwind", no_argument, NULL, 'u'},
	{"use-dynamic", no_argument, NULL, 'D'},
	{"version-info", no_argument, 0, 'V'},
	{"version", no_argument, 0, 'v'},
	{"wide", no_argument, 0, 'W'},
	{NULL, 0, NULL, 0}
};

static const char *
elf_osabi(unsigned int abi)
{
	static char s_abi[32];

	switch(abi) {
	case ELFOSABI_SYSV: return "SYSV";
	case ELFOSABI_HPUX: return "HPUS";
	case ELFOSABI_NETBSD: return "NetBSD";
	case ELFOSABI_LINUX: return "Linux";
	case ELFOSABI_HURD: return "HURD";
	case ELFOSABI_86OPEN: return "86OPEN";
	case ELFOSABI_SOLARIS: return "Solaris";
	case ELFOSABI_AIX: return "AIX";
	case ELFOSABI_IRIX: return "IRIX";
	case ELFOSABI_FREEBSD: return "FreeBSD";
	case ELFOSABI_TRU64: return "TRU64";
	case ELFOSABI_MODESTO: return "MODESTO";
	case ELFOSABI_OPENBSD: return "OpenBSD";
	case ELFOSABI_OPENVMS: return "OpenVMS";
	case ELFOSABI_NSK: return "NSK";
	case ELFOSABI_ARM: return "ARM";
	case ELFOSABI_STANDALONE: return "StandAlone";
	default:
		snprintf(s_abi, sizeof(s_abi), "<unknown: %#x>", abi);
		return (s_abi);
	}
};

static const char *
elf_machine(unsigned int mach)
{
	static char s_mach[32];

	switch (mach) {
	case EM_NONE: return "Unknown machine";
	case EM_M32: return "AT&T WE32100";
	case EM_SPARC: return "Sun SPARC";
	case EM_386: return "Intel i386";
	case EM_68K: return "Motorola 68000";
	case EM_88K: return "Motorola 88000";
	case EM_860: return "Intel i860";
	case EM_MIPS: return "MIPS R3000 Big-Endian only";
	case EM_S370: return "IBM System/370";
	case EM_MIPS_RS3_LE: return "MIPS R3000 Little-Endian";
	case EM_PARISC: return "HP PA-RISC";
	case EM_VPP500: return "Fujitsu VPP500";
	case EM_SPARC32PLUS: return "SPARC v8plus";
	case EM_960: return "Intel 80960";
	case EM_PPC: return "PowerPC 32-bit";
	case EM_PPC64: return "PowerPC 64-bit";
	case EM_S390: return "IBM System/390";
	case EM_V800: return "NEC V800";
	case EM_FR20: return "Fujitsu FR20";
	case EM_RH32: return "TRW RH-32";
	case EM_RCE: return "Motorola RCE";
	case EM_ARM: return "ARM";
	case EM_SH: return "Hitachi SH";
	case EM_SPARCV9: return "SPARC v9 64-bit";
	case EM_TRICORE: return "Siemens TriCore embedded processor";
	case EM_ARC: return "Argonaut RISC Core";
	case EM_H8_300: return "Hitachi H8/300";
	case EM_H8_300H: return "Hitachi H8/300H";
	case EM_H8S: return "Hitachi H8S";
	case EM_H8_500: return "Hitachi H8/500";
	case EM_IA_64: return "Intel IA-64 Processor";
	case EM_MIPS_X: return "Stanford MIPS-X";
	case EM_COLDFIRE: return "Motorola ColdFire";
	case EM_68HC12: return "Motorola M68HC12";
	case EM_MMA: return "Fujitsu MMA";
	case EM_PCP: return "Siemens PCP";
	case EM_NCPU: return "Sony nCPU";
	case EM_NDR1: return "Denso NDR1 microprocessor";
	case EM_STARCORE: return "Motorola Star*Core processor";
	case EM_ME16: return "Toyota ME16 processor";
	case EM_ST100: return "STMicroelectronics ST100 processor";
	case EM_TINYJ: return "Advanced Logic Corp. TinyJ processor";
	case EM_X86_64: return "Advanced Micro Devices x86-64";
	case EM_PDSP: return "Sony DSP Processor";
	case EM_FX66: return "Siemens FX66 microcontroller";
	case EM_ST9PLUS: return "STMicroelectronics ST9+ 8/16 microcontroller";
	case EM_ST7: return "STmicroelectronics ST7 8-bit microcontroller";
	case EM_68HC16: return "Motorola MC68HC16 microcontroller";
	case EM_68HC11: return "Motorola MC68HC11 microcontroller";
	case EM_68HC08: return "Motorola MC68HC08 microcontroller";
	case EM_68HC05: return "Motorola MC68HC05 microcontroller";
	case EM_SVX: return "Silicon Graphics SVx";
	case EM_ST19: return "STMicroelectronics ST19 8-bit mc";
	case EM_VAX: return "Digital VAX";
	case EM_CRIS: return "Axis Communications 32-bit embedded processor";
	case EM_JAVELIN: return "Infineon Tech. 32bit embedded processor";
	case EM_FIREPATH: return "Element 14 64-bit DSP Processor";
	case EM_ZSP: return "LSI Logic 16-bit DSP Processor";
	case EM_MMIX: return "Donald Knuth's educational 64-bit proc";
	case EM_HUANY: return "Harvard University MI object files";
	case EM_PRISM: return "SiTera Prism";
	case EM_AVR: return "Atmel AVR 8-bit microcontroller";
	case EM_FR30: return "Fujitsu FR30";
	case EM_D10V: return "Mitsubishi D10V";
	case EM_D30V: return "Mitsubishi D30V";
	case EM_V850: return "NEC v850";
	case EM_M32R: return "Mitsubishi M32R";
	case EM_MN10300: return "Matsushita MN10300";
	case EM_MN10200: return "Matsushita MN10200";
	case EM_PJ: return "picoJava";
	case EM_OPENRISC: return "OpenRISC 32-bit embedded processor";
	case EM_ARC_A5: return "ARC Cores Tangent-A5";
	case EM_XTENSA: return "Tensilica Xtensa Architecture";
	case EM_VIDEOCORE: return "Alphamosaic VideoCore processor";
	case EM_TMM_GPP: return "Thompson Multimedia General Purpose Processor";
	case EM_NS32K: return "National Semiconductor 32000 series";
	case EM_TPC: return "Tenor Network TPC processor";
	case EM_SNP1K: return "Trebia SNP 1000 processor";
	case EM_ST200: return "STMicroelectronics ST200 microcontroller";
	case EM_IP2K: return "Ubicom IP2xxx microcontroller family";
	case EM_MAX: return "MAX Processor";
	case EM_CR: return "National Semiconductor CompactRISC microprocessor";
	case EM_F2MC16: return "Fujitsu F2MC16";
	case EM_MSP430: return "TI embedded microcontroller msp430";
	case EM_BLACKFIN: return "Analog Devices Blackfin (DSP) processor";
	case EM_SE_C33: return "S1C33 Family of Seiko Epson processors";
	case EM_SEP: return "Sharp embedded microprocessor";
	case EM_ARCA: return "Arca RISC Microprocessor";
	case EM_UNICORE: return "Microprocessor series from PKU-Unity Ltd";
	default:
		snprintf(s_mach, sizeof(s_mach), "<unknown: %#x>", mach);
		return (s_mach);
	}

}

static const char *
elf_class(unsigned int class)
{
	static char s_class[32];

	switch (class) {
	case ELFCLASSNONE: return "none";
	case ELFCLASS32: return "ELF32";
	case ELFCLASS64: return "ELF64";
	default:
		snprintf(s_class, sizeof(s_class), "<unknown: %#x>", class);
		return (s_class);
	}
}

static const char *
elf_endian(unsigned int endian)
{
	static char s_endian[32];

	switch (endian) {
	case ELFDATANONE: return "none";
	case ELFDATA2LSB: return "2's complement, little endian";
	case ELFDATA2MSB: return "2's complement, big endian";
	default:
		snprintf(s_endian, sizeof(s_endian), "<unknown: %#x>", endian);
		return (s_endian);
	}
}

static const char *
elf_type(unsigned int type)
{
	static char s_type[32];

	switch (type) {
	case ET_NONE: return "NONE (None)";
	case ET_REL: return "REL (Relocatable file)";
	case ET_EXEC: return "EXEC (Executable file)";
	case ET_DYN: return "DYN (Shared object file)";
	case ET_CORE: return "CORE (Core file)";
	default:
		if (type >= ET_LOPROC)
			snprintf(s_type, sizeof(s_type), "<proc: %#x>", type);
		else if (type >= ET_LOOS && type <= ET_HIOS)
			snprintf(s_type, sizeof(s_type), "<os: %#x>", type);
		else
			snprintf(s_type, sizeof(s_type), "<unknown: %#x>",
			    type);
		return (s_type);
	}
}

static const char *
elf_ver(unsigned int ver)
{
	static char s_ver[32];

	switch (ver) {
	case EV_CURRENT: return "(current)";
	case EV_NONE: return "(none)";
	default:
		snprintf(s_ver, sizeof(s_ver), "<unknown: %#x>",
		    ver);
		return (s_ver);
	}
}

static const char *
phdr_type(unsigned int ptype)
{
	static char s_ptype[32];

	switch (ptype) {
	case PT_NULL: return "NULL";
	case PT_LOAD: return "LOAD";
	case PT_DYNAMIC: return "DYNAMIC";
	case PT_INTERP: return "INTERP";
	case PT_NOTE: return "NOTE";
	case PT_SHLIB: return "SHLIB";
	case PT_PHDR: return "PHDR";
	case PT_TLS: return "TLS";
	case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
#ifdef PT_GNU_STACK
	case PT_GNU_STACK: return "GNU_STACK";
#endif
#ifdef PT_GNU_RELRO
	case PT_GNU_RELRO: return "GNU_RELRO";
#endif
	default:
		if (ptype >= PT_LOPROC && ptype <= PT_HIPROC)
			snprintf(s_ptype, sizeof(s_ptype), "LOPROC+%#x",
			    ptype - PT_LOPROC);
		else if (ptype >= PT_LOOS && ptype <= PT_HIOS)
			snprintf(s_ptype, sizeof(s_ptype), "LOOS+%#x",
			    ptype - PT_LOOS);
		else
			snprintf(s_ptype, sizeof(s_ptype), "<unknown: %#x>",
			    ptype);
		return (s_ptype);
	}
}

static const char *
section_type(unsigned int stype)
{
	static char s_stype[32];

	switch (stype) {
	case SHT_NULL: return "NULL";
	case SHT_PROGBITS: return "PROGBITS";
	case SHT_SYMTAB: return "SYMTAB";
	case SHT_STRTAB: return "STRTAB";
	case SHT_RELA: return "RELA";
	case SHT_HASH: return "HASH";
	case SHT_DYNAMIC: return "DYNAMIC";
	case SHT_NOTE: return "NOTE";
	case SHT_NOBITS: return "NOBITS";
	case SHT_REL: return "REL";
	case SHT_SHLIB: return "SHLIB";
	case SHT_DYNSYM: return "DYNSYM";
	case SHT_INIT_ARRAY: return "INIT_ARRAY";
	case SHT_FINI_ARRAY: return "FINI_ARRAY";
	case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
	case SHT_GROUP: return "GROUP";
	case SHT_SYMTAB_SHNDX: return "SYMTAB_SHNDX";
	case SHT_SUNW_dof: return "SUNW_dof";
	case SHT_SUNW_cap: return "SUNW_cap";
	case SHT_GNU_HASH: return "GNU_HASH";
	case SHT_SUNW_ANNOTATE: return "SUNW_ANNOTATE";
	case SHT_SUNW_DEBUGSTR: return "SUNW_DEBUGSTR";
	case SHT_SUNW_DEBUG: return "SUNW_DEBUG";
	case SHT_SUNW_move: return "SUNW_move";
	case SHT_SUNW_COMDAT: return "SUNW_COMDAT";
	case SHT_SUNW_syminfo: return "SUNW_syminfo";
	case SHT_SUNW_verdef: return "SUNW_verdef";
	case SHT_SUNW_verneed: return "SUNW_verneed";
	case SHT_SUNW_versym: return "SUNW_versym";
	case SHT_AMD64_UNWIND: return "AMD64_UNWIND";
	default:
		if (stype >= SHT_LOOS && stype <= SHT_HIOS)
			snprintf(s_stype, sizeof(s_stype), "LOOS+%#x",
			    stype - SHT_LOOS);
		else if (stype >= SHT_LOPROC && stype <= SHT_HIPROC)
			snprintf(s_stype, sizeof(s_stype), "LOPROC+%#x",
			    stype - SHT_LOPROC);
		else if (stype >= SHT_LOUSER)
			snprintf(s_stype, sizeof(s_stype), "LOUSER+%#x",
			    stype - SHT_LOUSER);
		else
			snprintf(s_stype, sizeof(s_stype), "<unknown: %#x>",
			    stype);
		return (s_stype);
	}
}

static const char *
dt_type(unsigned int dtype)
{
	static char s_dtype[32];

	switch (dtype) {
	case DT_NULL: return "NULL";
	case DT_NEEDED: return "NEEDED";
	case DT_PLTRELSZ: return "PLTRELSZ";
	case DT_PLTGOT: return "PLTGOT";
	case DT_HASH: return "HASH";
	case DT_STRTAB: return "STRTAB";
	case DT_SYMTAB: return "SYMTAB";
	case DT_RELA: return "RELA";
	case DT_RELASZ: return "RELASZ";
	case DT_RELAENT: return "RELAENT";
	case DT_STRSZ: return "STRSZ";
	case DT_SYMENT: return "SYMENT";
	case DT_INIT: return "INIT";
	case DT_FINI: return "FINI";
	case DT_SONAME: return "SONAME";
	case DT_RPATH: return "RPATH";
	case DT_SYMBOLIC: return "SYMBOLIC";
	case DT_REL: return "REL";
	case DT_RELSZ: return "RELSZ";
	case DT_RELENT: return "RELENT";
	case DT_PLTREL: return "PLTREL";
	case DT_DEBUG: return "DEBUG";
	case DT_TEXTREL: return "TEXTREL";
	case DT_JMPREL: return "JMPREL";
	case DT_BIND_NOW: return "BIND_NOW";
	case DT_INIT_ARRAY: return "INIT_ARRAY";
	case DT_FINI_ARRAY: return "FINI_ARRAY";
	case DT_INIT_ARRAYSZ: return "INIT_ARRAYSZ";
	case DT_FINI_ARRAYSZ: return "FINI_ARRAYSZ";
	case DT_RUNPATH: return "RUNPATH";
	case DT_FLAGS: return "FLAGS";
	case DT_PREINIT_ARRAY: return "PREINIT_ARRAY";
	case DT_PREINIT_ARRAYSZ: return "PREINIT_ARRAYSZ";
	case DT_MAXPOSTAGS: return "MAXPOSTAGS";
	case DT_SUNW_AUXILIARY: return "SUNW_AUXILIARY";
	case DT_SUNW_RTLDINF: return "SUNW_RTLDINF";
	case DT_SUNW_FILTER: return "SUNW_FILTER";
	case DT_SUNW_CAP: return "SUNW_CAP";
	case DT_CHECKSUM: return "CHECKSUM";
	case DT_PLTPADSZ: return "PLTPADSZ";
	case DT_MOVEENT: return "MOVEENT";
	case DT_MOVESZ: return "MOVESZ";
	case DT_FEATURE_1: return "FEATURE_1";
	case DT_POSFLAG_1: return "POSFLAG_1";
	case DT_SYMINSZ: return "SYMINSZ";
	case DT_SYMINENT: return "SYMINENT";
	case DT_GNU_HASH: return "GNU_HASH";
	case DT_CONFIG: return "CONFIG";
	case DT_DEPAUDIT: return "DEPAUDIT";
	case DT_AUDIT: return "AUDIT";
	case DT_PLTPAD: return "PLTPAD";
	case DT_MOVETAB: return "MOVETAB";
	case DT_SYMINFO: return "SYMINFO";
	case DT_VERSYM: return "VERSYM";
	case DT_RELACOUNT: return "RELACOUNT";
	case DT_RELCOUNT: return "RELCOUNT";
	case DT_FLAGS_1: return "FLAGS_1";
	case DT_VERDEF: return "VERDEF";
	case DT_VERDEFNUM: return "VERDEFNUM";
	case DT_VERNEED: return "VERNEED";
	case DT_VERNEEDNUM: return "VERNEEDNUM";
	case DT_DEPRECATED_SPARC_REGISTER: return "DEPRECATED_SPARC_REGISTER";
	case DT_AUXILIARY: return "AUXILIARY";
	case DT_USED: return "USED";
	case DT_FILTER: return "FILTER";
	default:
		snprintf(s_dtype, sizeof(s_dtype), "<unknown: %#x>", dtype);
		return (s_dtype);
	}
}

static const char *
st_bind(unsigned int sbind)
{
	static char s_sbind[32];

	switch (sbind) {
	case STB_LOCAL: return "LOCAL";
	case STB_GLOBAL: return "GLOBAL";
	case STB_WEAK: return "WEAK";
	default:
		if (sbind >= STB_LOOS && sbind <= STB_HIOS)
			return "OS";
		else if (sbind >= STB_LOPROC && sbind <= STB_HIPROC)
			return "PROC";
		else
			snprintf(s_sbind, sizeof(s_sbind), "<unknown: %#x>",
			    sbind);
		return (s_sbind);
	}
}

static const char *
st_type(unsigned int stype)
{
	static char s_stype[32];

	switch (stype) {
	case STT_NOTYPE: return "NOTYPE";
	case STT_OBJECT: return "OBJECT";
	case STT_FUNC: return "FUNC";
	case STT_SECTION: return "SECTION";
	case STT_FILE: return "FILE";
	case STT_COMMON: return "COMMON";
	case STT_TLS: return "TLS";
	default:
		if (stype >= STT_LOOS && stype <= STT_HIOS)
			snprintf(s_stype, sizeof(s_stype), "OS+%#x",
			    stype - STT_LOOS);
		else if (stype >= STT_LOPROC && stype <= STT_HIPROC)
			snprintf(s_stype, sizeof(s_stype), "PROC+%#x",
			    stype - STT_LOPROC);
		else
			snprintf(s_stype, sizeof(s_stype), "<unknown: %#x>",
			    stype);
		return (s_stype);
	}
}

static const char *
st_shndx(unsigned int shndx)
{
	static char s_shndx[32];

	switch (shndx) {
	case SHN_UNDEF: return "UND";
	case SHN_ABS: return "ABS";
	case SHN_COMMON: return "COM";
	default:
		if (shndx >= SHN_LOPROC && shndx <= SHN_HIPROC)
			return "PRC";
		else if (shndx >= SHN_LOOS && shndx <= SHN_HIOS)
			return "OS";
		else
			snprintf(s_shndx, sizeof(s_shndx), "%u", shndx);
		return (s_shndx);
	}
}

static struct {
	const char *ln;
	char sn;
	int value;
} section_flag[] = {
	{"WRITE", 'W', SHF_WRITE},
	{"ALLOC", 'A', SHF_ALLOC},
	{"EXEC", 'X', SHF_EXECINSTR},
	{"MERGE", 'M', SHF_MERGE},
	{"STRINGS", 'S', SHF_STRINGS},
	{"INFO LINK", 'I', SHF_INFO_LINK},
	{"OS NONCONF", 'O', SHF_OS_NONCONFORMING},
	{"GROUP", 'G', SHF_GROUP},
	{"TLS", 'T', SHF_TLS},
	{NULL, 0, 0}
};

static const char *
r_type(unsigned int mach, unsigned int type)
{
	switch(mach) {
	case EM_NONE: return "";
	case EM_386:
		switch(type) {
		case 0: return "R_386_NONE";
		case 1: return "R_386_32";
		case 2: return "R_386_PC32";
		case 3: return "R_386_GOT32";
		case 4: return "R_386_PLT32";
		case 5: return "R_386_COPY";
		case 6: return "R_386_GLOB_DAT";
		case 7: return "R_386_JMP_SLOT";
		case 8: return "R_386_RELATIVE";
		case 9: return "R_386_GOTOFF";
		case 10: return "R_386_GOTPC";
		case 14: return "R_386_TLS_TPOFF";
		case 15: return "R_386_TLS_IE";
		case 16: return "R_386_TLS_GOTIE";
		case 17: return "R_386_TLS_LE";
		case 18: return "R_386_TLS_GD";
		case 19: return "R_386_TLS_LDM";
		case 24: return "R_386_TLS_GD_32";
		case 25: return "R_386_TLS_GD_PUSH";
		case 26: return "R_386_TLS_GD_CALL";
		case 27: return "R_386_TLS_GD_POP";
		case 28: return "R_386_TLS_LDM_32";
		case 29: return "R_386_TLS_LDM_PUSH";
		case 30: return "R_386_TLS_LDM_CALL";
		case 31: return "R_386_TLS_LDM_POP";
		case 32: return "R_386_TLS_LDO_32";
		case 33: return "R_386_TLS_IE_32";
		case 34: return "R_386_TLS_LE_32";
		case 35: return "R_386_TLS_DTPMOD32";
		case 36: return "R_386_TLS_DTPOFF32";
		case 37: return "R_386_TLS_TPOFF32";
		default: return "";
		}
	case EM_ARM:
		switch(type) {
		case 0: return "R_ARM_NONE";
		case 1: return "R_ARM_PC24";
		case 2: return "R_ARM_ABS32";
		case 3: return "R_ARM_REL32";
		case 4: return "R_ARM_PC13";
		case 5: return "R_ARM_ABS16";
		case 6: return "R_ARM_ABS12";
		case 7: return "R_ARM_THM_ABS5";
		case 8: return "R_ARM_ABS8";
		case 9: return "R_ARM_SBREL32";
		case 10: return "R_ARM_THM_PC22";
		case 11: return "R_ARM_THM_PC8";
		case 12: return "R_ARM_AMP_VCALL9";
		case 13: return "R_ARM_SWI24";
		case 14: return "R_ARM_THM_SWI8";
		case 15: return "R_ARM_XPC25";
		case 16: return "R_ARM_THM_XPC22";
		case 20: return "R_ARM_COPY";
		case 21: return "R_ARM_GLOB_DAT";
		case 22: return "R_ARM_JUMP_SLOT";
		case 23: return "R_ARM_RELATIVE";
		case 24: return "R_ARM_GOTOFF";
		case 25: return "R_ARM_GOTPC";
		case 26: return "R_ARM_GOT32";
		case 27: return "R_ARM_PLT32";
		case 100: return "R_ARM_GNU_VTENTRY";
		case 101: return "R_ARM_GNU_VTINHERIT";
		case 250: return "R_ARM_RSBREL32";
		case 251: return "R_ARM_THM_RPC22";
		case 252: return "R_ARM_RREL32";
		case 253: return "R_ARM_RABS32";
		case 254: return "R_ARM_RPC24";
		case 255: return "R_ARM_RBASE";
		default: return "";
		}
	case EM_IA_64:
		switch(type) {
		case 0: return "R_IA_64_NONE";
		case 33: return "R_IA_64_IMM14";
		case 34: return "R_IA_64_IMM22";
		case 35: return "R_IA_64_IMM64";
		case 36: return "R_IA_64_DIR32MSB";
		case 37: return "R_IA_64_DIR32LSB";
		case 38: return "R_IA_64_DIR64MSB";
		case 39: return "R_IA_64_DIR64LSB";
		case 42: return "R_IA_64_GPREL22";
		case 43: return "R_IA_64_GPREL64I";
		case 44: return "R_IA_64_GPREL32MSB";
		case 45: return "R_IA_64_GPREL32LSB";
		case 46: return "R_IA_64_GPREL64MSB";
		case 47: return "R_IA_64_GPREL64LSB";
		case 50: return "R_IA_64_LTOFF22";
		case 51: return "R_IA_64_LTOFF64I";
		case 58: return "R_IA_64_PLTOFF22";
		case 59: return "R_IA_64_PLTOFF64I";
		case 62: return "R_IA_64_PLTOFF64MSB";
		case 63: return "R_IA_64_PLTOFF64LSB";
		case 67: return "R_IA_64_FPTR64I";
		case 68: return "R_IA_64_FPTR32MSB";
		case 69: return "R_IA_64_FPTR32LSB";
		case 70: return "R_IA_64_FPTR64MSB";
		case 71: return "R_IA_64_FPTR64LSB";
		case 72: return "R_IA_64_PCREL60B";
		case 73: return "R_IA_64_PCREL21B";
		case 74: return "R_IA_64_PCREL21M";
		case 75: return "R_IA_64_PCREL21F";
		case 76: return "R_IA_64_PCREL32MSB";
		case 77: return "R_IA_64_PCREL32LSB";
		case 78: return "R_IA_64_PCREL64MSB";
		case 79: return "R_IA_64_PCREL64LSB";
		case 82: return "R_IA_64_LTOFF_FPTR22";
		case 83: return "R_IA_64_LTOFF_FPTR64I";
		case 84: return "R_IA_64_LTOFF_FPTR32MSB";
		case 85: return "R_IA_64_LTOFF_FPTR32LSB";
		case 86: return "R_IA_64_LTOFF_FPTR64MSB";
		case 87: return "R_IA_64_LTOFF_FPTR64LSB";
		case 92: return "R_IA_64_SEGREL32MSB";
		case 93: return "R_IA_64_SEGREL32LSB";
		case 94: return "R_IA_64_SEGREL64MSB";
		case 95: return "R_IA_64_SEGREL64LSB";
		case 100: return "R_IA_64_SECREL32MSB";
		case 101: return "R_IA_64_SECREL32LSB";
		case 102: return "R_IA_64_SECREL64MSB";
		case 103: return "R_IA_64_SECREL64LSB";
		case 108: return "R_IA_64_REL32MSB";
		case 109: return "R_IA_64_REL32LSB";
		case 110: return "R_IA_64_REL64MSB";
		case 111: return "R_IA_64_REL64LSB";
		case 116: return "R_IA_64_LTV32MSB";
		case 117: return "R_IA_64_LTV32LSB";
		case 118: return "R_IA_64_LTV64MSB";
		case 119: return "R_IA_64_LTV64LSB";
		case 121: return "R_IA_64_PCREL21BI";
		case 122: return "R_IA_64_PCREL22";
		case 123: return "R_IA_64_PCREL64I";
		case 128: return "R_IA_64_IPLTMSB";
		case 129: return "R_IA_64_IPLTLSB";
		case 133: return "R_IA_64_SUB";
		case 134: return "R_IA_64_LTOFF22X";
		case 135: return "R_IA_64_LDXMOV";
		case 145: return "R_IA_64_TPREL14";
		case 146: return "R_IA_64_TPREL22";
		case 147: return "R_IA_64_TPREL64I";
		case 150: return "R_IA_64_TPREL64MSB";
		case 151: return "R_IA_64_TPREL64LSB";
		case 154: return "R_IA_64_LTOFF_TPREL22";
		case 166: return "R_IA_64_DTPMOD64MSB";
		case 167: return "R_IA_64_DTPMOD64LSB";
		case 170: return "R_IA_64_LTOFF_DTPMOD22";
		case 177: return "R_IA_64_DTPREL14";
		case 178: return "R_IA_64_DTPREL22";
		case 179: return "R_IA_64_DTPREL64I";
		case 180: return "R_IA_64_DTPREL32MSB";
		case 181: return "R_IA_64_DTPREL32LSB";
		case 182: return "R_IA_64_DTPREL64MSB";
		case 183: return "R_IA_64_DTPREL64LSB";
		case 186: return "R_IA_64_LTOFF_DTPREL22";
		default: return "";
		}
	case EM_MIPS:
		switch(type) {
		case 0: return "R_MIPS_NONE";
		case 1: return "R_MIPS_16";
		case 2: return "R_MIPS_32";
		case 3: return "R_MIPS_REL32";
		case 4: return "R_MIPS_26";
		case 5: return "R_MIPS_HI16";
		case 6: return "R_MIPS_LO16";
		case 7: return "R_MIPS_GPREL16";
		case 8: return "R_MIPS_LITERAL";
		case 9: return "R_MIPS_GOT16";
		case 10: return "R_MIPS_PC16";
		case 11: return "R_MIPS_CALL16";
		case 12: return "R_MIPS_GPREL32";
		case 21: return "R_MIPS_GOTHI16";
		case 22: return "R_MIPS_GOTLO16";
		case 30: return "R_MIPS_CALLHI16";
		case 31: return "R_MIPS_CALLLO16";
		default: return "";
		}
	case EM_PPC:
		switch(type) {
		case 0: return "R_PPC_NONE";
		case 1: return "R_PPC_ADDR32";
		case 2: return "R_PPC_ADDR24";
		case 3: return "R_PPC_ADDR16";
		case 4: return "R_PPC_ADDR16_LO";
		case 5: return "R_PPC_ADDR16_HI";
		case 6: return "R_PPC_ADDR16_HA";
		case 7: return "R_PPC_ADDR14";
		case 8: return "R_PPC_ADDR14_BRTAKEN";
		case 9: return "R_PPC_ADDR14_BRNTAKEN";
		case 10: return "R_PPC_REL24";
		case 11: return "R_PPC_REL14";
		case 12: return "R_PPC_REL14_BRTAKEN";
		case 13: return "R_PPC_REL14_BRNTAKEN";
		case 14: return "R_PPC_GOT16";
		case 15: return "R_PPC_GOT16_LO";
		case 16: return "R_PPC_GOT16_HI";
		case 17: return "R_PPC_GOT16_HA";
		case 18: return "R_PPC_PLTREL24";
		case 19: return "R_PPC_COPY";
		case 20: return "R_PPC_GLOB_DAT";
		case 21: return "R_PPC_JMP_SLOT";
		case 22: return "R_PPC_RELATIVE";
		case 23: return "R_PPC_LOCAL24PC";
		case 24: return "R_PPC_UADDR32";
		case 25: return "R_PPC_UADDR16";
		case 26: return "R_PPC_REL32";
		case 27: return "R_PPC_PLT32";
		case 28: return "R_PPC_PLTREL32";
		case 29: return "R_PPC_PLT16_LO";
		case 30: return "R_PPC_PLT16_HI";
		case 31: return "R_PPC_PLT16_HA";
		case 32: return "R_PPC_SDAREL16";
		case 33: return "R_PPC_SECTOFF";
		case 34: return "R_PPC_SECTOFF_LO";
		case 35: return "R_PPC_SECTOFF_HI";
		case 36: return "R_PPC_SECTOFF_HA";
		case 67: return "R_PPC_TLS";
		case 68: return "R_PPC_DTPMOD32";
		case 69: return "R_PPC_TPREL16";
		case 70: return "R_PPC_TPREL16_LO";
		case 71: return "R_PPC_TPREL16_HI";
		case 72: return "R_PPC_TPREL16_HA";
		case 73: return "R_PPC_TPREL32";
		case 74: return "R_PPC_DTPREL16";
		case 75: return "R_PPC_DTPREL16_LO";
		case 76: return "R_PPC_DTPREL16_HI";
		case 77: return "R_PPC_DTPREL16_HA";
		case 78: return "R_PPC_DTPREL32";
		case 79: return "R_PPC_GOT_TLSGD16";
		case 80: return "R_PPC_GOT_TLSGD16_LO";
		case 81: return "R_PPC_GOT_TLSGD16_HI";
		case 82: return "R_PPC_GOT_TLSGD16_HA";
		case 83: return "R_PPC_GOT_TLSLD16";
		case 84: return "R_PPC_GOT_TLSLD16_LO";
		case 85: return "R_PPC_GOT_TLSLD16_HI";
		case 86: return "R_PPC_GOT_TLSLD16_HA";
		case 87: return "R_PPC_GOT_TPREL16";
		case 88: return "R_PPC_GOT_TPREL16_LO";
		case 89: return "R_PPC_GOT_TPREL16_HI";
		case 90: return "R_PPC_GOT_TPREL16_HA";
		case 101: return "R_PPC_EMB_NADDR32";
		case 102: return "R_PPC_EMB_NADDR16";
		case 103: return "R_PPC_EMB_NADDR16_LO";
		case 104: return "R_PPC_EMB_NADDR16_HI";
		case 105: return "R_PPC_EMB_NADDR16_HA";
		case 106: return "R_PPC_EMB_SDAI16";
		case 107: return "R_PPC_EMB_SDA2I16";
		case 108: return "R_PPC_EMB_SDA2REL";
		case 109: return "R_PPC_EMB_SDA21";
		case 110: return "R_PPC_EMB_MRKREF";
		case 111: return "R_PPC_EMB_RELSEC16";
		case 112: return "R_PPC_EMB_RELST_LO";
		case 113: return "R_PPC_EMB_RELST_HI";
		case 114: return "R_PPC_EMB_RELST_HA";
		case 115: return "R_PPC_EMB_BIT_FLD";
		case 116: return "R_PPC_EMB_RELSDA";
		default: return "";
		}
	case EM_SPARC:
	case EM_SPARCV9:
		switch(type) {
		case 0: return "R_SPARC_NONE";
		case 1: return "R_SPARC_8";
		case 2: return "R_SPARC_16";
		case 3: return "R_SPARC_32";
		case 4: return "R_SPARC_DISP8";
		case 5: return "R_SPARC_DISP16";
		case 6: return "R_SPARC_DISP32";
		case 7: return "R_SPARC_WDISP30";
		case 8: return "R_SPARC_WDISP22";
		case 9: return "R_SPARC_HI22";
		case 10: return "R_SPARC_22";
		case 11: return "R_SPARC_13";
		case 12: return "R_SPARC_LO10";
		case 13: return "R_SPARC_GOT10";
		case 14: return "R_SPARC_GOT13";
		case 15: return "R_SPARC_GOT22";
		case 16: return "R_SPARC_PC10";
		case 17: return "R_SPARC_PC22";
		case 18: return "R_SPARC_WPLT30";
		case 19: return "R_SPARC_COPY";
		case 20: return "R_SPARC_GLOB_DAT";
		case 21: return "R_SPARC_JMP_SLOT";
		case 22: return "R_SPARC_RELATIVE";
		case 23: return "R_SPARC_UA32";
		case 24: return "R_SPARC_PLT32";
		case 25: return "R_SPARC_HIPLT22";
		case 26: return "R_SPARC_LOPLT10";
		case 27: return "R_SPARC_PCPLT32";
		case 28: return "R_SPARC_PCPLT22";
		case 29: return "R_SPARC_PCPLT10";
		case 30: return "R_SPARC_10";
		case 31: return "R_SPARC_11";
		case 32: return "R_SPARC_64";
		case 33: return "R_SPARC_OLO10";
		case 34: return "R_SPARC_HH22";
		case 35: return "R_SPARC_HM10";
		case 36: return "R_SPARC_LM22";
		case 37: return "R_SPARC_PC_HH22";
		case 38: return "R_SPARC_PC_HM10";
		case 39: return "R_SPARC_PC_LM22";
		case 40: return "R_SPARC_WDISP16";
		case 41: return "R_SPARC_WDISP19";
		case 42: return "R_SPARC_GLOB_JMP";
		case 43: return "R_SPARC_7";
		case 44: return "R_SPARC_5";
		case 45: return "R_SPARC_6";
		case 46: return "R_SPARC_DISP64";
		case 47: return "R_SPARC_PLT64";
		case 48: return "R_SPARC_HIX22";
		case 49: return "R_SPARC_LOX10";
		case 50: return "R_SPARC_H44";
		case 51: return "R_SPARC_M44";
		case 52: return "R_SPARC_L44";
		case 53: return "R_SPARC_REGISTER";
		case 54: return "R_SPARC_UA64";
		case 55: return "R_SPARC_UA16";
		case 56: return "R_SPARC_TLS_GD_HI22";
		case 57: return "R_SPARC_TLS_GD_LO10";
		case 58: return "R_SPARC_TLS_GD_ADD";
		case 59: return "R_SPARC_TLS_GD_CALL";
		case 60: return "R_SPARC_TLS_LDM_HI22";
		case 61: return "R_SPARC_TLS_LDM_LO10";
		case 62: return "R_SPARC_TLS_LDM_ADD";
		case 63: return "R_SPARC_TLS_LDM_CALL";
		case 64: return "R_SPARC_TLS_LDO_HIX22";
		case 65: return "R_SPARC_TLS_LDO_LOX10";
		case 66: return "R_SPARC_TLS_LDO_ADD";
		case 67: return "R_SPARC_TLS_IE_HI22";
		case 68: return "R_SPARC_TLS_IE_LO10";
		case 69: return "R_SPARC_TLS_IE_LD";
		case 70: return "R_SPARC_TLS_IE_LDX";
		case 71: return "R_SPARC_TLS_IE_ADD";
		case 72: return "R_SPARC_TLS_LE_HIX22";
		case 73: return "R_SPARC_TLS_LE_LOX10";
		case 74: return "R_SPARC_TLS_DTPMOD32";
		case 75: return "R_SPARC_TLS_DTPMOD64";
		case 76: return "R_SPARC_TLS_DTPOFF32";
		case 77: return "R_SPARC_TLS_DTPOFF64";
		case 78: return "R_SPARC_TLS_TPOFF32";
		case 79: return "R_SPARC_TLS_TPOFF64";
		default: return "";
		}
	case EM_X86_64:
		switch(type) {
		case 0: return "R_X86_64_NONE";
		case 1: return "R_X86_64_64";
		case 2: return "R_X86_64_PC32";
		case 3: return "R_X86_64_GOT32";
		case 4: return "R_X86_64_PLT32";
		case 5: return "R_X86_64_COPY";
		case 6: return "R_X86_64_GLOB_DAT";
		case 7: return "R_X86_64_JMP_SLOT";
		case 8: return "R_X86_64_RELATIVE";
		case 9: return "R_X86_64_GOTPCREL";
		case 10: return "R_X86_64_32";
		case 11: return "R_X86_64_32S";
		case 12: return "R_X86_64_16";
		case 13: return "R_X86_64_PC16";
		case 14: return "R_X86_64_8";
		case 15: return "R_X86_64_PC8";
		case 16: return "R_X86_64_DTPMOD64";
		case 17: return "R_X86_64_DTPOFF64";
		case 18: return "R_X86_64_TPOFF64";
		case 19: return "R_X86_64_TLSGD";
		case 20: return "R_X86_64_TLSLD";
		case 21: return "R_X86_64_DTPOFF32";
		case 22: return "R_X86_64_GOTTPOFF";
		case 23: return "R_X86_64_TPOFF32";
		default: return "";
		}
	default: return "";
	}
}

static void	 add_dumpop(struct readelf *re, size_t sn, int op);
static void	 dump_elf(struct readelf *re);
static void	 dump_dyn_val(struct readelf *re, GElf_Dyn *dyn, uint32_t stab);
static void	 dump_dynamic(struct readelf *re);
static void	 dump_svr4_hash(struct section *s);
static void	 dump_svr4_hash64(struct readelf *re, struct section *s);
static void	 dump_gnu_hash(struct readelf *re, struct section *s);
static void	 dump_hash(struct readelf *re);
static void	 dump_phdr(struct readelf *re);
static void	 dump_symtab(struct readelf *re, int i);
static void	 dump_symtabs(struct readelf *re);
static void	 dump_ver(struct readelf *re);
static void	 dump_verdef(struct readelf *re, int dump);
static void	 dump_verneed(struct readelf *re, int dump);
static void	 dump_versym(struct readelf *re);
static struct dumpop *find_dumpop(struct readelf *re, size_t sn, int op);
static const char *get_string(struct readelf *re, int strtab, size_t off);
static const char *get_symbol_name(struct readelf *re, int symtab, int i);
static uint64_t	 get_symbol_value(struct readelf *re, int symtab, int i);
static void	 load_sections(struct readelf *re);
static void	 readelf_help(void);
static void	 readelf_usage(void);
static void	 readelf_version(void);
static void	 search_ver(struct readelf *re);

static void
dump_ehdr(struct readelf *re)
{
	size_t		 shnum, shstrndx;
	int		 i;

	printf("ELF Header:\n");

	/* e_ident[]. */
	printf("  Magic:   ");
	for (i = 0; i < EI_NIDENT; i++)
		printf("%.2x ", re->ehdr.e_ident[i]);
	putchar('\n');

	/* EI_CLASS. */
	printf("%-37s%s\n", "  Class:", elf_class(re->ehdr.e_ident[EI_CLASS]));

	/* EI_DATA. */
	printf("%-37s%s\n", "  Data:", elf_endian(re->ehdr.e_ident[EI_DATA]));

	/* EI_VERSION. */
	printf("%-37s%d %s\n", "  Version:", re->ehdr.e_ident[EI_VERSION],
	    elf_ver(re->ehdr.e_ident[EI_VERSION]));

	/* EI_OSABI. */
	printf("%-37s%s\n", "  OS/ABI:", elf_osabi(re->ehdr.e_ident[EI_OSABI]));

	/* EI_ABIVERSION. */
	printf("%-37s%d\n", "  ABI Version:", re->ehdr.e_ident[EI_ABIVERSION]);

	/* e_type. */
	printf("%-37s%s\n", "  Type:", elf_type(re->ehdr.e_type));

	/* e_machine. */
	printf("%-37s%s\n", "  Machine:", elf_machine(re->ehdr.e_machine));

	/* e_version. */
	printf("%-37s%#x\n", "  Version:", re->ehdr.e_version);

	/* e_entry. */
	printf("%-37s%#jx\n", "  Entry point address:",
	    (uintmax_t)re->ehdr.e_entry);

	/* e_phoff. */
	printf("%-37s%ju (bytes into file)\n", "  Start of program headers:",
	    (uintmax_t)re->ehdr.e_phoff);

	/* e_shoff. */
	printf("%-37s%ju (bytes into file)\n", "  Start of section headers:",
	    (uintmax_t)re->ehdr.e_shoff);

	/* e_flags. TODO: add machine flags parse. */
	printf("%-37s%#x\n", "  Flags:", re->ehdr.e_flags);

	/* e_ehsize. */
	printf("%-37s%u (bytes)\n", "  Size of this header:",
	    re->ehdr.e_ehsize);

	/* e_phentsize. */
	printf("%-37s%u (bytes)\n", "  Size of program headers:",
	    re->ehdr.e_phentsize);

	/* e_phnum. */
	printf("%-37s%u\n", "  Number of program headers:", re->ehdr.e_phnum);

	/* e_shentsize. */
	printf("%-37s%u (bytes)\n", "  Size of section headers:",
	    re->ehdr.e_shentsize);

	/* e_shnum. */
	printf("%-37s%u", "  Number of section headers:", re->ehdr.e_shnum);
	if (re->ehdr.e_shnum == SHN_UNDEF) {
		/* Extended section numbering is in use. */
		if (elf_getshnum(re->elf, &shnum))
			printf(" (%ju)", (uintmax_t)shnum);
	}
	putchar('\n');

	/* e_shstrndx. */
	printf("%-37s%u", "  Section header string table index:",
	    re->ehdr.e_shstrndx);
	if (re->ehdr.e_shstrndx == SHN_XINDEX) {
		/* Extended section numbering is in use. */
		if (elf_getshstrndx(re->elf, &shstrndx))
			printf(" (%ju)", (uintmax_t)shstrndx);
	}
	putchar('\n');
}

static void
dump_phdr(struct readelf *re)
{
	const char	*rawfile;
	GElf_Phdr	 phdr;
	size_t		 phnum;
	int		 i, j;

#define	PH_HDR	"Type", "Offset", "VirtAddr", "PhysAddr", "FileSiz",	\
		"MemSiz", "Flg", "Align"
#define	PH_CT	phdr_type(phdr.p_type), (uintmax_t)phdr.p_offset,	\
		(uintmax_t)phdr.p_vaddr, (uintmax_t)phdr.p_paddr,	\
		(uintmax_t)phdr.p_filesz, (uintmax_t)phdr.p_memsz,	\
		phdr.p_flags & PF_R ? 'R' : ' ',			\
		phdr.p_flags & PF_W ? 'W' : ' ',			\
		phdr.p_flags & PF_X ? 'E' : ' ',			\
		(uintmax_t)phdr.p_align

	if (elf_getphnum(re->elf, &phnum) == 0) {
		warnx("elf_getphnum failed: %s", elf_errmsg(-1));
		return;
	}
	if (phnum == 0) {
		printf("\nThere are no program headers in this file.\n");
		return;
	}

	printf("\nElf file type is %s", elf_type(re->ehdr.e_type));
	printf("\nEntry point 0x%jx\n", (uintmax_t)re->ehdr.e_entry);
	printf("There are %ju program headers, starting at offset %ju\n",
	    (uintmax_t)phnum, (uintmax_t)re->ehdr.e_phoff);

	/* Dump program headers. */
	printf("\nProgram Headers:\n");
	if (re->ec == ELFCLASS32)
		printf("  %-15s%-9s%-11s%-11s%-8s%-8s%-4s%s\n", PH_HDR);
	else if (re->options & RE_WW)
		printf("  %-15s%-9s%-19s%-19s%-9s%-9s%-4s%s\n", PH_HDR);
	else
		printf("  %-15s%-19s%-19s%s\n                 %-19s%-20s"
		    "%-7s%s\n", PH_HDR);
	for (i = 0; (size_t)i < phnum; i++) {
		if (gelf_getphdr(re->elf, i, &phdr) != &phdr) {
			warnx("gelf_getphdr failed: %s", elf_errmsg(-1));
			continue;
		}
		/* TODO: Add arch-specific segment type dump. */
		if (re->ec == ELFCLASS32)
			printf("  %-14.14s 0x%6.6jx 0x%8.8jx 0x%8.8jx "
			    "0x%5.5jx 0x%5.5jx %c%c%c %#jx\n", PH_CT);
		else if (re->options & RE_WW)
			printf("  %-14.14s 0x%6.6jx 0x%16.16jx 0x%16.16jx "
			    "0x%6.6jx 0x%6.6jx %c%c%c %#jx\n", PH_CT);
		else
			printf("  %-14.14s 0x%16.16jx 0x%16.16jx 0x%16.16jx\n"
			    "                 0x%16.16jx 0x%16.16jx  %c%c%c"
			    "    %#jx\n", PH_CT);
		if (phdr.p_type == PT_INTERP) {
			if ((rawfile = elf_rawfile(re->elf, NULL)) == NULL) {
				warnx("elf_rawfile failed: %s", elf_errmsg(-1));
				continue;
			}
			printf("      [Requesting program interpreter: %s]\n",
				rawfile + phdr.p_offset);
		}
	}

	/* Dump section to segment mapping. */
	printf("\n Section to Segment mapping:\n");
	printf("  Segment Sections...\n");
	for (i = 0; (size_t)i < phnum; i++) {
		if (gelf_getphdr(re->elf, i, &phdr) != &phdr) {
			warnx("gelf_getphdr failed: %s", elf_errmsg(-1));
			continue;
		}
		printf("   %2.2d     ", i);
		/* skip NULL section. */
		for (j = 1; (size_t)j < re->shnum; j++)
			if (re->sl[j].off >= phdr.p_offset &&
			    re->sl[j].off + re->sl[j].sz <=
			    phdr.p_offset + phdr.p_memsz)
				printf("%s ", re->sl[j].name);
		printf("\n");
	}
#undef	PH_HDR
#undef	PH_CT
}

static char *
section_flags(struct readelf *re, struct section *s)
{
#define BUF_SZ 256
	static char	buf[BUF_SZ];
	int		i, p, nb;

	p = 0;
	nb = re->ec == ELFCLASS32 ? 8 : 16;
	if (re->options & RE_T) {
		snprintf(buf, BUF_SZ, "[%*.*jx]: ", nb, nb,
		    (uintmax_t)s->flags);
		p += nb + 4;
	}
	for (i = 0; section_flag[i].ln != NULL; i++) {
		if ((s->flags & section_flag[i].value) == 0)
			continue;
		if (re->options & RE_T) {
			snprintf(&buf[p], BUF_SZ - p, "%s, ",
			    section_flag[i].ln);
			p += strlen(section_flag[i].ln) + 2;
		} else
			buf[p++] = section_flag[i].sn;
	}
	if (re->options & RE_T && p > nb + 4)
		p -= 2;
	buf[p] = '\0';

	return (buf);
}

static void
dump_shdr(struct readelf *re)
{
	struct section	*s;
	int		 i;

#define	S_HDR	"[Nr] Name", "Type", "Addr", "Off", "Size", "ES",	\
		"Flg", "Lk", "Inf", "Al"
#define	S_HDRL	"[Nr] Name", "Type", "Address", "Offset", "Size",	\
		"EntSize", "Flags", "Link", "Info", "Align"
#define	ST_HDR	"[Nr] Name", "Type", "Addr", "Off", "Size", "ES",	\
		"Lk", "Inf", "Al", "Flags"
#define	ST_HDRL	"[Nr] Name", "Type", "Address", "Offset", "Link",	\
		"Size", "EntSize", "Info", "Align", "Flags"
#define	S_CT	i, s->name, section_type(s->type), (uintmax_t)s->addr,	\
		(uintmax_t)s->off, (uintmax_t)s->sz,			\
		(uintmax_t)s->entsize, section_flags(re, s),		\
		s->link, s->info, (uintmax_t)s->align
#define	ST_CT	i, s->name, section_type(s->type), (uintmax_t)s->addr,	\
		(uintmax_t)s->off, (uintmax_t)s->sz,			\
		(uintmax_t)s->entsize, s->link, s->info,		\
		(uintmax_t)s->align, section_flags(re, s)
#define	ST_CTL	i, s->name, section_type(s->type), (uintmax_t)s->addr,	\
		(uintmax_t)s->off, s->link, (uintmax_t)s->sz,		\
		(uintmax_t)s->entsize, s->info, (uintmax_t)s->align,	\
		section_flags(re, s)

	if (re->shnum == 0) {
		printf("\nThere are no sections in this file.\n");
		return;
	}
	printf("There are %ju section headers, starting at offset 0x%jx:\n",
	    (uintmax_t)re->shnum, (uintmax_t)re->ehdr.e_shoff);
	printf("\nSection Headers:\n");
	if (re->ec == ELFCLASS32) {
		if (re->options & RE_T)
			printf("  %s\n       %-16s%-9s%-7s%-7s%-5s%-3s%-4s%s\n"
			    "%12s\n", ST_HDR);
		else
			printf("  %-23s%-16s%-9s%-7s%-7s%-3s%-4s%-3s%-4s%s\n",
			    S_HDR);
	} else if (re->options & RE_WW) {
		if (re->options & RE_T)
			printf("  %s\n       %-16s%-17s%-7s%-7s%-5s%-3s%-4s%s\n"
			    "%12s\n", ST_HDR);
		else
			printf("  %-23s%-16s%-17s%-7s%-7s%-3s%-4s%-3s%-4s%s\n",
			    S_HDR);
	} else {
		if (re->options & RE_T)
			printf("  %s\n       %-18s%-17s%-18s%s\n       %-18s"
			    "%-17s%-18s%s\n%12s\n", ST_HDRL);
		else
			printf("  %-23s%-17s%-18s%s\n       %-18s%-17s%-7s%"
			    "-6s%-6s%s\n", S_HDRL);
	}
	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (re->ec == ELFCLASS32) {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s %8.8jx"
				    " %6.6jx %6.6jx %2.2jx  %2u %3u %2ju\n"
				    "       %s\n", ST_CT);
			else
				printf("  [%2d] %-17.17s %-15.15s %8.8jx"
				    " %6.6jx %6.6jx %2.2jx %3s %2u %3u %2ju\n",
				    S_CT);
		} else if (re->options & RE_WW) {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s %16.16jx"
				    " %6.6jx %6.6jx %2.2jx  %2u %3u %2ju\n"
				    "       %s\n", ST_CT);
			else
				printf("  [%2d] %-17.17s %-15.15s %16.16jx"
				    " %6.6jx %6.6jx %2.2jx %3s %2u %3u %2ju\n",
				    S_CT);
		} else {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s  %16.16jx"
				    "  %16.16jx  %u\n       %16.16jx %16.16jx"
				    "  %-16u  %ju\n       %s\n", ST_CTL);
			else
				printf("  [%2d] %-17.17s %-15.15s  %16.16jx"
				    "  %8.8jx\n       %16.16jx  %16.16jx "
				    "%3s      %2u   %3u     %ju\n", S_CT);
		}
	}
	if ((re->options & RE_T) == 0)
		printf("Key to Flags:\n  W (write), A (alloc),"
		    " X (execute), M (merge), S (strings)\n"
		    "  I (info), L (link order), G (group), x (unknown)\n"
		    "  O (extra OS processing required)"
		    " o (OS specific), p (processor specific)\n");

#undef	S_HDR
#undef	S_HDRL
#undef	ST_HDR
#undef	ST_HDRL
#undef	S_CT
#undef	ST_CT
#undef	ST_CTL
}

static void
dump_dynamic(struct readelf *re)
{
	GElf_Dyn	 dyn;
	Elf_Data	*d;
	struct section	*s;
	int		 elferr, i, j;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type != SHT_DYNAMIC)
			continue;
		(void) elf_errno();
		if ((d = elf_getdata(s->scn, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				warnx("elf_getdata failed: %s", elf_errmsg(-1));
			return;
		}
		if (d->d_size <= 0)
			return;
		printf("\nDynamic section at offset 0x%jx", (uintmax_t)s->off);
		printf(" contains %ju entries:\n", s->sz / s->entsize);
		if (re->ec == ELFCLASS32)
			printf("%5s%12s%28s\n", "Tag", "Type", "Name/Value");
		else
			printf("%5s%20s%28s\n", "Tag", "Type", "Name/Value");
		for (j = 0; (uint64_t)j < s->sz / s->entsize; j++) {
			if (gelf_getdyn(d, j, &dyn) != &dyn) {
				warnx("gelf_getdyn failed: %s", elf_errmsg(-1));
				continue;
			}
			/* Dump dynamic entry type. */
			if (re->ec == ELFCLASS32)
				printf(" 0x%8.8jx", (uintmax_t)dyn.d_tag);
			else
				printf(" 0x%16.16jx", (uintmax_t)dyn.d_tag);
			printf(" %-20s", dt_type(dyn.d_tag));
			/* Dump dynamic entry value. */
			dump_dyn_val(re, &dyn, s->link);
		}
	}
}

static void
dump_dyn_val(struct readelf *re, GElf_Dyn *dyn, uint32_t stab)
{
	const char *name;

	/* These entry values are index into the string table. */
	name = NULL;
	if (dyn->d_tag == DT_NEEDED || dyn->d_tag == DT_SONAME ||
	    dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
		if (stab == SHN_UNDEF)
			name = "ERROR";
		else if ((name = elf_strptr(re->elf, stab, dyn->d_un.d_val)) ==
		    NULL) {
			(void) elf_errno(); /* clear error */
			name = "ERROR";
		}
	}

	switch(dyn->d_tag) {
	case DT_NULL:
	case DT_PLTGOT:
	case DT_HASH:
	case DT_STRTAB:
	case DT_SYMTAB:
	case DT_RELA:
	case DT_INIT:
	case DT_SYMBOLIC:
	case DT_REL:
	case DT_DEBUG:
	case DT_TEXTREL:
	case DT_JMPREL:
	case DT_FINI:
	case DT_VERDEF:
	case DT_VERNEED:
	case DT_VERSYM:
		printf(" 0x%jx\n", (uintmax_t)dyn->d_un.d_val);
		break;
	case DT_PLTRELSZ:
	case DT_RELASZ:
	case DT_RELAENT:
	case DT_STRSZ:
	case DT_SYMENT:
	case DT_RELSZ:
	case DT_RELENT:
	case DT_INIT_ARRAYSZ:
	case DT_FINI_ARRAYSZ:
		printf(" %ju (bytes)\n", dyn->d_un.d_val);
		break;
 	case DT_RELACOUNT:
	case DT_RELCOUNT:
	case DT_VERDEFNUM:
	case DT_VERNEEDNUM:
		printf(" %ju\n", dyn->d_un.d_val);
		break;
	case DT_NEEDED:
		printf(" Shared libarary: [%s]\n", name);
		break;
	case DT_SONAME:
		printf(" Library soname: [%s]\n", name);
		break;
	case DT_RPATH:
		printf(" Library rpath: [%s]\n", name);
		break;
	case DT_RUNPATH:
		printf(" Library runpath: [%s]\n", name);
		break;
	case DT_PLTREL:
		printf(" %s\n", dt_type(dyn->d_un.d_val));
		break;
	default:
		printf("\n");
	}
}

static void
dump_rel(struct readelf *re, struct section *s, Elf_Data *d)
{
	GElf_Rel r;
	const char *symname;
	uint64_t symval;
	int i, len;

#define	REL_HDR "r_offset", "r_info", "r_type", "st_value", "st_name"
#define	REL_CT  (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	   \
		r_type(re->ehdr.e_machine, GELF_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname			   \

	printf("\nRelocation section (%s):\n", s->name);
	if (re->ec == ELFCLASS32)
		printf("%-8s %-8s %-19s %-8s %s\n", REL_HDR);
	else {
		if (re->options & RE_WW)
			printf("%-16s %-16s %-24s %-16s %s\n", REL_HDR);
		else
			printf("%-12s %-12s %-19s %-16s %s\n", REL_HDR);
	}
	len = d->d_size / s->entsize;
	for (i = 0; i < len; i++) {
		if (gelf_getrel(d, i, &r) != &r) {
			warnx("gelf_getrel failed: %s", elf_errmsg(-1));
			continue;
		}
		symname = get_symbol_name(re, s->link, GELF_R_SYM(r.r_info));
		symval = get_symbol_value(re, s->link, GELF_R_SYM(r.r_info));
		if (re->ec == ELFCLASS32)
			printf("%8.8jx %8.8jx %-19.19s %8.8jx %s\n", REL_CT);
		else {
			if (re->options & RE_WW)
				printf("%16.16jx %16.16jx %-24.24s"
				    " %16.16jx %s\n", REL_CT);
			else
				printf("%12.12jx %12.12jx %-19.19s"
				    " %16.16jx %s\n", REL_CT);
		}
	}

#undef	REL_HDR
#undef	REL_CT
}

static void
dump_rela(struct readelf *re, struct section *s, Elf_Data *d)
{
	GElf_Rela r;
	const char *symname;
	uint64_t symval;
	int i, len;

#define	RELA_HDR "r_offset", "r_info", "r_type", "st_value", \
		"st_name + r_addend"
#define	RELA_CT  (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	   \
		r_type(re->ehdr.e_machine, GELF_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname, (intmax_t)r.r_addend   \

	printf("\nRelocation section with addend (%s):\n", s->name);
	if (re->ec == ELFCLASS32)
		printf("%-8s %-8s %-19s %-8s %s\n", RELA_HDR);
	else {
		if (re->options & RE_WW)
			printf("%-16s %-16s %-24s %-16s %s\n", RELA_HDR);
		else
			printf("%-12s %-12s %-19s %-16s %s\n", RELA_HDR);
	}
	len = d->d_size / s->entsize;
	for (i = 0; i < len; i++) {
		if (gelf_getrela(d, i, &r) != &r) {
			warnx("gelf_getrel failed: %s", elf_errmsg(-1));
			continue;
		}
		symname = get_symbol_name(re, s->link, GELF_R_SYM(r.r_info));
		symval = get_symbol_value(re, s->link, GELF_R_SYM(r.r_info));
		if (re->ec == ELFCLASS32)
			printf("%8.8jx %8.8jx %-19.19s %8.8jx %s + %jd\n",
			    RELA_CT);
		else {
			if (re->options & RE_WW)
				printf("%16.16jx %16.16jx %-24.24s"
				    " %16.16jx %s + %jd\n", RELA_CT);
			else
				printf("%12.12jx %12.12jx %-19.19s"
				    " %16.16jx %s + %jd\n", RELA_CT);
		}
	}

#undef	RELA_HDR
#undef	RELA_CT
}

static void
dump_reloc(struct readelf *re)
{
	struct section *s;
	Elf_Data *d;
	int i, elferr;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_REL || s->type == SHT_RELA) {
			(void) elf_errno();
			if ((d = elf_getdata(s->scn, NULL)) == NULL) {
				elferr = elf_errno();
				if (elferr != 0)
					warnx("elf_getdata failed: %s",
					    elf_errmsg(elferr));
				continue;
			}
			if (s->type == SHT_REL)
				dump_rel(re, s, d);
			else
				dump_rela(re, s, d);
		}
	}
}

static void
dump_symtab(struct readelf *re, int i)
{
	struct section *s;
	Elf_Data *d;
	GElf_Sym sym;
	const char *name;
	int elferr, stab, j;

	s = &re->sl[i];
	stab = s->link;
	(void) elf_errno();
	if ((d = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return;
	}
	if (d->d_size <= 0)
		return;
	printf("Symbol table (%s)", s->name);
	printf(" contains %ju entries:\n", s->sz / s->entsize);
	printf("%7s%9s%14s%5s%8s%6s%9s%5s\n", "Num:", "Value", "Size", "Type",
	    "Bind", "Vis", "Ndx", "Name");

	for (j = 0; (uint64_t)j < s->sz / s->entsize; j++) {
		if (gelf_getsym(d, j, &sym) != &sym) {
			warnx("gelf_getsym failed: %s", elf_errmsg(-1));
			continue;
		}
		printf("%6d:", j);
		printf(" %16.16jx", (uintmax_t)sym.st_value);
		printf(" %5ju", sym.st_size);
		printf(" %-7s", st_type(GELF_ST_TYPE(sym.st_info)));
		printf(" %-6s", st_bind(GELF_ST_BIND(sym.st_info)));
		printf(" DEFAULT "); /* FIXME */
		printf(" %3s", st_shndx(sym.st_shndx));
		if ((name = elf_strptr(re->elf, stab, sym.st_name)) != NULL)
			printf(" %s", name);
		/* Append symbol version string for SHT_DYNSYM symbol table. */
		if (s->type == SHT_DYNSYM && re->vname != NULL &&
		    re->vs != NULL && re->vs[j] > 1)
			printf("@%s (%d)", re->vname[re->vs[j]], re->vs[j]);
		putchar('\n');
	}

}

static void
dump_symtabs(struct readelf *re)
{
	GElf_Dyn dyn;
	Elf_Data *d;
	struct section *s;
	uint64_t dyn_off;
	int elferr, i;

	/*
	 * If -D is specified, only dump the symbol table specified by
	 * the DT_SYMTAB entry in the .dynamic section.
	 */
	dyn_off = 0;
	if (re->options & RE_DD) {
		s = NULL;
		for (i = 0; (size_t)i < re->shnum; i++)
			if (re->sl[i].type == SHT_DYNAMIC) {
				s = &re->sl[i];
				break;
			}
		if (s == NULL)
			return;
		(void) elf_errno();
		if ((d = elf_getdata(s->scn, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				warnx("elf_getdata failed: %s", elf_errmsg(-1));
			return;
		}
		if (d->d_size <= 0)
			return;

		for (i = 0; (uint64_t)i < s->sz / s->entsize; i++) {
			if (gelf_getdyn(d, i, &dyn) != &dyn) {
				warnx("gelf_getdyn failed: %s", elf_errmsg(-1));
				continue;
			}
			if (dyn.d_tag == DT_SYMTAB) {
				dyn_off = dyn.d_un.d_val;
				break;
			}
		}
	}

	/* Find and dump symbol tables. */
	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_SYMTAB || s->type == SHT_DYNSYM) {
			if (re->options & RE_DD) {
				if (dyn_off == s->addr) {
					dump_symtab(re, i);
					break;
				}
			} else
				dump_symtab(re, i);
		}
	}
}

static void
dump_svr4_hash(struct section *s)
{
	Elf_Data	*d;
	uint32_t	*buf;
	uint32_t	 nbucket, nchain;
	uint32_t	*bucket, *chain;
	uint32_t	*bl, *c, maxl, total;
	int		 elferr, i, j;

	/* Read and parse the content of .hash section. */
	(void) elf_errno();
	if ((d = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return;
	}
	if (d->d_size < 2 * sizeof(uint32_t)) {
		warnx(".hash section too small");
		return;
	}
	buf = d->d_buf;
	nbucket = buf[0];
	nchain = buf[1];
	if (nbucket <= 0 || nchain <= 0) {
		warnx("Malformed .hash section");
		return;
	}
	if (d->d_size != (nbucket + nchain + 2) * sizeof(uint32_t)) {
		warnx("Malformed .hash section");
		return;
	}
	bucket = &buf[2];
	chain = &buf[2 + nbucket];

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j < nchain; j = chain[j])
			if (++bl[i] > maxl)
				maxl = bl[i];
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("\nHistogram for bucket list length (total of %u buckets):\n",
	    nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint32_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10u\t(%5.1f%%)\t%5.1f%%\n", i, c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

static void
dump_svr4_hash64(struct readelf *re, struct section *s)
{
	Elf_Data	*d, dst;
	uint64_t	*buf;
	uint64_t	 nbucket, nchain;
	uint64_t	*bucket, *chain;
	uint64_t	*bl, *c, maxl, total;
	int		 elferr, i, j;

	/*
	 * ALPHA uses 64-bit hash entries. Since libelf assumes that
	 * .hash section contains only 32-bit entry, an explicit
	 * gelf_xlatetom is needed here.
	 */
	(void) elf_errno();
	if ((d = elf_rawdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_rawdata failed: %s",
			    elf_errmsg(elferr));
		return;
	}
	d->d_type = ELF_T_XWORD;
	memcpy(&dst, d, sizeof(Elf_Data));
	if (gelf_xlatetom(re->elf, &dst, d,
		re->ehdr.e_ident[EI_DATA]) != &dst) {
		warnx("gelf_xlatetom failed: %s", elf_errmsg(-1));
		return;
	}
	if (dst.d_size < 2 * sizeof(uint64_t)) {
		warnx(".hash section too small");
		return;
	}
	buf = dst.d_buf;
	nbucket = buf[0];
	nchain = buf[1];
	if (nbucket <= 0 || nchain <= 0) {
		warnx("Malformed .hash section");
		return;
	}
	if (d->d_size != (nbucket + nchain + 2) * sizeof(uint32_t)) {
		warnx("Malformed .hash section");
		return;
	}
	bucket = &buf[2];
	chain = &buf[2 + nbucket];

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j < nchain; j = chain[j])
			if (++bl[i] > maxl)
				maxl = bl[i];
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint64_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("Histogram for bucket list length (total of %ju buckets):\n",
	    (uintmax_t)nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint64_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10ju\t(%5.1f%%)\t%5.1f%%\n", i, (uintmax_t)c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

static void
dump_gnu_hash(struct readelf *re, struct section *s)
{
	struct section	*ds;
	Elf_Data	*d;
	uint32_t	*buf;
	uint32_t	*bucket, *chain;
	uint32_t	 nbucket, nchain, symndx, maskwords, shift2;
	uint32_t	*bl, *c, maxl, total;
	int		 elferr, dynsymcount, i, j;

	(void) elf_errno();
	if ((d = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s",
			    elf_errmsg(elferr));
		return;
	}
	if (d->d_size < 4 * sizeof(uint32_t)) {
		warnx(".gnu.hash section too small");
		return;
	}
	buf = d->d_buf;
	nbucket = buf[0];
	symndx = buf[1];
	maskwords = buf[2];
	shift2 = buf[3];
	buf += 4;
	ds = &re->sl[s->link];
	dynsymcount = ds->sz / ds->entsize;
	nchain = dynsymcount - symndx;
	if (d->d_size != 4 * sizeof(uint32_t) + maskwords *
	    (re->ec == ELFCLASS32 ? sizeof(uint32_t) : sizeof(uint64_t)) +
	    (nbucket + nchain) * sizeof(uint32_t)) {
		warnx("Malformed .gnu.hash section");
		return;
	}
	bucket = buf + (re->ec == ELFCLASS32 ? maskwords : maskwords * 2);
	chain = bucket + nbucket;

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j - symndx < nchain;
		     j++) {
			if (++bl[i] > maxl)
				maxl = bl[i];
			if (chain[j - symndx] & 1)
				break;
		}
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EX_SOFTWARE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("Histogram for bucket list length (total of %u buckets):\n",
	    nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint32_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10u\t(%5.1f%%)\t%5.1f%%\n", i, c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

static void
dump_hash(struct readelf *re)
{
	struct section	*s;
	int		 i;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_HASH || s->type == SHT_GNU_HASH) {
			if (s->type == SHT_GNU_HASH)
				dump_gnu_hash(re, s);
			else if (re->ehdr.e_machine == EM_ALPHA &&
			    s->entsize == 8)
				dump_svr4_hash64(re, s);
			else
				dump_svr4_hash(s);
		}
	}
}

/*
 * Symbol versioning sections are the same for 32bit and 64bit
 * ELF objects.
 */
#define Elf_Verdef	Elf32_Verdef
#define	Elf_Verdaux	Elf32_Verdaux
#define	Elf_Verneed	Elf32_Verneed
#define	Elf_Vernaux	Elf32_Vernaux

#define	SAVE_VERSION_NAME(ndx, name)					\
	do {								\
		while (ndx >= re->vname_sz) {				\
			nv = realloc(re->vname,				\
			    sizeof(*re->vname)*re->vname_sz*2);		\
			if (nv == NULL) {				\
				warn("realloc failed");			\
				free(re->vname);			\
				return;					\
			}						\
			re->vname = nv;					\
			for (i = re->vname_sz; i < re->vname_sz*2; i++) \
				re->vname[i] = NULL;			\
			re->vname_sz *= 2;				\
		}							\
		if (ndx > 1)						\
			re->vname[ndx] = name;				\
	} while (0)


static void
dump_verdef(struct readelf *re, int dump)
{
	struct section *s;
	Elf_Data *d;
	Elf_Verdef *vd;
	Elf_Verdaux *vda;
	uint8_t *buf, *end, *buf2;
	const char **nv, *name;
	int elferr, i;

	if ((s = re->vd_s) == NULL)
		return;

	if (re->vname == NULL) {
		re->vname_sz = 16;
		if ((re->vname = calloc(re->vname_sz, sizeof(*re->vname))) ==
		    NULL) {
			warn("calloc failed");
			return;
		}
		re->vname[0] = "*local*";
		re->vname[1] = "*global*";
	}

	if (dump)
		printf("\nVersion definition section (%s):\n", s->name);
	(void) elf_errno();
	if ((d = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return;
	}
	if (d->d_size == 0)
		return;

	buf = d->d_buf;
	end = buf + d->d_size;
	while (buf + sizeof(Elf_Verdef) <= end) {
		vd = (Elf_Verdef *) (uintptr_t) buf;
		if (dump) {
			printf("  0x%4.4lx", buf - (uint8_t *)d->d_buf);
			printf(" vd_version: %u vd_flags: %d"
			    " vd_ndx: %u vd_cnt: %u", vd->vd_version,
			    vd->vd_flags, vd->vd_ndx, vd->vd_cnt);
		}
		buf2 = buf + vd->vd_aux;
		vda = (Elf_Verdaux *) (uintptr_t) buf2;
		name = get_string(re, s->link, vda->vda_name);
		if (dump)
			printf(" vda_name: %s\n", name);
		SAVE_VERSION_NAME((int)vd->vd_ndx, name);
		if (vd->vd_next == 0)
			break;
		buf += vd->vd_next;
	}
}

static void
dump_verneed(struct readelf *re, int dump)
{
	struct section *s;
	Elf_Data *d;
	Elf_Verneed *vn;
	Elf_Vernaux *vna;
	uint8_t *buf, *end, *buf2;
	const char **nv, *name;
	int elferr, i, j;

	if ((s = re->vn_s) == NULL)
		return;

	if (re->vname == NULL) {
		re->vname_sz = 16;
		if ((re->vname = calloc(re->vname_sz, sizeof(*re->vname))) ==
		    NULL) {
			warn("calloc failed");
			return;
		}
		re->vname[0] = "*local*";
		re->vname[1] = "*global*";
	}

	if (dump)
		printf("\nVersion needed section (%s):\n", s->name);
	(void) elf_errno();
	if ((d = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return;
	}
	if (d->d_size == 0)
		return;

	buf = d->d_buf;
	end = buf + d->d_size;
	while (buf + sizeof(Elf_Verneed) <= end) {
		vn = (Elf_Verneed *) (uintptr_t) buf;
		if (dump) {
			printf("  0x%4.4lx", buf - (uint8_t *)d->d_buf);
			printf(" vn_version: %u vn_file: %s vn_cnt: %u\n",
			    vn->vn_version,
			    get_string(re, s->link, vn->vn_file),
			    vn->vn_cnt);
		}
		buf2 = buf + vn->vn_aux;
		j = 0;
		while (buf2 + sizeof(Elf_Vernaux) <= end && j < vn->vn_cnt) {
			vna = (Elf32_Vernaux *) (uintptr_t) buf2;
			if (dump)
				printf("  0x%4.4lx",
				    buf2 - (uint8_t *)d->d_buf);
			name = get_string(re, s->link, vna->vna_name);
			if (dump)
				printf("   vna_name: %s vna_flags: %u"
				    " vna_other: %u\n", name,
				    vna->vna_flags, vna->vna_other);
			SAVE_VERSION_NAME((int)vna->vna_other, name);
			if (vna->vna_next == 0)
				break;
			buf2 += vna->vna_next;
		}
		if (vn->vn_next == 0)
			break;
		buf += vn->vn_next;
	}
}

static void
dump_versym(struct readelf *re)
{
	int i;

	if (re->vs_s == NULL || re->vname == NULL || re->vs == NULL)
		return;
	printf("\nVersion symbol section (%s):\n", re->vs_s->name);
	for (i = 0; i < re->vs_sz; i++) {
		if ((i & 3) == 0) {
			if (i > 0)
				putchar('\n');
			printf("  %03x", i);
		}
		printf(" %3d %-12s ", re->vs[i], re->vname[re->vs[i]]);
	}
	putchar('\n');
}

static void
dump_ver(struct readelf *re)
{

	if (re->vs_s && re->vname && re->vs)
		dump_versym(re);
	if (re->vd_s)
		dump_verdef(re, 1);
	if (re->vn_s)
		dump_verneed(re, 1);
}

static void
search_ver(struct readelf *re)
{
	struct section *s;
	Elf_Data *d;
	int elferr, i;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_SUNW_versym)
			re->vs_s = s;
		if (s->type == SHT_SUNW_verneed)
			re->vn_s = s;
		if (s->type == SHT_SUNW_verdef)
			re->vd_s = s;
	}
	if (re->vd_s)
		dump_verdef(re, 0);
	if (re->vn_s)
		dump_verneed(re, 0);
	if (re->vs_s && re->vname != NULL) {
		(void) elf_errno();
		if ((d = elf_getdata(re->vs_s->scn, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				warnx("elf_getdata failed: %s",
				    elf_errmsg(elferr));
			return;
		}
		if (d->d_size == 0)
			return;
		re->vs = d->d_buf;
		re->vs_sz = d->d_size / sizeof(Elf32_Half);
	}
}

#undef	Elf_Verdef
#undef	Elf_Verdaux
#undef	Elf_Verneed
#undef	Elf_Vernaux
#undef	SAVE_VERSION_NAME

/*
 * Retrieve a string using string table section index and the string offset.
 */
static const char*
get_string(struct readelf *re, int strtab, size_t off)
{
	const char *name;

	if ((name = elf_strptr(re->elf, strtab, off)) == NULL)
		return ("");

	return (name);
}

/*
 * Retrieve the name of a symbol using the section index of the symbol
 * table and the index of the symbol within that table.
 */
static const char *
get_symbol_name(struct readelf *re, int symtab, int i)
{
	struct section	*s;
	const char	*name;
	GElf_Sym	 sym;
	Elf_Data	*data;
	int		 elferr;

	s = &re->sl[symtab];
	if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM)
		return ("");
	(void) elf_errno();
	if ((data = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return ("");
	}
	if (gelf_getsym(data, i, &sym) != &sym)
		return ("");
	if ((name = elf_strptr(re->elf, s->link, sym.st_name)) == NULL)
		return ("");

	return (name);
}

static uint64_t
get_symbol_value(struct readelf *re, int symtab, int i)
{
	struct section	*s;
	GElf_Sym	 sym;
	Elf_Data	*data;
	int		 elferr;

	s = &re->sl[symtab];
	if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM)
		return (0);
	(void) elf_errno();
	if ((data = elf_getdata(s->scn, NULL)) == NULL) {
		elferr = elf_errno();
		if (elferr != 0)
			warnx("elf_getdata failed: %s", elf_errmsg(elferr));
		return (0);
	}
	if (gelf_getsym(data, i, &sym) != &sym)
		return (0);

	return (sym.st_value);
}

static void
hex_dump(struct readelf *re)
{
	struct section *s;
	Elf_Data *d;
	uint8_t *buf;
	size_t sz, nbytes;
	uint64_t addr;
	int elferr, i, j;

	for (i = 1; (size_t)i < re->shnum; i++) {
		if (find_dumpop(re, (size_t)i, HEX_DUMP) == NULL)
			continue;
		s =& re->sl[i];
		(void) elf_errno();
		if ((d = elf_getdata(s->scn, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				warnx("elf_getdata failed: %s", elf_errmsg(-1));
			continue;
		}
		if (d->d_size <= 0)
			continue;
		buf = d->d_buf;
		sz = d->d_size;
		addr = s->addr;
		printf("\nHex dump of section '%s':\n", s->name);
		while (sz > 0) {
			printf("  0x%8.8jx ", (uintmax_t)addr);
			nbytes = sz > 16? 16 : sz;
			for (j = 0; j < 16; j++) {
				if ((size_t)j < nbytes)
					printf("%2.2x", buf[j]);
				else
					printf("  ");
				if ((j & 3) == 3)
					printf(" ");
			}
			for (j = 0; (size_t)j < nbytes; j++) {
				if (isprint(buf[j]))
					printf("%c", buf[j]);
				else
					printf(".");
			}
			printf("\n");
			buf += nbytes;
			addr += nbytes;
			sz -= nbytes;
		}
	}
}

static void
load_sections(struct readelf *re)
{
	struct section	*s;
	const char	*name;
	Elf_Scn		*scn;
	GElf_Shdr	 sh;
	size_t		 shstrndx, ndx;
	int		 elferr;

	/* Allocate storage for internal section list. */
	if (!elf_getshnum(re->elf, &re->shnum)) {
		warnx("elf_getshnum failed: %s", elf_errmsg(-1));
		return;
	}
	if (re->sl != NULL)
		free(re->sl);
	if ((re->sl = calloc(re->shnum, sizeof(*re->sl))) == NULL)
		err(EX_SOFTWARE, "calloc failed");

	/* Get the index of .shstrtab section. */
	if (!elf_getshstrndx(re->elf, &shstrndx)) {
		warnx("elf_getshstrndx failed: %s", elf_errmsg(-1));
		return;
	}

	if ((scn = elf_getscn(re->elf, 0)) == NULL) {
		warnx("elf_getscn failed: %s", elf_errmsg(-1));
		return;
	}

	(void) elf_errno();
	do {
		if (gelf_getshdr(scn, &sh) == NULL) {
			warnx("gelf_getshdr failed: %s", elf_errmsg(-1));
			(void) elf_errno();
			continue;
		}
		if ((name = elf_strptr(re->elf, shstrndx, sh.sh_name)) == NULL) {
			(void) elf_errno();
			name = "ERROR";
		}
		if ((ndx = elf_ndxscn(scn)) == SHN_UNDEF) {
			if ((elferr = elf_errno()) != 0)
				warnx("elf_ndxscn failed: %s",
				    elf_errmsg(elferr));
			continue;
		}
		if (ndx >= re->shnum) {
			warnx("section index of '%s' out of range", name);
			continue;
		}
		s = &re->sl[ndx];
		s->name = name;
		s->scn = scn;
		s->off = sh.sh_offset;
		s->sz = sh.sh_size;
		s->entsize = sh.sh_entsize;
		s->align = sh.sh_addralign;
		s->type = sh.sh_type;
		s->flags = sh.sh_flags;
		s->addr = sh.sh_addr;
		s->link = sh.sh_link;
		s->info = sh.sh_info;
	} while ((scn = elf_nextscn(re->elf, scn)) != NULL);
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));
}

static void
dump_elf(struct readelf *re)
{

	/* Fetch ELF header. No need to continue if it fails. */
	if (gelf_getehdr(re->elf, &re->ehdr) == NULL) {
		warnx("gelf_getehdr failed: %s", elf_errmsg(-1));
		return;
	}
	if ((re->ec = gelf_getclass(re->elf)) == ELFCLASSNONE) {
		warnx("gelf_getclass failed: %s", elf_errmsg(-1));
		return;
	}
	if (re->options & ~RE_H)
		load_sections(re);
	if ((re->options & RE_VV) || (re->options && RE_S))
		search_ver(re);
	if (re->options & RE_H)
		dump_ehdr(re);
	if (re->options & RE_L)
		dump_phdr(re);
	if (re->options & RE_SS)
		dump_shdr(re);
	if (re->options & RE_D)
		dump_dynamic(re);
	if (re->options & RE_R)
		dump_reloc(re);
	if (re->options & RE_S)
		dump_symtabs(re);
	if (re->options & RE_II)
		dump_hash(re);
	if (re->options & RE_X)
		hex_dump(re);
	if (re->options & RE_VV)
		dump_ver(re);
}

#ifndef LIBELF_AR

/*
 * Convenient wrapper for general libarchive error handling.
 */
#define	AC(CALL) do {							\
	if ((CALL))							\
		errx(EX_SOFTWARE, "%s", archive_error_string(a));	\
} while (0)

static int
ac_detect_ar(int fd)
{
	struct archive		*a;
	struct archive_entry	*entry;
	int			 r;

	r = -1;
	if ((a = archive_read_new()) == NULL)
		return (0);
	archive_read_support_compression_all(a);
	archive_read_support_format_ar(a);
	if (archive_read_open_fd(a, fd, 10240) == ARCHIVE_OK)
		r = archive_read_next_header(a, &entry);
	archive_read_close(a);
	archive_read_finish(a);

	return (r == ARCHIVE_OK);
}

static void
ac_dump_ar(struct readelf *re, int fd)
{
	struct archive		*a;
	struct archive_entry	*entry;
	const char		*name;
	void			*buff;
	size_t			 size;
	int			 r;

	if (lseek(fd, 0, SEEK_SET) == -1)
		err(EX_IOERR, "lseek failed");
	if ((a = archive_read_new()) == NULL)
		errx(EX_SOFTWARE, "%s", archive_error_string(a));
	archive_read_support_compression_all(a);
	archive_read_support_format_ar(a);
	AC(archive_read_open_fd(a, fd, 10240));
	for(;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_FATAL)
			errx(EX_DATAERR, "%s", archive_error_string(a));
		if (r == ARCHIVE_EOF)
			break;
		if (r == ARCHIVE_WARN || r == ARCHIVE_RETRY)
			warnx("%s", archive_error_string(a));
		if (r == ARCHIVE_RETRY)
			continue;

		name = archive_entry_pathname(entry);

		/* TODO: handle option '-c' here. */

		/* skip pseudo members. */
		if (strcmp(name, "/") == 0 || strcmp(name, "//") == 0)
			continue;

		size = archive_entry_size(entry);
		if (size > 0) {
			if ((buff = malloc(size)) == NULL)
				err(EX_SOFTWARE, "malloc failed");
			if (archive_read_data(a, buff, size) != (ssize_t)size) {
				warnx("%s", archive_error_string(a));
				free(buff);
				continue;
			}
			if ((re->elf = elf_memory(buff, size)) == NULL) {
				warnx("elf_memroy() failed: %s",
				    elf_errmsg(-1));
				free(buff);
				continue;
			}
			dump_elf(re);
			free(buff);
		}
	}
	AC(archive_read_close(a));
	AC(archive_read_finish(a));
}
#endif	/* ! LIBELF_AR */

static void
dump_object(struct readelf *re)
{
	int fd;

	if ((fd = open(re->filename, O_RDONLY)) == -1) {
		warn("open %s failed", re->filename);
		return;
	}

#ifndef	LIBELF_AR
	/*
	 * Detect and process ar(1) archive using libarchive.
	 */
	if (ac_detect_ar(fd)) {
		ac_dump_ar(re, fd);
		return;
	}
#endif	/* ! LIBELF_AR */

	if ((re->flags & DISPLAY_FILENAME) != 0)
		printf("\nFile: %s\n", re->filename);

	if ((re->elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		warnx("elf_begin() failed: %s", elf_errmsg(-1));
		return;
	}

	switch (elf_kind(re->elf)) {
	case ELF_K_NONE:
		warnx("Not an ELF file.");
		return;
	case ELF_K_ELF:
		dump_elf(re);
		break;
	case ELF_K_AR:
		/* dump_ar(re); */
		break;
	default:
		warnx("Internal: libelf returned unknown elf kind.");
		return;
	}

	elf_end(re->elf);
}

static void
add_dumpop(struct readelf *re, size_t sn, int op)
{
	struct dumpop *d;

	if ((d = find_dumpop(re, sn, 0)) == NULL) {
		if ((d = malloc(sizeof(*d))) == NULL)
			err(EX_SOFTWARE, "malloc failed");
		d->sn = sn;
		d->op = op;
		STAILQ_INSERT_TAIL(&re->v_dumpop, d, dumpop_list);
	} else
		d->op |= op;
}

static struct dumpop *
find_dumpop(struct readelf *re, size_t sn, int op)
{
	struct dumpop *d;

	STAILQ_FOREACH(d, &re->v_dumpop, dumpop_list) {
		if (d->sn == sn && (op == 0 || op & d->op))
			return (d);
	}

	return (NULL);
}

static void
readelf_version()
{

	exit(EX_OK);
}

static void
readelf_help()
{

	exit(EX_OK);
}

static void
readelf_usage()
{

	fprintf(stderr, "usage: readelf <options> object ...\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	struct readelf	*re, re_storage;
	unsigned long	 sn;
	int		 opt, i;

	if (argc < 3)
		readelf_usage();

	re = &re_storage;
	memset(re, 0, sizeof(*re));
	STAILQ_INIT(&re->v_dumpop);

	while ((opt = getopt_long(argc, argv, "AacDdegHhIi:lNnprSstuVvWw:x:",
	    longopts, NULL)) != -1) {
		switch(opt) {
		case 'A':
			re->options |= RE_AA;
			break;
		case 'a':
			re->options |= RE_AA | RE_D | RE_H | RE_II | RE_L |
			    RE_R | RE_SS | RE_S | RE_VV;
			break;
		case 'c':
			re->options |= RE_C;
			break;
		case 'D':
			re->options |= RE_DD;
			break;
		case 'd':
			re->options |= RE_D;
			break;
		case 'e':
			re->options |= RE_H | RE_L | RE_SS;
			break;
		case 'g':
			re->options |= RE_G;
			break;
		case 'H':
			readelf_help();
			break;
		case 'h':
			re->options |= RE_H;
			break;
		case 'I':
			re->options |= RE_II;
			break;
		case 'i':
			/* Not implemented yet. */
			break;
		case 'l':
			re->options |= RE_L;
			break;
		case 'N':
			re->options |= RE_NN;
			break;
		case 'n':
			re->options |= RE_N;
			break;
		case 'p':
			re->options |= RE_P;
			break;
		case 'r':
			re->options |= RE_R;
			break;
		case 'S':
			re->options |= RE_SS;
			break;
		case 's':
			re->options |= RE_S;
			break;
		case 't':
			re->options |= RE_T;
			break;
		case 'u':
			re->options |= RE_U;
			break;
		case 'V':
			re->options |= RE_VV;
			break;
		case 'v':
			readelf_version();
			break;
		case 'W':
			re->options |= RE_WW;
			break;
		case 'w':
			re->options |= RE_W;
			/* TODO: Parse -w optarg. */
			break;
		case 'x':
			re->options |= RE_X;
			sn = strtoul(optarg, NULL, 10);
			add_dumpop(re, (size_t)sn, HEX_DUMP);
			break;
		case OPTION_DEBUG_DUMP:
			re->options |= RE_W;
			/* TODO: Parse debug-dump optarg. */
		}
	}

	argv += optind;
	argc -= optind;

	if (argc > 1)
		re->flags |= DISPLAY_FILENAME;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EX_SOFTWARE, "ELF library initialization failed: %s",
		    elf_errmsg(-1));

	for (i = 0; i < argc; i++)
		if (argv[i] != NULL) {
			re->filename = argv[i];
			dump_object(re);
		}

	exit(EX_OK);
}