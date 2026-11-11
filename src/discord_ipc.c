// Discord Rich Presence: the activity's JSON and the connection to Discord. See discord_ipc.h.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "discord_ipc.h"

enum { OP_HANDSHAKE, OP_FRAME, OP_CLOSE, OP_PING, OP_PONG };
#define LINE_MAX_CHARS 128
#define MESSAGE_MAX 65536

// ------------------------------------------------------------------------------------------------
// JSON
// ------------------------------------------------------------------------------------------------
typedef struct {
  char *p;
  size_t n, used;
  int over;
} buf;

static void put(buf *b, const char *s, size_t len) {
  if (b->over || b->used + len >= b->n) {
    b->over = 1;
    return;
  }
  memcpy(b->p + b->used, s, len);
  b->used += len;
  b->p[b->used] = 0;
}
static void puts_(buf *b, const char *s) { put(b, s, strlen(s)); }

// s as a JSON string, at most max characters (0: any number)
static void put_string(buf *b, const char *s, int max) {
  int chars = 0;
  for (const char *c = s; *c; c++) chars += ((unsigned char)*c & 0xc0) != 0x80;
  int keep = max && chars > max ? max - 3 : chars;
  puts_(b, "\"");
  for (int i = 0; *s && i < keep; i++) {
    unsigned char c = (unsigned char)*s;
    char esc[8];
    if (c == '"' || c == '\\') {
      snprintf(esc, sizeof esc, "\\%c", c);
      puts_(b, esc);
      s++;
    } else if (c < 0x20) {
      snprintf(esc, sizeof esc, "\\u%04x", c);
      puts_(b, esc);
      s++;
    } else {
      size_t len = 1;
      while (((unsigned char)s[len] & 0xc0) == 0x80) len++;
      put(b, s, len);
      s += len;
    }
  }
  if (keep < chars) puts_(b, "...");
  if (chars == 1) puts_(b, " ");
  puts_(b, "\"");
}

size_t discord_activity_json(char *out, size_t n, const discord_activity *a, int pid, unsigned nonce) {
  buf b = {out, n, 0, 0};
  char num[96];
  snprintf(num, sizeof num, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":{\"type\":0", pid);
  puts_(&b, num);
  if (*a->details) {
    puts_(&b, ",\"details\":");
    put_string(&b, a->details, LINE_MAX_CHARS);
  }
  if (*a->state) {
    puts_(&b, ",\"state\":");
    put_string(&b, a->state, LINE_MAX_CHARS);
  }
  if (a->start > 0) {
    snprintf(num, sizeof num, ",\"timestamps\":{\"start\":%lld}", (long long)a->start);
    puts_(&b, num);
  }
  if (*a->image) {
    puts_(&b, ",\"assets\":{\"large_image\":");
    put_string(&b, a->image, 0);
    if (*a->image_text) {
      puts_(&b, ",\"large_text\":");
      put_string(&b, a->image_text, LINE_MAX_CHARS);
    }
    puts_(&b, "}");
  }
  snprintf(num, sizeof num, "}},\"nonce\":\"%u\"}", nonce);
  puts_(&b, num);
  return b.over ? 0 : b.used;
}

size_t discord_frame(uint8_t *out, size_t n, uint32_t op, const char *json) {
  size_t len = strlen(json);
  if (len + 8 > n || len > MESSAGE_MAX) return 0;
  uint32_t l = (uint32_t)len;
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)(op >> (8 * i)), out[4 + i] = (uint8_t)(l >> (8 * i));
  memcpy(out + 8, json, len);
  return len + 8;
}

// ------------------------------------------------------------------------------------------------
// The connection
// ------------------------------------------------------------------------------------------------
static int send_all(int fd, const uint8_t *p, size_t n) {
  while (n) {
    ssize_t w = send(fd, p, n, 0);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return 0;
    p += w, n -= (size_t)w;
  }
  return 1;
}

static int send_message(int fd, uint32_t op, const char *json) {
  uint8_t *m = malloc(strlen(json) + 8);
  size_t n = m ? discord_frame(m, strlen(json) + 8, op, json) : 0;
  int ok = n && send_all(fd, m, n);
  free(m);
  return ok;
}

// n bytes, waiting up to ms for each part of them
static int read_all(int fd, uint8_t *p, size_t n, int ms) {
  while (n) {
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, ms);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return 0;
    ssize_t got = recv(fd, p, n, 0);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) return 0;
    p += got, n -= (size_t)got;
  }
  return 1;
}

// The next message: its opcode, and its JSON in *json (malloc'ed); -1 when the connection is gone or says nonsense
static int read_message(int fd, char **json, int ms) {
  uint8_t h[8];
  *json = NULL;
  if (!read_all(fd, h, 8, ms)) return -1;
  uint32_t op = h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
  uint32_t len = h[4] | (uint32_t)h[5] << 8 | (uint32_t)h[6] << 16 | (uint32_t)h[7] << 24;
  if (len > MESSAGE_MAX) return -1;
  char *body = malloc((size_t)len + 1);
  if (!body || !read_all(fd, (uint8_t *)body, len, 2000)) {
    free(body);
    return -1;
  }
  body[len] = 0;
  *json = body;
  return (int)op;
}

static int connect_at(const char *dir, int i) {
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  size_t l = strlen(dir);
  if (snprintf(addr.sun_path, sizeof addr.sun_path, "%s%sdiscord-ipc-%d", dir, l && dir[l - 1] == '/' ? "" : "/", i) >=
      (int)sizeof addr.sun_path)
    return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);  // a Discord that has quit is an error, not a signal
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr)) {
    close(fd);
    return -1;
  }
  return fd;
}

int discord_connect(const char *dir, const char *application_id) {
  char tmp[1024];
  if (!dir) {
    // the user's temporary folder, whatever TMPDIR the game was started with
    if (!confstr(_CS_DARWIN_USER_TEMP_DIR, tmp, sizeof tmp)) {
      const char *env = getenv("TMPDIR");
      snprintf(tmp, sizeof tmp, "%s", env && *env ? env : "/tmp");
    }
    dir = tmp;
  }
  for (const char *c = application_id; *c; c++)
    if (*c < '0' || *c > '9') return -1;
  if (!*application_id) return -1;
  for (int i = 0; i < 10; i++) {
    int fd = connect_at(dir, i);
    if (fd < 0) continue;
    char hello[128], *reply;
    snprintf(hello, sizeof hello, "{\"v\":1,\"client_id\":\"%s\"}", application_id);
    int op = send_message(fd, OP_HANDSHAKE, hello) ? read_message(fd, &reply, 5000) : -1;
    int ok = op == OP_FRAME && strstr(reply, "\"READY\"");
    if (op >= 0) free(reply);
    if (ok) return fd;
    close(fd);
  }
  return -1;
}

int discord_set_activity(int fd, const discord_activity *a, unsigned nonce) {
  char json[2048];
  return discord_activity_json(json, sizeof json, a, (int)getpid(), nonce) && send_message(fd, OP_FRAME, json);
}

int discord_poll(int fd, char *error, size_t n) {
  if (error && n) *error = 0;
  for (;;) {
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) return 0;
    if (!r) return 1;
    char *json;
    int op = read_message(fd, &json, 2000);
    if (op < 0) return 0;
    int alive = op != OP_CLOSE;
    if (op == OP_PING) alive = send_message(fd, OP_PONG, json);
    if (error && n && (op == OP_CLOSE || strstr(json, "\"evt\":\"ERROR\""))) snprintf(error, n, "%s", json);
    free(json);
    if (!alive) return 0;
  }
}

void discord_close(int fd) {
  if (fd >= 0) close(fd);
}
