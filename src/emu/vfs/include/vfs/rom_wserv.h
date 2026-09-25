#pragma once

#include <string>

namespace eka2l1 {
    // Retain the panel/plugin settings, but let wserv's normal startup machinery
    // own the resident phone-less state publisher instead of the modem Starter.
    inline void configure_rom_wserv_startup(std::u16string &ini) {
        std::u16string out;
        bool has_startup = false;
        std::size_t begin = 0;
        while (begin < ini.size()) {
            const auto newline = ini.find_first_of(u"\r\n", begin);
            const auto stop = newline == std::u16string::npos ? ini.size() : newline;
            auto key = begin;
            while (key < stop && (ini[key] == u' ' || ini[key] == u'\t' || ini[key] == 0xFEFF)) ++key;
            auto key_end = key;
            while (key_end < stop && ini[key_end] != u' ' && ini[key_end] != u'\t') ++key_end;
            auto name = ini.substr(key, key_end - key);
            for (auto &ch : name) {
                if (ch >= u'a' && ch <= u'z') ch -= u'a' - u'A';
            }
            if (name == u"STARTUP") {
                if (!has_startup) out += u"STARTUP C:\\System\\Programs\\SysState.exe";
                has_startup = true;
            } else {
                out.append(ini, begin, stop - begin);
            }
            auto end = stop;
            while (end < ini.size() && (ini[end] == u'\r' || ini[end] == u'\n')) ++end;
            out.append(ini, stop, end - stop);
            begin = end;
        }
        if (!has_startup) out += u"\r\nSTARTUP C:\\System\\Programs\\SysState.exe";
        ini = std::move(out);
    }
}
