/* RT/NTS -- a lightweight, high performance news transit server. */
/* 
 * Copyright (c) 2011 River Tarnell.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
/* $Header: /cvsroot/nts/database.c,v 1.12 2012/01/10 17:14:03 river Exp $ */

#include	<sys/types.h>
#include	<sys/stat.h>

#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>

#include	"database.h"
#include	"config.h"
#include	"log.h"
#include	"nts.h"
#include	"msg.h"
#include	"dbmsg.h"

static char	*db_location;
static uint64_t	 db_cache_size = 1024 * 1024 * 5; /* 5 MB */
DB_ENV		*db_env;

static void	 db_errcall(DB_ENV const *, char const *, char const *);
static void	 db_flush(uv_timer_t *, int);
static void	 db_checkpoint(uv_timer_t *, int);

static uv_timer_t	flush_timer,
			checkpoint_timer;

config_schema_opt_t db_opts[] = {
	{ "path",	OPT_TYPE_STRING,	config_simple_string, &db_location },
	{ "cache-size",	OPT_TYPE_QUANTITY,	config_simple_quantity, &db_cache_size },
	{ }
};

config_schema_stanza_t db_stanza = {
	"database", 0, db_opts
};

int
db_init()
{
	config_add_stanza(&db_stanza);
	return 0;
}

int
db_run()
{
int		 ret;
uv_cpu_info_t	*cpu_infos;
int		 ncpu;

	if (ret = uv_cpu_info(&cpu_infos, &ncpu))
		ncpu = 1;
	else
		uv_free_cpu_info(cpu_infos, ncpu);

	if (!db_location) {
		nts_logm(DB_fac, M_DB_NOLOC);
		return -1;
	}

	if (mkdir(db_location, 0700) == -1 && errno != EEXIST) {
		nts_logm(DB_fac, M_DB_MKDIR, db_location, strerror(errno));
		return -1;
	}

	if (ret = db_env_create(&db_env, 0)) {
		nts_logm(DB_fac, M_DB_ENVFAIL, db_strerror(ret));
		return -1;
	}

	db_env->set_errcall(db_env, db_errcall);

	if (ret = db_env->set_tx_max(db_env, 5 + (ncpu * 2))) {
		nts_logm(DB_fac, M_DB_MXTXNFAIL, db_strerror(ret));
		return -1;
	}

	if (ret = db_env->log_set_config(db_env, DB_LOG_AUTO_REMOVE, 1)) {
		nts_logm(DB_fac, M_DB_LOGCFGFAIL, db_strerror(ret));
		return -1;
	}

	if (ret = db_env->set_lk_detect(db_env, DB_LOCK_DEFAULT)) {
		nts_logm(DB_fac, M_DB_DLKFAIL, db_strerror(ret));
		return -1;
	}

	if (ret = db_env->set_cachesize(db_env, db_cache_size / (1024 * 1024 * 1024),
				db_cache_size % (1024 * 1024 * 1024), 1)) {
		nts_logm(DB_fac, M_DB_CACHEFAIL, db_strerror(ret));
		return -1;
	}

	if (ret = db_env->open(db_env, db_location,
			       	DB_CREATE | DB_INIT_TXN | 
				DB_INIT_LOG | DB_INIT_MPOOL |
				DB_INIT_LOCK | DB_RECOVER |
				DB_PRIVATE | DB_THREAD, 0)) {
		nts_logm(DB_fac, M_DB_ENVOPNFAIL, db_strerror(ret));
		return -1;
	}

	uv_timer_init(loop, &flush_timer);
	uv_timer_start(&flush_timer, db_flush, 1000, 1000);

	uv_timer_init(loop, &checkpoint_timer);
	uv_timer_start(&checkpoint_timer, db_checkpoint, 600 * 1000, 600 * 1000);
	return 0;
}

void
db_shutdown()
{
int	ret;
	if (db_env && (ret = db_env->close(db_env, 0)))
		nts_logm(DB_fac, M_DB_CLSFAIL, db_strerror(ret));
}

DB *
db_open(name, type, flags1, flags2, sorter)
	char const		*name;
	db_sort_function	 sorter;
	uint32_t		 flags1, flags2;
{
DB	*db;
int	 ret;
	if (ret = db_create(&db, db_env, 0)) {
		nts_logm(DB_fac, M_DB_DBHDLFAIL, db_strerror(ret));
		return NULL;
	}
	
	if (ret = db->set_flags(db, flags1)) {
		nts_logm(DB_fac, M_DB_DBFLGFAIL, db_strerror(ret));
		return NULL;
	}

	if (sorter && (ret = db->set_bt_compare(db, sorter))) {
		nts_logm(DB_fac, M_DB_DBSRTFAIL, db_strerror(ret));
		return NULL;
	}

	if (ret = db->open(db, NULL, name, NULL, type, flags2, 0600)) {
		nts_logm(DB_fac, M_DB_DBOPNFAIL, db_strerror(ret));
		return NULL;
	}

	return db;
}

DB_TXN *
db_new_txn(flags)
	uint32_t	flags;
{
DB_TXN	*txn;
int	 ret;
	if (ret = db_env->txn_begin(db_env, NULL, &txn, flags)) {
		nts_logm(DB_fac, M_DB_NTXNFAIL, db_strerror(ret));
		panic("unrecoverable database error");
		/*NOTREACHED*/
	}

	return txn;
}

int
db_txn_commit(txn)
	DB_TXN	*txn;
{
int	ret;
	if (ret = txn->commit(txn, 0)) {
		nts_logm(DB_fac, M_DB_CTXNFAIL, db_strerror(ret));
		panic("unrecoverable database error");
		/*NOTREACHED*/
	}
	return 0;
}

int
db_txn_abort(txn)
	DB_TXN	*txn;
{
int	ret;
	if (ret = txn->abort(txn)) {
		nts_logm(DB_fac, M_DB_ATXNFAIL, db_strerror(ret));
		panic("unrecoverable database error");
		/*NOTREACHED*/
	}
	return 0;
}

static void
db_errcall(env, pfx, msg)
	DB_ENV const	*env;
	char const	*pfx, *msg;
{
	if (pfx)
		nts_logm(DB_fac, M_DB_BKENDP, pfx, msg);
	else
		nts_logm(DB_fac, M_DB_BKEND, msg);
}

static void
db_flush(timer, status)
	uv_timer_t	*timer;
{
	db_env->log_flush(db_env, NULL);
}

static void
db_checkpoint(timer, status)
	uv_timer_t	*timer;
{
	db_env->txn_checkpoint(db_env, 0, 0, 0);
}
