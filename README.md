# Event-Driven Asenkron I/O Sunucusu

Tek bir event loop ile birden fazla istemciyi **bloklamadan** yöneten, `select()` tabanlı asenkron broadcast chat sunucusu.

## Özellikler

- `select()` mekanizması ile çok istemcili I/O yönetimi
- Tek thread, tek event loop — istemci başına thread yok
- Broadcast sistemi: bir istemcinin mesajı diğer herkese iletilir
- Bağlantı, veri ve kopma olaylarının doğru yönetimi
- Her 10 saniyede otomatik istatistik (aktif istemci, mesaj/sn, toplam bayt)
- 10+ istemci ile eş zamanlı test desteği
- Windows (Winsock2) ve Linux (POSIX) uyumlu

## Dosya Yapısı

```
├── server.c           # Event-driven sunucu (select())
├── client.c           # Test istemcisi (otomatik + interaktif)
├── Makefile           # Derleme
├── test_clients.ps1   # 10+ istemci paralel test scripti
└── report.txt         # Blocking vs Non-blocking I/O raporu
```

## Derleme

**GCC (MinGW / Linux):**
```bash
gcc -Wall -O2 -o server.exe server.c -lws2_32   # Windows
gcc -Wall -O2 -o client.exe client.c -lws2_32

gcc -Wall -O2 -o server server.c                 # Linux
gcc -Wall -O2 -o client client.c
```

**Make ile:**
```bash
make
```

## Çalıştırma

**Sunucuyu başlat:**
```bash
./server.exe        # Windows
./server            # Linux
```

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

## Test Sonuçları

| Parametre | Değer |
|---|---|
| Eş zamanlı istemci | 10 |
| İstemci başına mesaj | 15 |
| Toplam mesaj | 150 |
| Toplam veri | 6060 bayt |
| Mesaj/saniye | ~0.88 |
| Bloklanma | Yok |

## Mimari

```
[ select() Event Loop ]
        │
   ┌────┴────┐
   │         │
server_fd  client_fd[0..63]
   │         │
 accept()  recv() / send()
   │         │
yeni bağlantı  broadcast
```

Sunucu her iterasyonda tüm soketleri `fd_set`'e ekler, `select()` ile hangisinin hazır olduğunu öğrenir ve yalnızca o soketi işler. Hiçbir soket diğerini bloklamaz.

## Gereksinimler

- GCC veya uyumlu C derleyici (C11)
- Windows: Winsock2 (`-lws2_32`)
- Linux: ek kütüphane gerekmez

## Konu

`asynchronous I/O` · `sockets` · `event loop` · `select()` · `scalability`
