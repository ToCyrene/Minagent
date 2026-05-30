#ifndef TOOLS_H
#define TOOLS_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *desc;
    const char *params_schema;
    char *(*execute)(const char *args_json);
} tool_def_t;

void tools_set_allowed_dirs(const char **dirs, int count);
void tools_set_banned_cmds(const char **cmds, int count);
void tools_set_timeout(int seconds);
char *tool_read_file(const char *args_json);
char *tool_write_file(const char *args_json);
char *tool_run_bash(const char *args_json);

#endif
