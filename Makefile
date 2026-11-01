CC = gcc
CFLAGS = -std=gnu11 -Wall -Wextra

PROGRAMS = problem1 minifs
ifneq ($(wildcard problem3.c),)
PROGRAMS += problem3
endif

.PHONY: all clean
all: $(PROGRAMS)

problem1: problem1.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -pthread $(LDLIBS)

minifs: problem2.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

problem3: problem3.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) problem1 minifs problem3
