CFLAGS=-std=c11 -g -fno-common

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

angcc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): weicc.h

test/%.exe: angcc test/%.c
	$(CC) -o- -E -P -C test/$*.c | ./angcc -o test/$*.s -
	$(CC) -o $@ test/$*.s -xc test/common


test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh

clean:
	rm -rf angcc tmp* $(TESTS) test/*.s test/*.exe
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean
