CC = gcc
CFLAGS = -std=c17 -Werror -Wall -Wextra -Wpedantic -Wfloat-equal -Wfloat-conversion -Wstrict-prototypes -Wvla -Iinc 
LDFLAGS = -luuid

SRC_DIR = src
INC_DIR = inc
OBJ_DIR = obj

ALL_SOURCES = $(wildcard $(SRC_DIR)/**/*.c) $(wildcard $(SRC_DIR)/*.c)
TEST_SOURCES = $(wildcard $(SRC_DIR)/**/test_*.c) $(wildcard $(SRC_DIR)/test_*.c)
SOURCES = $(filter-out $(TEST_SOURCES), $(ALL_SOURCES))
MAIN_SOURCES = $(filter-out $(SRC_DIR)/main.c, $(SOURCES))

OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))
TEST_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(TEST_SOURCES))
EXECUTABLE = main.app
TEST_EXECUTABLE = test.app

.PHONY: all clean test all-debug

all: $(EXECUTABLE)

all-debug: CFLAGS += -g
all-debug: $(EXECUTABLE)

test: CFLAGS += -g
test-debug: CFLAGS += -fprofile-arcs -ftest-coverage
test: $(TEST_EXECUTABLE)

test-debug: CFLAGS += -g
test-debug: CFLAGS += -fprofile-arcs -ftest-coverage
test-debug: $(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(TEST_OBJECTS) $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(MAIN_SOURCES))
	$(CC) $(LDFLAGS) $^ -o $@ -lcheck -lpthread -lgcov

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(EXECUTABLE) $(TEST_EXECUTABLE)
	find . -name "*.gcno" -delete
	find . -name "*.gcda" -delete
	find . -name "*.gcov" -delete

