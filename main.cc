#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <uv.h>
#include <cassert>
#include <unordered_map>
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
    static constexpr int MAX_BUFF_SIZE = 1 << 10;   // 最大读取文件缓冲区大小

private:
    std::string _dir;
    uv_loop_t *_loop;
    uv_fs_event_t *_fs_event;
    bool _show;

    std::vector<FileInfo *> _contents;
    std::unordered_map<std::string, std::vector<FileInfo *>> _files;

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

    void show_title() const {
        assert(!_contents.empty());
        char buffer[80]{0};
        std::fill(buffer, buffer + 80, 0);
        auto info = _contents.back();
        auto now_c = std::chrono::system_clock::to_time_t(info->timeval);
        auto now_tm = std::localtime(&now_c);
        std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", now_tm);
        printf("The file [%s] was modified at %s\n", info->fileName.c_str(), buffer);
    }

    ~FileWatcher() {
        for (auto &c: _contents)
            delete c;
        uv_stop(_loop);
        uv_loop_close(_loop);
        delete _fs_event;
    }


private:


    static std::string fs_read_all(uv_loop_t *loop, const char *filename) {
        char buf[MAX_BUFF_SIZE]{0};
        uv_fs_t fs;
        uv_buf_t iov = uv_buf_init(buf, sizeof(buf));
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
            fill(buf, buf + MAX_BUFF_SIZE, 0);
        }
        return s;
    }

    void show_same_file_version(const std::string &fileName) const {
        auto iterator = _files.find(fileName);

        if (iterator == _files.end()) {
            return;
        }

        auto files = iterator->second;
        for (auto file: files) {
            printf("%s\n", file->fileName.c_str());

        }
    }


    void show_diff_file_content(const std::string &fileName) const {
        auto iterator = _files.find(fileName);

        if (iterator == _files.end()) {
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


    void add_file_info(const FileInfo &info) {
        if (_contents.size() > MAX_DIFF_SIZE) {
            contents_clear();
            _files.clear();
        }

        auto *inf = new FileInfo(info);
        _contents.emplace_back(inf);
        _files[info.fileName].emplace_back(inf);
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
            return;
        }

        if (events & UV_CHANGE) {
            std::string content = fs_read_all(handle->loop, filename);
            add_file_info({
                                  .fileName = filename,
                                  .content =content,
                                  .timeval = std::chrono::system_clock::now()
                          });

            if (_show && !_contents.empty()) {
                show_title();
                show_diff_file_content(filename);
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
        for (auto &content: _contents)
            delete content;
        _contents.clear();
    }


};

int main(int argc, char **argv) {
    FileWatcher watcher;
    watcher.watch();
}

