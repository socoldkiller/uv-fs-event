#include "FileWatcher.hpp"


void show_title(const FileWatcher *watcher) {
    assert(watcher);
    static char buffer[80];
    std::fill(buffer, buffer + 80, 0);
    auto info = watcher->_files_versions.at(watcher->_now_changed_file).back();
    auto now_c = std::chrono::system_clock::to_time_t(info->timeval);
    auto now_tm = std::localtime(&now_c);
    std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", now_tm);
    printf("\033[33m The file [%s] was modified at %s\n", info->fileName.c_str(), buffer);
    dtl::resetColor(cout);
}


void show_diff_file_content(const FileWatcher *watcher) {
    assert(watcher);
    auto iterator = watcher->_files_versions.find(watcher->_now_changed_file);
    if (iterator == watcher->_files_versions.end()) {
        return;
    }
    const auto &files = iterator->second;

    if (files.size() <= 1) {
        return;
    }
    size_t second = files.size() - 1;
    size_t first = second - 1;
    diff_file_by_lines(files[first]->contents, files[second]->contents);
    printf("\n\n\n\n");
}

/* watcher demo */
int main(int argc, char **argv) {
    FileWatcher watcher(ConfigurationFileWatcher{
            .is_show =          true,
            .is_recursive =     true,
            .is_pre_read =      true,
            ._suffix_files =    {"cc", "h", "txt", "hpp"},
            .root =             "."

    });
    watcher.set_printCallbacks(show_title, show_diff_file_content);
    watcher.watch();
}

