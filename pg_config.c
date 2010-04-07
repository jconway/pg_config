/*-------------------------------------------------------------------------
 *
 * pg_config.c
 *		Expose output of pg_config as a system view.
 *
 * Copyright (c) 2010, PostgreSQL Global Development Group
 * ALL RIGHTS RESERVED;
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE AUTHOR OR DISTRIBUTORS HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND DISTRIBUTORS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUTHOR AND DISTRIBUTORS HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

#include "postgres.h"

#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "funcapi.h"
#include "miscadmin.h"
#include "catalog/pg_control.h"
#include "catalog/pg_type.h"
#include "port.h"

static void cleanup_path(char *path);
static void get_configdata(void);
static size_t conf_strlcat(char *dst, const char *src, size_t siz);

#ifdef PGDLLIMPORT
/* Postgres global */
extern PGDLLIMPORT char my_exec_path[];
#else
/* Postgres global */
extern DLLIMPORT char my_exec_path[];
#endif /* PGDLLIMPORT */


PG_MODULE_MAGIC;

struct configdata
{
	char	   *name;
	char	   *setting;
};

static struct configdata ConfigData[] =
{
	{"BINDIR", NULL},
	{"DOCDIR", NULL},
	{"HTMLDIR", NULL},
	{"INCLUDEDIR", NULL},
	{"PKGINCLUDEDIR", NULL},
	{"INCLUDEDIR-SERVER", NULL},
	{"LIBDIR", NULL},
	{"PKGLIBDIR", NULL},
	{"LOCALEDIR", NULL},
	{"MANDIR", NULL},
	{"SHAREDIR", NULL},
	{"SYSCONFDIR", NULL},
	{"PGXS", NULL},
	{"CONFIGURE", NULL},
	{"CC", NULL},
	{"CPPFLAGS", NULL},
	{"CFLAGS", NULL},
	{"CFLAGS_SL", NULL},
	{"LDFLAGS", NULL},
	{"LDFLAGS_SL", NULL},
	{"LIBS", NULL},
	{"VERSION", NULL},
	{NULL, NULL}
};

static const char *dbState(DBState state);
static void get_configdata(void);

Datum pg_config(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_config);
Datum
pg_config(PG_FUNCTION_ARGS)
{
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	Tuplestorestate	   *tupstore;
	HeapTuple			tuple;
	TupleDesc			tupdesc;
	AttInMetadata	   *attinmeta;
	MemoryContext		per_query_ctx;
	MemoryContext		oldcontext;
	char			   *values[2];
	int					i = 0;

	/* check to see if caller supports us returning a tuplestore */
	if (!rsinfo || !(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* get the requested return tuple description */
	tupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);

	/*
	 * Check to make sure we have a reasonable tuple descriptor
	 */
	if (tupdesc->natts != 2 ||
		tupdesc->attrs[0]->atttypid != TEXTOID ||
		tupdesc->attrs[1]->atttypid != TEXTOID)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("query-specified return tuple and "
						"function return type are not compatible")));

	/* OK to use it */
	attinmeta = TupleDescGetAttInMetadata(tupdesc);

	/* let the caller know we're sending back a tuplestore */
	rsinfo->returnMode = SFRM_Materialize;

	/* initialize our tuplestore */
	tupstore = tuplestore_begin_heap(true, false, work_mem);

	get_configdata();
	while (ConfigData[i].name)
	{
		values[0] = ConfigData[i].name;
		values[1] = ConfigData[i].setting;

		tuple = BuildTupleFromCStrings(attinmeta, values);
		tuplestore_puttuple(tupstore, tuple);
		++i;
	}
	
	/*
	 * no longer need the tuple descriptor reference created by
	 * TupleDescGetAttInMetadata()
	 */
	ReleaseTupleDesc(tupdesc);

	tuplestore_donestoring(tupstore);
	rsinfo->setResult = tupstore;

	/*
	 * SFRM_Materialize mode expects us to return a NULL Datum. The actual
	 * tuples are in our tuplestore and passed back through
	 * rsinfo->setResult. rsinfo->setDesc is set to the tuple description
	 * that we actually used to build our tuples with, so the caller can
	 * verify we did what it was expecting.
	 */
	rsinfo->setDesc = tupdesc;
	MemoryContextSwitchTo(oldcontext);

	return (Datum) 0;
}


/*
 * This function cleans up the paths for use with either cmd.exe or Msys
 * on Windows. We need them to use filenames without spaces, for which a
 * short filename is the safest equivalent, eg:
 *		C:/Progra~1/
 */
static void
cleanup_path(char *path)
{
#ifdef WIN32
	char	   *ptr;

	/*
	 * GetShortPathName() will fail if the path does not exist, or short names
	 * are disabled on this file system.  In both cases, we just return the
	 * original path.  This is particularly useful for --sysconfdir, which
	 * might not exist.
	 */
	GetShortPathName(path, path, MAXPGPATH - 1);

	/* Replace '\' with '/' */
	for (ptr = path; *ptr; ptr++)
	{
		if (*ptr == '\\')
			*ptr = '/';
	}
#endif
}

static void
get_configdata(void)
{
	char			path[MAXPGPATH];
	char		   *lastsep;

	strcpy(path, my_exec_path);
	lastsep = strrchr(path, '/');
	if (lastsep)
		*lastsep = '\0';
	cleanup_path(path);
	ConfigData[0].setting = pstrdup(path);

	get_doc_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[1].setting = pstrdup(path);

	get_html_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[2].setting = pstrdup(path);

	get_include_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[3].setting = pstrdup(path);

	get_pkginclude_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[4].setting = pstrdup(path);

	get_includeserver_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[5].setting = pstrdup(path);

	get_lib_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[6].setting = pstrdup(path);

	get_pkglib_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[7].setting = pstrdup(path);

	get_locale_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[8].setting = pstrdup(path);

	get_man_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[9].setting = pstrdup(path);

	get_share_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[10].setting = pstrdup(path);

	get_etc_path(my_exec_path, path);
	cleanup_path(path);
	ConfigData[11].setting = pstrdup(path);

	get_pkglib_path(my_exec_path, path);
	conf_strlcat(path, "/pgxs/src/makefiles/pgxs.mk", sizeof(path));
	cleanup_path(path);
	ConfigData[12].setting = pstrdup(path);

#ifdef VAL_CONFIGURE
	ConfigData[13].setting = pstrdup(VAL_CONFIGURE);
#else
	ConfigData[13].setting = pstrdup("not recorded");
#endif

#ifdef VAL_CC
	ConfigData[14].setting = pstrdup(VAL_CC);
#else
	ConfigData[14].setting = pstrdup("not recorded");
#endif

#ifdef VAL_CPPFLAGS
	ConfigData[15].setting = pstrdup(VAL_CPPFLAGS);
#else
	ConfigData[15].setting = pstrdup("not recorded");
#endif

#ifdef VAL_CFLAGS
	ConfigData[16].setting = pstrdup(VAL_CFLAGS);
#else
	ConfigData[16].setting = pstrdup("not recorded");
#endif

#ifdef VAL_CFLAGS_SL
	ConfigData[17].setting = pstrdup(VAL_CFLAGS_SL);
#else
	ConfigData[17].setting = pstrdup("not recorded");
#endif

#ifdef VAL_LDFLAGS
	ConfigData[18].setting = pstrdup(VAL_LDFLAGS);
#else
	ConfigData[18].setting = pstrdup("not recorded");
#endif

#ifdef VAL_LDFLAGS_SL
	ConfigData[19].setting = pstrdup(VAL_LDFLAGS_SL);
#else
	ConfigData[19].setting = pstrdup("not recorded");
#endif

#ifdef VAL_LIBS
	ConfigData[20].setting = pstrdup(VAL_LIBS);
#else
	ConfigData[20].setting = pstrdup("not recorded");
#endif

	ConfigData[21].setting = pstrdup("PostgreSQL " PG_VERSION);
}

static size_t
conf_strlcat(char *dst, const char *src, size_t siz)
{
	char	   *d = dst;
	const char *s = src;
	size_t		n = siz;
	size_t		dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0)
		return (dlen + strlen(s));
	while (*s != '\0')
	{
		if (n != 1)
		{
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return (dlen + (s - src));	/* count does not include NUL */
}
