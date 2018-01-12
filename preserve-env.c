/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 * 
 *  This file is part of chaos-spankings, a set of spank plugins for SLURM.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

/****************************************************************************
 *
 *  preserve-env.so
 *
 *   This SLURM spank plugin will preserve all SLURM_* environment 
 *    variables from srun's invoking shell to the remote node or nodes
 *    on which the command specified by srun is invoked. The main purpose
 *    is to preserve the environment from a SLURM allocation shell
 *    (e.g. salloc), onto a remote "login" shell spawned with
 *
 *      srun -n1 --pty $SHELL.
 *   
 *    Normally, SLURM environment variables would be reset in the
 *     remote shell, but when using --preserve-slurm-env, they will
 *     remain essentially the same as in the shell spawned by salloc.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "lib/list.h"

#include <slurm/spank.h>

SPANK_PLUGIN (preserve-env, 1)

 
/****************************************************************************
 *
 *  Set up a --preserve-slurm-env option for srun:
 *
 ****************************************************************************/
static unsigned int enabled = 0;
static unsigned int debug = 0;

static int preserve_slurm_env (void);

static int preserve_opt_process (int val, const char *optarg, int remote)
{
    enabled = 1;

    if (optarg && (strcmp (optarg, "verbose") || strcmp (optarg, "v")))
        debug = 1;

    /* 
     *  If in srun, preserve all SLURM_* env vars as soon as possible
     *   before srun starts redefining them. NNODES in particular is
     *   redefined quite early on.
     */
    if (!remote)
        preserve_slurm_env ();

    return (0);
}

struct spank_option spank_options [] = 
{
    { "preserve-slurm-env", NULL,
      "Preserve all current SLURM env vars in remote task(s). "
      "Useful with \"salloc [opts] srun --pty -n1 -N1 $SHELL\" "
      "so that remote environment mirrors salloc's environment",
      2, 0, (spank_opt_cb_f) preserve_opt_process
    },
    SPANK_OPTIONS_TABLE_END
};

/****************************************************************************/

/*
 *  Allocate copy of env var entry in [entry] into buffer [bufp],
 *   NUL terminating at '='. Furthermore, if [valp] is non-NULL,
 *   set [valp] to point to first character after nullified '='.
 *
 */
static int get_env_var (const char *entry, char **bufp, char **valp)
{
    char *var;
    const char *p = entry;
    int len = strlen (entry) + 1;

    *bufp = malloc (len * sizeof (char));
    if (*bufp == NULL)
        return (-1);

    memset (*bufp, 0, len);

    var = *bufp;

    while (*p != '\0') {
        *var = *p;

        if (*var == '=') {
            *var = '\0';
            if (valp)
                *valp = var + 1;
        }
        p++;
        var++;
    }

    return 0;
}

/*
 *  Preserve the SLURM_* environment entry in [entry] by renaming
 *   it save_SLURM_*.
 */
static int preserve_slurm_var (const char *entry)
{
    char newvar [1024];
    char *var = NULL;
    char *val = NULL;
    int len = sizeof (newvar) - 1;
    int n;

    if (get_env_var (entry, &var, &val) < 0) {
        fprintf (stderr, "Failed to allocate copy of %s\n", entry);
        return (-1);
    }

    n = snprintf (newvar, len, "save_%s", var);

    if (n < 0 || n >= len) {
        fprintf (stderr, "preserve-env: name %s too long to copy!\n", var);
        free (var);
        return (-1);
    }

    if (setenv (newvar, val, 1) < 0) {
        fprintf (stderr, "preserve-env: Failed to set %s=%s: %s\n",
                newvar, val, strerror (errno));
        return (-1);
    }

    if (debug)
        fprintf (stderr, "preserve-env: preserving %s\n", entry);

    free (var);

    return (0);
}

extern char **environ;

static int preserve_slurm_env (void)
{
    char **p = environ;
    char *entry;
    LSDList l;

    /*
     *  Be careful to iterate through environment variables
     *   first before setting any new ones so that char **environ
     *   doesn't get reset out from under us.
     */
    l = list_create (NULL);
    while (*p != NULL) {
        /*
         *   Preserve SLURM environment variables
         *    (except for those we know we don't need)
         */
        if (strncmp (*p, "SLURM_", 6) == 0 &&
            strncmp (*p, "SLURM_RLIMIT", 12) != 0 &&
            strncmp (*p, "SLURM_UMASK", 11) != 0 &&
            strncmp (*p, "SLURM_PRIO", 10) != 0)
            list_push (l, strdup (*p));
        ++p;
    }

    while ((entry = list_pop (l))) {
        if (preserve_slurm_var (entry) < 0)
            return (-1);
        free (entry);
    }

    list_destroy (l);

    return (0);
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    LSDList l;
    const char **env;
    char *entry;
    char *var;
    char *val;

    if (!enabled)
        return (0);

    /*
     *  The following routine unsets all SLURM_* and MPIRUN_*
     *   environment variables, and resets the saved variables
     *   in save_*. We are careful not to walk the env array
     *   at the same time as adding and removing variables, so
     *   we instead use the list 'l' to hold environment entries
     *   for the next operation.
     *  
     *  The first step accumulates and removes all unwanted variables,
     *   then the second step resets the saved variables.
     */
    l = list_create (NULL);

    if (spank_get_item (sp, S_JOB_ENV, &env) != ESPANK_SUCCESS) {
        fprintf (stderr, "preserve-env: Failed to get job environment!\n");
        return (-1);
    }

    /*
     *  First collect all env vars to unset
     */
    while (*env != NULL) {
        if (strncmp (*env, "SLURM_", 6) == 0 ||
            strncmp (*env, "MPIRUN_", 7) == 0) 
            list_push (l, strdup (*env));
        ++env;
    }

    while ((entry = list_pop (l))) {
        get_env_var (entry, &var, &val);
        spank_unsetenv (sp, var);
        if (debug)
            fprintf (stderr, "preserve-env: unsetenv (%s)\n", var);
        free (entry);
        free (var);
    }

    /*
     *  Now search for saved SLURM env vars to reset
     */

    if (spank_get_item (sp, S_JOB_ENV, &env) != ESPANK_SUCCESS) {
        fprintf (stderr, "preserve-env: Failed to get job environment!\n");
        return (-1);
    }

    while (*env != NULL) {
        if (strncmp (*env, "save_SLURM_", 11) == 0) 
            list_push (l, strdup (*env));
        env++;
    }

    while ((entry = list_pop (l))) {
        get_env_var (entry, &var, &val);

        if (debug)
            fprintf (stderr, "preserve-env: set %s\n", entry + 5);

        if (spank_setenv (sp, var + 5, val, 1) != ESPANK_SUCCESS) {
            fprintf (stderr, "preserve-env: spank_setenv (%s) failed\n",
                    var + 5);
        }

        /*
         *  Now unset the unneeded save_* var
         */
        spank_unsetenv (sp, var);

        free (entry);
        free (var);
    }

    list_destroy (l);

    return (0);
}

/*
 *  vi: ts=4 sw=4 expandtab
 */
