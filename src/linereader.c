#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include "linereader.h"

#define MAX_LINE 65536
#define MAX_HIST 64

static struct {
    char **entries;
    int    count;
    int    cap;
} g_history = {NULL, 0, 0};

void lr_add_history(const char *line)
{
    if (!line || !*line) return;
    if (g_history.count > 0 &&
        strcmp(g_history.entries[g_history.count - 1], line) == 0)
        return;
    if (g_history.count >= g_history.cap)
    {
        int newcap = g_history.cap ? g_history.cap * 2 : 16;
        char **tmp = realloc(g_history.entries, sizeof(char *) * newcap);
        if (!tmp) return;
        g_history.entries = tmp;
        g_history.cap = newcap;
    }
    g_history.entries[g_history.count++] = strdup(line);
    if (g_history.count > MAX_HIST)
    {
        free(g_history.entries[0]);
        memmove(g_history.entries, g_history.entries + 1,
                (g_history.count - 1) * sizeof(char *));
        g_history.count--;
    }
}

static int is_utf8_cont(char c)
{
    return (unsigned char)c >= 0x80 && (unsigned char)c < 0xC0;
}

static int utf8_charlen(const char *s)
{
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

static void del_char(char *buf, int *pos, int *len)
{
    if (*pos <= 0) return;
    int remove = 1;
    while (remove < *pos && is_utf8_cont(buf[*pos - remove]))
        remove++;
    int n = *pos - remove;
    memmove(buf + n, buf + *pos, *len - *pos);
    *pos = n;
    *len -= remove;
    buf[*len] = '\0';
}

static void write_str(const char *s)
{
    ssize_t ignored = write(STDOUT_FILENO, s, strlen(s));
    (void)ignored;
}

char *lr_readline(const char *prompt, FILE *in, FILE *out)
{
    (void)out;
    if (!isatty(fileno(in)))
    {
        write_str(prompt);
        char *buf = malloc(MAX_LINE);
        if (!buf) return NULL;
        if (!fgets(buf, MAX_LINE, in))
        { free(buf); return NULL; }
        size_t l = strlen(buf);
        if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
        return buf;
    }

    struct termios old, raw;
    tcgetattr(STDIN_FILENO, &old);
    raw = old;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char *buf = malloc(MAX_LINE);
    if (!buf) { tcsetattr(STDIN_FILENO, TCSANOW, &old); return NULL; }
    int pos = 0, len = 0;
    buf[0] = '\0';

    write_str(prompt);

    int hist_idx = g_history.count;

    while (1)
    {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
        {
            if (errno == EINTR) continue;
            break;
        }

        if (c == 3)
        {
            buf[0] = '\0'; len = 0; pos = 0;
            write_str("^C\n");
            tcsetattr(STDIN_FILENO, TCSANOW, &old);
            return NULL;
        }

        if (c == 4)
        {
            if (len == 0) { break; }
            continue;
        }

        if (c == '\n' || c == '\r')
        {
            write_str("\n");
            break;
        }

        if (c == 127 || c == 8)
        {
            if (pos > 0) del_char(buf, &pos, &len);
            write_str("\r\033[K");
            write_str(prompt);
            write_str(buf);
            if (pos < len)
            {
                char fmt[32];
                snprintf(fmt, sizeof(fmt), "\033[%dG", (int)(strlen(prompt) + pos + 1));
                write_str(fmt);
            }
            continue;
        }

        if (c == 27)
        {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;

            if (seq[0] == '[')
            {
                if (seq[1] == 'A')
                {
                    if (hist_idx > 0)
                    {
                        hist_idx--;
                        write_str("\r\033[K");
                        write_str(prompt);
                        const char *h = g_history.entries[hist_idx];
                        int hl = strlen(h);
                        if (hl >= MAX_LINE) hl = MAX_LINE - 1;
                        memcpy(buf, h, hl);
                        len = pos = hl;
                        buf[len] = '\0';
                        write_str(buf);
                    }
                    continue;
                }
                if (seq[1] == 'B')
                {
                    if (hist_idx < g_history.count)
                    {
                        hist_idx++;
                        write_str("\r\033[K");
                        write_str(prompt);
                        if (hist_idx >= g_history.count)
                        {
                            buf[0] = '\0';
                            len = pos = 0;
                        }
                        else
                        {
                            const char *h = g_history.entries[hist_idx];
                            int hl = strlen(h);
                            if (hl >= MAX_LINE) hl = MAX_LINE - 1;
                            memcpy(buf, h, hl);
                            len = pos = hl;
                            buf[len] = '\0';
                        }
                        write_str(buf);
                    }
                    continue;
                }
                if (seq[1] == 'C' && pos < len)
                {
                    int clen = utf8_charlen(buf + pos);
                    pos += clen;
                    char fmt[32];
                    snprintf(fmt, sizeof(fmt), "\033[%dG", (int)(strlen(prompt) + pos + 1));
                    write_str(fmt);
                    continue;
                }
                if (seq[1] == 'D' && pos > 0)
                {
                    int remove = utf8_charlen(buf + pos - 1);
                    pos -= remove;
                    char fmt[32];
                    snprintf(fmt, sizeof(fmt), "\033[%dG", (int)(strlen(prompt) + pos + 1));
                    write_str(fmt);
                    continue;
                }
            }
            continue;
        }

        if (c == '\t')
            continue;

        if (len >= MAX_LINE - 4)
            continue;

        if (pos < len)
            memmove(buf + pos + 1, buf + pos, len - pos);
        buf[pos] = c;
        pos++;
        len++;
        buf[len] = '\0';

        write_str("\r\033[K");
        write_str(prompt);
        write_str(buf);
        if (pos < len)
        {
            char fmt[32];
            snprintf(fmt, sizeof(fmt), "\033[%dG", (int)(strlen(prompt) + pos + 1));
            write_str(fmt);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return buf;
}

void lr_cleanup(void)
{
    for (int i = 0; i < g_history.count; i++)
        free(g_history.entries[i]);
    free(g_history.entries);
    g_history.entries = NULL;
    g_history.count = 0;
    g_history.cap = 0;
}
