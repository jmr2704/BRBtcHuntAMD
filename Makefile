# ── Makefile — BRBtcHuntAMD ───────────────────────────────────────────
# Default backend: HIP/ROCm.
# Targets: AMD Radeon RX 6600 (gfx1032), Vega iGPU (gfx90c)

TARGET      := BRBtcHuntAMD
SRC_DIR     := src
INC_DIR     := include
OBJ_DIR     := obj

# GPUWorker.cpp includes HashPipeline.cpp as a single TU
SRCS        := $(SRC_DIR)/main.cpp $(SRC_DIR)/GPUWorker.cpp
OBJS        := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

CC          := hipcc

# ── GPU architectures ─────────────────────────────────────────────────
# Auto-detect via rocminfo (recommended), or fallback to common targets.
# Run: make auto
GPU_ARCHS  ?= $(shell rocminfo 2>/dev/null | grep -oP 'gfx\w+' | sort -u | tr '\n' ' ')
ifeq ($(GPU_ARCHS),)
  GPU_ARCHS := gfx1032 gfx90c gfx1010 gfx906
endif

GENCODE     = $(foreach arch,$(GPU_ARCHS),--offload-arch=$(arch))

# ── Compiler flags ────────────────────────────────────────────────────
HIP_FLAGS_BASE := -O3 -ffast-math -std=c++17
HIP_FLAGS_BASE += -I$(INC_DIR)
HIP_FLAGS_BASE += -DBTC_GPU_BACKEND_HIP=1
HIP_FLAGS_BASE += -Wno-unused-result -Wno-ignored-attributes
HIP_FLAGS       = $(HIP_FLAGS_BASE) $(GENCODE)

# Linker flags. Linux ROCm commonly needs libamdhip64 explicitly; Windows
# HIP SDK builds should let hipcc discover the SDK libraries from its env.
ROCM_PATH  ?= /opt/rocm
ifeq ($(OS),Windows_NT)
LDFLAGS    ?=
else
LDFLAGS    ?= -L$(ROCM_PATH)/lib -lamdhip64 -lcrypto
endif

all: $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(HIP_FLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CC) $(HIP_FLAGS) -c $< -o $@

# ── Phony targets ────────────────────────────────────────────────────
.PHONY: clean run info auto

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET) --help

info:
	@echo "=== AMD GPU Info ==="
	rocminfo 2>/dev/null | grep -E "Name:|Marketing Name:|Compute Unit:" || echo "rocminfo not found"
	@echo "=== HIP Version ==="
	hipcc --version 2>/dev/null | head -2 || echo "hipcc not found"

# Debug build
debug: HIP_FLAGS_BASE := -O0 -g -std=c++17 -I$(INC_DIR) -DBTC_GPU_BACKEND_HIP=1 -Wno-unused-result
debug: clean $(TARGET)

# Single GPU architecture (fast build)
.PHONY: fast
fast: GPU_ARCHS := gfx1032
fast: clean $(TARGET)

# cpu target removed — BRBtcHuntAMD is GPU-only

# Auto-detect GPU architectures and build
auto:
	@echo "=== Detectando GPUs ==="
	rocminfo 2>/dev/null | grep -oP 'gfx\w+' | sort -u || echo "rocminfo não disponível"
	@echo ""
	$(MAKE) clean
	GPU_ARCHS="$$(rocminfo 2>/dev/null | grep -oP 'gfx\w+' | sort -u | tr '\n' ' ')" $(MAKE) all
