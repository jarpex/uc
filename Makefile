NAME = uc
TEST_NAME = uc_test

CC = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I.

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

test: $(TEST_NAME)
	./$(TEST_NAME)

clean:
	rm -f $(NAME) $(TEST_NAME) $(OBJS)

.PHONY: all clean test