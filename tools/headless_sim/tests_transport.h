// UDP transport and fragmentation gates (tests_transport.cpp).
#pragma once

#include <string>

int run_udp_check();
int run_udp_fragment_check();
int run_fragment_check();
int run_udp_session_check(const std::string& data_path);
int run_timeout_test();
