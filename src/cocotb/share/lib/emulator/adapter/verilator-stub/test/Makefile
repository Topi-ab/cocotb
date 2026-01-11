.PHONY: all verilate emulator adapter verilator_root run clean rebuild

REPO_ROOT := $(shell git -C $(CURDIR) rev-parse --show-toplevel)
SCRIPT_DIR := $(CURDIR)
STUB_DIR := $(CURDIR)/..
RTL_DIR := $(SCRIPT_DIR)/rtl
OUT_DIR := $(STUB_DIR)/tmp
LIB_OUT := $(REPO_ROOT)/src/cocotb/libs/libemu_adapter.so
LIB_OUT_TMP := $(OUT_DIR)/libemu_adapter.so
ADAPTER_OBJ := $(OUT_DIR)/verilator_adapter.o
ADAPTER_DEP := $(OUT_DIR)/verilator_adapter.d

VERILATOR_ROOT ?= $(shell verilator -V | sed -n 's/.*VERILATOR_ROOT[[:space:]]*=[[:space:]]*//p' | head -n 1)
VERILATOR_ROOT_FALLBACK := /usr/share/verilator
VERILATOR_ROOT_RESOLVED := $(if $(VERILATOR_ROOT),$(VERILATOR_ROOT),$(VERILATOR_ROOT_FALLBACK))

VTEST_SOURCES := $(OUT_DIR)/Vtest*.cpp
VTEST_MK := $(OUT_DIR)/Vtest.mk
CCACHE := $(shell command -v ccache 2>/dev/null)
EMULATOR_BIN := $(REPO_ROOT)/src/cocotb/libs/emulator

all: run

verilate: $(VTEST_MK)

$(VTEST_MK): $(RTL_DIR)/test.sv | $(OUT_DIR)
	@echo "RTL source dir: $(RTL_DIR)"
	@echo "Output dir: $(OUT_DIR)"
	@echo "Step 1/7: create output dir"
	@echo "Step 2/7: run Verilator to generate C++"
	@time verilator -cc --Mdir $(OUT_DIR) \
	  $(RTL_DIR)/test.sv \
	  --top-module test

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

emulator: $(EMULATOR_BIN)

$(EMULATOR_BIN): $(REPO_ROOT)/src/cocotb/share/lib/emulator/emulator.cpp
	@echo "Step 4/7: build emulator binary"; \
	if command -v ccache >/dev/null 2>&1; then \
	  CXX="ccache g++"; \
	  echo "Using ccache"; \
	else \
	  CXX="g++"; \
	fi; \
	time $$CXX -std=c++20 -O0 -g0 -DNDEBUG \
	  -I"$(REPO_ROOT)/src/cocotb/share/include" \
	  -I"$(REPO_ROOT)/src/cocotb" \
	  -I"$(REPO_ROOT)/src/cocotb/share/lib/gpi" \
	  "$(REPO_ROOT)/src/cocotb/share/lib/emulator/emulator.cpp" \
	  -L"$(REPO_ROOT)/src/cocotb/libs" -lgpi -ldl \
	  -Wl,-rpath,'$$ORIGIN' \
	  -o "$(EMULATOR_BIN)"

verilator_root:
	@echo "Step 3/7: detect VERILATOR_ROOT"
	@test -n "$(VERILATOR_ROOT_RESOLVED)" || (echo "ERROR: VERILATOR_ROOT not set or detected"; exit 1)
	@test -d "$(VERILATOR_ROOT_RESOLVED)" || (echo "ERROR: VERILATOR_ROOT=$(VERILATOR_ROOT_RESOLVED) not found"; exit 1)
	@echo "VERILATOR_ROOT=$(VERILATOR_ROOT_RESOLVED)"

adapter: $(LIB_OUT)

$(LIB_OUT): $(LIB_OUT_TMP)
	@cp "$(LIB_OUT_TMP)" "$(LIB_OUT)"

$(LIB_OUT_TMP): verilate emulator verilator_root $(ADAPTER_OBJ) $(VTEST_MK)
	@echo "Step 6/7: build emulator adapter shared library"
	@rm -f "$(LIB_OUT_TMP)"
	@time $(MAKE) -C $(OUT_DIR) -f Vtest.mk CXX="$(if $(CCACHE),$(CCACHE) ,)g++" USER_CPPFLAGS="-fPIC" libVtest.a libverilated.a
	@$(if $(CCACHE),$(CCACHE) )g++ -shared \
	  $(ADAPTER_OBJ) \
	  $(OUT_DIR)/libVtest.a \
	  $(OUT_DIR)/libverilated.a \
	  -pthread -latomic \
	  -o "$(LIB_OUT_TMP)"

$(ADAPTER_OBJ): $(STUB_DIR)/verilator_adapter.cpp $(VTEST_MK) | verilator_root
	@mkdir -p $(OUT_DIR)
	@time $(if $(CCACHE),$(CCACHE) )g++ -std=c++20 -O0 -g0 -DNDEBUG -fPIC -MMD -MP \
	  -I"$(VERILATOR_ROOT_RESOLVED)/include" \
	  -I"$(OUT_DIR)" \
	  -I"$(REPO_ROOT)/src/cocotb/share/lib/emulator" \
	  -c "$<" -o "$@"

run: $(LIB_OUT) $(EMULATOR_BIN)
	@echo "Step 7/7: run emulator with cocotb test"
	@if [ -f "$(REPO_ROOT)/venv/bin/activate" ]; then \
	  . "$(REPO_ROOT)/venv/bin/activate"; \
	fi; \
	PYGPI_PYTHON_BIN=$${PYGPI_PYTHON_BIN:-$${VIRTUAL_ENV:+$$VIRTUAL_ENV/bin/python}}; \
	PYGPI_PYTHON_BIN=$${PYGPI_PYTHON_BIN:-python3}; \
	export PYGPI_PYTHON_BIN="$$PYGPI_PYTHON_BIN"; \
	export COCOTB_TEST_MODULES=test_counter; \
	export COCOTB_TOPLEVEL=dut; \
	export COCOTB_TOPLEVEL_LANG=verilog; \
	export EMU_VERILATOR_DEBUG=1; \
	export EMU_EMULATOR_DEBUG=1; \
	export PYTHONPATH="$(SCRIPT_DIR)$${PYTHONPATH:+:$$PYTHONPATH}"; \
	export GPI_USERS="$$( $$PYGPI_PYTHON_BIN -m cocotb_tools.config --pygpi-entry-point )"; \
	export EMULATOR_ADAPTER_SO="$(LIB_OUT)"; \
	if [ ! -x "$(REPO_ROOT)/src/cocotb/libs/emulator" ]; then \
	  echo "ERROR: emulator binary not found at $(REPO_ROOT)/src/cocotb/libs/emulator"; \
	  exit 1; \
	fi; \
	"$(REPO_ROOT)/src/cocotb/libs/emulator"

clean:
	rm -rf $(OUT_DIR)

rebuild: clean
	@rm -f $(EMULATOR_BIN) $(LIB_OUT) $(LIB_OUT_TMP) $(ADAPTER_OBJ) $(ADAPTER_DEP)
	@$(MAKE) run

-include $(ADAPTER_DEP)
