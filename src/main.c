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

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            conf_path = argv[++i];
    }

    agent_conf_t conf;
    if (conf_load(conf_path, &conf) != 0)
    {
        fprintf(stderr, "failed to load config: %s\n", conf_path);
        return 1;
    }

    setup_tools(&conf);

    printf("minagent ready (timeout=%ds)\n", conf.timeout);

    conf_free(&conf);
    return 0;
}
