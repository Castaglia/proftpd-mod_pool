/*
 * ProFTPD: mod_pool: Module for debugging ProFTPD memory pools
 * Copyright (c) 2016 TJ Saunders
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 *
 * This is mod_pool, contrib software for proftpd 1.3.x and above.
 * For more information contact TJ Saunders <tj@castaglia.org>.
 */

#include "conf.h"
#include "privs.h"

#define MOD_POOL_VERSION	"mod_pool/0.0.0"

/* Make sure the version of proftpd is as necessary. */
#if PROFTPD_VERSION_NUMBER < 0x0001030602
# error "ProFTPD 1.3.6rc2 or later required"
#endif

/* Memory pool debugging requires that the --enable-devel build option be
 * in effect.
 */
#ifndef PR_USE_DEVEL
# error "Requires the --enable-devel build option"
# define pr_pool_debug_memory(f)
#endif

/* PoolEvents */
#define POOL_EVENT_FL_SESSION		0x00001
#define POOL_EVENT_FL_DOWNLOAD		0x00002
#define POOL_EVENT_FL_UPLOAD		0x00004
#define POOL_EVENT_FL_LOGIN		0x00008
#define POOL_EVENT_FL_DIRLIST		0x00010
#define POOL_EVENT_FL_TRANSFER		0x00020

#define POOL_EVENT_FL_MISC		0x80000

#define POOL_EVENT_FL_ALL \
 (POOL_EVENT_FL_SESSION | \
  POOL_EVENT_FL_DOWNLOAD | \
  POOL_EVENT_FL_UPLOAD | \
  POOL_EVENT_FL_LOGIN | \
  POOL_EVENT_FL_DIRLIST | \
  POOL_EVENT_FL_TRANSFER | \
  POOL_EVENT_FL_MISC)

module pool_module;

static pr_table_t *pool_counts = NULL;
static int pool_engine = FALSE;
static unsigned long pool_events = POOL_EVENT_FL_ALL;
static int pool_logfd = -1;
static pool *pool_pool = NULL;

static const char *trace_channel = "pool";

/* TODO: Track memory pool consumption for daemon (restarts, parsing). */

/* TODO: Use JSON, track "before after, after last" stats.  Use them to
 * determine when to analyze further; narrow down the list of pools that
 * grew in size.
 *
 * Right now, the above analysis can/will be done via separate script
 * which ships with module, runs on the generated files.
 */

/* Support routines
 */

static unsigned int get_event_count(const char *event, unsigned int incr) {
  unsigned int count, *v;

  v = (unsigned int *) pr_table_get(pool_counts, event, NULL);
  if (v == NULL) {
    v = palloc(pool_pool, sizeof(unsigned int));
    *v = 1;

    if (pr_table_add(pool_counts, pstrdup(pool_pool, event), v,
        sizeof(unsigned int)) < 0) {
      pr_trace_msg(trace_channel, 3, "error stashing counter for '%s': %s",
        event, strerror(errno));
    }
  }

  count = *v;

  /* Increment our counter as requested by the caller. */
  *v = *v + incr;

  return count;
}

static int is_event_enabled(cmd_rec *cmd) {
  int enabled = FALSE;

  if (pool_events == POOL_EVENT_FL_ALL) {
    return TRUE;
  }

  if (cmd->cmd_id == 0) {
    cmd->cmd_id = pr_cmd_get_id(cmd->argv[0]);
  }

  switch (cmd->cmd_id) {
    case PR_CMD_RETR_ID:
      if (pool_events & POOL_EVENT_FL_DOWNLOAD) {
        enabled = TRUE;
      }
      break;

    case PR_CMD_APPE_ID:
    case PR_CMD_STOR_ID:
      if (pool_events & POOL_EVENT_FL_UPLOAD) {
        enabled = TRUE;
      }
      break;

    case PR_CMD_LIST_ID:
    case PR_CMD_MLSD_ID:
    case PR_CMD_MLST_ID:
    case PR_CMD_NLST_ID:
      if (pool_events & POOL_EVENT_FL_DIRLIST) {
        enabled = TRUE;
      }
      break;

    case PR_CMD_EPRT_ID: 
    case PR_CMD_EPSV_ID: 
    case PR_CMD_MODE_ID: 
    case PR_CMD_PASV_ID: 
    case PR_CMD_PORT_ID: 
    case PR_CMD_STRU_ID: 
    case PR_CMD_TYPE_ID: 
      if (pool_events & POOL_EVENT_FL_TRANSFER) {
        enabled = TRUE;
      }
      break;

    case PR_CMD_PASS_ID:
    case PR_CMD_USER_ID:
      if (pool_events & POOL_EVENT_FL_LOGIN) {
        enabled = TRUE;
      }
      break;

    default:
      /* This will catch completely unknown commands, as well as SSH-related
       * commands.
       */
      if (pool_events & POOL_EVENT_FL_MISC) {
        enabled = TRUE;
      }
      break;
  }

  return enabled;
}

/* XXX Include PID, but NOT timestamps or anything else. Or should each
 * session get its own collection of log files?  What about multiple
 * commands (and overwriting existing files?  Maybe keep a counter for each
 * command, and use "BEGIN %s #%u MEMORY POOLS"?
 *
 *  PoolLogs/$pid/$event-$count.txt
 *
 * Unfortunately, we also have to deal with chrooted sessions, which would
 * not necessarily allow for separate per-event files.  And we're back to
 *
 *  PoolLogs/pid-$pid.txt
 *
 * And that, in turn, suggests that this module should come with a script
 * (Perl?  Python?) which analyses that file for the per-event memory pool
 * breakdown.
 */

static int open_session_log(pool *p, const char *parent_dir) {
  int buflen, res, fd, xerrno;
  char buf[256], *path;

  buflen = snprintf(buf, sizeof(buf)-1, "%lu", (unsigned long) session.pid);
  buf[sizeof(buf)-1] = '\0';
  buf[buflen] = '\0';

  path = pstrcat(p, parent_dir, "/pid-", buf, ".txt", NULL);

  pr_signals_block();
  PRIVS_ROOT
  res = pr_log_openfile(path, &fd, 0644);
  xerrno = errno;
  PRIVS_RELINQUISH
  pr_signals_unblock();

  if (res < 0) {
    if (res == PR_LOG_WRITABLE_DIR) {
      pr_log_pri(PR_LOG_NOTICE, MOD_POOL_VERSION
        ": notice: unable to open PoolsLog '%s': parent directory is "
        "world-writable", path);
      xerrno = EPERM;

    } else if (res == PR_LOG_SYMLINK) {
      pr_log_pri(PR_LOG_NOTICE, MOD_POOL_VERSION
        ": notice: unable to open PoolsLog '%s': cannot log to a symbolic link",
        path);
      xerrno = EPERM;
    }
  }

  errno = xerrno;
  return fd;
}

static void pool_log(const char *fmt, ...) {
  char buf[PR_TUNABLE_BUFFER_SIZE];
  va_list msg;
  int buflen;

  va_start(msg, fmt);
  buflen = vsnprintf(buf, sizeof(buf)-1, fmt, msg);
  va_end(msg);

  if (buflen < (int) (sizeof(buf) - 2)) {
    buf[buflen] = '\n';
    buflen++;
  }

  buf[sizeof(buf)-1] = '\0';
  (void) write(pool_logfd, buf, buflen);
}

static int pool_mkdir(const char *dir, uid_t uid, gid_t gid, mode_t mode) {
  mode_t prev_mask;
  struct stat st;
  int res = -1;

  pr_fs_clear_cache2(dir);
  res = pr_fsio_stat(dir, &st);
  if (res < 0 &&
      errno != ENOENT) {
    return -1;
  }

  /* The directory already exists. */
  if (res == 0) {
    return 0;
  }

  /* The given mode is absolute, not subject to any Umask setting. */
  prev_mask = umask(0);

  if (pr_fsio_mkdir(dir, mode) < 0) {
    int xerrno = errno;

    (void) umask(prev_mask);
    errno = xerrno;
    return -1;
  }

  umask(prev_mask);

  if (pr_fsio_chown(dir, uid, gid) < 0) {
    return -1;
  }

  return 0;
}

static int pool_mkpath(pool *p, const char *path, uid_t uid, gid_t gid,
    mode_t mode) {
  char *currpath = NULL, *tmppath = NULL;
  struct stat st;

  pr_fs_clear_cache2(path);
  if (pr_fsio_stat(path, &st) == 0) {
    /* Path already exists, nothing to be done. */
    errno = EEXIST;
    return -1;
  }

  tmppath = pstrdup(p, path);

  currpath = "/";
  while (tmppath && *tmppath) {
    char *currdir = strsep(&tmppath, "/");
    currpath = pdircat(p, currpath, currdir, NULL);

    if (pool_mkdir(currpath, uid, gid, mode) < 0) {
      return -1;
    }

    pr_signals_handle();
  }

  return 0;
}

/* Configuration handlers
 */

/* usage: PoolEngine on|off */
MODRET set_poolengine(cmd_rec *cmd) {
  config_rec *c;
  int engine = -1;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  engine = get_boolean(cmd, 1);
  if (engine == -1) {
    CONF_ERROR(cmd, "expected Boolean parameter");
  }

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = palloc(c->pool, sizeof(int));
  *((int *) c->argv[0]) = engine;

  return PR_HANDLED(cmd);
}

/* usage: PoolEvents event1 ... */
MODRET set_poolevents(cmd_rec *cmd) {
  register unsigned int i;
  unsigned long events = 0UL;
  config_rec *c;

  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  if (cmd->argc < 2) {
    CONF_ERROR(cmd, "wrong number of parameters");
  }

  for (i = 1; i < cmd->argc; i++) {
    if (strcasecmp(cmd->argv[i], "Sessions") == 0) {
      events |= POOL_EVENT_FL_SESSION;

    } else if (strcasecmp(cmd->argv[i], "Downloads") == 0) {
      events |= POOL_EVENT_FL_DOWNLOAD;

    } else if (strcasecmp(cmd->argv[i], "Uploads") == 0) {
      events |= POOL_EVENT_FL_UPLOAD;

    } else if (strcasecmp(cmd->argv[i], "Logins") == 0) {
      events |= POOL_EVENT_FL_LOGIN;

    } else if (strcasecmp(cmd->argv[i], "Directories") == 0) {
      events |= POOL_EVENT_FL_DIRLIST;

    } else if (strcasecmp(cmd->argv[i], "Transfers") == 0) {
      events |= POOL_EVENT_FL_TRANSFER;

    } else if (strcasecmp(cmd->argv[i], "Misc") == 0) {
      events |= POOL_EVENT_FL_MISC;

    } else if (strcasecmp(cmd->argv[i], "All") == 0) {
      events = POOL_EVENT_FL_ALL;
      break;

    } else {
      CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, ": unknown PoolEvent '",
        cmd->argv[i], "'", NULL));
    }
  }

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = palloc(c->pool, sizeof(unsigned long));
  *((unsigned long *) c->argv[0]) = events;

  return PR_HANDLED(cmd);
}

/* usage: PoolLogs path */
MODRET set_poollogs(cmd_rec *cmd) {
  int res;
  const char *path;
  struct stat st;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT);

  path = cmd->argv[1];
  if (*path != '/') {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "must be an absolute path: ", path,
      NULL));
  }

  res = stat(path, &st);
  if (res < 0) {
    if (errno != ENOENT) {
      CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "unable to stat '", path, "': ",
        strerror(errno), NULL));
    }

    pr_log_debug(DEBUG5, MOD_POOL_VERSION
      ": PoolLogs directory '%s' does not exist, creating it", path);

    res = pool_mkpath(cmd->tmp_pool, path, geteuid(), getegid(), 0755);
    if (res < 0) {
      CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "unable to create directory '",
        path, "': ", strerror(errno), NULL));
    }

    pr_log_debug(DEBUG5, MOD_POOL_VERSION
      ": created PoolLogs directory '%s'", path);

  } else {
    if (!S_ISDIR(st.st_mode)) {
      CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "unable to use '", path,
        "': Not a directory", NULL));
    }
  }

  (void) add_config_param_str(cmd->argv[0], 1, path);
  return PR_HANDLED(cmd);
}

/* Command handlers
 */

MODRET pool_log_any(cmd_rec *cmd) {
  const char *event;
  unsigned int count;

  if (pool_engine == FALSE) {
    return PR_DECLINED(cmd);
  }

  if (is_event_enabled(cmd) == FALSE) {
    return PR_DECLINED(cmd);
  }

  event = cmd->argv[0];
  count = get_event_count(event, 1);

  pool_log("-----BEGIN POOLS: POST-%s #%u-----", event, count);
  pr_pool_debug_memory(pool_log);
  pool_log("-----END POOLS: POST-%s #%u-----", event, count);

  return PR_DECLINED(cmd);
}

MODRET pool_pre_any(cmd_rec *cmd) {
  const char *event;
  unsigned int count;

  if (pool_engine == FALSE) {
    return PR_DECLINED(cmd);
  }

  if (is_event_enabled(cmd) == FALSE) {
    return PR_DECLINED(cmd);
  }

  event = cmd->argv[0];
  count = get_event_count(event, 0);

  pool_log("-----BEGIN POOLS: PRE-%s #%u-----", event, count);
  pr_pool_debug_memory(pool_log);
  pool_log("-----END POOLS: PRE-%s #%u-----", event, count);

  return PR_DECLINED(cmd);
}

/* Event listeners
 */

static void pool_exit_ev(const void *event_data, void *user_data) {
  const char *event;
  unsigned int count;

  event = "SESSION";
  count = get_event_count(event, 1);

  pool_log("-----BEGIN POOLS: POST-%s #%u-----", event, count);
  pr_pool_debug_memory(pool_log);
  pool_log("-----END POOLS: POST-%s #%u-----", event, count);

  if (close(pool_logfd) < 0) {
    pr_log_pri(PR_LOG_NOTICE, MOD_POOL_VERSION
      ": error writing PoolLogs file: %s", strerror(errno));
  }

  pool_logfd = -1;
  destroy_pool(pool_pool);
  pool_pool = NULL;
}

#if defined(PR_SHARED_MODULE)
static void pool_mod_unload_ev(const void *event_data, void *user_data) {
  if (strcmp((const char *) event_data, "mod_pool.c") == 0) {
    pr_event_unregister(&pool_module, NULL, NULL);
  }
}
#endif /* PR_SHARED_MODULE */

static void pool_postparse_ev(const void *event_data, void *user_data) {
}

static void pool_restart_ev(const void *event_data, void *user_data) {
}

/* Initialization
 */

static int pool_init(void) {
#if defined(PR_SHARED_MODULE)
  pr_event_register(&pool_module, "core.module-unload", pool_mod_unload_ev,
    NULL);
#endif /* PR_SHARED_MODULE */
  pr_event_register(&pool_module, "core.postparse", pool_postparse_ev, NULL);
  pr_event_register(&pool_module, "core.restart", pool_restart_ev, NULL);

  return 0;
}

static int pool_sess_init(void) {
  config_rec *c;

  c = find_config(main_server->conf, CONF_PARAM, "PoolEngine", FALSE);
  if (c != NULL) {
    pool_engine = *((int *) c->argv[0]);
  }

  if (pool_engine == FALSE) {
    return 0;
  }

  c = find_config(main_server->conf, CONF_PARAM, "PoolEvents", FALSE);
  if (c != NULL) {
    pool_events = *((unsigned long *) c->argv[0]);
  }

  c = find_config(main_server->conf, CONF_PARAM, "PoolLogs", FALSE);
  if (c == NULL) {
    pr_log_pri(PR_LOG_NOTICE, MOD_POOL_VERSION
      ": notice: missing required PoolLogs directive, disabling module");
    pool_engine = FALSE;
    return 0;
  }

  pool_pool = make_sub_pool(session.pool);
  pr_pool_tag(pool_pool, MOD_POOL_VERSION);

  pool_logfd = open_session_log(pool_pool, c->argv[0]);
  if (pool_logfd < 0) {
    pr_log_pri(PR_LOG_NOTICE, MOD_POOL_VERSION
      ": notice: unable to open PoolLogs logfile, disabling module: %s",
      strerror(errno));
    destroy_pool(pool_pool);
    pool_pool = NULL;
    pool_engine = FALSE;
    return 0;
  }

  pool_counts = pr_table_alloc(pool_pool, 0);

  if (pool_events & POOL_EVENT_FL_SESSION) {
    const char *event;
    unsigned int count;

    pr_event_register(&pool_module, "core.exit", pool_exit_ev, NULL);

    event = "SESSION";
    count = get_event_count(event, 0);

    pool_log("-----BEGIN POOLS: PRE-%s #%u-----", event, count);
    pr_pool_debug_memory(pool_log);
    pool_log("-----END POOLS: PRE-%s #%u-----", event, count);
  }

  return 0;
}

/* Module API tables
 */

static conftable pool_conftab[] = {
  { "PoolEngine",	set_poolengine,		NULL },
  { "PoolEvents",	set_poolevents,		NULL },
  { "PoolLogs",		set_poollogs,		NULL },

  { NULL }
};

static cmdtable pool_cmdtab[] = {
  { PRE_CMD,		C_ANY,	G_NONE,	pool_pre_any,	FALSE,	FALSE },
  { LOG_CMD,		C_ANY,	G_NONE,	pool_log_any,	FALSE,	FALSE },
  { LOG_CMD_ERR,	C_ANY,	G_NONE,	pool_log_any,	FALSE,	FALSE },

  { 0, NULL }
};

module pool_module = {
  NULL, NULL,

  /* Module API version 2.0 */
  0x20,

  /* Module name */
  "pool",

  /* Module configuration handler table */
  pool_conftab,

  /* Module command handler table */
  pool_cmdtab,

  /* Module authentication handler table */
  NULL,

  /* Module initialization function */
  pool_init,

  /* Session initialization function */
  pool_sess_init,

  /* Module version */
  MOD_POOL_VERSION
};
