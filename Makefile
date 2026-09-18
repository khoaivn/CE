CXX := g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pedantic

SOURCE := experiment.cpp
TARGET := experiment.o
GRAPH ?= facebook
ALPHA ?= 0.3
EPSILON ?= 0.02
UNCERTAINTY ?= 0.5
IC_PROBABILITY ?= 0.01
RR_SAMPLES ?= 50000
EVAL_SAMPLES ?= 100000
RESULT_DIR ?= results
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
	--uncertainty $(UNCERTAINTY) \
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
	$(EXECUTABLE) $(COMMON_ARGS) --budget 16 > $(RESULT_DIR)/facebook_B16.csv
	$(EXECUTABLE) $(COMMON_ARGS) --budget 20 > $(RESULT_DIR)/facebook_B20.csv
	$(EXECUTABLE) $(COMMON_ARGS) --budget 24 > $(RESULT_DIR)/facebook_B24.csv
	$(EXECUTABLE) $(COMMON_ARGS) --budget 28 > $(RESULT_DIR)/facebook_B28.csv

# Ví dụ: make run-one BUDGET=16 ALPHA=0.3
run-one: $(TARGET) | $(RESULT_DIR)
	@test -n "$(BUDGET)" || { echo "BUDGET is required" >&2; exit 2; }
	$(EXECUTABLE) $(COMMON_ARGS) --budget $(BUDGET) \
		> $(RESULT_DIR)/facebook_B$(BUDGET)_alpha$(ALPHA).csv

clean:
	rm -f $(TARGET)
