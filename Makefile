CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

all: main test_rx_bit_chain test_symbol_rate test_rolloff test_cfo test_frontend test_frac_delay test_cumulants test_jamlse test_sweep

main: main.cpp
	$(CXX) $(CXXFLAGS) -I. -o main main.cpp

test_rx_bit_chain: test_rx_bit_chain.cpp rx_bit_chain.cpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ test_rx_bit_chain.cpp rx_bit_chain.cpp

test_symbol_rate: test_symbol_rate.cpp symbol_rate.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
                  symbol_rate.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ symbol_rate.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_symbol_rate.cpp

test_rolloff: test_rolloff.cpp rolloff.cpp psd.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
              rolloff.hpp psd.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ rolloff.cpp psd.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_rolloff.cpp

test_cfo: test_cfo.cpp cfo.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
          cfo.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ cfo.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_cfo.cpp

test_frontend: test_frontend.cpp frontend.cpp cfo.cpp symbol_rate.cpp rolloff.cpp psd.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
               frontend.hpp cfo.hpp symbol_rate.hpp rolloff.hpp psd.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ frontend.cpp cfo.cpp symbol_rate.cpp rolloff.cpp psd.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_frontend.cpp

test_frac_delay: test_frac_delay.cpp frac_delay.cpp frontend.cpp cfo.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
                 frac_delay.hpp frontend.hpp cfo.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ frac_delay.cpp frontend.cpp cfo.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_frac_delay.cpp

test_cumulants: test_cumulants.cpp cumulants.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
                cumulants.hpp frontend.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ cumulants.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_cumulants.cpp

test_jamlse: test_jamlse.cpp jamlse.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
             jamlse.hpp frontend.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ jamlse.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_jamlse.cpp

test_sweep: test_sweep.cpp jamlse.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp \
            jamlse.hpp frontend.hpp fft.hpp truth.hpp rx_bit_chain.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ jamlse.cpp frontend.cpp fft.cpp truth.cpp rx_bit_chain.cpp test_sweep.cpp

clean:
	rm -f main test_rx_bit_chain test_symbol_rate test_rolloff test_cfo test_frontend test_frac_delay test_cumulants test_jamlse test_sweep