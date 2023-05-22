#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <fstream>
#include <uv.h>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include "unidiff.h"

struct FileInfo {
    std::string fileName;
    std::string contents;
    std::chrono::time_point<std::chrono::system_clock> timeval;
public:
    FileInfo() = delete;

    FileInfo(const FileInfo &) = default;

    FileInfo &operator=(const FileInfo &) = delete;

    FileInfo(const std::string &fileName) {
        this->fileName = fileName;
        timeval = std::chrono::system_clock::now();
        read_all_contents();
    }

private:
    void read_all_contents() {
        std::FILE *fp = std::fopen(fileName.c_str(), "r");
        if (fp) {
            std::fseek(fp, 0, SEEK_END);
            contents.resize(std::ftell(fp));
            std::rewind(fp);
            std::fread(&contents[0], 1, contents.size(), fp);
            std::fclose(fp);
        }
    }

};

class FileWatcher {

private:
    static constexpr int MAX_DIFF_SIZE = 1 << 4;   // 最大比较文件变化个数
    static constexpr int MAX_BUFF_SIZE = (1 << 10) + 1;   // 最大读取文件缓冲区大小

private:
    uv_loop_t *_loop;
    uv_fs_event_t *_fs_event;

public:
    std::string _dir;
    bool _show;
    std::unordered_map<std::string, std::vector<FileInfo *>> _files_versions;
    std::unordered_set<std::string> _suffix_files;
    std::vector<std::function<void(FileWatcher *)>> _print_callbacks;
    std::string _now_changed_file;
    bool _is_pre_read;

public:
    FileWatcher(const FileWatcher &) = delete;

    FileWatcher &operator=(const FileWatcher &) = delete;

    /**
     * 文件监听器的入口。
     * 默认监听当前文件夹，并展示内容
     * */
    explicit FileWatcher(const std::string &dir = ".", bool show = true) : _dir(dir),
                                                                           _loop(uv_default_loop()),
                                                                           _show(show) {
        _fs_event = new uv_fs_event_t;
        uv_fs_event_init(_loop, _fs_event);
        _fs_event->data = this;
        uv_fs_event_start(_fs_event, on_fs_event, dir.c_str(), UV_FS_EVENT_RECURSIVE);
    }

    /**
     * 预先读取所有文件 用以最开始进行比较的情况
     **/
    void pre_read_files() {
        const std::string &root = _dir;
        traverseDirectory(root);
        _is_pre_read = true;
    }


    void add_file_suffix(const std::vector<std::string> &files) {

        for (auto &file: files) {
            _suffix_files.insert(file);
        }

    }

    template<typename F = std::function<void>(const FileWatcher *), typename ...Fs>
    void set_printCallbacks(F callback, Fs ... callbacks) {
        _print_callbacks.push_back(callback);
        if constexpr (sizeof...(Fs) > 0)
            _print_callbacks.push_back({callbacks...});
    }

    void watch() const {
        uv_run(_loop, UV_RUN_DEFAULT);
    }

    void stop_watch() const {
        printf("good bye");
        uv_stop(_loop);
    }


    ~FileWatcher() {
        for (auto &iterator: _files_versions) {
            auto &files = iterator.second;
            clear_files_version(files);
        }
        uv_stop(_loop);
        uv_loop_close(_loop);
        delete _fs_event;
    }


private:
    void show_same_file_version(const std::string &fileName) const {
        auto iterator = _files_versions.find(fileName);

        if (iterator == _files_versions.end()) {
            return;
        }

        auto files = iterator->second;
        for (auto file: files) {
            printf("%s\n", file->fileName.c_str());

        }
    }

    void traverseDirectory(const std::filesystem::path &path) {
        for (const auto &file: std::filesystem::recursive_directory_iterator(path)) {
            if (file.is_regular_file()) {
                std::string fileName = file.path().lexically_normal();
                std::string ext = get_suffix_fileName(fileName);
                if (_suffix_files.find(ext) == _suffix_files.end())
                    continue;
                add_file_info(FileInfo(fileName));
            }
        }
    }

    void add_file_info(const FileInfo &info) {
        std::vector<FileInfo *> &files_ = _files_versions[info.fileName];
        if (files_.size() > MAX_DIFF_SIZE) {
            FileInfo *last = files_.back();
            files_.pop_back();
            clear_files_version(files_);
            files_.push_back(last);
        }
        auto *inf = new FileInfo(info);
        _files_versions[info.fileName].emplace_back(inf);
    }

    std::string get_suffix_fileName(const std::string &fileName) const {
        auto path = std::filesystem::path(fileName);
        std::string pathFile = path.extension();
        return pathFile == "" ? pathFile : pathFile.substr(1);
    }

    static void on_fs_event(uv_fs_event_t *handle, const char *filename, int events, int status) {
        assert(handle->data);
        auto fw = static_cast<FileWatcher *>(handle->data);
        fw->_on_fs_event(handle, filename, events, status);
    }


    void _on_fs_event(uv_fs_event_t *handle, const char *filename, int events, int status) {
        assert(_loop == handle->loop);
        if (status < 0) {
            fprintf(stderr, "Error watching file: %s\n", uv_strerror(status));
            exit(1);
            return;
        }

        std::string suffix = get_suffix_fileName(filename);
        if (_suffix_files.find(suffix) == _suffix_files.end())
            return;
        _now_changed_file = filename;
        add_file_info(FileInfo(filename));
        if (_show && !_files_versions.empty()) {
            if (!_print_callbacks.empty()) {
                for (const auto &callback: _print_callbacks) {
                    callback(this);
                }
                return;
            }
            auto print = [] {
                printf("please set print_callback");
            };
            print();
        }
    }


    void clear_files_version(std::vector<FileInfo *> &files_version) {
        for (const FileInfo *f: files_version) {
            delete f;
        }
        files_version.clear();
    }
};


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
    unifiedDiff(files[first]->contents, files[second]->contents);
    printf("\n\n\n\n");
}

void show_title(const FileWatcher *watcher) {
    assert(watcher);
    static char buffer[80];
    std::fill(buffer, buffer + 80, 0);
    auto info = watcher->_files_versions.at(watcher->_now_changed_file).back();
    auto now_c = std::chrono::system_clock::to_time_t(info->timeval);
    auto now_tm = std::localtime(&now_c);
    std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", now_tm);
    printf("The file [%s] was modified at %s\n", info->fileName.c_str(), buffer);
}

int main(int argc, char **argv) {
    FileWatcher watcher;
    watcher.add_file_suffix({"txt", "cc", "c", "h"});
    watcher.set_printCallbacks(show_title, show_diff_file_content);
    watcher.pre_read_files();
    watcher.watch();
}

