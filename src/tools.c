#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/wait.h>

#include "tools.h"

void tools_set_allowed_dirs(const char **dirs, int count)
{
    (void)dirs;
    (void)count;
}

void tools_set_banned_cmds(const char **cmds, int count)
{
    (void)cmds;
    (void)count;
}

char *tool_read_file(const char *args_json)
{
    (void)args_json;
    return NULL;
}

char *tool_write_file(const char *args_json)
{
    (void)args_json;
    return NULL;
}

char *tool_run_bash(const char *args_json)
{
    (void)args_json;
    return NULL;
}
