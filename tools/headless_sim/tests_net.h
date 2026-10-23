// Network behaviour gates. Definitions live in tests_net.cpp; main.cpp only dispatches.
// Transport-level and lobby-level checks now live in tests_transport / tests_lobby.
#pragma once

#include <string>

struct Options;

int run_join_phase(const std::string& data_path);
int run_net_events_check(const std::string& data_path);
int run_reconnect_test();
int run_seqwrap_check(const std::string& data_path);
int run_config_test();
int run_dual_channel_test();
int run_clock_check(const std::string& data_path);
int run_catch_up_check(const std::string& data_path);
int run_rollback_repair_check(const std::string& data_path);
int run_net_alloc_check(const std::string& data_path);
int run_net_test(const Options& opt);
