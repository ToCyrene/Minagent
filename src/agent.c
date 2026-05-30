#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "net.h"
#include "json.h"
#include "tools.h"

#define MAX_TOOL_CALLS 8

static const tool_def_t g_tools[] = {
    {
        .name = "read_file",
        .desc = "Read a file from the local filesystem. Returns the entire file content as a string.",
        .params_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path to the file\"}},\"required\":[\"path\"]}",
        .execute = tool_read_file
    },
    {
        .name = "write_file",
        .desc = "Write content to a file on the local filesystem. Overwrites existing content entirely.",
        .params_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path to the file\"},\"content\":{\"type\":\"string\",\"description\":\"Content to write\"}},\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file
    },
    {
        .name = "run_bash",
        .desc = "Execute a bash command and return its combined stdout and stderr output. Has a configurable timeout.",
        .params_schema = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Bash command to execute\"}},\"required\":[\"command\"]}",
        .execute = tool_run_bash
    }
};
#define TOOL_COUNT ((int)(sizeof(g_tools) / sizeof(g_tools[0])))

static int msg_add(agent_t *a, role_t role, const char *content,
                   const char *reasoning_content,
                   const char *tool_call_id, const char *tool_name,
                   const char *tool_args)
{
    if (a->msg_count >= a->msg_cap)
    {
        int newcap = a->msg_cap ? a->msg_cap * 2 : 16;
        msg_t *tmp = realloc(a->msgs, sizeof(msg_t) * newcap);
        if (!tmp) return -1;
        a->msgs = tmp;
        a->msg_cap = newcap;
    }

    msg_t *m = &a->msgs[a->msg_count++];
    memset(m, 0, sizeof(*m));
    m->role = role;
    m->content = strdup(content ? content : "");
    m->reasoning_content = reasoning_content ? strdup(reasoning_content) : NULL;
    m->tool_call_id = tool_call_id ? strdup(tool_call_id) : NULL;
    m->tool_name = tool_name ? strdup(tool_name) : NULL;
    m->tool_args = tool_args ? strdup(tool_args) : NULL;
    return 0;
}

static tool_def_t *find_tool(const char *name)
{
    for (int i = 0; i < TOOL_COUNT; i++)
        if (strcmp(g_tools[i].name, name) == 0)
            return (tool_def_t *)&g_tools[i];
    return NULL;
}

int agent_init(agent_t *a, const char *model, const char *sys_prompt,
               const char *history_path, int max_turns, int max_context_msgs,
               const char *api_url, const char *api_key)
{
    memset(a, 0, sizeof(*a));

    a->model = strdup(model ? model : "gpt-4");
    if (!a->model) return -1;
    a->system_prompt = strdup(sys_prompt ? sys_prompt : "");
    if (!a->system_prompt) { free(a->model); return -1; }
    a->history_path = strdup(history_path ? history_path : "agent_history.json");
    if (!a->history_path) { free(a->model); free(a->system_prompt); return -1; }
    a->max_turns = (max_turns > 0) ? max_turns : 8;
    a->max_context_msgs = (max_context_msgs > 0) ? max_context_msgs : 64;

    a->msg_cap = 16;
    a->msgs = malloc(sizeof(msg_t) * a->msg_cap);
    if (!a->msgs) { free(a->model); free(a->system_prompt); free(a->history_path); return -1; }
    a->msg_count = 0;

    if (http_init(&a->http, api_url, api_key) != 0)
    {
        free(a->model); free(a->system_prompt); free(a->history_path);
        free(a->msgs);
        memset(a, 0, sizeof(*a));
        return -1;
    }

    return 0;
}

int agent_load_history(agent_t *a, const char *path)
{
    if (!a) return -1;
    agent_clear_history(a);
    if (!path) path = a->history_path;
    return json_load_history(path, &a->msgs, &a->msg_count);
}

int agent_save_history(agent_t *a, const char *path)
{
    if (!a) return -1;
    if (!path) path = a->history_path;
    return json_save_history(path, a->msgs, a->msg_count);
}

static void msg_free(msg_t *m)
{
    free(m->content);
    free(m->reasoning_content);
    free(m->tool_call_id);
    free(m->tool_name);
    free(m->tool_args);
}

static void agent_trim_history(agent_t *a)
{
    if (a->msg_count <= a->max_context_msgs)
        return;

    int sys_offset = (a->msg_count > 0 && a->msgs[0].role == ROLE_SYSTEM) ? 1 : 0;
    int remove = a->msg_count - a->max_context_msgs;
    if (remove <= sys_offset)
        return;

    for (int i = sys_offset; i < sys_offset + remove; i++)
        msg_free(&a->msgs[i]);

    int src = sys_offset + remove;
    int dst = sys_offset;
    int remain = a->msg_count - src;
    memmove(&a->msgs[dst], &a->msgs[src], sizeof(msg_t) * remain);

    a->msg_count = dst + remain;
}

void agent_clear_history(agent_t *a)
{
    if (!a || !a->msgs) return;
    for (int i = 0; i < a->msg_count; i++)
        msg_free(&a->msgs[i]);
    a->msg_count = 0;
}

void agent_free(agent_t *a)
{
    if (!a) return;
    agent_clear_history(a);
    free(a->msgs);
    free(a->model);
    free(a->system_prompt);
    free(a->history_path);
    http_free(&a->http);
    memset(a, 0, sizeof(*a));
}

char *agent_chat(agent_t *a, const char *input)
{
    if (!a || !input) return NULL;

    if (msg_add(a, ROLE_USER, input, NULL, NULL, NULL, NULL) != 0)
        return strdup("Error: failed to add user message");

    char content_buf[65536];
    char reasoning_buf[65536];
    char finish_reason[32];

    for (int turn = 0; turn < a->max_turns; turn++)
    {
        agent_trim_history(a);

        char *req = json_build_request(a->model, a->system_prompt,
                                        a->msgs, a->msg_count,
                                        g_tools, TOOL_COUNT);
        if (!req)
            return strdup("Error: failed to build request");

        char *resp = NULL;
        size_t resp_len = 0;
        int http_ok = http_post(&a->http, req, &resp, &resp_len);

        free(req);

        if (http_ok != 0)
        {
            http_reset(&a->http);
            if (resp && resp[0])
            {
                char errmsg[512];
                snprintf(errmsg, sizeof(errmsg), "Error: API request failed: %.400s", resp);
                return strdup(errmsg);
            }
            return strdup("Error: API request failed");
        }

        if (json_get_str(resp, "choices.0.finish_reason", finish_reason, sizeof(finish_reason)) != 0)
        {
            http_reset(&a->http);
            return strdup("Error: cannot parse API response (finish_reason)");
        }

        int has_content = (json_get_str(resp, "choices.0.message.content",
                                         content_buf, sizeof(content_buf)) == 0
                           && content_buf[0] != '\0'
                           && strcmp(content_buf, "null") != 0);

        int has_reasoning = (json_get_str(resp, "choices.0.message.reasoning_content",
                                           reasoning_buf, sizeof(reasoning_buf)) == 0
                             && reasoning_buf[0] != '\0'
                             && strcmp(reasoning_buf, "null") != 0);
        const char *rc = has_reasoning ? reasoning_buf : NULL;

        if (strcmp(finish_reason, "stop") == 0)
        {
            const char *c = has_content ? content_buf : "";
            msg_add(a, ROLE_ASSISTANT, c, rc, NULL, NULL, NULL);
            http_reset(&a->http);
            return strdup(c);
        }

        if (strcmp(finish_reason, "tool_calls") == 0)
        {
            tool_call_t calls[MAX_TOOL_CALLS];
            int n = json_extract_tool_calls(resp, calls, MAX_TOOL_CALLS);
            http_reset(&a->http);

            if (n <= 0)
            {
                if (has_content)
                {
                    msg_add(a, ROLE_ASSISTANT, content_buf, rc, NULL, NULL, NULL);
                    return strdup(content_buf);
                }
                return strdup("Error: tool_calls response but no calls found");
            }

            for (int i = 0; i < n; i++)
            {
                const char *cid = calls[i].id ? calls[i].id : "";
                msg_add(a, ROLE_ASSISTANT, has_content ? content_buf : "",
                        rc, cid, calls[i].name, calls[i].arguments);

                tool_def_t *tool = find_tool(calls[i].name);
                char *result;
                if (tool)
                    result = tool->execute(calls[i].arguments);
                else
                {
                    char err[256];
                    snprintf(err, sizeof(err), "Error: unknown tool '%s'", calls[i].name);
                    result = strdup(err);
                }

                msg_add(a, ROLE_TOOL, result ? result : "", NULL, cid, NULL, NULL);
                free(result);
            }

            for (int i = 0; i < n; i++)
            {
                free(calls[i].id);
                free(calls[i].name);
                free(calls[i].arguments);
            }

            continue;
        }

        if (has_content)
        {
            msg_add(a, ROLE_ASSISTANT, content_buf, rc, NULL, NULL, NULL);
            http_reset(&a->http);
            return strdup(content_buf);
        }

        http_reset(&a->http);
        return strdup("Error: unexpected finish_reason from API");
    }

    return strdup("Error: max turns reached");
}
