// Local development commands. Only the socket thread touches sockets; handlers run at the frame boundary.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "rt.h"
#include "control.h"

enum { MAX_COMMANDS = 128, MAX_CLIENTS = 4, MAX_LINE = 4096, MAX_PENDING = 64, MAX_JSON = 1024 * 1024 };
#ifdef _WIN32
typedef SOCKET ControlSocket;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
static bool socket_again(void) { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
static bool nonblocking(ControlSocket s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
#else
typedef int ControlSocket;
#define BAD_SOCKET (-1)
#define close_socket close
static bool socket_again(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static bool nonblocking(ControlSocket s) {
    int flags = fcntl(s, F_GETFL, 0);
    return s < FD_SETSIZE && flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

typedef struct { char *s; size_t len, cap; bool bad; } Json;
struct ControlReply { Json data; char *error; bool failed, exit_after; };
typedef struct Request {
    struct Request *next;
    int client, argc;
    uint64_t generation;
    char *line, **argv, *reply;
    const char *error;
    size_t len, sent;
    bool exit_after;
} Request;
typedef struct {
    ControlSocket socket;
    uint64_t generation;
    char line[MAX_LINE + 1], input[MAX_LINE];
    size_t used, pos, end, bytes;
    int pending;
    bool dropping, eof;
    Request *head, *tail;
} Connection;

static ControlCmd commands[MAX_COMMANDS];
static int command_count;
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static Request *requests, *requests_tail, *finished, *finished_tail;
static _Atomic bool enabled, queued;
static ControlSocket listener = BAD_SOCKET;
static Connection clients[MAX_CLIENTS];  // owned by the socket thread
static int inflight;

static void append(Json *j, const char *s, size_t n) {
    if (j->bad) return;
    if (n > MAX_JSON - j->len) { j->bad = true; return; }
    size_t need = j->len + n + 1;
    if (need > j->cap) {
        size_t cap = need * 2;
        if (cap > MAX_JSON + 1) cap = MAX_JSON + 1;
        char *p = realloc(j->s, cap);
        if (!p) { j->bad = true; return; }
        j->s = p;
        j->cap = cap;
    }
    memcpy(j->s + j->len, s, n);
    j->len += n;
    j->s[j->len] = 0;
}
static void text(Json *j, const char *s) { append(j, s, strlen(s)); }
static void string(Json *j, const char *s) {
    text(j, "\"");
    const unsigned char *p = (const unsigned char *)(s ? s : ""), *start = p;
    for (; *p && !j->bad; p++) {
        if (*p >= 0x20 && *p != '"' && *p != '\\') continue;
        append(j, (const char *)start, (size_t)(p - start));
        if (*p == '"' || *p == '\\') { char esc[] = {'\\', (char)*p}; append(j, esc, 2); }
        else { char esc[7]; snprintf(esc, sizeof esc, "\\u%04x", *p); text(j, esc); }
        start = p + 1;
    }
    append(j, (const char *)start, (size_t)(p - start));
    text(j, "\"");
}
static void field(ControlReply *r, const char *key) {
    if (r->data.len) text(&r->data, ",");
    string(&r->data, key);
    text(&r->data, ":");
}
void control_field_str(ControlReply *r, const char *key, const char *value) {
    field(r, key); string(&r->data, value);
}
void control_field_int(ControlReply *r, const char *key, int64_t value) {
    char s[32]; snprintf(s, sizeof s, "%" PRId64, value);
    field(r, key); text(&r->data, s);
}
void control_field_num(ControlReply *r, const char *key, double value) {
    char s[128];
    if (!isfinite(value)) strcpy(s, "null");
    else {
        snprintf(s, sizeof s, "%.17g", value);
        // The runtime can use the system locale, but JSON always has a decimal point.
        const char *point = localeconv()->decimal_point;
        char *p = *point ? strstr(s, point) : NULL;
        if (p && strcmp(point, ".")) { memmove(p + 1, p + strlen(point), strlen(p + strlen(point)) + 1); *p = '.'; }
    }
    field(r, key); text(&r->data, s);
}
void control_field_bool(ControlReply *r, const char *key, bool value) {
    field(r, key); text(&r->data, value ? "true" : "false");
}
void control_fail(ControlReply *r, const char *fmt, ...) {
    if (r->failed) return;
    r->failed = true;
    va_list ap, copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n >= 0 && n < MAX_JSON / 2) {
        r->error = malloc((size_t)n + 1);
        if (r->error) vsnprintf(r->error, (size_t)n + 1, fmt, ap);
    }
    va_end(ap);
}

void control_register(const ControlCmd *cmds, int count) {
    pthread_mutex_lock(&registry_lock);
    for (int i = 0; i < count; i++) {
        const ControlCmd *c = &cmds[i];
        if (!c->name || !*c->name || !c->fn) { LOG("[control] invalid command registration\n"); continue; }
        bool duplicate = false;
        for (int k = 0; k < command_count; k++) if (!strcmp(commands[k].name, c->name)) duplicate = true;
        if (duplicate) { LOG("[control] duplicate command: %s\n", c->name); continue; }
        if (command_count == MAX_COMMANDS) { LOG("[control] command table full: %s\n", c->name); continue; }
        commands[command_count++] = *c;
    }
    pthread_mutex_unlock(&registry_lock);
}
static void help(int argc, char **argv, ControlReply *r) {
    (void)argc; (void)argv;
    ControlCmd snapshot[MAX_COMMANDS];
    pthread_mutex_lock(&registry_lock);
    int count = command_count;
    memcpy(snapshot, commands, (size_t)count * sizeof *snapshot);
    pthread_mutex_unlock(&registry_lock);
    field(r, "commands");
    text(&r->data, "[");
    for (int i = 0; i < count; i++) {
        if (i) text(&r->data, ",");
        text(&r->data, "{\"name\":"); string(&r->data, snapshot[i].name);
        text(&r->data, ",\"args\":"); string(&r->data, snapshot[i].args);
        text(&r->data, ",\"help\":"); string(&r->data, snapshot[i].help);
        text(&r->data, "}");
    }
    text(&r->data, "]");
}
static void ping(int argc, char **argv, ControlReply *r) {
    (void)argc; (void)argv;
    control_field_bool(r, "pong", true);
}
static void quit(int argc, char **argv, ControlReply *r) {
    (void)argc; (void)argv;
    r->exit_after = true;
}

static void push(Request **head, Request **tail, Request *q) {
    q->next = NULL;
    if (*tail) (*tail)->next = q;
    else *head = q;
    *tail = q;
}
static void free_request(Request *q) {
    free(q->argv); free(q->line); free(q->reply); free(q);
    inflight--;
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool enqueue(int slot, const char *error) {
    Connection *c = &clients[slot];
    Request *q = calloc(1, sizeof *q);
    if (!q) return false;
    q->client = slot;
    q->generation = c->generation;
    q->error = error;
    if (!error) {
        q->line = malloc(c->used + 1);
        q->argv = malloc((c->used + 1) * sizeof *q->argv);
        if (!q->line || !q->argv) { free(q->line); free(q->argv); free(q); return false; }
        memcpy(q->line, c->line, c->used);
        q->line[c->used] = 0;
        if (memchr(q->line, 0, c->used)) q->error = "request contains NUL";
        char *src = q->line, *dst = q->line;
        while (*src && !q->error) {
            while (space(*src)) src++;
            if (!*src) break;
            q->argv[q->argc++] = dst;
            bool quoted = false;
            while (*src && (quoted || !space(*src))) {
                if (*src == '"') { quoted = !quoted; src++; }
                else if (quoted && *src == '\\' && (src[1] == '"' || src[1] == '\\')) { src++; *dst++ = *src++; }
                else *dst++ = *src++;
            }
            if (quoted) q->error = "unterminated double quote";
            while (space(*src)) src++;
            *dst++ = 0;
        }
        if (!q->argc && !q->error) { free(q->line); free(q->argv); free(q); return true; }
    }
    inflight++;
    c->pending++;
    pthread_mutex_lock(&queue_lock);
    push(&requests, &requests_tail, q);
    queued = true;
    pthread_mutex_unlock(&queue_lock);
    return true;
}

void control_frame(uint64_t frame) {
    if (!enabled) return;
    control_game_frame(frame);
    if (!queued) return;
    pthread_mutex_lock(&queue_lock);
    Request *batch = requests;
    requests = requests_tail = NULL;
    queued = false;
    pthread_mutex_unlock(&queue_lock);
    while (batch) {
        Request *q = batch;
        batch = q->next;
        ControlReply r = {0};
        if (q->error) control_fail(&r, "%s", q->error);
        else {
            ControlFn fn = NULL;
            pthread_mutex_lock(&registry_lock);
            for (int i = 0; i < command_count; i++) if (!strcmp(commands[i].name, q->argv[0])) { fn = commands[i].fn; break; }
            pthread_mutex_unlock(&registry_lock);
            if (fn) fn(q->argc, q->argv, &r);
            else control_fail(&r, "unknown command '%s'; try help", q->argv[0]);
        }
        Json out = {0};
        if (r.failed || r.data.bad) {
            text(&out, "{\"ok\":false,\"error\":");
            string(&out, r.data.bad ? "reply too large or out of memory" : r.error ? r.error : "command failed");
            text(&out, "}\n");
        } else {
            text(&out, "{\"ok\":true,\"data\":{");
            if (r.data.len) append(&out, r.data.s, r.data.len);
            text(&out, "}}\n");
        }
        if (out.bad) {
            free(out.s);
            out.s = strdup("{\"ok\":false,\"error\":\"reply too large or out of memory\"}\n");
            out.len = out.s ? strlen(out.s) : 0;
        }
        free(r.data.s); free(r.error);
        q->reply = out.s; q->len = out.len; q->exit_after = r.exit_after;
        pthread_mutex_lock(&queue_lock);
        push(&finished, &finished_tail, q);
        pthread_mutex_unlock(&queue_lock);
    }
}

static void disconnect(int slot) {
    Connection *c = &clients[slot];
    close_socket(c->socket);
    while (c->head) { Request *q = c->head; c->head = q->next; free_request(q); }
    uint64_t generation = c->generation + 1;
    memset(c, 0, sizeof *c);
    c->socket = BAD_SOCKET;
    c->generation = generation;
}
static void stop(void) { fflush(stderr); _exit(0); }
static bool buffered(int slot) {
    Connection *c = &clients[slot];
    while (c->pos < c->end && c->pending < MAX_PENDING && inflight < MAX_CLIENTS * MAX_PENDING) {
        char ch = c->input[c->pos++];
        if (ch == '\n') {
            if (!c->dropping && !enqueue(slot, NULL)) return false;
            c->used = 0; c->dropping = false;
        } else if (!c->dropping) {
            if (c->used == MAX_LINE) {
                if (!enqueue(slot, "line exceeds 4096 bytes")) return false;
                c->used = 0; c->dropping = true;
            } else c->line[c->used++] = ch;
        }
    }
    return true;
}
static void *serve(void *arg) {
    (void)arg;
    uint64_t exit_deadline = 0;
    for (;;) {
        pthread_mutex_lock(&queue_lock);
        Request *batch = finished;
        finished = finished_tail = NULL;
        pthread_mutex_unlock(&queue_lock);
        while (batch) {
            Request *q = batch;
            batch = q->next;
            Connection *c = &clients[q->client];
            if (q->exit_after && !exit_deadline) exit_deadline = plat_now_ns() + 1000000000;
            if (c->socket == BAD_SOCKET || c->generation != q->generation) { free_request(q); continue; }
            if (!q->reply || q->len > MAX_JSON - c->bytes) { int slot = q->client; free_request(q); disconnect(slot); continue; }
            c->bytes += q->len;
            push(&c->head, &c->tail, q);
        }
        // Give exit's reply a bounded chance to drain, even if its client stopped reading.
        if (exit_deadline && plat_now_ns() >= exit_deadline) stop();
        fd_set reads, writes;
        FD_ZERO(&reads); FD_ZERO(&writes);
        FD_SET(listener, &reads);
        ControlSocket high = listener;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            Connection *c = &clients[i];
            if (c->socket == BAD_SOCKET) continue;
            if (!buffered(i) || (c->eof && !c->pending)) { disconnect(i); continue; }
            if (!c->eof && c->pos == c->end && c->pending < MAX_PENDING && inflight < MAX_CLIENTS * MAX_PENDING)
                FD_SET(c->socket, &reads);
            if (c->head) FD_SET(c->socket, &writes);
            if (c->socket > high) high = c->socket;
        }
        struct timeval timeout = {0, 10000};
        if (select((int)(high + 1), &reads, &writes, NULL, &timeout) < 0) continue;
        if (FD_ISSET(listener, &reads)) {
            ControlSocket s = accept(listener, NULL, NULL);
            if (s != BAD_SOCKET) {
                int slot = 0;
                while (slot < MAX_CLIENTS && clients[slot].socket != BAD_SOCKET) slot++;
                if (slot == MAX_CLIENTS || !nonblocking(s)) close_socket(s);
                else {
#ifdef SO_NOSIGPIPE
                    int on = 1;
                    if (setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on)) { close_socket(s); continue; }
#endif
                    clients[slot].socket = s;
                    // This socket was not in this select call's sets (a descriptor may have been reused).
                    FD_CLR(s, &reads); FD_CLR(s, &writes);
                }
            }
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            Connection *c = &clients[i];
            if (c->socket == BAD_SOCKET) continue;
            if (FD_ISSET(c->socket, &writes)) {
                Request *q = c->head;
                int flags = 0;
#ifdef MSG_NOSIGNAL
                flags = MSG_NOSIGNAL;
#endif
                int n = (int)send(c->socket, q->reply + q->sent, (int)(q->len - q->sent), flags);
                if (n < 0 && socket_again()) continue;
                if (n <= 0) { disconnect(i); continue; }
                q->sent += (size_t)n; c->bytes -= (size_t)n;
                if (q->sent == q->len) {
                    bool exit_after = q->exit_after;
                    c->head = q->next;
                    if (!c->head) c->tail = NULL;
                    c->pending--; free_request(q);
                    if (exit_after) stop();
                }
            }
            if (FD_ISSET(c->socket, &reads)) {
                int n = (int)recv(c->socket, c->input, sizeof c->input, 0);
                if (n < 0 && socket_again()) continue;
                if (n < 0) { disconnect(i); continue; }
                if (!n) { c->eof = true; continue; }  // allow a write-half-closed client to read its replies
                c->pos = 0; c->end = (size_t)n;
                if (!buffered(i)) disconnect(i);
            }
        }
    }
    return NULL;
}

void control_init(void) {
    const char *port = getenv("SAVAGEWIND_CONTROL");
    if (!port) return;
    unsigned value = 0;
    const char *p = port;
    while (*p >= '0' && *p <= '9' && value <= 65535) value = value * 10 + (unsigned)(*p++ - '0');
    if (!*port || *p || !value || value > 65535) { LOG("[control] invalid port: %s\n", port); return; }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) { LOG("[control] Winsock initialization failed\n"); return; }
#endif
    listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)value);
    bool ready = listener != BAD_SOCKET && nonblocking(listener);
#ifndef _WIN32
    int on = 1;
    if (ready) ready = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0;
#endif
    if (!ready || bind(listener, (struct sockaddr *)&address, sizeof address) || listen(listener, MAX_CLIENTS)) {
        LOG("[control] cannot listen on 127.0.0.1:%u\n", value);
        if (listener != BAD_SOCKET) close_socket(listener);
        listener = BAD_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].socket = BAD_SOCKET;
    static const ControlCmd builtins[] = {
        {"help", "", "list the commands", help},
        {"ping", "", "check that the game answers", ping},
        {"exit", "", "end the game", quit},
    };
    control_register(builtins, sizeof builtins / sizeof *builtins);
    control_game_init();
    pthread_t thread;
    if (pthread_create(&thread, NULL, serve, NULL)) {
        LOG("[control] cannot start socket thread\n");
        close_socket(listener); listener = BAD_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }
    pthread_detach(thread);
    enabled = true;
    LOG("[control] listening on 127.0.0.1:%u\n", value);
}
