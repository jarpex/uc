NAME = uc
TEST_NAME = uc_test

CC = cc
COSMOCC ?= cosmocc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I.

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

OBJS = uc.o main.o

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

cosmo: $(NAME).exe

$(NAME).exe: uc.c main.c uc.h
	$(COSMOCC) $(CFLAGS) -o $@ uc.c main.c

uc.o: uc.c uc.h
	$(CC) $(CFLAGS) -c uc.c

main.o: main.c uc.h
	$(CC) $(CFLAGS) -c main.c

$(TEST_NAME): test.c uc.c uc.h
	$(CC) $(CFLAGS) -o $@ test.c uc.c

test: $(TEST_NAME) $(NAME)
	./$(TEST_NAME)
	sh test-cli.sh

install: $(NAME) uc.1
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	cp $(NAME) $(DESTDIR)$(BINDIR)/$(NAME)
	cp uc.1 $(DESTDIR)$(MANDIR)/$(NAME).1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME) $(DESTDIR)$(MANDIR)/$(NAME).1

clean:
	rm -f $(NAME) $(NAME).exe $(TEST_NAME) $(OBJS) *.aarch64.elf *.com.dbg

.PHONY: all clean test install uninstall cosmo
