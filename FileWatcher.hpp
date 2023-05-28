#pragma once


#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <uv.h>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include "dtl/Color.hpp"
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


struct ConfigurationFileWatcher {
public:
    bool is_show;                               //是否展示
    bool is_recursive;                          //是否递归遍历文件
    bool is_pre_read;                           //是否进行预读
    const int MAX_DIFF;                         //最大比较文件变化个数
    const int MAX_BUFF;                         //最大读取文件缓冲区大小
    std::vector<std::string> _suffix_files;     //监听的文件后缀
    string root;                                //监听的根节点
};

class FileWatcher {

private:
    int MAX_DIFF_SIZE;
    int MAX_BUFF_SIZE;

private:
    uv_loop_t *_loop;
    uv_fs_event_t *_fs_event{};

private:
    void init_loop() {
        _fs_event = new uv_fs_event_t;
        uv_fs_event_init(_loop, _fs_event);
        _fs_event->data = this;
        int flag = _is_recursive ? UV_FS_EVENT_RECURSIVE : 0;
        uv_fs_event_start(_fs_event, on_fs_event, _dir.c_str(), flag);
    }

private:
    function<void(const FileWatcher *)> default_print_callback;

    static void _default_call_back(const FileWatcher *) {
        ::printf("please set callback\n");
    }

public:
    std::string _dir;
    bool _show;
    std::unordered_map<std::string, std::vector<FileInfo *>> _files_versions;
    std::unordered_set<std::string> _suffix_files;
    std::vector<std::function<void(FileWatcher *)>> _print_callbacks;
    std::string _now_changed_file;
    bool _is_pre_read;
    bool _is_recursive;

public:
    FileWatcher(const FileWatcher &) = delete;

    FileWatcher &operator=(const FileWatcher &) = delete;

    /**
     * 文件监听器的入口。
     * 默认监听当前文件夹，并展示内容
     * */
    explicit FileWatcher(const ConfigurationFileWatcher &config) : _loop(uv_default_loop()) {
        _dir = config.root;
        _show = config.is_show;
        _is_pre_read = config.is_pre_read;
        _is_recursive = config.is_recursive;
        auto &_suffix = config._suffix_files;
        _suffix_files = std::move(unordered_set(_suffix.begin(), _suffix.end()));
        MAX_DIFF_SIZE = config.MAX_DIFF ? config.MAX_DIFF : 1 << 4;
        MAX_BUFF_SIZE = config.MAX_BUFF ? config.MAX_BUFF : (1 << 10) + 1;
        if (_is_pre_read)
            pre_read_files();
        init_loop();
    }

    /**
     * 预先读取所有文件 用以最开始进行比较的情况
     **/
    void pre_read_files() {
        namespace fs = std::filesystem;
        const std::string &root = _dir;
        _is_recursive ? traverseDirectory<fs::recursive_directory_iterator>(root)
                      : traverseDirectory<fs::directory_iterator>(root);
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
    template<typename Iterator>
    void traverseDirectory(const std::filesystem::path &path) {
        for (const auto &file: Iterator(path)) {
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

    static std::string get_suffix_fileName(const std::string &fileName) {
        auto path = std::filesystem::path(fileName);
        std::string pathFile = path.extension();
        return pathFile.empty() ? pathFile : pathFile.substr(1);
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
        _now_changed_file = filename == "."s ? filename : _dir + "/" + filename;

        // ./
        if (_now_changed_file.size() > 2)
            if (_now_changed_file[0] == '.' && _now_changed_file[1] == '/')
                _now_changed_file = _now_changed_file.substr(2);


        add_file_info(FileInfo(_now_changed_file));
        if (_show && !_files_versions.empty()) {
            if (_print_callbacks.empty())
                _print_callbacks.emplace_back(default_print_callback);
            for (const auto &callback: _print_callbacks) {
                callback(this);
            }
            return;
        }
    }


    static void clear_files_version(std::vector<FileInfo *> &files_version) {
        for (const FileInfo *f: files_version) {
            delete f;
        }
        files_version.clear();
    }
};

