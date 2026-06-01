# Event-Driven Asenkron I/O Sunucusu

## Amaç

Tek bir event loop ile birden fazla istemciyi **bloklamadan** yöneten, `select()` tabanlı asenkron broadcast chat sunucusu geliştirmektir.

Geleneksel sunucularda her istemci için ayrı bir thread veya process açılır; bu yaklaşım yüzlerce istemcide bellek ve context-switch maliyetini ciddi ölçüde artırır. Bu proje, tek bir thread'in `select()` mekanizması aracılığıyla 64 istemciyi eş zamanlı ve bloklanmadan nasıl yönetebileceğini göstermektedir. Broadcast chat senaryosu seçilmiştir: bir istemcinin gönderdiği mesaj, bağlı diğer tüm istemcilere anında iletilir.

## Tasarım

```
                  ┌─────────────────────────────┐
                  │        Ana Event Loop        │
                  │  while(running) { select() } │
                  └──────────────┬──────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                  │
        server_fd         client_fd[0]  …  client_fd[63]
              │                  │
          accept()           recv() / send()
              │                  │
       yeni bağlantı         broadcast()
                                 │
                    ┌────────────┴────────────┐
                    │      Logger Thread       │
                    │  pthread + stats_mutex   │
                    │  her 10 sn → server.log  │
                    └─────────────────────────┘
```

Sunucu her iterasyonda tüm soketleri `fd_set`'e ekler, `select()` ile hangisinin hazır olduğunu öğrenir ve yalnızca o soketi işler. Hiçbir soket diğerini bloklamaz.

Ana event loop'tan bağımsız olarak çalışan `logger_thread`, her 10 saniyede bir anlık istatistikleri (aktif istemci, toplam mesaj, bayt, mesaj/sn) `server.log` dosyasına yazar. Logger thread paylaşılan sayaçlara `stats_mutex` ile erişir.

## Kullanılan Sistem Programlama Kavramları

| Kavram | Kullanım yeri |
|---|---|
| **POSIX Soketleri** | `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()` |
| **`select()` (event-driven I/O)** | Ana event loop — tüm fd'leri tek çağrıda izler |
| **Non-blocking soket** | `fcntl(fd, F_SETFL, O_NONBLOCK)` / `ioctlsocket(FIONBIO)` |
| **Thread (`pthread`)** | `logger_thread` — log dosyasına periyodik yazım |
| **Mutex (`pthread_mutex_t`)** | `stats_mutex` — paylaşılan sayaçlara thread-safe erişim |
| **Sinyal yönetimi** | `SIGINT`, `SIGTERM`, `SIGPIPE` — temiz kapanma |
| **Hata yönetimi** | `perror()`, `strerror(errno)`, özel hata mesajları |
| **Loglama** | Konsol (stdout) + `server.log` dosyası |

## Çalıştırma Adımları

### Derleme

**GCC (MinGW / Linux):**
```bash
# Windows
gcc -Wall -O2 -std=c11 -o server.exe server.c -lws2_32 -lpthread
gcc -Wall -O2 -std=c11 -o client.exe client.c -lws2_32

# Linux
gcc -Wall -O2 -std=c11 -o server server.c -lpthread
gcc -Wall -O2 -std=c11 -o client client.c
```

**Make ile:**
```bash
make
```

### Çalıştırma

**Sunucuyu başlat:**
```bash
./server.exe        # Windows
./server            # Linux
```

Sunucu başlayınca `server.log` dosyası oluşur ve her 10 saniyede güncellenir.

**Tek istemci (interaktif):**
```bash
./client.exe
```

**Otomatik test istemcisi** (id=1, 15 mesaj, 200ms aralık):
```bash
./client.exe 1 15 200
```

**10 istemciyi aynı anda başlat (PowerShell):**
```powershell
.\test_clients.ps1
```

**Özel parametre ile:**
```powershell
.\test_clients.ps1 -n 15 -m 20 -d 150
```

## Testler

### 10 İstemci Testi

| Parametre | Değer |
|---|---|
| Eş zamanlı istemci | 10 |
| İstemci başına mesaj | 15 |
| Toplam mesaj | 150 |
| Toplam veri | 6060 bayt |
| Mesaj/saniye | ~0.88 |
| Bloklanma | Yok |

### Örnek Sunucu Çıktısı

```
[+] Client#001 baglandi  IP=127.0.0.1 PORT=52100  (aktif=1)
[+] Client#002 baglandi  IP=127.0.0.1 PORT=52101  (aktif=2)
[MSG] Client#003: Merhaba! Bu Client#003'den 1/15. mesaj.
[-] Client#001 ayrildi  sure=4s  mesaj=15  bayt=450

========== SUNUCU ISTATISTIKLERI ==========
  Calisma suresi  : 10 saniye
  Aktif istemci   : 8 / 64
  Toplam baglanma : 10
  Toplam mesaj    : 120
  Toplam veri     : 3600 bayt
  Mesaj/saniye    : 12.00
===========================================
```

### Örnek `server.log` İçeriği

```
=== Sunucu baslatildi ===
[1748789100] uptime=10s aktif=8 baglanti=10 mesaj=120 bayt=3600 msg/s=12.00
[1748789110] uptime=20s aktif=3 baglanti=10 mesaj=150 bayt=6060 msg/s=7.50
=== Sunucu kapatildi ===
```

Bloklayıcı ve bloklamayan I/O farkının ayrıntılı karşılaştırması için bkz. [`report.txt`](report.txt).

## Karşılaşılan Problemler

**1. Windows/Linux çift platform desteği**
`select()`, soket tipi (`int` vs `SOCKET`) ve non-blocking ayarı (`fcntl` vs `ioctlsocket`) platformlar arasında farklıdır. `#ifdef _WIN32` blokları ve `sock_t` / `CLOSE_SOCK` gibi makrolarla soyutlanarak her iki platform aynı kodla desteklendi.

**2. Bağlantı kopunca yanlış sırada bildirim**
İlk tasarımda istemci slotu temizlenmeden önce diğer istemcilere bildirim gönderiliyordu; bu durum kapatılmış fd'ye `send()` çağrısına yol açıyordu. Soket kapatıldıktan sonra bildirim gönderilecek şekilde sıralama düzeltildi.

**3. `select()` sonrası fd geçerliliği**
Bir istemci veri gönderip hemen ardından bağlantıyı kestiğinde, aynı iterasyonda `FD_ISSET` hâlâ `true` döndürebilir. `recv()` dönüş değeri (`n <= 0`) kontrol edilerek her durumda doğru dal işletilmektedir.

**4. Logger thread ile paylaşılan sayaçlar**
`logger_thread` global istatistikleri (aktif istemci, toplam mesaj vb.) okurken ana event loop bunları değiştirebilir. Tutarsız okumayı önlemek için `pthread_mutex_t stats_mutex` eklendi ve tüm okuma/yazma noktaları kilitleme ile korundu.

## Dosya Yapısı

```
├── server.c           # Event-driven sunucu (select() + logger thread)
├── client.c           # Test istemcisi (otomatik + interaktif)
├── Makefile           # Derleme
├── test_clients.ps1   # 10+ istemci paralel test scripti
├── report.txt         # Blocking vs Non-blocking I/O raporu
└── server.log         # Çalışma zamanı log dosyası (otomatik oluşur)
```

## Gereksinimler

- GCC veya uyumlu C derleyici (C11)
- Windows: Winsock2 (`-lws2_32`), winpthreads (`-lpthread`, MinGW ile gelir)
- Linux: yalnızca `-lpthread`

---

`asynchronous I/O` · `sockets` · `event loop` · `select()` · `pthread` · `mutex` · `scalability`
