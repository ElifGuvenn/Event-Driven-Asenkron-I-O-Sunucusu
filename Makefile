# ============================================================
# Event-Driven Asenkron I/O Sunucusu — Makefile
# Derleyici: GCC (MinGW/MSYS2 Windows veya GCC Linux/Mac)
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
LDFLAGS =

# Windows tespiti
ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32 -lpthread
    EXT      = .exe
    RM       = del /Q
    SEP      = \\
else
    CFLAGS  += -D_POSIX_C_SOURCE=200112L
    LDFLAGS += -lpthread
    EXT      =
    RM       = rm -f
    SEP      = /
endif

TARGETS = server$(EXT) client$(EXT)

.PHONY: all clean help

all: $(TARGETS)
	@echo ""
	@echo "Derleme tamamlandi:"
	@echo "  server$(EXT)  -  Event-driven sunucu"
	@echo "  client$(EXT)  -  Test istemcisi"
	@echo ""
	@echo "Baslat : server$(EXT)"
	@echo "Test   : client$(EXT) 1 10 200"

server$(EXT): server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "  [OK] $@"

client$(EXT): client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "  [OK] $@"

clean:
	$(RM) $(foreach t,$(TARGETS),$(t)) 2>nul || true

help:
	@echo "Kullanim:"
	@echo "  make          -> server ve client derle"
	@echo "  make clean    -> derlenenleri sil"
	@echo ""
	@echo "Calistirma:"
	@echo "  ./server$(EXT)              -> sunucuyu baslat"
	@echo "  ./client$(EXT)              -> interaktif istemci"
	@echo "  ./client$(EXT) 1 20 150     -> otomatik: id=1, 20 mesaj, 150ms"
