/* Copyright (C) 2026 The Avium Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "LatestLcdQueue.h"

#ifdef NDEBUG
#error "Queue checks require assertions enabled"
#endif

#include <cassert>
#include <iostream>

using nx809j::als::LatestLcdQueue;
using nx809j::als::LcdParams;
using nx809j::als::validParams;

int main() {
    LatestLcdQueue queue;
    const LcdParams first{1, 2, 3, 2048, 33, 20};
    const LcdParams last{255, 0, 128, 16383, -1, 73};
    assert(!queue.submit(first));
    queue.setActive(true);
    assert(queue.submit(first));
    const auto inFlight = queue.latest().value();
    for (int i = 0; i < 10000; ++i) assert(queue.submit(last));
    assert(queue.latest()->params == last);
    assert(queue.isCurrent(inFlight));
    queue.setActive(false);
    assert(!queue.latest());
    assert(!queue.isCurrent(inFlight));
    queue.setActive(true);
    assert(!queue.isCurrent(inFlight));
    assert(queue.submit(first));
    assert(queue.latest()->generation != inFlight.generation);
    assert(validParams(first) && validParams(last));
    auto invalid = first;
    invalid[0] = 256;
    assert(!validParams(invalid));
    invalid = first;
    invalid[3] = -1;
    assert(!validParams(invalid));
    invalid = first;
    invalid[5] = 24;
    assert(!validParams(invalid));
    std::cout << "PASS: latest sample, inactive rejection, session invalidation, input bounds\n";
}
