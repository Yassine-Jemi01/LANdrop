#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define LDP_HEADER_SIZE 16u
#define LDP_NAME_MAX 255u
#define LDP_CHUNK_SIZE (64u * 1024u)

static void put_u16_be(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xffu);
}
static void put_u64_be(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (56u - 8u * i));
}
static uint16_t get_u16_be(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static uint64_t get_u64_be(const unsigned char *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v = (v << 8) | (uint64_t)p[i];
    return v;
}
static int send_all(int fd, const void *data, size_t len) {
    const unsigned char *p = data;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += (size_t)n; len -= (size_t)n;
    }
    return 0;
}
static int recv_all(int fd, void *data, size_t len) {
    unsigned char *p = data;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += (size_t)n; len -= (size_t)n;
    }
    return 0;
}
static int write_all(int fd, const void *data, size_t len) {
    const unsigned char *p = data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += (size_t)n; len -= (size_t)n;
    }
    return 0;
}
static int safe_filename(const char *s, size_t n) {
    if (n == 0 || n > LDP_NAME_MAX) return 0;
    if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}
static const char *base_name(const char *path) {
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a;
    if (!p || (b && b > p)) p = b;
    return p ? p + 1 : path;
}
static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints, *list = NULL, *it;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &list);
    if (rc != 0) { fprintf(stderr, "LANdrop: %s\n", gai_strerror(rc)); return -1; }
    int fd = -1;
    for (it = list; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0) perror("LANdrop: connect");
    return fd;
}
static int listen_tcp(const char *port) {
    struct addrinfo hints, *list = NULL, *it;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int rc = getaddrinfo(NULL, port, &hints, &list);
    if (rc != 0) { fprintf(stderr, "LANdrop: %s\n", gai_strerror(rc)); return -1; }
    int fd = -1;
    for (it = list; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, 1) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0) perror("LANdrop: listen");
    return fd;
}
static int send_file(const char *host, const char *port, const char *path) {
    const char *name = base_name(path);
    size_t name_len = strlen(name);
    if (!safe_filename(name, name_len)) {
        fprintf(stderr, "LANdrop: filename must contain only ASCII letters, digits, '.', '_' or '-'.\n");
        return 1;
    }
    int in = open(path, O_RDONLY);
    if (in < 0) { perror("LANdrop: open source"); return 1; }
    struct stat st;
    if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        fprintf(stderr, "LANdrop: source must be a regular file.\n"); close(in); return 1;
    }
    int sock = connect_tcp(host, port);
    if (sock < 0) { close(in); return 1; }
    unsigned char header[LDP_HEADER_SIZE] = {'L','D','P','0',1,0,0,0,0,0,0,0,0,0,0,0};
    put_u16_be(header + 6, (uint16_t)name_len);
    put_u64_be(header + 8, (uint64_t)st.st_size);
    int ok = send_all(sock, header, sizeof header) == 0 &&
             send_all(sock, name, name_len) == 0;
    unsigned char buf[LDP_CHUNK_SIZE];
    uint64_t remaining = (uint64_t)st.st_size;
    while (ok && remaining) {
        size_t want = remaining > sizeof buf ? sizeof buf : (size_t)remaining;
        ssize_t n = read(in, buf, want);
        if (n < 0) { if (errno == EINTR) continue; ok = 0; break; }
        if (n == 0) { ok = 0; break; }
        if (send_all(sock, buf, (size_t)n) != 0) { ok = 0; break; }
        remaining -= (uint64_t)n;
    }
    shutdown(sock, SHUT_WR);
    close(sock); close(in);
    if (!ok || remaining) { fprintf(stderr, "LANdrop: transfer failed.\n"); return 1; }
    printf("Sent %s (%" PRIu64 " bytes).\n", name, (uint64_t)st.st_size);
    return 0;
}
static int receive_file(const char *port, const char *directory) {
    int server = listen_tcp(port);
    if (server < 0) return 1;
    printf("Waiting for one file on TCP port %s...\n", port); fflush(stdout);
    int sock;
    do { sock = accept(server, NULL, NULL); } while (sock < 0 && errno == EINTR);
    close(server);
    if (sock < 0) { perror("LANdrop: accept"); return 1; }

    unsigned char header[LDP_HEADER_SIZE];
    int ok = recv_all(sock, header, sizeof header) == 0;
    if (!ok || memcmp(header, "LDP0", 4) != 0 || header[4] != 1 || header[5] != 0) {
        fprintf(stderr, "LANdrop: invalid protocol header.\n"); close(sock); return 1;
    }
    uint16_t name_len = get_u16_be(header + 6);
    uint64_t file_size = get_u64_be(header + 8);
    char name[LDP_NAME_MAX + 1];
    if (name_len == 0 || name_len > LDP_NAME_MAX ||
        recv_all(sock, name, name_len) != 0) {
        fprintf(stderr, "LANdrop: invalid filename header.\n"); close(sock); return 1;
    }
    name[name_len] = '\0';
    if (!safe_filename(name, name_len)) {
        fprintf(stderr, "LANdrop: unsafe or unsupported filename.\n"); close(sock); return 1;
    }
    struct stat dst;
    if (stat(directory, &dst) != 0 || !S_ISDIR(dst.st_mode)) {
        fprintf(stderr, "LANdrop: destination directory does not exist.\n"); close(sock); return 1;
    }
    size_t template_len = strlen(directory) + sizeof "/.landrop-XXXXXX";
    char *temp = malloc(template_len);
    if (!temp) { close(sock); return 1; }
    (void)snprintf(temp, template_len, "%s/.landrop-XXXXXX", directory);
    int out = mkstemp(temp);
    if (out < 0) { perror("LANdrop: create temporary file"); free(temp); close(sock); return 1; }
    (void)fchmod(out, 0600);

    unsigned char buf[LDP_CHUNK_SIZE];
    uint64_t remaining = file_size;
    while (remaining) {
        size_t want = remaining > sizeof buf ? sizeof buf : (size_t)remaining;
        ssize_t n = recv(sock, buf, want, 0);
        if (n < 0) { if (errno == EINTR) continue; ok = 0; break; }
        if (n == 0) { ok = 0; break; }
        if (write_all(out, buf, (size_t)n) != 0) { ok = 0; break; }
        remaining -= (uint64_t)n;
    }
    if (fsync(out) != 0) ok = 0;
    if (close(out) != 0) ok = 0;
    close(sock);
    if (ok && remaining == 0) {
        size_t final_len = strlen(directory) + 1 + strlen(name) + 1;
        char *final_path = malloc(final_len);
        if (!final_path) ok = 0;
        else {
            (void)snprintf(final_path, final_len, "%s/%s", directory, name);
            /* link() makes publication fail rather than overwrite an existing file. */
            if (link(temp, final_path) != 0) {
                if (errno == EEXIST) fprintf(stderr, "LANdrop: destination already exists: %s\n", final_path);
                else perror("LANdrop: publish file");
                ok = 0;
            }
            free(final_path);
        }
    }
    if (ok && remaining == 0) {
        (void)unlink(temp);
        printf("Received %s (%" PRIu64 " bytes).\n", name, file_size);
    } else {
        (void)unlink(temp);
        fprintf(stderr, "LANdrop: incomplete transfer; temporary file removed.\n");
    }
    free(temp);
    return ok && remaining == 0 ? 0 : 1;
}
static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s send <host> <port> <file>\n"
        "  %s receive <port> <destination-directory>\n", argv0, argv0);
}
int main(int argc, char **argv) {
    if (argc == 5 && strcmp(argv[1], "send") == 0)
        return send_file(argv[2], argv[3], argv[4]);
    if (argc == 4 && strcmp(argv[1], "receive") == 0)
        return receive_file(argv[2], argv[3]);
    usage(argv[0]);
    return 2;
}
