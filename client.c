/*
 * Event-Driven Sunucu — Test Istemcisi
 *
 * Kullanim:
 *   client.exe [id] [mesaj_sayisi] [bekleme_ms]
 *   client.exe 3 20 200   ->  id=3, 20 mesaj, 200ms araliklarla
 *
 * Interaktif mod (arguman yok):
 *   Klavyeden mesaj girip Enter'a basilinca sunucuya gonderir,
 *   ayni anda sunucudan gelen yayinlari ekrana basar.
 *   (select() ile ayni anda stdin + soket izlenir)
 */

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <conio.h>
    typedef SOCKET sock_t;
    #define CLOSE_SOCK(s)  closesocket(s)
    #define SLEEP_MS(ms)   Sleep(ms)
    #define WIN_INTERACTIVE 1
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    typedef int  sock_t;
    #define CLOSE_SOCK(s)  close(s)
    #define SLEEP_MS(ms)   usleep((ms)*1000)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SERVER_IP   "127.0.0.1"
#define PORT        8080
#define BUF_SIZE    1024

/* ---- Soketi non-blocking yap ---- */
static void set_nonblock(sock_t s) {
#ifdef _WIN32
    u_long m = 1;
    ioctlsocket(s, FIONBIO, &m);
#else
    int f = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, f | O_NONBLOCK);
#endif
}

/* ================================================================
   OTOMATIK MOD: id mesaj_sayisi bekleme_ms argumanlari ile calis
   ================================================================ */
static int auto_mode(int id, int num_msgs, int delay_ms) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == (sock_t)(-1)) { fprintf(stderr, "[%d] socket hatasi\n", id); return 1; }

    struct sockaddr_in srv = {0};
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

    if (connect(s, (struct sockaddr *)&srv, sizeof(srv)) != 0) {
        fprintf(stderr, "[Client#%03d] Baglanti kurulamadi (%s:%d)\n", id, SERVER_IP, PORT);
        CLOSE_SOCK(s);
        return 1;
    }

    printf("[Client#%03d] Sunucuya baglandi\n", id);

    /* Hosgeldin mesajini oku */
    char buf[BUF_SIZE];
    int n = recv(s, buf, sizeof(buf)-1, 0);
    if (n > 0) { buf[n] = '\0'; printf("[Client#%03d] << %s", id, buf); }

    srand((unsigned)(time(NULL) + id * 1000));

    for (int i = 1; i <= num_msgs; i++) {
        /* Mesaj gonder */
        char msg[128];
        int mlen = snprintf(msg, sizeof(msg),
            "Merhaba! Bu Client#%03d'den %d/%d. mesaj.\n", id, i, num_msgs);
        send(s, msg, mlen, 0);
        printf("[Client#%03d] >> %s", id, msg);

        /* Sunucudan gelebilecek yayinlari oku (non-blocking) */
        set_nonblock(s);
        n = recv(s, buf, sizeof(buf)-1, 0);
        if (n > 0) { buf[n] = '\0'; printf("[Client#%03d] << %s", id, buf); }

        /* Rastgele gecikme */
        int wait = delay_ms + rand() % (delay_ms + 1);
        SLEEP_MS(wait);
    }

    printf("[Client#%03d] Tamamlandi, baglanti kapatiliyor.\n", id);
    CLOSE_SOCK(s);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

/* ================================================================
   INTERAKTIF MOD: Kullanici klavyeden yazar, sunucudan gelenleri gorur
   ================================================================ */
static int interactive_mode(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in srv = {0};
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

    if (connect(s, (struct sockaddr *)&srv, sizeof(srv)) != 0) {
        fprintf(stderr, "Sunucuya baglanilabilir (%s:%d)\n", SERVER_IP, PORT);
        CLOSE_SOCK(s);
        return 1;
    }

    printf("Sunucuya baglandi. Mesaj yazin (cikmak icin 'quit'):\n");

    char buf[BUF_SIZE];

#ifdef WIN_INTERACTIVE
    /* Windows: soket non-blocking, stdin polling ile calis */
    set_nonblock(s);
    while (1) {
        /* Sunucudan gelen mesajlari oku */
        int n = recv(s, buf, sizeof(buf)-1, 0);
        if (n > 0) {
            buf[n] = '\0';
            printf("\r<< %s> ", buf);
        } else if (n == 0) {
            printf("Sunucu baglantisi kapatti.\n");
            break;
        }

        /* Non-blocking stdin kontrolu (Windows console) */
        if (_kbhit()) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;
            int len = (int)strlen(buf);
            if (strncmp(buf, "quit", 4) == 0) break;
            send(s, buf, len, 0);
        }
        SLEEP_MS(50);
    }
#else
    /* Linux/Mac: select() ile hem stdin hem soket izle */
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(s, &rfds);
        int maxfd = s > STDIN_FILENO ? (int)s : STDIN_FILENO;

        struct timeval tv = {0, 100000}; /* 100ms */
        int activity = select(maxfd+1, &rfds, NULL, NULL, &tv);
        if (activity < 0) break;

        if (FD_ISSET(s, &rfds)) {
            int n = recv(s, buf, sizeof(buf)-1, 0);
            if (n <= 0) { printf("Sunucu baglantisi kapatti.\n"); break; }
            buf[n] = '\0';
            printf("<< %s", buf);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;
            if (strncmp(buf, "quit", 4) == 0) break;
            send(s, buf, (int)strlen(buf), 0);
        }
    }
#endif

    CLOSE_SOCK(s);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

/* ---- main ---- */
int main(int argc, char *argv[]) {
    if (argc >= 2) {
        int id        = atoi(argv[1]);
        int num_msgs  = argc >= 3 ? atoi(argv[2]) : 10;
        int delay_ms  = argc >= 4 ? atoi(argv[3]) : 300;
        return auto_mode(id, num_msgs, delay_ms);
    }
    return interactive_mode();
}
