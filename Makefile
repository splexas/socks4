CC = clang
CFLAGS = -g -O3 -Wall -Wextra -levent

BUILD_DIR = build/
BINARY_OUT = socks4

OBJFILES := $(BUILD_DIR)main.o $(BUILD_DIR)socks4.o

.PHONY: server
server: $(OBJFILES)
	$(CC) $(CFLAGS) $^ -o $(BINARY_OUT)

$(BUILD_DIR)%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -g -Wall -Wextra -c $< -o $@

.PHONY: clean
clean:
	@rm -r $(BUILD_DIR)* $(BINARY_OUT)