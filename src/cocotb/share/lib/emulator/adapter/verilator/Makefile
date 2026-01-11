.PHONY: all verilate adapter run verilator_run docker_adapter docker_verilate docker_build docker_run docker_headers fix_vtop_mk local_build clean

REPO_ROOT := $(shell git -C $(CURDIR) rev-parse --show-toplevel)
RTL_DIR := $(CURDIR)/test/rtl
OUT_DIR := $(CURDIR)/tmp
VERILATOR_HEADERS_DIR := $(OUT_DIR)/cocotb-verilator/include
VTOP_MK := $(OUT_DIR)/cocotb-verilator/Vtop.mk
VERILATOR_ROOT_OVERRIDE := $(OUT_DIR)/cocotb-verilator
DOCKER_BUILD_DIR := /work/src/cocotb/share/lib/emulator/adapter/verilator/tmp/cocotb-verilator
ADAPTER_CPP := $(CURDIR)/test/verilator_adapter.cpp
ADAPTER_OBJ := $(OUT_DIR)/verilator_adapter.o
ADAPTER_DEP := $(OUT_DIR)/verilator_adapter.d
CCACHE := $(shell command -v ccache 2>/dev/null)
LIB_OUT := $(REPO_ROOT)/src/cocotb/libs/libemu_adapter.so

VERILATOR_ROOT ?= $(shell verilator -V | sed -n 's/.*VERILATOR_ROOT[[:space:]]*=[[:space:]]*//p' | head -n 1)
VERILATOR_DOCKER_IMAGE ?= verilator/verilator:v5.036
DOCKER_WORKDIR := /work/src/cocotb/share/lib/emulator/adapter/verilator
DOCKER_RUN := docker run --rm -u $$(id -u):$$(id -g) -v $(REPO_ROOT):/work -w $(DOCKER_WORKDIR) --entrypoint /bin/sh $(VERILATOR_DOCKER_IMAGE) -lc

all: adapter

verilate:
	@mkdir -p $(OUT_DIR)
	verilator -cc --Mdir $(OUT_DIR) \
	  $(RTL_DIR)/emulator_wrapper.sv \
	  --top-module emulator_wrapper

adapter: verilate $(ADAPTER_OBJ)
	@test -n "$(VERILATOR_ROOT)" || (echo "ERROR: VERILATOR_ROOT not set or detected"; exit 1)
	$(MAKE) -C $(OUT_DIR) -f Vemulator_wrapper.mk USER_CPPFLAGS="-fPIC" libVemulator_wrapper.a libverilated.a
	$(if $(CCACHE),$(CCACHE) )g++ -shared \
	  $(ADAPTER_OBJ) \
	  $(OUT_DIR)/libVemulator_wrapper.a \
	  $(OUT_DIR)/libverilated.a \
	  -pthread -latomic \
	  -o $(LIB_OUT)

$(ADAPTER_OBJ): $(ADAPTER_CPP)
	@mkdir -p $(OUT_DIR)
	$(if $(CCACHE),$(CCACHE) )g++ -std=c++20 -O0 -g0 -DNDEBUG -fPIC -MMD -MP \
	  -I$(VERILATOR_ROOT)/include \
	  -I$(OUT_DIR) \
	  -I$(REPO_ROOT)/src/cocotb/share/lib/emulator \
	  -c $(ADAPTER_CPP) -o $(ADAPTER_OBJ)

run: adapter
	@test -x $(REPO_ROOT)/src/cocotb/libs/emulator || (echo "ERROR: emulator binary not found at $(REPO_ROOT)/src/cocotb/libs/emulator"; exit 1)
	@PYTHON_BIN="$(REPO_ROOT)/venv/bin/python"; \
	if [ ! -x "$$PYTHON_BIN" ]; then PYTHON_BIN=python3; fi; \
	export PYGPI_PYTHON_BIN="$$PYTHON_BIN"; \
	export COCOTB_TEST_MODULES=tb_try; \
	export COCOTB_TOPLEVEL=dut; \
	export COCOTB_TOPLEVEL_LANG=verilog; \
	export PYTHONPATH="$(CURDIR)/test"; \
	export GPI_USERS="$$( $$PYTHON_BIN -m cocotb_tools.config --pygpi-entry-point )"; \
	export EMULATOR_ADAPTER_SO="$(LIB_OUT)"; \
	"$(REPO_ROOT)/src/cocotb/libs/emulator"

docker_adapter:
	$(DOCKER_RUN) "make adapter"

docker_verilate:
	$(DOCKER_RUN) "make verilate"

docker_build:
	$(DOCKER_RUN) "mkdir -p $(DOCKER_BUILD_DIR) && verilator -cc --exe -Mdir $(DOCKER_BUILD_DIR) \
	  --top-module emulator_wrapper --vpi --public-flat-rw --prefix Vtop -o emulator_wrapper \
	  -LDFLAGS '-Wl,-rpath,/work/src/cocotb/libs -L/work/src/cocotb/libs -lcocotbvpi_verilator' \
	  /work/src/cocotb/share/lib/verilator/verilator.cpp \
	  /work/src/cocotb/share/lib/emulator/adapter/verilator/test/rtl/emulator_wrapper.sv"
docker_headers:
	@mkdir -p $(OUT_DIR)/cocotb-verilator
	$(DOCKER_RUN) "rm -rf /work/$(subst $(REPO_ROOT)/,,$(VERILATOR_HEADERS_DIR))"
	$(DOCKER_RUN) "mkdir -p /work/$(subst $(REPO_ROOT)/,,$(OUT_DIR)/cocotb-verilator) && cp -a /usr/local/share/verilator/include /work/$(subst $(REPO_ROOT)/,,$(VERILATOR_HEADERS_DIR))"
	$(DOCKER_RUN) "mkdir -p /work/$(subst $(REPO_ROOT)/,,$(OUT_DIR)/cocotb-verilator/bin) && cp -a /usr/local/share/verilator/bin/verilator_includer /work/$(subst $(REPO_ROOT)/,,$(OUT_DIR)/cocotb-verilator/bin/)"

docker_run: docker_adapter
	$(MAKE) run

fix_vtop_mk:
	@test -f $(VTOP_MK) || (echo "ERROR: $(VTOP_MK) not found"; exit 1)
	@sed -i 's|^VERILATOR_ROOT[[:space:]]*=.*|VERILATOR_ROOT = $(VERILATOR_ROOT_OVERRIDE)|' $(VTOP_MK)
	@sed -i 's|/work/src/cocotb/|$(REPO_ROOT)/src/cocotb/|g' $(VTOP_MK)

local_build:
	@test -f $(VTOP_MK) || (echo "ERROR: $(VTOP_MK) not found"; exit 1)
	$(MAKE) -C $(OUT_DIR)/cocotb-verilator -f Vtop.mk VM_TRACE=0

verilator_run: docker_build docker_headers fix_vtop_mk local_build
	@bash -c 'unset VERILATOR_ROOT; . "$(REPO_ROOT)/venv/bin/activate" && COCOTB_SKIP_BUILD=1 COCOTB_TEST_MODULES=tb_try PYTHONPATH="$(REPO_ROOT)/src" python "$(CURDIR)/test/run_verilator.py"'

clean:
	rm -rf $(OUT_DIR)

-include $(ADAPTER_DEP)
