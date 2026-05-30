#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "json.h"
#include "utils.h"

/* ── Growing buffer ────────────────────────────────────────── */

typedef struct {
    char *buf;
    size_t len, cap;
} gbuf_t;

static int gbuf_init(gbuf_t *g, size_t init)
{
    g->buf = malloc(init);
    if (!g->buf) return -1;
    g->buf[0] = '\0';
    g->len = 0;
    g->cap = init;
    return 0;
}

static int gbuf_grow(gbuf_t *g, size_t needed)
{
    if (needed <= g->cap) return 0;
    size_t newcap = g->cap ? g->cap : 4096;
    while (newcap < needed) newcap *= 2;
    char *tmp = realloc(g->buf, newcap);
    if (!tmp) return -1;
    g->buf = tmp;
    g->cap = newcap;
    return 0;
}

static int gbuf_append(gbuf_t *g, const char *s, size_t n)
{
    if (gbuf_grow(g, g->len + n + 1) != 0) return -1;
    memcpy(g->buf + g->len, s, n);
    g->len += n;
    g->buf[g->len] = '\0';
    return 0;
}

static int gbuf_appendf(gbuf_t *g, const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;

    if ((size_t)n < sizeof(buf))
        return gbuf_append(g, buf, (size_t)n);

    if (gbuf_grow(g, g->len + n + 1) != 0) return -1;
    va_start(ap, fmt);
    vsnprintf(g->buf + g->len, g->cap - g->len, fmt, ap);
    va_end(ap);
    g->len += n;
    return 0;
}

/* ── JSON value skippers ───────────────────────────────────── */

static void skip_ws(const char *j, int *p)
{
    while (j[*p] && isspace((unsigned char)j[*p])) (*p)++;
}

static void skip_string(const char *j, int *p)
{
    if (j[*p] != '"') return;
    (*p)++;
    while (j[*p]) {
        if (j[*p] == '\\') { (*p) += 2; continue; }
        if (j[*p] == '"') { (*p)++; return; }
        (*p)++;
    }
}

static void skip_value(const char *j, int *p)
{
    skip_ws(j, p);
    if (!j[*p]) return;

    char c = j[*p];
    if (c == '"') {
        skip_string(j, p);
    } else if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        int depth = 1;
        int in_str = 0;
        (*p)++;
        while (j[*p] && depth > 0) {
            if (in_str) {
                if (j[*p] == '\\') { (*p) += 2; continue; }
                if (j[*p] == '"') in_str = 0;
                (*p)++; continue;
            }
            if (j[*p] == '"') { in_str = 1; (*p)++; continue; }
            if (j[*p] == '{' || j[*p] == '[') { depth++; (*p)++; continue; }
            if (j[*p] == close) { depth--; (*p)++; continue; }
            if (j[*p] == '}' || j[*p] == ']') { depth--; (*p)++; continue; }
            (*p)++;
        }
    } else {
        while (j[*p] && !isspace((unsigned char)j[*p]) && j[*p] != ',' && j[*p] != '}' && j[*p] != ']')
            (*p)++;
    }
}

/* pointer-based skippers for json_extract_tool_calls */
static void skip_ws_p(const char **s)
{
    while (**s && isspace((unsigned char)**s)) (*s)++;
}

static void skip_value_p(const char **s)
{
    skip_ws_p(s);
    if (!**s) return;

    char c = **s;
    if (c == '"') {
        (*s)++;
        while (**s) {
            if (**s == '\\') { (*s) += 2; continue; }
            if (**s == '"') { (*s)++; return; }
            (*s)++;
        }
    } else if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        int depth = 1;
        int in_str = 0;
        (*s)++;
        while (**s && depth > 0) {
            if (in_str) {
                if (**s == '\\') { (*s) += 2; continue; }
                if (**s == '"') in_str = 0;
                (*s)++; continue;
            }
            if (**s == '"') { in_str = 1; (*s)++; continue; }
            if (**s == '{' || **s == '[') { depth++; (*s)++; continue; }
            if (**s == close) { depth--; (*s)++; continue; }
            if (**s == '}' || **s == ']') { depth--; (*s)++; continue; }
            (*s)++;
        }
    } else {
        while (**s && !isspace((unsigned char)**s) && **s != ',' && **s != '}' && **s != ']')
            (*s)++;
    }
}

/* ── json_build_request ────────────────────────────────────── */

char *json_build_request(const char *model, const char *sys_prompt,
                          msg_t *msgs, int msg_count,
                          const tool_def_t *tools, int tool_count)
{
    gbuf_t g;
    if (gbuf_init(&g, 4096) != 0) return NULL;

    gbuf_append(&g, "{", 1);
    gbuf_appendf(&g, "\"model\":\"%s\"", model);
    gbuf_append(&g, ",\"messages\":[", strlen(",\"messages\":["));

    int first = 1;

    if (sys_prompt && sys_prompt[0])
    {
        char esc[8192];
        if (utils_escape_json(sys_prompt, esc, sizeof(esc)) == 0)
        {
            gbuf_appendf(&g, "{\"role\":\"system\",\"content\":\"%s\"}", esc);
            first = 0;
        }
    }

    for (int i = 0; i < msg_count; i++)
    {
        msg_t *m = &msgs[i];
        if (!m->content) continue;

        char esc[8192];
        if (utils_escape_json(m->content, esc, sizeof(esc)) != 0)
            continue;

        if (!first) gbuf_append(&g, ",", 1);
        first = 0;

        const char *role_str = "";
        switch (m->role) {
            case ROLE_USER:      role_str = "user";      break;
            case ROLE_ASSISTANT: role_str = "assistant";  break;
            case ROLE_TOOL:      role_str = "tool";       break;
            case ROLE_SYSTEM:    role_str = "system";     break;
        }

        gbuf_appendf(&g, "{\"role\":\"%s\",\"content\":\"%s\"", role_str, esc);

        if (m->role == ROLE_ASSISTANT && m->reasoning_content)
        {
            char ert[8192];
            if (utils_escape_json(m->reasoning_content, ert, sizeof(ert)) == 0)
                gbuf_appendf(&g, ",\"reasoning_content\":\"%s\"", ert);
        }

        if (m->role == ROLE_TOOL && m->tool_call_id)
        {
            char eid[256];
            if (utils_escape_json(m->tool_call_id, eid, sizeof(eid)) == 0)
                gbuf_appendf(&g, ",\"tool_call_id\":\"%s\"", eid);
        }

        if (m->role == ROLE_ASSISTANT && m->tool_name)
        {
            gbuf_append(&g, ",\"tool_calls\":[{\"id\":\"", strlen(",\"tool_calls\":[{\"id\":\""));
            char eid[256];
            if (m->tool_call_id && utils_escape_json(m->tool_call_id, eid, sizeof(eid)) == 0)
                gbuf_append(&g, eid, strlen(eid));
            gbuf_append(&g, "\",\"type\":\"function\",\"function\":{", strlen("\",\"type\":\"function\",\"function\":{"));
            char en[256];
            if (utils_escape_json(m->tool_name, en, sizeof(en)) == 0)
                gbuf_appendf(&g, "\"name\":\"%s\"", en);
            if (m->tool_args)
            {
                char eargs[8192];
                if (utils_escape_json(m->tool_args, eargs, sizeof(eargs)) == 0)
                    gbuf_appendf(&g, ",\"arguments\":\"%s\"", eargs);
            }
            gbuf_append(&g, "}}]", 3);
        }

        gbuf_append(&g, "}", 1);
    }

    gbuf_append(&g, "]", 1);

    if (tools && tool_count > 0)
    {
        gbuf_append(&g, ",\"tools\":[", 10);
        for (int i = 0; i < tool_count; i++)
        {
            if (i > 0) gbuf_append(&g, ",", 1);
            gbuf_appendf(&g, "{\"type\":\"function\",\"function\":{");
            char en[256];
            if (utils_escape_json(tools[i].name, en, sizeof(en)) == 0)
                gbuf_appendf(&g, "\"name\":\"%s\",", en);
            if (utils_escape_json(tools[i].desc, en, sizeof(en)) == 0)
                gbuf_appendf(&g, "\"description\":\"%s\",", en);
            gbuf_append(&g, "\"parameters\":", 13);
            if (tools[i].params_schema)
                gbuf_append(&g, tools[i].params_schema, strlen(tools[i].params_schema));
            else
                gbuf_append(&g, "{}", 2);
            gbuf_append(&g, "}}", 2);
        }
        gbuf_append(&g, "]", 1);
    }

    gbuf_append(&g, "}", 1);

    return g.buf;
}

/* ── json_get_str ──────────────────────────────────────────── */

int json_get_str(const char *json, const char *path, char *out, size_t outlen)
{
    if (!json || !path || !out || outlen == 0) return -1;
    out[0] = '\0';

    int pos = 0;
    skip_ws(json, &pos);
    if (!json[pos]) return -1;

    const char *p = path;
    while (*p)
    {
        while (*p == '.') p++;
        if (!*p) break;

        const char *seg_start = p;
        while (*p && *p != '.') p++;
        int seg_len = (int)(p - seg_start);
        if (seg_len == 0) return -1;

        int is_num = 1;
        for (int i = 0; i < seg_len; i++)
            if (!isdigit((unsigned char)seg_start[i])) { is_num = 0; break; }

        if (is_num)
        {
            skip_ws(json, &pos);
            if (json[pos] != '[') return -1;
            pos++;

            int target = 0;
            for (int i = 0; i < seg_len; i++)
                target = target * 10 + (seg_start[i] - '0');

            int idx = 0;
            while (idx < target)
            {
                skip_ws(json, &pos);
                if (json[pos] == ']') return -1;
                skip_value(json, &pos);
                skip_ws(json, &pos);
                if (json[pos] == ',') pos++;
                idx++;
            }
            skip_ws(json, &pos);
        }
        else
        {
            skip_ws(json, &pos);
            if (json[pos] != '{') return -1;
            pos++;

            int found = 0;
            while (json[pos])
            {
                skip_ws(json, &pos);
                if (json[pos] == '}') break;
                if (json[pos] != '"') { skip_value(json, &pos); continue; }

                int ks = pos + 1;
                int ke = ks;
                while (json[ke] && json[ke] != '"')
                {
                    if (json[ke] == '\\') ke++;
                    if (json[ke]) ke++;
                }
                if (!json[ke]) return -1;

                int klen = ke - ks;
                if (klen == seg_len && strncmp(json + ks, seg_start, seg_len) == 0)
                {
                    pos = ke + 1;
                    skip_ws(json, &pos);
                    if (json[pos] == ':') pos++;
                    skip_ws(json, &pos);
                    found = 1;
                    break;
                }
                else
                {
                    pos = ke + 1;
                    skip_ws(json, &pos);
                    if (json[pos] == ':') pos++;
                    skip_value(json, &pos);
                    skip_ws(json, &pos);
                    if (json[pos] == ',') pos++;
                }
            }
            if (!found) return -1;
        }
    }

    skip_ws(json, &pos);
    if (!json[pos]) return -1;

    if (json[pos] == '"')
    {
        pos++;
        size_t opos = 0;
        while (json[pos] && json[pos] != '"' && opos + 1 < outlen)
        {
            if (json[pos] == '\\')
            {
                pos++;
                switch (json[pos]) {
                    case '"':  out[opos++] = '"';  break;
                    case '\\': out[opos++] = '\\'; break;
                    case 'n':  out[opos++] = '\n'; break;
                    case 'r':  out[opos++] = '\r'; break;
                    case 't':  out[opos++] = '\t'; break;
                    default:   out[opos++] = json[pos]; break;
                }
                if (json[pos]) pos++;
            }
            else
            {
                out[opos++] = json[pos++];
            }
        }
        out[opos] = '\0';
        if (json[pos] == '"') pos++;
        return 0;
    }
    else if (json[pos] == '{' || json[pos] == '[')
    {
        return -1;
    }
    else
    {
        size_t opos = 0;
        while (json[pos] && !isspace((unsigned char)json[pos]) && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' && opos + 1 < outlen)
            out[opos++] = json[pos++];
        out[opos] = '\0';
        return 0;
    }
}

/* ── json_get_int ──────────────────────────────────────────── */

int json_get_int(const char *json, const char *path, int *out)
{
    char buf[64];
    if (json_get_str(json, path, buf, sizeof(buf)) != 0)
        return -1;
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf) return -1;
    *out = (int)v;
    return 0;
}

/* ── json_extract_tool_calls ──────────────────────────────── */

int json_extract_tool_calls(const char *json, tool_call_t *calls, int max)
{
    if (!json || !calls || max <= 0) return -1;

    const char *tc = strstr(json, "\"tool_calls\"");
    if (!tc) return 0;

    tc += 12;
    skip_ws_p(&tc);
    if (*tc != ':') return 0;
    tc++;
    skip_ws_p(&tc);
    if (*tc != '[') return 0;
    tc++;

    int count = 0;
    while (*tc && *tc != ']' && count < max)
    {
        skip_ws_p(&tc);
        if (*tc != '{') break;
        tc++;

        const char *id_str = NULL, *fn_str = NULL, *args_str = NULL;
        int id_len = 0, fn_len = 0, args_len = 0;
        char *args_unesc = NULL;

        while (*tc && *tc != '}')
        {
            skip_ws_p(&tc);
            if (*tc != '"') { skip_value_p(&tc); goto next_comma_tc; }

            tc++;
            const char *ks = tc;
            while (*tc && *tc != '"') { if (*tc == '\\') tc++; if (*tc) tc++; }
            const char *ke = tc;
            if (!*tc) break;
            tc++;

            skip_ws_p(&tc);
            if (*tc != ':') break;
            tc++;
            skip_ws_p(&tc);

            int klen = (int)(ke - ks);

            if (klen == 2 && strncmp(ks, "id", 2) == 0)
            {
                if (*tc == '"')
                {
                    tc++;
                    id_str = tc;
                    while (*tc && *tc != '"') { if (*tc == '\\') tc++; if (*tc) tc++; }
                    id_len = (int)(tc - id_str);
                    if (*tc) tc++;
                }
            }
            else if (klen == 4 && strncmp(ks, "type", 4) == 0)
            {
                if (*tc == '"') { tc++; while (*tc && *tc != '"') { if (*tc == '\\') tc++; if (*tc) tc++; } if (*tc) tc++; }
                else { while (*tc && *tc != ',' && *tc != '}') tc++; }
            }
            else if (klen == 8 && strncmp(ks, "function", 8) == 0)
            {
                if (*tc == '{')
                {
                    tc++;
                    while (*tc && *tc != '}')
                    {
                        skip_ws_p(&tc);
                        if (*tc != '"') { skip_value_p(&tc); goto next_inner_comma; }
                        tc++;
                        const char *fks = tc;
                        while (*tc && *tc != '"') { if (*tc == '\\') tc++; if (*tc) tc++; }
                        const char *fke = tc;
                        if (!*tc) break;
                        tc++;
                        skip_ws_p(&tc);
                        if (*tc != ':') break;
                        tc++;
                        skip_ws_p(&tc);

                        int fklen = (int)(fke - fks);
                        if (fklen == 4 && strncmp(fks, "name", 4) == 0)
                        {
                            if (*tc == '"')
                            {
                                tc++;
                                fn_str = tc;
                                while (*tc && *tc != '"') { if (*tc == '\\') tc++; if (*tc) tc++; }
                                fn_len = (int)(tc - fn_str);
                                if (*tc) tc++;
                            }
                        }
                        else if (fklen == 9 && strncmp(fks, "arguments", 9) == 0)
                        {
                                if (*tc == '"')
                            {
                                tc++;
                                size_t cap = 1024;
                                args_unesc = malloc(cap);
                                if (!args_unesc) { args_str = NULL; args_len = 0; break; }
                                size_t u = 0;
                                while (*tc && *tc != '"')
                                {
                                    if (u + 1 >= cap)
                                    {
                                        cap *= 2;
                                        char *nt = realloc(args_unesc, cap);
                                        if (!nt) { free(args_unesc); args_unesc = NULL; break; }
                                        args_unesc = nt;
                                    }
                                    if (*tc == '\\')
                                    {
                                        tc++;
                                        switch (*tc) {
                                            case '"':  args_unesc[u++] = '"';  break;
                                            case '\\': args_unesc[u++] = '\\'; break;
                                            case 'n':  args_unesc[u++] = '\n'; break;
                                            case 'r':  args_unesc[u++] = '\r'; break;
                                            case 't':  args_unesc[u++] = '\t'; break;
                                            default:   args_unesc[u++] = *tc;  break;
                                        }
                                        if (*tc) tc++;
                                    }
                                    else
                                    {
                                        args_unesc[u++] = *tc++;
                                    }
                                }
                                args_unesc[u] = '\0';
                                if (*tc == '"') tc++;
                                args_str = args_unesc;
                                args_len = (int)u;
                            }
                            else
                            {
                                args_str = tc;
                                const char *aend = tc;
                                int bdepth = 0;
                                while (*aend)
                                {
                                    if (*aend == '"') { /* skip strings */ }
                                    else if (*aend == '\\') { aend++; }
                                    else if (*aend == '{' || *aend == '[') bdepth++;
                                    else if ((*aend == '}' || *aend == ']') && bdepth > 0) bdepth--;
                                    else if (bdepth == 0 && (*aend == ',' || *aend == '}')) break;
                                    aend++;
                                }
                                args_len = (int)(aend - args_str);
                                tc = aend;
                            }
                        }
                        else
                        {
                            skip_value_p(&tc);
                        }

                    next_inner_comma:
                        skip_ws_p(&tc);
                        if (*tc == ',') tc++;
                    }
                    if (*tc == '}') tc++;
                }
            }
            else
            {
                skip_value_p(&tc);
            }

        next_comma_tc:
            skip_ws_p(&tc);
            if (*tc == ',') tc++;
        }

        if (*tc == '}') tc++;

        if (fn_str && fn_len > 0)
        {
            calls[count].id = (id_str && id_len > 0) ? strndup(id_str, id_len) : NULL;
            calls[count].name = strndup(fn_str, fn_len);
            calls[count].arguments = (args_str && args_len > 0) ? strndup(args_str, args_len) : strdup("");
            free(args_unesc);
            args_unesc = NULL;
            count++;
        }

        skip_ws_p(&tc);
        if (*tc == ',') tc++;
    }

    return count;
}

/* ── json_load_history ─────────────────────────────────────── */

int json_load_history(const char *filepath, msg_t **msgs, int *count)
{
    *msgs = NULL;
    *count = 0;

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return -1; }
    size_t r = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[r] = '\0';

    int pos = 0;
    skip_ws(buf, &pos);
    if (buf[pos] != '[') { free(buf); return -1; }
    pos++;

    int cap = 16;
    msg_t *arr = malloc(sizeof(msg_t) * cap);
    if (!arr) { free(buf); return -1; }
    int cnt = 0;

    while (buf[pos])
    {
        skip_ws(buf, &pos);
        if (buf[pos] == ']') break;
        if (buf[pos] != '{') { skip_value(buf, &pos); continue; }
        pos++;

        msg_t m;
        memset(&m, 0, sizeof(m));

        while (buf[pos])
        {
            skip_ws(buf, &pos);
            if (buf[pos] == '}') { pos++; break; }
            if (buf[pos] != '"') { skip_value(buf, &pos); goto next_comma_lh; }

            pos++;
            const char *ks = buf + pos;
            while (buf[pos] && buf[pos] != '"') { if (buf[pos] == '\\') pos++; if (buf[pos]) pos++; }
            const char *ke = buf + pos;
            if (!buf[pos]) break;
            pos++;

            skip_ws(buf, &pos);
            if (buf[pos] == ':') pos++;
            skip_ws(buf, &pos);

            int klen = (int)(ke - ks);

            if (klen == 4 && strncmp(ks, "role", 4) == 0)
            {
                if (buf[pos] == '"')
                {
                    pos++;
                    const char *vs = buf + pos;
                    while (buf[pos] && buf[pos] != '"') pos++;
                    int vlen = (int)(buf + pos - vs);
                    if (vlen == 4 && strncmp(vs, "user", 4) == 0) m.role = ROLE_USER;
                    else if (vlen == 9 && strncmp(vs, "assistant", 9) == 0) m.role = ROLE_ASSISTANT;
                    else if (vlen == 4 && strncmp(vs, "tool", 4) == 0) m.role = ROLE_TOOL;
                    else if (vlen == 6 && strncmp(vs, "system", 6) == 0) m.role = ROLE_SYSTEM;
                    if (buf[pos]) pos++;
                }
            }
            else if (klen == 7 && strncmp(ks, "content", 7) == 0)
            {
                if (buf[pos] == '"')
                {
                    pos++;
                    size_t opos = 0;
                    size_t tmpcap = 1024;
                    char *tmp = malloc(tmpcap);
                    if (!tmp) break;

                    while (buf[pos] && buf[pos] != '"')
                    {
                        if (opos + 1 >= tmpcap)
                        {
                            tmpcap *= 2;
                            char *nt = realloc(tmp, tmpcap);
                            if (!nt) { free(tmp); break; }
                            tmp = nt;
                        }
                        if (buf[pos] == '\\')
                        {
                            pos++;
                            switch (buf[pos]) {
                                case '"':  tmp[opos++] = '"';  break;
                                case '\\': tmp[opos++] = '\\'; break;
                                case 'n':  tmp[opos++] = '\n'; break;
                                case 'r':  tmp[opos++] = '\r'; break;
                                case 't':  tmp[opos++] = '\t'; break;
                                default:   tmp[opos++] = buf[pos]; break;
                            }
                            if (buf[pos]) pos++;
                        }
                        else
                        {
                            tmp[opos++] = buf[pos++];
                        }
                    }
                    tmp[opos] = '\0';
                    if (buf[pos] == '"') pos++;
                    m.content = tmp;
                }
            }
            else if (klen == 16 && strncmp(ks, "reasoning_content", 16) == 0)
            {
                if (buf[pos] == '"')
                {
                    pos++;
                    size_t opos = 0;
                    size_t tmpcap = 1024;
                    char *tmp = malloc(tmpcap);
                    if (!tmp) break;

                    while (buf[pos] && buf[pos] != '"')
                    {
                        if (opos + 1 >= tmpcap)
                        {
                            tmpcap *= 2;
                            char *nt = realloc(tmp, tmpcap);
                            if (!nt) { free(tmp); break; }
                            tmp = nt;
                        }
                        if (buf[pos] == '\\')
                        {
                            pos++;
                            switch (buf[pos]) {
                                case '"':  tmp[opos++] = '"';  break;
                                case '\\': tmp[opos++] = '\\'; break;
                                case 'n':  tmp[opos++] = '\n'; break;
                                case 'r':  tmp[opos++] = '\r'; break;
                                case 't':  tmp[opos++] = '\t'; break;
                                default:   tmp[opos++] = buf[pos]; break;
                            }
                            if (buf[pos]) pos++;
                        }
                        else
                        {
                            tmp[opos++] = buf[pos++];
                        }
                    }
                    tmp[opos] = '\0';
                    if (buf[pos] == '"') pos++;
                    m.reasoning_content = tmp;
                }
            }
            else if (klen == 12 && strncmp(ks, "tool_call_id", 12) == 0)
            {
                if (buf[pos] == '"')
                {
                    pos++;
                    const char *vs = buf + pos;
                    while (buf[pos] && buf[pos] != '"') pos++;
                    int vlen = (int)(buf + pos - vs);
                    m.tool_call_id = strndup(vs, vlen);
                    if (buf[pos]) pos++;
                }
            }
            else
            {
                skip_value(buf, &pos);
            }

        next_comma_lh:
            skip_ws(buf, &pos);
            if (buf[pos] == ',') pos++;
        }

        if (m.role != ROLE_SYSTEM || m.content)
        {
            if (!m.content) m.content = strdup("");
            if (cnt >= cap)
            {
                cap *= 2;
                msg_t *tmp = realloc(arr, sizeof(msg_t) * cap);
                if (!tmp) break;
                arr = tmp;
            }
            arr[cnt++] = m;
        }
        else
        {
            free(m.content);
            free(m.reasoning_content);
            free(m.tool_call_id);
        }

        skip_ws(buf, &pos);
        if (buf[pos] == ',') pos++;
    }

    free(buf);

    if (cnt == 0) { free(arr); return -1; }
    *msgs = arr;
    *count = cnt;
    return 0;
}

/* ── json_save_history ─────────────────────────────────────── */

int json_save_history(const char *filepath, msg_t *msgs, int count)
{
    gbuf_t g;
    if (gbuf_init(&g, 4096) != 0) return -1;

    gbuf_append(&g, "[", 1);

    for (int i = 0; i < count; i++)
    {
        msg_t *m = &msgs[i];
        if (i > 0) gbuf_append(&g, ",", 1);

        const char *role_str = "";
        switch (m->role) {
            case ROLE_USER:      role_str = "user";      break;
            case ROLE_ASSISTANT: role_str = "assistant";  break;
            case ROLE_TOOL:      role_str = "tool";       break;
            case ROLE_SYSTEM:    role_str = "system";     break;
        }

        char esc[8192];
        gbuf_appendf(&g, "{\"role\":\"%s\"", role_str);

        if (m->content)
        {
            if (utils_escape_json(m->content, esc, sizeof(esc)) == 0)
                gbuf_appendf(&g, ",\"content\":\"%s\"", esc);
        }

        if (m->role == ROLE_ASSISTANT && m->reasoning_content)
        {
            if (utils_escape_json(m->reasoning_content, esc, sizeof(esc)) == 0)
                gbuf_appendf(&g, ",\"reasoning_content\":\"%s\"", esc);
        }

        if (m->role == ROLE_TOOL && m->tool_call_id)
        {
            if (utils_escape_json(m->tool_call_id, esc, sizeof(esc)) == 0)
                gbuf_appendf(&g, ",\"tool_call_id\":\"%s\"", esc);
        }

        if (m->role == ROLE_ASSISTANT && m->tool_name)
        {
            if (utils_escape_json(m->tool_name, esc, sizeof(esc)) == 0)
                gbuf_appendf(&g, ",\"tool_name\":\"%s\"", esc);
            if (m->tool_args)
                gbuf_appendf(&g, ",\"tool_args\":\"%s\"", m->tool_args);
        }

        gbuf_append(&g, "}", 1);
    }

    gbuf_append(&g, "]\n", 2);

    FILE *f = fopen(filepath, "w");
    if (!f) { free(g.buf); return -1; }
    size_t written = fwrite(g.buf, 1, g.len, f);
    fclose(f);
    free(g.buf);

    return (written == g.len) ? 0 : -1;
}
