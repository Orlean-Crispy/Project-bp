// Network behaviour gates. Definitions live in tests_net.cpp; main.cpp only dispatches.
#pragma once

#include <string>

struct Options;

int run_join_phase(const std::string& data_path);
int run_net_events_check(const std::string& data_path);
int run_udp_check();
int run_fragment_check();
int run_udp_fragment_check();
int run_udp_session_check(const std::string& data_path);
int run_timeout_test();
int run_ticket_test();
int run_reliable_test();
int run_match_test();
int run_slot_release_test();
int run_reconnect_test();
int run_seqwrap_check(const std::string& data_path);
int run_config_test();
int run_dual_channel_test();
int run_signal_test();
int run_clock_check(const std::string& data_path);
int run_catch_up_check(const std::string& data_path);
int run_net_test(const Options& opt);
int run_net_alloc_check(const std::string& data_path);

