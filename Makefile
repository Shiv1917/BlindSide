CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

all: main test_rx_bit_chain test_symbol_rate

# The Simulator
main: main.cpp
	$(CXX) $(CXXFLAGS) -I. -o main main.cpp

# The Bit Chain Test
test_rx_bit_chain: test_rx_bit_chain.cpp rx_bit_chain.cpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o test_rx_bit_chain test_rx_bit_chain.cpp rx_bit_chain.cpp

# The Symbol Rate Test
test_symbol_rate: test_symbol_rate.cpp symbol_rate.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
                  symbol_rate.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o test_symbol_rate symbol_rate.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_symbol_rate.cpp

clean:
	rm -f main test_rx_bit_chain test_symbol_rate