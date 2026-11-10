RGB_DIR = ../rpi-rgb-led-matrix
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -Wno-unused-parameter -I$(RGB_DIR)/include
LDFLAGS = -L$(RGB_DIR)/lib -lrgbmatrix -lrt -lm -lpthread

ledcube: ledcube.cc $(RGB_DIR)/lib/librgbmatrix.a
	$(CXX) $(CXXFLAGS) ledcube.cc -o $@ $(LDFLAGS)

$(RGB_DIR)/lib/librgbmatrix.a:
	$(MAKE) -C $(RGB_DIR)/lib

clean:
	rm -f ledcube

.PHONY: clean
