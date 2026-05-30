#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "linereader.h"

#include "conf.h"
#include "agent.h"
#include "tools.h"

static agent_t      *g_agent = NULL;
static agent_conf_t *g_conf  = NULL;
static const char   *g_conf_path = NULL;
static int           g_sock_fd   = -1;
static int           g_exit_flag = 0;

static void save_history(void)
{
    if (g_agent)
        agent_save_history(g_agent, NULL);
}

static void shutdown_signal(int sig)
{
    save_history();
    if (g_sock_fd >= 0)
    {
        close(g_sock_fd);
        if (g_conf && g_conf->socket_path[0])
            unlink(g_conf->socket_path);
    }
    if (g_agent) agent_free(g_agent);
    if (g_conf)  conf_free(g_conf);
    _exit(128 + sig);
}

static void reload_signal(int sig)
{
    (void)sig;
    if (!g_conf_path || !g_conf) return;
    agent_conf_t new_conf;
    if (conf_load(g_conf_path, &new_conf) != 0) return;
    conf_free(g_conf);
    memcpy(g_conf, &new_conf, sizeof(new_conf));
    tools_set_allowed_dirs((const char **)g_conf->allowed_dirs, g_conf->allowed_dirs_count);
    tools_set_banned_cmds((const char **)g_conf->banned_cmds, g_conf->banned_cmds_count);
    tools_set_timeout(g_conf->timeout);
}

static void reap_signal(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = shutdown_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    sa.sa_handler = reload_signal;
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = reap_signal;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

static void setup_tools(agent_conf_t *conf)
{
    if (conf->allowed_dirs_count > 0)
        tools_set_allowed_dirs((const char **)conf->allowed_dirs, conf->allowed_dirs_count);
    if (conf->banned_cmds_count > 0)
        tools_set_banned_cmds((const char **)conf->banned_cmds, conf->banned_cmds_count);
    tools_set_timeout(conf->timeout);
}

static int interactive_loop(agent_t *agent, FILE *in, FILE *out)
{
    (void)in;
    (void)out;

    while (1)
    {
        char *line = lr_readline("> ", in, out);
        if (!line)
            break;

        if (line[0] == '\0')
        {
            free(line);
            continue;
        }

        if (strcmp(line, "/exit") == 0 || strcmp(line, "/quit") == 0)
        {
            free(line);
            break;
        }

        if (strcmp(line, "/clear") == 0)
        {
            agent_clear_history(agent);
            fprintf(out, "history cleared\n");
            free(line);
            continue;
        }

        if (strncmp(line, "/save", 5) == 0)
        {
            const char *p = line + 5;
            while (*p == ' ') p++;
            const char *spath = (*p) ? p : NULL;
            if (agent_save_history(agent, spath) == 0)
                fprintf(out, "history saved\n");
            else
                fprintf(out, "failed to save history\n");
            free(line);
            continue;
        }

        if (strncmp(line, "/load", 5) == 0)
        {
            const char *p = line + 5;
            while (*p == ' ') p++;
            if (!*p)
            {
                fprintf(out, "usage: /load <file>\n");
                free(line);
                continue;
            }
            if (agent_load_history(agent, p) == 0)
                fprintf(out, "history loaded from %s\n", p);
            else
                fprintf(out, "failed to load history from %s\n", p);
            free(line);
            continue;
        }

        lr_add_history(line);

        char *resp = agent_chat(agent, line);
        free(line);

        if (resp)
        {
            fprintf(out, "%s\n", resp);
            free(resp);
        }
        else
        {
            fprintf(out, "Error: agent returned NULL\n");
        }
    }
    return 0;
}

static void run_daemon(agent_conf_t *conf)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) { printf("daemon started (pid %d)\n", pid); exit(0); }

    if (setsid() < 0) { perror("setsid"); exit(1); }

    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    if (freopen("/dev/null", "w", stderr) == NULL) { perror("freopen stderr"); exit(1); }

    const char *sock_path = conf->socket_path[0] ? conf->socket_path : "/tmp/minagent.sock";
    unlink(sock_path);

    g_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_sock_fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t slen = strlen(sock_path);
    if (slen >= sizeof(addr.sun_path))
        slen = sizeof(addr.sun_path) - 1;
    memcpy(addr.sun_path, sock_path, slen);
    addr.sun_path[slen] = '\0';

    if (bind(g_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    { perror("bind"); close(g_sock_fd); exit(1); }

    chmod(sock_path, 0666);

    if (listen(g_sock_fd, 16) < 0)
    { perror("listen"); close(g_sock_fd); unlink(sock_path); exit(1); }

    setup_signals();
    g_exit_flag = 0;

    while (!g_exit_flag)
    {
        int client_fd = accept(g_sock_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        pid = fork();
        if (pid < 0) { close(client_fd); continue; }

        if (pid > 0)
        {
            close(client_fd);
            continue;
        }

        close(g_sock_fd);
        g_sock_fd = -1;

        FILE *cf = fdopen(client_fd, "r+");
        if (!cf) _exit(1);

        agent_t child_agent;
        if (agent_init(&child_agent, conf->model, conf->system_prompt,
                       conf->history_file, conf->max_turns,
                       conf->api_url, conf->api_key) != 0)
            _exit(1);

        g_agent = &child_agent;
        g_conf = conf;

        interactive_loop(&child_agent, cf, cf);
        agent_save_history(&child_agent, NULL);
        agent_free(&child_agent);
        fclose(cf);
        _exit(0);
    }

    close(g_sock_fd);
    unlink(sock_path);
}

int main(int argc, char *argv[])
{
    const char *conf_path = "minagent.conf";
    int daemon_mode = 0;
    const char *override_url = NULL;
    const char *override_key = NULL;
    const char *override_model = NULL;
    const char *override_history = NULL;
    const char *override_socket = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            conf_path = argv[++i];
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
            daemon_mode = 1;
        else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--url") == 0) && i + 1 < argc)
            override_url = argv[++i];
        else if ((strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--key") == 0) && i + 1 < argc)
            override_key = argv[++i];
        else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) && i + 1 < argc)
            override_model = argv[++i];
        else if ((strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--history") == 0) && i + 1 < argc)
            override_history = argv[++i];
        else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc)
            override_socket = argv[++i];
    }

    agent_conf_t conf;
    if (conf_load(conf_path, &conf) != 0)
    {
        fprintf(stderr, "failed to load config: %s\n", conf_path);
        return 1;
    }

    if (override_url)     snprintf(conf.api_url,      sizeof(conf.api_url),      "%s", override_url);
    if (override_key)     snprintf(conf.api_key,       sizeof(conf.api_key),       "%s", override_key);
    if (override_model)   snprintf(conf.model,         sizeof(conf.model),         "%s", override_model);
    if (override_history) snprintf(conf.history_file,  sizeof(conf.history_file),  "%s", override_history);
    if (override_socket)  snprintf(conf.socket_path,   sizeof(conf.socket_path),   "%s", override_socket);

    setup_tools(&conf);

    if (daemon_mode)
    {
        g_conf_path = conf_path;
        g_conf = &conf;
        run_daemon(&conf);
        conf_free(&conf);
        return 0;
    }

    agent_t agent;
    if (agent_init(&agent, conf.model, conf.system_prompt,
                   conf.history_file, conf.max_turns,
                   conf.api_url, conf.api_key) != 0)
    {
        fprintf(stderr, "failed to initialize agent\n");
        conf_free(&conf);
        return 1;
    }

    g_agent = &agent;
    g_conf  = &conf;
    g_conf_path = conf_path;

    setup_signals();

    printf("minagent ready (model=%s, timeout=%ds, max_turns=%d)\n",
           conf.model, conf.timeout, conf.max_turns);

    interactive_loop(&agent, stdin, stdout);

    agent_save_history(&agent, NULL);
    agent_free(&agent);
    conf_free(&conf);
    lr_cleanup();
    printf("bye\n");
    return 0;
}
