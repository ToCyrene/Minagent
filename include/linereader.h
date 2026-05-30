#ifndef LINEREADER_H
#define LINEREADER_H

#include <stdio.h>

char *lr_readline(const char *prompt, FILE *in, FILE *out);
void  lr_add_history(const char *line);
void  lr_cleanup(void);

#endif
