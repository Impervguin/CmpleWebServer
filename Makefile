CC = gcc
CFLAGS = -std=c17 -Werror -Wall -Wextra -Wpedantic -Wfloat-equal -Wfloat-conversion -Wstrict-prototypes -Wvla -Iinc
LDFLAGS = -luuid

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj

SOURCES = $(SRC_DIR)/main.c $(SRC_DIR)/hash.c $(SRC_DIR)/cache/cache.c $(SRC_DIR)/reader/reader.c $(SRC_DIR)/reader/stat.c $(SRC_DIR)/server/request.c $(SRC_DIR)/utils/string.c $(SRC_DIR)/utils/date.c
OBJECTS = $(OBJ_DIR)/main.o $(OBJ_DIR)/hash.o $(OBJ_DIR)/cache.o $(OBJ_DIR)/reader.o $(OBJ_DIR)/stat.o $(OBJ_DIR)/request.o $(OBJ_DIR)/string.o $(OBJ_DIR)/date.o
EXECUTABLE = main.app

TEST_SOURCES = $(SRC_DIR)/test_runner.c $(SRC_DIR)/cache/test_cache.c $(SRC_DIR)/test_hash.c $(SRC_DIR)/reader/test_reader.c
TEST_OBJECTS = $(OBJ_DIR)/test_runner.o $(OBJ_DIR)/test_cache.o $(OBJ_DIR)/test_hash.o $(OBJ_DIR)/test_reader.o
TEST_EXECUTABLE = test.app

.PHONY: all clean test all-debug

all: $(EXECUTABLE)

all-debug: CFLAGS += -g
all-debug: $(EXECUTABLE)

test: CFLAGS += -g
test: $(TEST_EXECUTABLE)

test-debug: CFLAGS += -g
test-debug: $(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(TEST_OBJECTS) $(OBJ_DIR)/cache.o $(OBJ_DIR)/hash.o $(OBJ_DIR)/reader.o $(OBJ_DIR)/stat.o
	$(CC) $(LDFLAGS) $^ -o $@ -lcheck -lpthread

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cache.o: $(SRC_DIR)/cache/cache.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/reader.o: $(SRC_DIR)/reader/reader.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/stat.o: $(SRC_DIR)/reader/stat.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/request.o: $(SRC_DIR)/server/request.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/string.o: $(SRC_DIR)/utils/string.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/date.o: $(SRC_DIR)/utils/date.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_cache.o: $(SRC_DIR)/cache/test_cache.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_runner.o: $(SRC_DIR)/test_runner.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_hash.o: $(SRC_DIR)/test_hash.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_reader.o: $(SRC_DIR)/reader/test_reader.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(EXECUTABLE) $(TEST_EXECUTABLE)
