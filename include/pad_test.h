#ifndef WEACT_PAD_TEST_H
#define WEACT_PAD_TEST_H

#include <stdbool.h>
#include <stdint.h>

// A short hold at boot requests WAD import. A long hold enters the input test.
bool pad_test_check_boot(uint16_t *pixels);

#endif
