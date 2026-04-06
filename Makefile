# IMS / z/OS Mainframe Simulator Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -I$(SRC_DIR)
LDFLAGS =

SRC_DIR   = src
BUILD_DIR = build
BIN_DIR   = .

CORE_SRC = $(SRC_DIR)/core/ims_system.c \
           $(SRC_DIR)/core/database.c \
           $(SRC_DIR)/core/dli_calls.c \
           $(SRC_DIR)/core/ssa_parser.c

TM_SRC = $(SRC_DIR)/tm/msgqueue.c \
         $(SRC_DIR)/tm/mpp.c \
         $(SRC_DIR)/tm/bmp.c

UI_SRC = $(SRC_DIR)/ui/terminal.c \
         $(SRC_DIR)/ui/ispf.c

ZOS_SRC = $(SRC_DIR)/zos/address_space.c \
          $(SRC_DIR)/zos/svc.c \
          $(SRC_DIR)/zos/zos_init.c \
          $(SRC_DIR)/zos/console.c \
          $(SRC_DIR)/zos/as_monitor.c

DATASETS_SRC = $(SRC_DIR)/datasets/catalog.c \
               $(SRC_DIR)/datasets/ps.c \
               $(SRC_DIR)/datasets/pds.c \
               $(SRC_DIR)/datasets/vsam.c \
               $(SRC_DIR)/datasets/gdg.c \
               $(SRC_DIR)/datasets/idcams.c

MAIN_SRC = $(SRC_DIR)/main.c

ALL_SRC = $(CORE_SRC) $(TM_SRC) $(UI_SRC) $(ZOS_SRC) $(DATASETS_SRC) $(MAIN_SRC)

OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(ALL_SRC))

TARGET = $(BIN_DIR)/ims.exe

all: dirs $(TARGET)

TEST_DATASETS_SRC = tests/test_datasets.c $(DATASETS_SRC)
TEST_DATASETS_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(DATASETS_SRC))

dirs:
	mkdir -p $(BUILD_DIR)/core $(BUILD_DIR)/tm $(BUILD_DIR)/ui $(BUILD_DIR)/zos $(BUILD_DIR)/datasets

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "========================"
	@echo "Build successful: $(TARGET)"
	@echo "========================"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Clean complete"

run: $(TARGET)
	./$(TARGET)

demo: $(TARGET)
	./$(TARGET) -l

batch: $(TARGET)
	./$(TARGET) -b -l

zos: $(TARGET)
	./$(TARGET) -z

test_datasets: dirs $(TEST_DATASETS_OBJ)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/test_datasets tests/test_datasets.c $(TEST_DATASETS_OBJ) $(LDFLAGS)
	@echo "test_datasets built"

test: test_datasets
	./test_datasets

help:
	@echo "IBM z/OS Mainframe Simulator"
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build (default)"
	@echo "  clean  - Remove build files"
	@echo "  run    - Interactive mode"
	@echo "  demo   - Load HOSPITAL DB and run"
	@echo "  batch  - Batch demo"
	@echo "  zos    - z/OS IPL and AS Monitor"
	@echo "  help   - This help"

.PHONY: all clean run demo batch zos test test_datasets help dirs
