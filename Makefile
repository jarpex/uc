NAME = uc
TEST_NAME = uc_test

CC = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I.

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

OBJS = uc.o main.o

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

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
	rm -f $(NAME) $(TEST_NAME) $(OBJS)

.PHONY: all clean test install uninstall