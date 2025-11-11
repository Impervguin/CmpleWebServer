CC = gcc
CFLAGS = -std=c17 -Werror -Wall -Wextra -Wpedantic -Wfloat-equal -Wfloat-conversion -Wstrict-prototypes -Wvla -Iinc
LDFLAGS =

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj

SOURCES = $(SRC_DIR)/main.c $(SRC_DIR)/hash.c $(SRC_DIR)/cache/cache.c
OBJECTS = $(OBJ_DIR)/main.o $(OBJ_DIR)/hash.o $(OBJ_DIR)/cache.o
EXECUTABLE = main.app

TEST_SOURCES = $(SRC_DIR)/test_runner.c $(SRC_DIR)/cache/test_cache.c $(SRC_DIR)/test_hash.c
TEST_OBJECTS = $(OBJ_DIR)/test_runner.o $(OBJ_DIR)/test_cache.o $(OBJ_DIR)/test_hash.o
TEST_EXECUTABLE = test.app

.PHONY: all clean test all-debug

all: $(EXECUTABLE)

all-debug: CFLAGS += -g
all-debug: $(EXECUTABLE)

test: CFLAGS += -g
test: $(TEST_EXECUTABLE)

test-debug: CFLAGS += -g
test-debug: $(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(TEST_OBJECTS) $(OBJ_DIR)/cache.o $(OBJ_DIR)/hash.o
	$(CC) $(LDFLAGS) $^ -o $@ -lcheck -lpthread

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cache.o: $(SRC_DIR)/cache/cache.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_cache.o: $(SRC_DIR)/cache/test_cache.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_runner.o: $(SRC_DIR)/test_runner.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_hash.o: $(SRC_DIR)/test_hash.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(EXECUTABLE) $(TEST_EXECUTABLE)
