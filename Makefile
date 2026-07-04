# ── Makefile — BRBtcHuntAMD (HIP/ROCm port of CUDACyclone) ────────────
# Targets: AMD Radeon RX 6600 (gfx1032), Vega iGPU (gfx90c)

TARGET      := BRBtcHuntAMD
SRC_DIR     := src
INC_DIR     := include

SRC         := $(SRC_DIR)/AMDcyclone.cpp
OBJ         := $(SRC:.cpp=.o)

CC          := hipcc

# ── GPU architectures ─────────────────────────────────────────────────
# gfx1032 = RDNA 2 (RX 6600), gfx90c = Vega iGPU (Ryzen 5600G)
# Detect automatically via rocminfo, or use pre-defined
GPU_ARCHS  ?= gfx1032 gfx90c

GENCODE    := $(foreach arch,$(GPU_ARCHS),--offload-arch=$(arch))

# ── Compiler flags ────────────────────────────────────────────────────
HIP_FLAGS  := -O3 -ffast-math -std=c++17
HIP_FLAGS  += -I$(INC_DIR)
HIP_FLAGS  += $(GENCODE)
HIP_FLAGS  += -Wno-unused-result -Wno-ignored-attributes

# Linker flags
LDFLAGS    := -L/opt/rocm/lib -lamdhip64

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(HIP_FLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) $(HIP_FLAGS) -c $< -o $@

# ── Phony targets ────────────────────────────────────────────────────
.PHONY: clean run info

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET) --help

info:
	@echo "=== AMD GPU Info ==="
	rocminfo 2>/dev/null | grep -E "Name:|Marketing Name:|Compute Unit:" || echo "rocminfo not found"
	@echo "=== HIP Version ==="
	hipcc --version 2>/dev/null | head -2 || echo "hipcc not found"

# Debug build
debug: HIP_FLAGS := -O0 -g -std=c++17 -I$(INC_DIR) $(GENCODE) -Wno-unused-result
debug: clean $(TARGET)

# Single GPU architecture (fast build)
.PHONY: fast
fast: GPU_ARCHS := gfx1032
fast: clean $(TARGET)

cpu:
	$(warning Building for CPU fallback — no GPU acceleration)
	$(CC) $(HIP_FLAGS) --offload-arch=gfx1032 --offload-arch=gfx90c $(SRC) -o $(TARGET) $(LDFLAGS)
