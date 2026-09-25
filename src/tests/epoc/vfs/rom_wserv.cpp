#include <catch2/catch.hpp>
#include <vfs/rom_wserv.h>

TEST_CASE("controlled ROM boot replaces startup while preserving panel settings", "[rom-wserv]") {
    std::u16string ini = u"\uFEFFWINDOWMODE COLOR64K\r\nSTARTUP \\SYSTEM\\PROGRAMS\\STARTER\r\nKEYCLICKPLUGIN keyclicksoundplugin.dll";
    eka2l1::configure_rom_wserv_startup(ini);
    REQUIRE(ini == u"\uFEFFWINDOWMODE COLOR64K\r\nSTARTUP C:\\System\\Programs\\SysState.exe\r\nKEYCLICKPLUGIN keyclicksoundplugin.dll");
    std::u16string other = u"// STARTUP foo\nSTARTUPMODE x\n startup x\nSTARTUP foo";
    eka2l1::configure_rom_wserv_startup(other);
    REQUIRE(other == u"// STARTUP foo\nSTARTUPMODE x\nSTARTUP C:\\System\\Programs\\SysState.exe\n");
    std::u16string absent = u"WINDOWMODE COLOR64K";
    eka2l1::configure_rom_wserv_startup(absent);
    REQUIRE(absent == u"WINDOWMODE COLOR64K\r\nSTARTUP C:\\System\\Programs\\SysState.exe");
}
