/*-
 * Copyright (c) 2006 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ar.h>
#include <errno.h>
#include <libelf.h>
#include <string.h>
#include <unistd.h>

#include "elfts.h"
#include "tet_api.h"

IC_REQUIRES_VERSION_INIT();

include(`elfts.m4')
/*
 * The following defines should match that in `./Makefile'.
 */
define(`TP_ELFFILE',`"a1.o"')
define(`TP_ARFILE', `"a.ar"')

/*
 * A NULL `Elf' argument fails.
 */
void
tcArgs_tpNull(void)
{
	int error, result;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr(NULL) fails.");

	result = TET_PASS;
	if (elf_getarhdr(NULL) != NULL ||
	    (error = elf_errno()) != ELF_E_ARGUMENT) {
		TP_FAIL("error=%d \"%s\".", error, elf_errmsg(error));
		result = TET_FAIL;
	}

	tet_result(result);
}

/*
 * elf_getarhdr() on a non-Ar file fails.
 */
static char *nonar = "This is not an AR file.";

void
tcArgs_tpNonAr(void)
{
	Elf *e;
	int error, result;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr(non-ar) fails.");

	TS_OPEN_MEMORY(e, nonar);

	result = TET_PASS;

	if (elf_getarhdr(e) != NULL ||
	    (error = elf_errno()) != ELF_E_ARGUMENT) {
		TP_FAIL("error=%d \"%s\".", error, elf_errmsg(error));
		result = TET_FAIL;
	}

	(void) elf_end(e);

	tet_result(result);
}

/*
 * elf_getarhdr() on a top-level ELF file fails.
 */

void
tcArgs_tpElf(void)
{
	Elf *e;
	int error, fd, result;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr(elf) fails.");

	TS_OPEN_FILE(e, TP_ELFFILE, ELF_C_READ, fd);

	result = TET_PASS;

	if (elf_getarhdr(e) != NULL ||
	    (error = elf_errno()) != ELF_E_ARGUMENT) {
		TP_FAIL("error=%d \"%s\".", error, elf_errmsg(error));
		result = TET_FAIL;
	}

	(void) elf_end(e);

	tet_result(result);
}


/*
 * elf_getarhdr() on an ar archive (not a member) fails.
 */

void
tcAr_tpAr(void)
{
	Elf *e;
	int error, fd, result;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr(ar-descriptor) fails.");

	TS_OPEN_FILE(e, TP_ARFILE, ELF_C_READ, fd);

	result = TET_PASS;

	if (elf_getarhdr(e) != NULL ||
	    (error = elf_errno()) != ELF_E_ARGUMENT) {
		TP_FAIL("error=%d \"%s\".", error, elf_errmsg(error));
		result = TET_FAIL;
	}

	(void) elf_end(e);

	tet_result(result);
}

/*
 * elf_getarhdr() on ar archive members succeed.
 */

/* This list of files must match the order of the files in test archive. */
static char *rfn[] = {
	"s1",
	"a1.o",
	"s------------------------2", /* long file name */
	"a2.o",
	"s 3"	/* file name with blanks */
};

void
tcAr_tpArMember(void)
{
	Elf_Arhdr *arh;
	Elf *ar_e, *e;
	Elf_Cmd c;
	int error, fd, result;
	char **fn;
	struct stat sb;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr() succeeds for all members of an archive.");

	ar_e = e = NULL;
	c = ELF_C_READ;

	TS_OPEN_FILE(ar_e, TP_ARFILE, c, fd);

	result = TET_FAIL;

	fn = rfn;
	while ((e = elf_begin(fd, c, ar_e)) != NULL) {

		if ((arh = elf_getarhdr(e)) == NULL) {
			TP_FAIL("elf_getarhdr(\"%s\") failed: \"%s\".", *fn,
			    elf_errmsg(-1));
			goto done;
		}

		if (stat(*fn, &sb) < 0) {
			TP_UNRESOLVED("stat \"%s\" failed: %s.", *fn,
			    strerror(errno));
			result = TET_UNRESOLVED;
			goto done;
		}

		if (strcmp(arh->ar_name, *fn) != 0) {
			TP_FAIL("name: \"%s\" != \"%s\".", *fn,
			    arh->ar_name);
			goto done;
		}

		if (arh->ar_mode != sb.st_mode) {
			TP_FAIL("\%s\" mode: 0%x != 0%o.", *fn,
			    arh->ar_mode, sb.st_mode);
			goto done;
		}

		if (arh->ar_size != sb.st_size) {
			TP_FAIL("\"%s\" size: %d != %d.", *fn,
			    arh->ar_size, sb.st_size);
			goto done;
		}

		if (arh->ar_uid != sb.st_uid) {
			TP_FAIL("\"%s\" uid: %d != %d.", *fn,
			    arh->ar_uid, sb.st_uid);
			goto done;
		}

		if (arh->ar_gid != sb.st_gid) {
			TP_FAIL("\"%s\" gid: %d != %d.", *fn,
			    arh->ar_gid, sb.st_gid);
			goto done;
		}

		c = elf_next(e);
		(void) elf_end(e); e = NULL;
		fn++;
	}

	if ((error = elf_errno()) != ELF_E_NONE) {
		TP_UNRESOLVED("elf_begin() failed: \"%s\".", elf_errmsg(error));
		result = TET_UNRESOLVED;
	} else
		result = TET_PASS;

 done:
	if (e)
		(void) elf_end(e);
	if (ar_e)
		(void) elf_end(ar_e);

	tet_result(result);

}

#define	RAWNAME_SIZE	16	/* See <ar.h> */

struct arnames {
	char *name;
	char rawname[RAWNAME_SIZE];
};

/* This list of names and raw names must match that in the test archive. */
static struct arnames rn[] = {
	{
		.name = "/",
		.rawname = { '/', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	},
	{
		.name = "//",
		.rawname = { '/', '/', ' ', ' ', ' ', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	},
	{
		.name = "s1",
		.rawname = { 's', '1', '/', ' ', ' ', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	},
	{
		.name = "a1.o",
		.rawname = { 'a', '1', '.', 'o', '/', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	},
	{
		.name = "s------------------------2", /* long file name */
		.rawname = { '/', '0', ' ', ' ', ' ', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	},
	{
		.name =	"a2.o",
		.rawname = { 'a', '2', '.', 'o', '/', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }

	},
	{
		.name = "s 3",	/* file name with blanks */
		.rawname = { 's', ' ', '3', '/', ' ', ' ', ' ', ' ',
			     ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
	}
};

define(`CHECK_SPECIAL',`
	if ((e = elf_begin(fd, c, ar_e)) == NULL) {
		TP_FAIL("elf_begin($1) failed: \"%s\".", elf_errmsg(-1));
		goto done;
	}

	CHECK_NAMES();
')

define(`CHECK_NAMES',`
		if ((arh = elf_getarhdr(e)) == NULL) {
			TP_FAIL("elf_getarhdr(\"%s\") failed: \"%s\".", fn->name,
			    elf_errmsg(-1));
			goto done;
		}

		if (strcmp(arh->ar_name, fn->name) != 0) {
			TP_FAIL("name: \"%s\" != \"%s\".", fn->name,
			    arh->ar_name);
			goto done;
		}


		if (memcmp(arh->ar_rawname, fn->rawname, RAWNAME_SIZE) != 0) {
			TP_FAIL("rawname: \"%s\" != \"%s\".", fn->rawname,
			    arh->ar_rawname);
			goto done;
		}
')

void
tcAr_tpArSpecial(void)
{

	Elf_Arhdr *arh;
	Elf *ar_e, *e;
	Elf_Cmd c;
	int fd, result;
	struct arnames *fn;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr() after an elf_rand(SARMAG) retrieves special members.");

	ar_e = e = NULL;
	c = ELF_C_READ;
	fn = rn;

	TS_OPEN_FILE(ar_e, TP_ARFILE, c, fd);

	result = TET_PASS;

	if (elf_rand(ar_e, (off_t) SARMAG) != (off_t)SARMAG) {
		TP_UNRESOLVED("elf_rand(SARMAG) failed: \"%s\".", elf_errmsg(-1));
		goto done;

	}

	CHECK_SPECIAL(`/');
	CHECK_SPECIAL(`//');

 done:
	if (e)
		(void) elf_end(e);
	if (ar_e)
		(void) elf_end(ar_e);

	tet_result(result);
}

void
tcAr_tpArRawnames(void)
{
	Elf_Arhdr *arh;
	Elf *ar_e, *e;
	Elf_Cmd c;
	int fd, result;
	struct arnames *fn;

	TP_CHECK_INITIALIZATION();

	TP_ANNOUNCE("elf_getarhdr() returns the correct rawnames.");

	ar_e = e = NULL;
	c = ELF_C_READ;
	fn = rn;

	TS_OPEN_FILE(ar_e, TP_ARFILE, c, fd);

	result = TET_PASS;

	if (elf_rand(ar_e, (off_t) SARMAG) != (off_t)SARMAG) {
		TP_UNRESOLVED("elf_rand(SARMAG) failed: \"%s\".", elf_errmsg(-1));
		goto done;

	}

	CHECK_SPECIAL(`/');
	CHECK_SPECIAL(`//');

	/* Check the rest of the archive members. */

	while ((e = elf_begin(fd, c, ar_e)) != NULL) {


		CHECK_NAMES();

		c = elf_next(e);
		(void) elf_end(e); e = NULL;
		fn ++;
	}

 done:
	if (e)
		(void) elf_end(e);
	if (ar_e)
		(void) elf_end(ar_e);

	tet_result(result);
}