YOSYS_PARAM ?=
YOSYS_CMDS ?=
YOSYS_DEFS_OPT ?=
TARGETS ?= impl
BOARD ?= lichee_tang
FPGA ?= eagle_s20
PACKAGE ?= BG256
VERILOG_FILES ?= top.v
TOP ?= top
FIRMWARE ?= firmware.bin
ROOT_DIR=$(dir $(lastword $(MAKEFILE_LIST)))
OFL   = openFPGALoader
NEXTPNR = nextpnr-himbaechel
YOSYS = yosys
TANGPACK = tangpack
TANGUNPACK = tangunpack
EQY_CLOCK = async2sync
SCOPENAME ?=

define check_td_tool
  $(if $(strip $(TD_TOOL)),,$(error TD_TOOL variable is not set))
endef

ifndef BOARD
$(error BOARD variable is not set)
endif

ifeq ($(BOARD), lichee_tang)
OFL_BOARD=-b licheeTang
FREQ = 24
else
$(error Unknown BOARD '$(BOARD)')
endif

ifdef SEED
SEED_OPT=--seed $(SEED)
endif

ifdef MULTICLOCK
EQY_CLOCK=clk2fflogic
endif

ifdef YOSYS_DEFS
YOSYS_DEFS_OPT=verilog_defines $(YOSYS_DEFS);
endif

SDC_OPT ?=
ifneq ($(wildcard constraints.sdc),)
SDC_OPT = --sdc constraints.sdc
endif

NEXTPNR_PARAMS ?=

all:
	@echo "Use one of targets for '$(BOARD)' board:"
	@echo " - synth - running Yosys synthesys only"
	@echo " - pr - Yosys + td"
	@echo " - unpack - Unpack td bitstream"
	@echo " - jtag_td - programming bitstream produced by td"
	@echo " - json - Yosys for nextpnr"
	@echo " - nextpnr - Yosys + nextpnr"
	@echo " - jtag - programming bitstream produced by nextpnr"
	@echo " - post - generate post pnr verilog netlist by nextpnr"
	@exit

synth: $(TEST_CASE)_synth.v
json: $(TEST_CASE).json

$(TEST_CASE)_synth.v: ../$(BOARD).vh $(VERILOG_FILES)
	@echo "Running Yosys"
	@$(YOSYS) -ql synth.log -p '$(YOSYS_DEFS_OPT) read_verilog -defer -sv $^; $(YOSYS_CMDS) synth_anlogic $(YOSYS_PARAM) -top $(TOP); write_verilog $(TEST_CASE)_synth.v'

$(TEST_CASE).json $(TEST_CASE)_ys.v: ../$(BOARD).vh $(VERILOG_FILES)
	@echo "Running Yosys"
	@$(YOSYS) -ql synth.log -p '$(YOSYS_DEFS_OPT) read_verilog -defer -sv $^; $(YOSYS_CMDS) synth_anlogic $(YOSYS_PARAM) -top $(TOP) -json $(TEST_CASE).json; write_verilog -norename -noattr $(TEST_CASE)_ys.v'

$(TEST_CASE)_$(BOARD).tcl:
	@echo 'import_device $(FPGA).db -package $(PACKAGE)' > $@
	@echo 'read_verilog $(TEST_CASE)_synth.v:' >> $@
	@echo 'read_adc $(BOARD).adc' >> $@
	@echo 'optimize_rtl' >> $@
	@echo 'optimize_gate' >> $@
	@echo 'legalize_phy_inst' >> $@
	@echo 'place' >> $@
	@echo 'route' >> $@
	@echo 'write_pnl $(TEST_CASE)_$(BOARD).pnl' >> $@
	@echo 'bitgen -bit "$(TEST_CASE)_$(BOARD).bit" -version 0X00 -g ucode:000000000000000000000000 -info -log_file $(TEST_CASE)_$(BOARD).rep' >> $@

$(TEST_CASE)_$(BOARD).bit: $(TEST_CASE)_synth.v $(BOARD).adc $(TEST_CASE)_$(BOARD).tcl
	$(call check_td_tool)
	@echo "Running TD for '$(BOARD)' board"
	@$(TD_TOOL) $(TEST_CASE)_$(BOARD).tcl > $(TEST_CASE)_$(BOARD).log

$(TEST_CASE)_$(BOARD).txt $(TEST_CASE)_$(BOARD)_pnr.json: $(TEST_CASE).json $(BOARD).adc
	@echo "Running nextpnr for '$(BOARD)' board"
	$(NEXTPNR) --device $(FPGA) --json $(TEST_CASE).json --vopt ccf=$(BOARD).adc --vopt out=$(TEST_CASE)_$(BOARD).txt --router router2 --freq $(FREQ) $(SEED_OPT) $(SDC_OPT) --write $(TEST_CASE)_$(BOARD)_pnr.json --sdf $(TEST_CASE)_$(BOARD).sdf --placer-heap-beta 0.9 --vopt time_mode=typical $(NEXTPNR_PARAMS)

$(TEST_CASE)_$(BOARD)_td.txt: $(TEST_CASE)_$(BOARD).bit
	@echo "Running unpack for '$(BOARD)' board"
	@$(TANGUNPACK) $(TEST_CASE)_$(BOARD).bit $(TEST_CASE)_$(BOARD)_td.txt

pr: $(TEST_CASE)_$(BOARD).bit

unpack: $(TEST_CASE)_$(BOARD)_td.txt

nextpnr: $(TEST_CASE)_$(BOARD).txt

post: $(TEST_CASE)_$(BOARD)_pnr.v

jtag_td: $(TEST_CASE)_$(BOARD).bit
	@echo "Running openFPGALoader for '$(BOARD)' board"
	@$(OFL) $(OFL_BOARD) $^

jtag: $(TEST_CASE)_$(BOARD).txt
	@echo "Running openFPGALoader for '$(BOARD)' board"
	@$(TANGPACK) $(TEST_CASE)_$(BOARD).txt $(TEST_CASE)_$(BOARD).bit
	@$(OFL) $(OFL_BOARD) $(TEST_CASE)_$(BOARD).bit

$(TEST_CASE)_$(BOARD)_pnr.v: $(TEST_CASE)_$(BOARD).txt
	@echo "Generate post PnR Verilog netlist"
	@$(YOSYS) -q -p 'read_json $(TEST_CASE)_$(BOARD)_pnr.json; write_verilog -norename -noattr $(TEST_CASE)_$(BOARD)_pnr.v'

firmware: $(FIRMWARE)
	@echo "Running openFPGALoader to program '$(FIRMWARE)' on '$(BOARD)' board"
	@$(OFL) $(OFL_BOARD) --index-chain 0 -f -o 1048576 $(FIRMWARE)

clean:
	@rm -rf *.log *_synth.v *_tdroute.v *.txt *.bit *.rep
	@rm -rf *.tcl *.json *.pnl *_ys.v
	@rm -rf *_pnr.v check_yosys check_yosys.eqy check_pnr check_pnr.eqy check_pnr_post check_pnr_post.eqy

force-nextpnr:
	$(NEXTPNR) --device $(FPGA) --json $(TEST_CASE).json --vopt ccf=$(BOARD).adc --vopt out=../logs/$(TEST_CASE)_$(BOARD)_$(SEED).txt --router router2 --freq $(FREQ) $(SEED_OPT) $(SDC_OPT) --placer-heap-beta 0.9 --vopt time_mode=typical $(NEXTPNR_PARAMS)

.PHONY: synth json pr nextpnr force-nextpnr jtag_td jtag unpack check_yosys check_pnr check_pnr_post

check_yosys.eqy: $(TEST_CASE)_ys.v ../$(BOARD).vh $(VERILOG_FILES)
	@echo "Generang EQY file"
	@{ \
	  echo "[gold]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog -defer -sv ../$(BOARD).vh $(VERILOG_FILES)"; \
	  echo "$(YOSYS_CMDS)"; \
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[gate]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog $(TEST_CASE)_ys.v"; \
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[collect *]"; \
	  echo "bind *"; \
	  echo ""; \
	  echo "[strategy pdr]"; \
	  echo "use sby"; \
	  echo "engine abc scorr; pdr"; \
	} > $@

check_yosys: check_yosys.eqy
	@eqy -f $^

check_pnr.eqy: $(TEST_CASE)_$(BOARD)_pnr.v ../$(BOARD).vh $(VERILOG_FILES)
	@echo "Generating EQY file"
	@{ \
	  echo "[gold]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog -defer -sv ../$(BOARD).vh $(VERILOG_FILES)"; \
	  echo "$(YOSYS_CMDS)"; \
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[gate]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog $(TEST_CASE)_$(BOARD)_pnr.v"; \
	  echo "rename top $(TOP)";\
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[collect *]"; \
	  echo "bind *"; \
	  echo ""; \
	  echo "[strategy pdr]"; \
	  echo "use sby"; \
	  echo "engine abc scorr; pdr"; \
	} > $@

check_pnr: check_pnr.eqy
	@eqy -f $^

check_pnr_post.eqy: $(TEST_CASE)_$(BOARD)_pnr.v ../$(BOARD).vh $(TEST_CASE)_ys.v
	@echo "Generating EQY file"
	@{ \
	  echo "[gold]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog $(TEST_CASE)_ys.v"; \
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[gate]"; \
	  echo "$(YOSYS_DEFS_OPT)"; \
	  echo "read_verilog -nowb -noblackbox +/anlogic/cells_sim.v"; \
	  echo "read_verilog $(TEST_CASE)_$(BOARD)_pnr.v"; \
	  echo "rename top $(TOP)";\
	  echo "prep -top $(TOP)"; \
	  echo "flatten";\
	  echo "$(EQY_CLOCK)";\
	  echo "memory_map";\
	  echo ""; \
	  echo "[collect *]"; \
	  echo "bind *"; \
	  echo ""; \
	  echo "[strategy pdr]"; \
	  echo "use sby"; \
	  echo "engine abc scorr; pdr"; \
	} > $@

check_pnr_post: check_pnr_post.eqy
	@eqy -f $^
