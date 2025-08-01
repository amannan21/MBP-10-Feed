# -------- Makefile --------
CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -pipe -Wall -Wextra

# source file name updated ↓↓↓
reconstruction: reconstruction_AJMannan.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f reconstruction
# --------------------------
