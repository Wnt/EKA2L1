#include <catch2/catch.hpp>
#include <config/config.h>
#include <mem/mem.h>
#include <mem/process.h>
#include <vfs/rom_assets.h>
#include <vfs/vfs.h>
#include <loader/rom.h>
#include <common/fileutils.h>
#include <fstream>

namespace {
    std::vector<std::uint8_t> asset() {
        std::vector<std::uint8_t> bytes(256);
        auto put = [&](std::size_t offset, std::uint32_t value) { std::memcpy(bytes.data() + offset, &value, 4); };
        put(16, 0x10000041); put(20, 1); put(24, 12);
        put(28, 0x10000040); put(52, 40); put(92, 68);
        return bytes;
    }
}

TEST_CASE("ROM asset recognition validates embedded object offsets", "[rom-assets]") {
    auto bytes = asset();
    REQUIRE(eka2l1::contains_rom_bitmap_store(bytes));
    bytes[24] = 255;
    REQUIRE_FALSE(eka2l1::contains_rom_bitmap_store(bytes));
    bytes = asset(); bytes[52] = 7;
    REQUIRE_FALSE(eka2l1::contains_rom_bitmap_store(bytes));
    bytes = asset(); bytes.resize(95);
    REQUIRE_FALSE(eka2l1::contains_rom_bitmap_store(bytes));
}

TEST_CASE("immutable ROM data has stable global addresses across clients and allocation growth", "[rom-assets]") {
    for (auto model : {eka2l1::mem::mem_model_type::multiple, eka2l1::mem::mem_model_type::flexible}) {
        eka2l1::config::state conf;
        eka2l1::memory_system memory(nullptr, &conf, model, true);
        auto first = eka2l1::mem::make_new_mem_model_process(memory.get_control(), model);
        const auto bytes = asset();
        const auto address = memory.map_rom_data(u"z:\\test.aif", bytes);
        REQUIRE(address != 0);
        REQUIRE(memory.map_rom_data(u"z:\\test.aif", {1, 2, 3}) == address);
        REQUIRE(std::memcmp(memory.get_real_pointer(address, first->address_space_id()), bytes.data(), bytes.size()) == 0);
        REQUIRE(memory.get_control()->get_page_info(first->address_space_id(), address)->perm == prot_read);
        first.reset();
        std::vector<std::uint8_t> large(0x110000, 42);
        REQUIRE(memory.map_rom_data(u"z:\\large.mbm", large) != address);
        auto second = eka2l1::mem::make_new_mem_model_process(memory.get_control(), model);
        REQUIRE(std::memcmp(memory.get_real_pointer(address, second->address_space_id()), bytes.data(), bytes.size()) == 0);
    }
}

TEST_CASE("extracted ROM file address and seek remain valid after close", "[rom-assets]") {
    struct env_guard {
        std::string name, value;
        bool had;
        explicit env_guard(const char *key) : name(key), had(std::getenv(key) != nullptr) {
            if (had) value = std::getenv(key);
            set("1");
        }
        void set(const char *text) {
#ifdef _WIN32
            _putenv_s(name.c_str(), text ? text : "");
#else
            if (text) setenv(name.c_str(), text, 1); else unsetenv(name.c_str());
#endif
        }
        ~env_guard() { set(had ? value.c_str() : nullptr); }
    } wserv("EKA2L1_ROM_WSERV"), fbs("EKA2L1_ROM_FBS");
    eka2l1::config::state conf;
    eka2l1::memory_system memory(nullptr, &conf, eka2l1::mem::mem_model_type::multiple, true);
    eka2l1::loader::rom rom{};
    auto fs = eka2l1::create_rom_filesystem(&rom, &memory, epocver::epoc7, "test");
    eka2l1::io_system io;
    io.add_filesystem(fs);
    eka2l1::common::create_directories("rom_asset_test/test");
    const auto bytes = asset();
    { std::ofstream out("rom_asset_test/test/test.aif", std::ios::binary); out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }
    REQUIRE(io.mount_physical_path(drive_number::drive_z, drive_media::rom, io_attrib_internal, u"rom_asset_test"));
    auto file = io.open_file(u"Z:\\test.aif", READ_MODE | BIN_MODE);
    REQUIRE(file);
    const auto addr = file->rom_address();
    REQUIRE(addr != 0);
    REQUIRE(file->seek(16, eka2l1::file_seek_mode::address) == addr + 16);
    REQUIRE(file->seek(-1, eka2l1::file_seek_mode::address) == UINT64_MAX);
    REQUIRE_FALSE(file->resize(4));
    file->close(); file.reset();
    REQUIRE(std::memcmp(memory.get_real_pointer(addr), bytes.data(), bytes.size()) == 0);
    file = io.open_file(u"z:\\TEST.AIF", READ_MODE | BIN_MODE);
    REQUIRE(file);
    REQUIRE(file->rom_address() == addr);
    std::vector<std::uint8_t> read(bytes.size());
    REQUIRE(file->read_file(read.data(), 1, read.size()) == bytes.size());
    REQUIRE(read == bytes);
    file.reset();
    REQUIRE_FALSE(io.open_file(u"Z:\\test.aif", WRITE_MODE | BIN_MODE));
    eka2l1::common::delete_folder("rom_asset_test");
}

TEST_CASE("extension ROM root publishes the header recognized by EKA1 User IsRomAddress", "[rom-assets]") {
    eka2l1::config::state conf;
    eka2l1::memory_system memory(nullptr, &conf, eka2l1::mem::mem_model_type::multiple, true);
    std::vector<std::uint8_t> core_bytes(0x100000);
    const std::uint32_t roots[] = {1, 0x12345678, 0x50004000};
    std::memcpy(core_bytes.data() + 0x20, roots, sizeof(roots));
    auto core = eka2l1::mem::make_new_mem_model_chunk(memory.get_control(), 0, memory.get_model_type());
    eka2l1::mem::mem_model_chunk_creation_info info{};
    info.size = core_bytes.size(); info.host_map = core_bytes.data(); info.perm = prot_read;
    info.flags = eka2l1::mem::MEM_MODEL_CHUNK_REGION_USER_ROM | eka2l1::mem::MEM_MODEL_CHUNK_TYPE_NORMAL;
    REQUIRE(core->do_create(info) == eka2l1::mem::MEM_MODEL_CHUNK_ERR_OK);
    core->commit(0, info.size);
    const auto old_root = core->base(nullptr) + 0x20;
    REQUIRE(memory.rom_root_directory(old_root) == old_root);
    const auto mapped = memory.map_rom_data(u"z:\\test.aif", asset(), old_root);
    REQUIRE(mapped != 0);
    const auto root = memory.rom_root_directory(old_root);
    const auto header = root & ~std::uint32_t(0xFFFFF);
    REQUIRE(root > old_root);
    REQUIRE(memory.read<std::uint32_t>(header + 12) == header);
    REQUIRE(mapped >= header);
    REQUIRE(mapped < header + memory.read<std::uint32_t>(header + 16));
    REQUIRE(std::memcmp(memory.get_real_pointer(root), roots, sizeof(roots)) == 0);
}
