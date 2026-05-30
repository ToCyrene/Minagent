#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/wait.h>
#include <errno.h>

#include "tools.h"
#include "json.h"

/* ── Globals ────────────────────────────────────────────────── */

static char **g_allowed_dirs = NULL;
static int    g_allowed_cnt  = 0;
static char **g_banned_cmds  = NULL;
static int    g_banned_cnt   = 0;
static int    g_timeout      = 30;
#define MAX_TOOL_OUTPUT (1024 * 1024)

/* ── Setters ────────────────────────────────────────────────── */

void tools_set_allowed_dirs(const char **dirs, int count)
{
    for (int i = 0; i < g_allowed_cnt; i++)
        free(g_allowed_dirs[i]);
    free(g_allowed_dirs);
    g_allowed_dirs = NULL;
    g_allowed_cnt  = 0;

    if (count <= 0 || !dirs)
        return;

    g_allowed_dirs = malloc(sizeof(char *) * count);
    if (!g_allowed_dirs) return;

    for (int i = 0; i < count; i++)
    {
        char *resolved = realpath(dirs[i], NULL);
        g_allowed_dirs[i] = resolved ? resolved : strdup(dirs[i]);
    }
    g_allowed_cnt = count;
}

void tools_set_banned_cmds(const char **cmds, int count)
{
    for (int i = 0; i < g_banned_cnt; i++)
        free(g_banned_cmds[i]);
    free(g_banned_cmds);
    g_banned_cmds = NULL;
    g_banned_cnt  = 0;

    if (count <= 0 || !cmds)
        return;

    g_banned_cmds = malloc(sizeof(char *) * count);
    if (!g_banned_cmds) return;

    for (int i = 0; i < count; i++)
        g_banned_cmds[i] = strdup(cmds[i]);
    g_banned_cnt = count;
}

void tools_set_timeout(int seconds)
{
    if (seconds > 0)
        g_timeout = seconds;
}

/* ── Security helpers ──────────────────────────────────────── */

static int path_allowed(const char *path)
{
    if (g_allowed_cnt == 0)
        return 0;

    char *resolved = realpath(path, NULL);

    if (!resolved)
    {
        /* file may not exist yet (write_file); try parent directory */
        char *copy = strdup(path);
        if (!copy) return 0;
        char *slash = strrchr(copy, '/');
        if (slash && slash != copy)
            *slash = '\0';
        else if (slash == copy)
            *(slash + 1) = '\0';
        resolved = realpath(copy, NULL);
        free(copy);
    }

    if (!resolved)
        return 0;

    int ok = 0;
    for (int i = 0; i < g_allowed_cnt; i++)
    {
        size_t dlen = strlen(g_allowed_dirs[i]);
        if (strncmp(resolved, g_allowed_dirs[i], dlen) == 0 &&
            (resolved[dlen] == '/' || resolved[dlen] == '\0'))
        {
            ok = 1;
            break;
        }
    }
    free(resolved);
    return ok;
}

static int cmd_banned(const char *cmd)
{
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if (!*cmd)
        return 1;

    const char *end = cmd;
    while (*end && *end != ' ' && *end != '\t')
        end++;
    int len = (int)(end - cmd);

    for (int i = 0; i < g_banned_cnt; i++)
    {
        if ((int)strlen(g_banned_cmds[i]) == len &&
            strncmp(cmd, g_banned_cmds[i], len) == 0)
            return 1;
    }
    return 0;
}

/* ── Growing buffer for tool output ────────────────────────── */

static char *gbuf_read(int fd)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    while (1)
    {
        if (len >= MAX_TOOL_OUTPUT)
        {
            buf[len] = '\0';
            return buf;
        }
        if (len + 4096 > cap)
        {
            if (cap >= MAX_TOOL_OUTPUT)
            {
                cap = MAX_TOOL_OUTPUT + 1;
            }
            else
            {
                cap *= 2;
                if (cap > MAX_TOOL_OUTPUT)
                    cap = MAX_TOOL_OUTPUT + 1;
            }
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        size_t avail = (cap - len - 1) < (size_t)(MAX_TOOL_OUTPUT - len) ? (cap - len - 1) : (size_t)(MAX_TOOL_OUTPUT - len);
        ssize_t n = read(fd, buf + len, avail);
        if (n <= 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    return buf;
}

/* ── SIGALRM handler ────────────────────────────────────────── */

static void sigalrm_handler(int sig)
{
    (void)sig;
}

/* ── tool_read_file ────────────────────────────────────────── */

char *tool_read_file(const char *args_json)
{
    char path[4096];
    if (json_get_str(args_json, "path", path, sizeof(path)) != 0)
        return strdup("Error: missing 'path' in arguments");

    if (!path_allowed(path))
        return strdup("Error: path not in allowed_dirs");

    FILE *f = fopen(path, "r");
    if (!f)
        return strdup("Error: cannot open file");

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return strdup("Error: ftell failed"); }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (!content) { fclose(f); return strdup("Error: malloc failed"); }

    size_t r = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    content[r] = '\0';
    return content;
}

/* ── tool_write_file ────────────────────────────────────────── */

char *tool_write_file(const char *args_json)
{
    char path[4096];
    if (json_get_str(args_json, "path", path, sizeof(path)) != 0)
        return strdup("Error: missing 'path' in arguments");

    if (!path_allowed(path))
        return strdup("Error: path not in allowed_dirs");

    size_t content_cap = 65536;
    char *content = malloc(content_cap);
    if (!content)
        return strdup("Error: malloc failed");

    if (json_get_str(args_json, "content", content, content_cap) != 0)
    {
        free(content);
        return strdup("Error: missing 'content' in arguments");
    }

    FILE *f = fopen(path, "w");
    if (!f)
    {
        free(content);
        return strdup("Error: cannot open file for writing");
    }

    size_t to_write = strlen(content);
    size_t written = fwrite(content, 1, to_write, f);
    fclose(f);
    free(content);

    char result[8192];
    snprintf(result, sizeof(result), "Written %zu bytes to %s", written, path);
    return strdup(result);
}

/* ── tool_run_bash ──────────────────────────────────────────── */

char *tool_run_bash(const char *args_json)
{
    char command[8192];
    if (json_get_str(args_json, "command", command, sizeof(command)) != 0)
        return strdup("Error: missing 'command' in arguments");

    if (cmd_banned(command))
        return strdup("Error: command is banned");

    int out_pipe[2];
    if (pipe(out_pipe) != 0)
        return strdup("Error: pipe failed");

    pid_t pid = fork();
    if (pid == 0)
    {
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(127);
    }

    close(out_pipe[1]);

    struct sigaction old_sa;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, &old_sa);

    alarm((unsigned int)g_timeout);

    int status;
    pid_t rv = waitpid(pid, &status, 0);
    int saved_errno = errno;

    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);

    if (rv == -1 && saved_errno == EINTR)
    {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(out_pipe[0]);
        return strdup("Error: command timed out");
    }

    char *output = gbuf_read(out_pipe[0]);
    close(out_pipe[0]);

    if (!output)
        return strdup("Error: failed to read output");

    return output;
}
