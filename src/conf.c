#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"

int conf_load(const char *path, agent_conf_t *conf)
{
    (void)path;
    (void)conf;
    return -1;
}

void conf_print(const agent_conf_t *conf)
{
    (void)conf;
}

void conf_free(agent_conf_t *conf)
{
    (void)conf;
}
