#ifdef APP_VERSION_BUILD
#include "app_version.h"
#endif
#include <iostream>
#include <string>

#include "app/application.h"

namespace {

void PrintHelp() {
    std::cout << "NAME\n"
              << "     audioplayer - Interactive Audio Player with Transcript & Cropping\n\n"
              << "SYNOPSIS\n"
              << "     audioplayer [OPTIONS]\n\n"
              << "DESCRIPTION\n"
              << "     audioplayer is a GUI-based interactive audio player designed for navigating\n"
              << "     long recordings with JSON transcripts. It features an asynchronous audio\n"
              << "     engine, a transcript interface with live search and drag-to-copy, and\n"
              << "     lossless/re-encoded audio cropping.\n\n"
              << "OPTIONS\n"
              << "     -f, --foreground, --no-detach\n"
              << "             Run in the foreground (do not detach from the terminal session).\n\n"
              << "     -h, --help\n"
              << "             Print this help message and exit.\n\n"
              << "OPERATION\n"
              << "     By default, the application runs in the background. It calls fork() and\n"
              << "     setsid() to detach from the controlling terminal, redirecting standard\n"
              << "     streams to /dev/null. If started with --foreground, it remains attached\n"
              << "     to the shell.\n\n"
              << "EXAMPLES\n"
              << "     Start the player in the background (default):\n"
              << "             audioplayer\n\n"
              << "     Start the player in the foreground:\n"
              << "             audioplayer --foreground\n\n"
              << "FILES\n"
              << "     ~/.config/imgui.ini\n"
              << "             ImGui state configuration file.\n\n"
              << "AUTHORS\n"
              << "     Terrydaktal <9lewis9@gmail.com>\n";
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef APP_VERSION_BUILD
    app_version::handle_cli(argc, argv);
#endif
    bool daemonize = true;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            PrintHelp();
            return 0;
        }
        if (argument == "-f" || argument == "--foreground" || argument == "--no-detach") {
            daemonize = false;
        }
    }

    Application application;
    return application.Run(daemonize);
}
