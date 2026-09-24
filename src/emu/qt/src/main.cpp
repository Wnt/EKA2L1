/*
 * Copyright (c) 2021 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <drivers/input/common.h>

#include <qt/kiosk.h>
#include <qt/state.h>
#include <qt/thread.h>
#include <qt/utils.h>

#include <common/arghandler.h>
#include <common/fileutils.h>
#include <common/path.h>
#include <common/platform.h>
#include <common/log.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#if EKA2L1_PLATFORM(UNIX)
// The AppImage bundles Qt's GStreamer backend but none of the plugins it needs,
// leaving Qt Multimedia with no camera. Its FFmpeg backend is bundled whole, so
// prefer that inside an AppImage. An explicit choice still wins.
static void prefer_selfcontained_media_backend() {
    if (!qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) {
        return;
    }

    if (qEnvironmentVariableIsEmpty("APPIMAGE") && qEnvironmentVariableIsEmpty("APPDIR")) {
        return;
    }

    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
}
#endif

int main(int argc, char *argv[]) {
    // --help answers before anything else: no display connection, no device boot, no data
    // directory, whatever state the machine is in.
    for (int i = 1; i < argc; i++) {
        if ((std::strcmp(argv[i], "--help") == 0) || (std::strcmp(argv[i], "-h") == 0)) {
            eka2l1::common::arg_parser parser(argc, const_cast<const char **>(argv));
            eka2l1::desktop::register_command_line_options(parser);

            std::cout << parser.get_help_string();
            std::cout.flush();

            return 0;
        }
    }

#if EKA2L1_PLATFORM(UNIX)
    prefer_selfcontained_media_backend();

    // SDL (initialised for game controllers) otherwise turns SIGTERM and SIGINT into SDL_QUIT
    // events nobody reads: the process ignored both, and a station could only SIGKILL it.
    if (qEnvironmentVariableIsEmpty("SDL_NO_SIGNAL_HANDLERS")) {
        qputenv("SDL_NO_SIGNAL_HANDLERS", "1");
    }
#endif

    QApplication a(argc, argv);

    QCoreApplication::setOrganizationName("EKA2L1");
    QCoreApplication::setApplicationName("EKA2L1");

    QTranslator translator;
    QSettings settings;

    QVariant language_variant = settings.value(LANGUAGE_SETTING_NAME);
    bool lang_loaded = false;

    if (language_variant.isValid()) {
        const QString base_name = "eka2l1_" + language_variant.toString();
        if (translator.load(":/languages/" + base_name)) {
            a.installTranslator(&translator);
            lang_loaded = true;
        }
    }

    if (!lang_loaded) {
        const QStringList ui_languages = QLocale::system().uiLanguages();
        for (const QString &locale : ui_languages) {
            const QString locale_name = QLocale(locale).name();
            const QString base_name = "eka2l1_" + locale_name;

            if (translator.load(":/languages/" + base_name)) {
                a.installTranslator(&translator);
                settings.setValue(LANGUAGE_SETTING_NAME, locale_name);

                break;
            }
        }
    }
    
    qRegisterMetaType<std::vector<std::string>>("std::vector<std::string>");
    qRegisterMetaType<eka2l1::drivers::input_event>("eka2l1::drivers::input_event");

#if !EKA2L1_PLATFORM(WIN32)
    // Relative paths on the command line (--log-file, --control-socket) mean the launch directory.
    eka2l1::desktop::set_launch_directory(QDir::currentPath().toStdString());

    QString data_path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/EKA2L1/";
    QDir root_dir = QDir::root();
    root_dir.mkpath(data_path);
    std::string data_path_str = data_path.toUtf8().toStdString();
    
    QString app_path = QDir(QCoreApplication::applicationDirPath()).path();
    std::string app_path_str = app_path.toUtf8().toStdString();
    eka2l1::common::copy_folder(app_path_str + "/patch", data_path_str + "/patch", 0, nullptr);
    eka2l1::common::copy_folder(app_path_str + "/resources", data_path_str + "/resources", 0, nullptr);

    // Keep shipped compatibility scripts current across application upgrades.
    // copy_folder merges into the destination, so separately named user scripts
    // remain untouched while updated bundled scripts replace stale copies.
    eka2l1::common::copy_folder(app_path_str + "/scripts", data_path_str + "/scripts", 0, nullptr);
    
    if (!eka2l1::common::exists(data_path_str + "/compat/")) {
        eka2l1::common::copy_folder(app_path_str + "/compat", data_path_str + "/compat", 0, nullptr);
    }

    eka2l1::common::set_current_directory(data_path_str);
#endif

    eka2l1::desktop::emulator emulator_state;
    return eka2l1::desktop::emulator_entry(a, emulator_state, argc, const_cast<const char **>(argv));
}
