#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <uv.h>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "unidiff.h"

struct FileInfo {
    std::string fileName;
    std::string content;
    std::chrono::time_point<std::chrono::system_clock> timeval;
};


class FileWatcher {

private:
    static constexpr int MAX_DIFF_SIZE = 1 << 10;   // 最大比较文件变化个数
    static constexpr int MAX_BUFF_SIZE = (1 << 10) + 1;   // 最大读取文件缓冲区大小

private:
    uv_loop_t *_loop;
    uv_fs_event_t *_fs_event;

public:
    std::string _dir;
    bool _show;
    std::vector<FileInfo *> _contents_versions;
    std::unordered_map<std::string, std::vector<FileInfo *>> _files_versions;
    std::unordered_set<std::string> _suffix_files;
    vector<std::function<void(FileWatcher *)>> _print_callbacks;
    std::string now_changed_file;

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

    void watch() const {
        uv_run(_loop, UV_RUN_DEFAULT);
    }

    void add_file_suffix(const vector<string> &files) {
        for (auto &file: files) {
            _suffix_files.insert(file);
        }

    }

    void show_title() const {
        assert(!_contents_versions.empty());
        char buffer[80]{0};
        std::fill(buffer, buffer + 80, 0);
        auto info = _contents_versions.back();
        auto now_c = std::chrono::system_clock::to_time_t(info->timeval);
        auto now_tm = std::localtime(&now_c);
        std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", now_tm);
        printf("The file [%s] was modified at %s\n", info->fileName.c_str(), buffer);
    }


    template<typename F = function<void>(const FileWatcher *), typename ...Fs>
    void set_printCallbacks(F callback, Fs ... callbacks) {
        _print_callbacks.push_back(callback);
        if constexpr (sizeof...(Fs) > 0)
            _print_callbacks.push_back({callbacks...});
    }

    ~FileWatcher() {
        for (auto &c: _contents_versions)
            delete c;
        uv_stop(_loop);
        uv_loop_close(_loop);
        delete _fs_event;
    }


private:


    static std::string fs_read_all(uv_loop_t *loop, const char *filename) {
        char buf[MAX_BUFF_SIZE]{0};
        uv_fs_t fs;
        uv_buf_t iov = uv_buf_init(buf, sizeof(buf) - 1);
        int fd = uv_fs_open(loop, &fs, filename, O_RDONLY, 0, nullptr);
        std::string s;
        while (true) {
            fill(buf, buf + MAX_BUFF_SIZE, 0);
            uv_fs_t read_req;
            uv_fs_read(loop, &read_req, fd, &iov, 1, -1, nullptr);
            s += iov.base;
            if (read_req.result == 0) {
                break;
            }
        }
        return s;
    }

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

    void add_file_info(const FileInfo &info) {
        if (_contents_versions.size() > MAX_DIFF_SIZE) {
            contents_clear();
            _files_versions.clear();
        }

        auto *inf = new FileInfo(info);
        _contents_versions.emplace_back(inf);
        _files_versions[info.fileName].emplace_back(inf);
    }

    string get_suffix_fileName(const string &fileName) {
        size_t dot_pos = fileName.rfind('.');
        string suffix;
        if (dot_pos == std::string::npos) {
            suffix = "";
        } else {
            suffix = fileName.substr(dot_pos + 1);
        }

        return suffix;
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

        string suffix = get_suffix_fileName(filename);
        if (_suffix_files.find(suffix) == _suffix_files.end()) {
            return;
        }
        now_changed_file = filename;
        std::string content = fs_read_all(handle->loop, filename);
        add_file_info({
                              .fileName = filename,
                              .content =content,
                              .timeval = std::chrono::system_clock::now()
                      });

        if (_show && !_contents_versions.empty()) {
            if (!_print_callbacks.empty()) {
                for (const auto &callback: _print_callbacks) {
                    callback(this);
                }
            }
        }
    }

    void first_read(const string &fileName) {
        string content = fs_read_all(_loop, fileName.c_str());
        add_file_info({
                              .fileName = fileName,
                              .content =content,
                              .timeval = std::chrono::system_clock::now()
                      });
    }

    void contents_clear() {
        for (auto &content: _contents_versions)
            delete content;
        _contents_versions.clear();
    }
};


void show_diff_file_content(const FileWatcher *watcher) {
    assert(watcher);
    auto iterator = watcher->_files_versions.find(watcher->now_changed_file);
    if (iterator == watcher->_files_versions.end()) {
        return;
    }

    auto &files = iterator->second;

    if (files.size() <= 1) {
        return;
    }

    size_t last = files.size() - 1;
    size_t last_two = last - 1;
    unifiedDiff(files[last_two]->content, files[last]->content);
    printf("\n\n\n\n");
}

void show_title(const FileWatcher *watcher) {
    assert(watcher);
    char buffer[80]{0};
    std::fill(buffer, buffer + 80, 0);
    auto info = watcher->_contents_versions.back();
    auto now_c = std::chrono::system_clock::to_time_t(info->timeval);
    auto now_tm = std::localtime(&now_c);
    std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", now_tm);
    printf("The file [%s] was modified at %s\n", info->fileName.c_str(), buffer);
}

int main(int argc, char **argv) {
    FileWatcher watcher;
    watcher.add_file_suffix({"txt", "cc", "c"});
    watcher.set_printCallbacks(show_title, show_diff_file_content);
    watcher.watch();
}

