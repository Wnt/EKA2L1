// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#include <services/fbs/fbs.h>
#include <catch2/catch.hpp>
#include <cstdlib>
#include <optional>
#include <string>

namespace {
    struct scoped_env {
        const char *name;
        std::optional<std::string> previous;
        scoped_env(const char *key) : name(key) {
            if (const char *value = std::getenv(key)) previous = value;
        }
        void set(const char *value) {
#ifdef _WIN32
            _putenv_s(name, value ? value : "");
#else
            if (value) setenv(name, value, 1);
            else unsetenv(name);
#endif
        }
        ~scoped_env() { set(previous ? previous->c_str() : nullptr); }
    };
}

TEST_CASE("ROM FBS preserves public names and isolates host consumers only with ROM wserv", "fbs") {
    using namespace eka2l1;
    scoped_env fbs("EKA2L1_ROM_FBS"), ws("EKA2L1_ROM_WSERV");
    const bool fbs_on = GENERATE(false, true);
    const bool ws_on = GENERATE(false, true);
    fbs.set(fbs_on ? "1" : nullptr);
    ws.set(ws_on ? "1" : nullptr);
    for (const auto ver : {epocver::epoc6, epocver::epoc7, epocver::epoc81a, epocver::epoc95}) {
        const bool enabled = fbs_on && ws_on && ver == epocver::epoc7;
        REQUIRE(epoc::rom_fbs_enabled(ver) == enabled);
        const auto guest = epoc::get_fbs_server_name_by_epocver(ver);
        REQUIRE(guest == (ver < epocver::epoc81a ? "Fontbitmapserver" : "!Fontbitmapserver"));
        const auto host = epoc::get_host_fbs_server_name_by_epocver(ver);
        REQUIRE(host == (enabled ? "EKA2L1HostFbs" : guest));
    }
}
