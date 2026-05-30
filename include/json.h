#ifndef JSON_H
#define JSON_H

#include <stddef.h>

#include "agent.h"
#include "tools.h"

char *json_build_request(const char *model, const char *sys_prompt,
                         msg_t *msgs, int msg_count,
                         const tool_def_t *tools, int tool_count);

int json_get_str(const char *json, const char *path, char *out, size_t outlen);
int json_get_int(const char *json, const char *path, int *out);

typedef struct {
    char *id;
    char *name;
    char *arguments;
} tool_call_t;

int json_extract_tool_calls(const char *json, tool_call_t *calls, int max);

int json_load_history(const char *filepath, msg_t **msgs, int *count);
int json_save_history(const char *filepath, msg_t *msgs, int count);

#endif
