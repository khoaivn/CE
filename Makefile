CXX := g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pedantic

SOURCE := experiment.cpp
TARGET := experiment.o
GRAPH ?= facebook
ALPHA ?= 0.3
EPSILON ?= 0.02
IC_PROBABILITY ?= 0.01
RR_SAMPLES ?= 50000
EVAL_SAMPLES ?= 100000
RESULT_DIR ?= results
BUDGET_RATIOS ?= 0.01 0.02 0.03 0.04 0.05
ifndef RUN_DATE
RUN_DATE := $(shell date +%Y-%m-%d)
endif
TEXT_RESULT ?= $(RESULT_DIR)/facebook_results_$(RUN_DATE).txt
OFFLINE_RESULT ?= $(RESULT_DIR)/facebook_offline_metrics_$(RUN_DATE).txt
FOCUS_RESULT ?= $(RESULT_DIR)/facebook_focus_metrics_$(RUN_DATE).txt
EXECUTABLE = $(if $(filter /%,$(TARGET)),$(TARGET),./$(TARGET))

# GNU g++ accepts -fopenmp. Apple clang does not unless libomp is installed.
# The current source has no OpenMP pragmas, so omitting the flag does not
# change its result. This probe adds the flag automatically when supported.
OPENMP_FLAG := $(shell printf 'int main(){}\n' | \
	$(CXX) -x c++ -std=c++17 -fopenmp -c -o /dev/null - >/dev/null 2>&1 \
	&& printf '%s' '-fopenmp')

COMMON_ARGS := --graph $(GRAPH) \
	--alpha $(ALPHA) \
	--epsilon $(EPSILON) \
	--p $(IC_PROBABILITY) \
	--rr-samples $(RR_SAMPLES) \
	--eval-samples $(EVAL_SAMPLES)

.PHONY: all experiment run run-one clean

all: experiment

experiment: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAG) $< -o $@

$(RESULT_DIR):
	mkdir -p $@

run: $(TARGET) | $(RESULT_DIR)
	@if [ ! -s "$(TEXT_RESULT)" ]; then \
		printf 'budget_ratio\tB\tsum_mu\toffline_f_value\toffline_eval_f_value\toffline_queries\toffline_memory_mb_est\toffline_running_time_ms\tfocus_f_value\tfocus_eval_f_value\tfocus_queries\tfocus_memory_mb_est\tfocus_running_time_ms\n' >> "$(TEXT_RESULT)"; \
	fi
	@if [ ! -s "$(OFFLINE_RESULT)" ]; then \
		printf '%%B\tB\tf_value\tqueries\tmemory_mb_est\trunning_time_ms\n' >> "$(OFFLINE_RESULT)"; \
	fi
	@if [ ! -s "$(FOCUS_RESULT)" ]; then \
		printf '%%B\tB\tf_value\tqueries\tmemory_mb_est\trunning_time_ms\n' >> "$(FOCUS_RESULT)"; \
	fi
	@set -e; \
	for ratio in $(BUDGET_RATIOS); do \
		output="$$($(EXECUTABLE) $(COMMON_ARGS) --budget-ratio "$$ratio")"; \
		row="$$(printf '%s\n' "$$output" | awk -F, ' \
			BEGIN { OFS="\t" } \
			NR == 1 { next } \
			$$1 == "Offline_Greedy_CC" { br=$$2; b=$$3; sm=$$4; of=$$5; oef=$$6; oq=$$7; om=$$8; ot=$$9; have_offline=1 } \
			$$1 == "FOCUS_RR" { fbr=$$2; fb=$$3; ff=$$5; fef=$$6; fq=$$7; fm=$$8; ft=$$9; have_focus=1 } \
			END { \
				if (!have_offline || !have_focus || br != fbr || b != fb) exit 1; \
				print br,b,sm,of,oef,oq,om,ot,ff,fef,fq,fm,ft \
			}')"; \
		printf '%s\n' "$$row" >> "$(TEXT_RESULT)"; \
		printf '%s\n' "$$row" | awk 'BEGIN { FS=OFS="\t" } { print sprintf("%g%%", $$1*100),$$2,$$4,$$6,$$7,$$8 }' >> "$(OFFLINE_RESULT)"; \
		printf '%s\n' "$$row" | awk 'BEGIN { FS=OFS="\t" } { print sprintf("%g%%", $$1*100),$$2,$$9,$$11,$$12,$$13 }' >> "$(FOCUS_RESULT)"; \
	done
	@echo "Wrote $(TEXT_RESULT)"
	@echo "Wrote $(OFFLINE_RESULT)"
	@echo "Wrote $(FOCUS_RESULT)"

# Ví dụ: make run-one BUDGET_RATIO=0.01 ALPHA=0.3
run-one: $(TARGET) | $(RESULT_DIR)
	@test -n "$(BUDGET_RATIO)" || { echo "BUDGET_RATIO is required" >&2; exit 2; }
	@$(MAKE) --no-print-directory run \
		RUN_DATE="$(RUN_DATE)" \
		BUDGET_RATIOS="$(BUDGET_RATIO)" \
		TEXT_RESULT="$(RESULT_DIR)/facebook_ratio$(BUDGET_RATIO)_alpha$(ALPHA)_$(RUN_DATE).txt" \
		OFFLINE_RESULT="$(RESULT_DIR)/facebook_ratio$(BUDGET_RATIO)_alpha$(ALPHA)_offline_metrics_$(RUN_DATE).txt" \
		FOCUS_RESULT="$(RESULT_DIR)/facebook_ratio$(BUDGET_RATIO)_alpha$(ALPHA)_focus_metrics_$(RUN_DATE).txt"

clean:
	rm -f $(TARGET)
