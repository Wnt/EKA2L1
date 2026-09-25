// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#include <kernel/rendezvous.h>
#include <catch2/catch.hpp>
#include <vector>

TEST_CASE("host startup observers do not launch a dependent before rendezvous or on failure", "kernel") {
    eka2l1::kernel::rendezvous_callbacks requests;
    int launches = 0, notifications = 0;
    requests.arm([&](int result) { ++notifications; if (result == 0) ++launches; });
    REQUIRE(launches == 0);
    const int result = GENERATE(0, -4, -13);
    requests.complete(result);
    REQUIRE(notifications == 1);
    REQUIRE(launches == (result == 0 ? 1 : 0));
    // Process death or a second rendezvous must not start another window server.
    requests.complete(-13);
    requests.complete(0);
    REQUIRE(notifications == 1);
}

TEST_CASE("a host rendezvous callback can arm the next request without invalidating this delivery", "kernel") {
    eka2l1::kernel::rendezvous_callbacks requests;
    std::vector<int> delivered;
    requests.arm([&](int value) {
        delivered.push_back(value);
        requests.arm([&](int next) { delivered.push_back(next); });
    });
    requests.arm([&](int value) { delivered.push_back(value + 1); });
    requests.complete(0);
    REQUIRE(delivered == std::vector<int>{0, 1});
    requests.complete(-4);
    REQUIRE(delivered == std::vector<int>{0, 1, -4});
}
