#include <catch2/catch.hpp>
#include <services/window/rom_bridge.h>

TEST_CASE("ROM window snapshots publish atomically and reject malformed wire data", "[window][rom]") {
    eka2l1::rom_window_snapshot state;
    eka2l1_rom_bridge::snapshot packet{};
    packet.protocol_version = eka2l1_rom_bridge::version;
    packet.count = 2;
    packet.focus = 9;
    packet.groups[0].id = 9;
    packet.groups[0].thread = 100;
    packet.groups[1].id = 12;
    const std::size_t size = 16 + 2 * sizeof(eka2l1_rom_bridge::group);
    REQUIRE(state.assign(&packet, size));
    REQUIRE(state.ready);
    REQUIRE(state.groups.size() == 2);
    REQUIRE(state.focus == 9);

    SECTION("truncated record") { REQUIRE_FALSE(state.assign(&packet, size - 1)); }
    SECTION("unbounded count") { packet.count = 65; REQUIRE_FALSE(state.assign(&packet, size)); }
    SECTION("unsupported protocol") { packet.protocol_version = 2; REQUIRE_FALSE(state.assign(&packet, size)); }
    SECTION("duplicate group") { packet.groups[1].id = 9; REQUIRE_FALSE(state.assign(&packet, size)); }
    SECTION("name exceeds record") { packet.groups[0].name_length = 257; REQUIRE_FALSE(state.assign(&packet, size)); }
    SECTION("invalid group id") { packet.groups[0].id = -1; REQUIRE_FALSE(state.assign(&packet, size)); }
    REQUIRE(state.groups.size() == 2);
    REQUIRE(state.groups[1].id == 12);
    REQUIRE(state.focus == 9);
}

TEST_CASE("ROM window snapshots replace closed groups and retain wire order", "[window][rom]") {
    eka2l1::rom_window_snapshot state;
    eka2l1_rom_bridge::snapshot packet{};
    packet.protocol_version = 1;
    packet.count = 2;
    packet.groups[0].id = 12;
    packet.groups[1].id = 9;
    packet.focus = 9;
    REQUIRE(state.assign(&packet, 16 + 2 * sizeof(eka2l1_rom_bridge::group)));
    REQUIRE(state.groups[0].id == 12);
    REQUIRE(state.groups[1].id == 9);
    packet.count = 0;
    packet.focus = 0;
    REQUIRE(state.assign(&packet, 16));
    REQUIRE(state.groups.empty());
    REQUIRE(state.focus == 0);
}
