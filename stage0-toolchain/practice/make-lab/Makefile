CC		= gcc
CFLAGS	= -Wall -g -MMD -MP
TARGET	= app
OBJS	= main.o a.o b.o
DEPS	= $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

-include $(DEPS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS) *.d