/*
 * Event-Driven Asenkron I/O Sunucusu
 * Mekanizma : select()
 * Senaryo   : Broadcast Chat Server (cok istemcili mesajlasma)
 * Platform  : Windows (Winsock2) / Linux (POSIX)
 */

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #include <windows.h>
    typedef SOCKET  sock_t;
    #define INVALID_SOCK    INVALID_SOCKET
    #define CLOSE_SOCK(s)   closesocket(s)
    #define SOCK_ERR        SOCKET_ERROR
    #define set_nonblocking(s) do { u_long m=1; ioctlsocket(s,FIONBIO,&m); } while(0)
    #define inet_ntop_compat(af,src,dst,sz) InetNtopA(af,src,dst,sz)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <signal.h>
    typedef int     sock_t;
    #define INVALID_SOCK    (-1)
    #define CLOSE_SOCK(s)   close(s)
    #define SOCK_ERR        (-1)
    #define set_nonblocking(s) do { int f=fcntl(s,F_GETFL,0); fcntl(s,F_SETFL,f|O_NONBLOCK); } while(0)
    #define inet_ntop_compat(af,src,dst,sz) inet_ntop(af,src,dst,sz)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

/* ---- Sabitler ---- */
#define PORT            8080
#define MAX_CLIENTS     64
#define BUFFER_SIZE     1024
#define STATS_INTERVAL  10      /* saniyede bir istatistik yazdir */
#define BACKLOG         16
#define LOG_FILE        "server.log"

/* ---- Istemci kaydi ---- */
typedef struct {
    sock_t  fd;
    struct  sockaddr_in addr;
    char    name[32];           /* Client#N */
    time_t  connect_time;
    long long bytes_rx;
    int     msg_count;
    int     active;
} Client;

/* ---- Global durum ---- */
static Client   clients[MAX_CLIENTS];
static int      active_count   = 0;
static int      total_conns    = 0;
static long long total_msgs    = 0;
static long long total_bytes   = 0;
static time_t   server_start;
static volatile int running    = 1;

static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       logger_tid;
static int             logger_running = 0;

/* ---- Yardimci: istemci slotu bul ---- */
static int find_slot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!clients[i].active) return i;
    return -1;
}

/* ---- Yardimci: mesaji gonder ---- */
static void safe_send(sock_t fd, const char *msg, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(fd, msg + sent, len - sent, 0);
        if (r <= 0) break;
        sent += r;
    }
}

/* ---- Broadcast: gonderen haric herkese ilet ---- */
static void broadcast(int sender, const char *buf, int len) {
    char out[BUFFER_SIZE + 64];
    int olen = snprintf(out, sizeof(out), "[%s] %.*s",
                        clients[sender].name, len, buf);
    /* Satir sonunu garantile */
    if (olen > 0 && out[olen-1] != '\n' && olen < (int)sizeof(out)-1) {
        out[olen++] = '\n';
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && i != sender)
            safe_send(clients[i].fd, out, olen);
    }
}

/* ---- Herkese (gonderen dahil) bildirim gonder ---- */
static void notify_all(const char *msg) {
    int len = (int)strlen(msg);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active)
            safe_send(clients[i].fd, msg, len);
}

/* ---- Logger thread: istatistikleri server.log dosyasina yazar ---- */
static void *logger_thread(void *arg) {
    (void)arg;
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        fprintf(stderr, "HATA: %s acilamadi: %s\n", LOG_FILE, strerror(errno));
        return NULL;
    }
    fprintf(f, "=== Sunucu baslatildi ===\n");
    fflush(f);

    while (running) {
        /* 1'er saniye uyuyarak STATS_INTERVAL saniye bekle,
           running kapandiginda en fazla 1 sn'de cik */
        for (int i = 0; i < STATS_INTERVAL && running; i++) {
#ifdef _WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
        }
        if (!running) break;

        pthread_mutex_lock(&stats_mutex);
        double uptime = difftime(time(NULL), server_start);
        fprintf(f, "[%ld] uptime=%.0fs aktif=%d baglanti=%d mesaj=%lld bayt=%lld",
                (long)time(NULL), uptime, active_count, total_conns, total_msgs, total_bytes);
        if (uptime > 0)
            fprintf(f, " msg/s=%.2f", (double)total_msgs / uptime);
        fprintf(f, "\n");
        fflush(f);
        pthread_mutex_unlock(&stats_mutex);
    }

    fprintf(f, "=== Sunucu kapatildi ===\n");
    fclose(f);
    return NULL;
}

/* ---- Istatistik yazdir ---- */
static void print_stats(void) {
    pthread_mutex_lock(&stats_mutex);
    double    uptime = difftime(time(NULL), server_start);
    int       ac     = active_count;
    int       tc     = total_conns;
    long long tm     = total_msgs;
    long long tb     = total_bytes;
    pthread_mutex_unlock(&stats_mutex);

    printf("\n========== SUNUCU ISTATISTIKLERI ==========\n");
    printf("  Calisma suresi  : %.0f saniye\n", uptime);
    printf("  Aktif istemci   : %d / %d\n", ac, MAX_CLIENTS);
    printf("  Toplam baglanma : %d\n", tc);
    printf("  Toplam mesaj    : %lld\n", tm);
    printf("  Toplam veri     : %lld bayt\n", tb);
    if (uptime > 0)
        printf("  Mesaj/saniye    : %.2f\n", (double)tm / uptime);
    printf("===========================================\n\n");
}

/* ---- Yeni baglanti kabul et ---- */
static void handle_new_connection(sock_t server_fd) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);

    sock_t cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
    if (cfd == INVALID_SOCK) return;

    int slot = find_slot();
    if (slot < 0) {
        /* Kapasite dolu */
        safe_send(cfd, "HATA: Sunucu kapasitesi dolu. Sonra tekrar deneyin.\n", 51);
        CLOSE_SOCK(cfd);
        return;
    }

    set_nonblocking(cfd);

    clients[slot].fd           = cfd;
    clients[slot].addr         = caddr;
    clients[slot].connect_time = time(NULL);
    clients[slot].bytes_rx     = 0;
    clients[slot].msg_count    = 0;
    clients[slot].active       = 1;
    pthread_mutex_lock(&stats_mutex);
    snprintf(clients[slot].name, sizeof(clients[slot].name),
             "Client#%03d", ++total_conns);
    active_count++;
    pthread_mutex_unlock(&stats_mutex);

    char ip[INET_ADDRSTRLEN];
    inet_ntop_compat(AF_INET, &caddr.sin_addr, ip, sizeof(ip));

    printf("[+] %-12s baglandi  IP=%-15s PORT=%d  (aktif=%d)\n",
           clients[slot].name, ip, ntohs(caddr.sin_port), active_count);

    /* Hosgeldin mesaji */
    char welcome[200];
    int wlen = snprintf(welcome, sizeof(welcome),
        "=== Hosgeldiniz, %s! Sunucuda %d istemci var. ===\n",
        clients[slot].name, active_count);
    safe_send(cfd, welcome, wlen);

    /* Diger istemcilere bildir */
    char notif[128];
    snprintf(notif, sizeof(notif), "*** %s sohbete katildi ***\n", clients[slot].name);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && i != slot)
            safe_send(clients[i].fd, notif, (int)strlen(notif));
}

/* ---- Istemciden veri oku ---- */
static void handle_client_data(int idx) {
    char buf[BUFFER_SIZE];
    int  n = recv(clients[idx].fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        /* Baglanti kesildi */
        time_t session = time(NULL) - clients[idx].connect_time;
        printf("[-] %-12s ayrildi  sure=%lds  mesaj=%d  bayt=%lld\n",
               clients[idx].name, (long)session,
               clients[idx].msg_count, clients[idx].bytes_rx);

        char notif[128];
        snprintf(notif, sizeof(notif), "*** %s sohbetten ayrildi ***\n", clients[idx].name);

        CLOSE_SOCK(clients[idx].fd);
        clients[idx].active = 0;
        clients[idx].fd     = INVALID_SOCK;
        pthread_mutex_lock(&stats_mutex);
        active_count--;
        pthread_mutex_unlock(&stats_mutex);

        /* Bildirim, istemci kapatildiktan sonra gider */
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].active)
                safe_send(clients[i].fd, notif, (int)strlen(notif));
        return;
    }

    buf[n] = '\0';
    pthread_mutex_lock(&stats_mutex);
    clients[idx].bytes_rx  += n;
    clients[idx].msg_count++;
    total_msgs++;
    total_bytes += n;
    pthread_mutex_unlock(&stats_mutex);

    /* Satir sonunu temizle (logda duzgun gorunsun) */
    char log_buf[BUFFER_SIZE];
    strncpy(log_buf, buf, sizeof(log_buf)-1);
    log_buf[sizeof(log_buf)-1] = '\0';
    int lb = (int)strlen(log_buf);
    while (lb > 0 && (log_buf[lb-1] == '\n' || log_buf[lb-1] == '\r')) lb--;
    log_buf[lb] = '\0';


    printf("[MSG] %-12s: %s\n", clients[idx].name, log_buf);

    /* Tum diger istemcilere ilet */
    broadcast(idx, buf, n);
}

/* ---- Ctrl+C yakalayici (Windows) ---- */
#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        running = 0;
        return TRUE;
    }
    return FALSE;
}
#else
static void sig_handler(int s) { (void)s; running = 0; }
#endif

/* ================================================================
   ANA FONKSIYON — EVENT LOOP
   ================================================================ */
int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup basarisiz\n");
        return 1;
    }
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);   /* kopuk baglantilarda crash olmasi */
#endif

    server_start = time(NULL);

    /* Tum istemci slotlarini sifirla */
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = INVALID_SOCK;

    /* ---- Sunucu soketi olustur ---- */
    sock_t server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCK) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
    set_nonblocking(server_fd);

    struct sockaddr_in saddr = {0};
    saddr.sin_family      = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&saddr, sizeof(saddr)) == SOCK_ERR) {
        perror("bind");
        CLOSE_SOCK(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) == SOCK_ERR) {
        perror("listen");
        CLOSE_SOCK(server_fd);
        return 1;
    }

    printf("============================================\n");
    printf("  Event-Driven Asenkron I/O Sunucusu\n");
    printf("  Mekanizma : select()\n");
    printf("  Port      : %d\n", PORT);
    printf("  Kapasite  : %d istemci\n", MAX_CLIENTS);
    printf("  Mod       : Broadcast Chat\n");
    printf("  Log dosyasi: %s\n", LOG_FILE);
    printf("  Cikis     : Ctrl+C\n");
    printf("============================================\n\n");

    /* Logger thread'i baslat */
    if (pthread_create(&logger_tid, NULL, logger_thread, NULL) == 0) {
        logger_running = 1;
        printf("[LOG] Logger thread baslatildi -> %s\n\n", LOG_FILE);
    } else {
        perror("pthread_create");
    }

    time_t last_stats = time(NULL);

    /* ================================================================
       SELECT() TABANLI ANA EVENT DONGUSU
       ================================================================
       Her iterasyonda:
         1. fd_set'i sifirla, server + aktif istemcileri ekle
         2. select() ile olay bekle (1 sn timeout)
         3. Server fd'de olay varsa -> yeni baglanti kabul et
         4. Her istemci fd'sinde olay varsa -> veri oku / kopuklugu isle
         5. Periyodik istatistik yazdir
    */
    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        sock_t max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > max_fd)
                    max_fd = clients[i].fd;
            }
        }

        struct timeval tv = {1, 0};   /* 1 saniye timeout */
        int activity = select((int)(max_fd + 1), &rfds, NULL, NULL, &tv);

        if (activity < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            perror("select");
            break;
        }

        /* Periyodik istatistik */
        time_t now = time(NULL);
        if (difftime(now, last_stats) >= STATS_INTERVAL) {
            print_stats();
            last_stats = now;
        }

        if (activity == 0) continue;  /* timeout — olay yok */

        /* Yeni baglanti olayi */
        if (FD_ISSET(server_fd, &rfds))
            handle_new_connection(server_fd);

        /* Istemci olaylari */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].fd, &rfds))
                handle_client_data(i);
        }
    }

    /* ---- Temiz kapanma ---- */
    printf("\nSunucu kapatiliyor...\n");
    notify_all("*** Sunucu kapatiliyor. Baglanti sonlandiriliyor. ***\n");
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active) CLOSE_SOCK(clients[i].fd);
    CLOSE_SOCK(server_fd);
    print_stats();

    if (logger_running)
        pthread_join(logger_tid, NULL);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
