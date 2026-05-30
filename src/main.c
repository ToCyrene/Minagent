#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "conf.h"
#include "agent.h"
#include "tools.h"

static void setup_tools(agent_conf_t *conf)
{
    if (conf->allowed_dirs_count > 0)
        tools_set_allowed_dirs((const char **)conf->allowed_dirs, conf->allowed_dirs_count);
    if (conf->banned_cmds_count > 0)
        tools_set_banned_cmds((const char **)conf->banned_cmds, conf->banned_cmds_count);
    tools_set_timeout(conf->timeout);
}

int main(int argc, char *argv[])
{
    const char *conf_path = "minagent.conf";
    int daemon_mode = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            conf_path = argv[++i];
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
            daemon_mode = 1;
    }

    agent_conf_t conf;
    if (conf_load(conf_path, &conf) != 0)
    {
        fprintf(stderr, "failed to load config: %s\n", conf_path);
        return 1;
    }

    setup_tools(&conf);

    agent_t agent;
    if (agent_init(&agent, conf.model, conf.system_prompt,
                   conf.history_file, conf.max_turns,
                   conf.api_url, conf.api_key) != 0)
    {
        fprintf(stderr, "failed to initialize agent\n");
        conf_free(&conf);
        return 1;
    }

    if (daemon_mode)
    {
        fprintf(stderr, "daemon mode not yet implemented\n");
        agent_free(&agent);
        conf_free(&conf);
        return 1;
    }

    printf("minagent ready (model=%s, timeout=%ds, max_turns=%d)\n",
           conf.model, conf.timeout, conf.max_turns);

    char line[65536];
    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (len == 0 || line[0] == '\0')
            continue;

        if (strcmp(line, "/exit") == 0 || strcmp(line, "/quit") == 0)
            break;

        if (strcmp(line, "/clear") == 0)
        {
            agent_clear_history(&agent);
            printf("history cleared\n");
            continue;
        }

        if (strncmp(line, "/save", 5) == 0)
        {
            const char *p = line + 5;
            while (*p == ' ') p++;
            const char *spath = (*p) ? p : NULL;
            if (agent_save_history(&agent, spath) == 0)
                printf("history saved\n");
            else
                printf("failed to save history\n");
            continue;
        }

        if (strncmp(line, "/load", 5) == 0)
        {
            const char *p = line + 5;
            while (*p == ' ') p++;
            if (!*p)
            {
                printf("usage: /load <file>\n");
                continue;
            }
            if (agent_load_history(&agent, p) == 0)
                printf("history loaded from %s\n", p);
            else
                printf("failed to load history from %s\n", p);
            continue;
        }

        char *resp = agent_chat(&agent, line);
        if (resp)
        {
            printf("%s\n", resp);
            free(resp);
        }
        else
        {
            printf("Error: agent returned NULL\n");
        }
    }

    agent_free(&agent);
    conf_free(&conf);
    printf("bye\n");
    return 0;
}
