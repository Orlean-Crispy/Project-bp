// Authoritative-field gate (AUD-39): every registered field must move
// state_hash, and the fields that are deliberately excluded must not.
#pragma once

struct Options;

int run_fields_test(const Options& opt);
