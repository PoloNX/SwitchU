#pragma once
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <switchu/log_utils.hpp>
#include <switch.h>

namespace switchu {

class FileLog {
public:
    static constexpr const char* LOG_DIR = "sdmc:/config/SwitchU";
    static constexpr const char* LOG_EXTENSION = ".log";
    static constexpr size_t MAX_LOG_FILES = 5;
    static constexpr size_t MAX_ARCHIVED_LOGS = MAX_LOG_FILES - 1;
    static constexpr size_t MAX_PENDING_LINES = 2048;

    // shareable=true keeps the log readable (e.g. over FTP) while the process
    // is still running: the file is only held open while a batch is being
    // appended, then closed again. Lines are coalesced for a short window to
    // keep SD open/close churn low. Intended for the long-lived daemon.
    static void open(const char* tag, bool shareable = false) {
        auto& self = inst();
        stop_writer(self);

        log_detail::ensure_log_dir(LOG_DIR);
        {
            std::lock_guard<std::mutex> fileLock(self.m_fileMutex);
            close_current_file(self);

            self.m_shareable = shareable;
            const bool can_truncate = log_detail::rotate_current_log(
                LOG_DIR, tag, LOG_EXTENSION, MAX_ARCHIVED_LOGS);

            char path[256];
            log_detail::build_current_log_path(path, sizeof(path), LOG_DIR, tag, LOG_EXTENSION);
            self.m_path = path;
            self.m_file.open(path, canTruncateMode(can_truncate));
            if (self.m_file.is_open()) {
                self.m_hasSession = true;
                char timestamp[32];
                log_detail::format_line_timestamp(timestamp, sizeof(timestamp));
                self.m_file << '[' << timestamp << "] === " << tag << " log start ===\n";
                self.m_file.flush();
                if (self.m_shareable)
                    self.m_file.close();
            }
        }

        {
            std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
            self.m_stop = false;
            self.m_acceptLogs = true;
            self.m_writerBusy = false;
            self.m_flushWanted = false;
            self.m_droppedLines = 0;
        }
        self.m_writer = std::thread([&self]() { writer_loop(self); });
    }

    
    static void logCommit(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log("%s", buf);
        flush();
        fsdevCommitDevice("sdmc");
    }

    static void close() {
        auto& self = inst();
        stop_writer(self);
        std::lock_guard<std::mutex> fileLock(self.m_fileMutex);
        close_current_file(self);
    }

    static void log(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        char timestamp[32];
        log_detail::format_line_timestamp(timestamp, sizeof(timestamp));

        auto& self = inst();
        std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
        if (!self.m_acceptLogs)
            return;
        if (self.m_pendingLines.size() >= MAX_PENDING_LINES) {
            self.m_pendingLines.pop_front();
            ++self.m_droppedLines;
        }
        self.m_pendingLines.emplace_back(std::string("[") + timestamp + "] " + buf);
        self.m_queueCv.notify_one();
    }

private:
    static FileLog& inst() { static FileLog s; return s; }

    static std::ios::openmode canTruncateMode(bool canTruncate) {
        return canTruncate ? std::ios::out | std::ios::trunc
                           : std::ios::out | std::ios::app;
    }

    static void flush() {
        auto& self = inst();
        {
            std::unique_lock<std::mutex> queueLock(self.m_queueMutex);
            // Skip the shareable-mode coalescing delay for explicit commits.
            self.m_flushWanted = true;
            self.m_queueCv.notify_all();
            self.m_drainedCv.wait(queueLock, [&self]() {
                return self.m_pendingLines.empty() && !self.m_writerBusy;
            });
            self.m_flushWanted = false;
        }
        std::lock_guard<std::mutex> fileLock(self.m_fileMutex);
        if (self.m_file.is_open())
            self.m_file.flush();
    }

    static void writer_loop(FileLog& self) {
        std::vector<std::string> batch;
        for (;;) {
            size_t droppedLines = 0;
            {
                std::unique_lock<std::mutex> queueLock(self.m_queueMutex);
                self.m_queueCv.wait(queueLock, [&self]() {
                    return self.m_stop || !self.m_pendingLines.empty();
                });
                if (self.m_pendingLines.empty() && self.m_stop)
                    break;

                // Shareable mode: let a short burst of lines accumulate so each
                // open/append/close cycle writes more than one line.
                if (self.m_shareable && !self.m_stop && !self.m_flushWanted) {
                    self.m_queueCv.wait_for(queueLock, std::chrono::milliseconds(250),
                        [&self]() { return self.m_stop || self.m_flushWanted; });
                }

                self.m_writerBusy = true;
                droppedLines = self.m_droppedLines;
                self.m_droppedLines = 0;
                batch.reserve(self.m_pendingLines.size() + (droppedLines ? 1 : 0));
                while (!self.m_pendingLines.empty()) {
                    batch.push_back(std::move(self.m_pendingLines.front()));
                    self.m_pendingLines.pop_front();
                }
            }

            bool requeue = false;
            {
                std::lock_guard<std::mutex> fileLock(self.m_fileMutex);
                if (self.m_shareable && !self.m_file.is_open() && self.m_hasSession)
                    self.m_file.open(self.m_path, std::ios::out | std::ios::app);
                if (self.m_file.is_open()) {
                    if (droppedLines)
                        self.m_file << "[log] dropped " << droppedLines
                                    << " pending lines while SD was busy\n";
                    for (const auto& line : batch)
                        self.m_file << line << '\n';
                    self.m_file.flush();
                    if (self.m_shareable)
                        self.m_file.close();
                } else if (self.m_shareable && self.m_hasSession) {
                    // Someone (e.g. an FTP client) has the file open right now;
                    // keep the lines and retry on the next cycle.
                    requeue = true;
                }
            }
            if (requeue) {
                std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
                if (self.m_stop || self.m_flushWanted) {
                    // Shutting down or committing: don't spin on a locked file.
                    self.m_droppedLines += droppedLines + batch.size();
                } else {
                    // Re-insert at the front, keeping the newest lines if over cap.
                    for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                        if (self.m_pendingLines.size() >= MAX_PENDING_LINES) {
                            ++self.m_droppedLines;
                            continue;
                        }
                        self.m_pendingLines.emplace_front(std::move(*it));
                    }
                    self.m_droppedLines += droppedLines;
                }
            }
            batch.clear();

            {
                std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
                self.m_writerBusy = false;
                if (self.m_pendingLines.empty())
                    self.m_drainedCv.notify_all();
            }
        }

        std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
        self.m_writerBusy = false;
        self.m_drainedCv.notify_all();
    }

    static void stop_writer(FileLog& self) {
        {
            std::lock_guard<std::mutex> queueLock(self.m_queueMutex);
            self.m_acceptLogs = false;
            self.m_stop = true;
            self.m_queueCv.notify_all();
        }
        if (self.m_writer.joinable())
            self.m_writer.join();
    }

    static void close_current_file(FileLog& self) {
        if (!self.m_hasSession)
            return;
        self.m_hasSession = false;

        if (!self.m_file.is_open() && self.m_shareable)
            self.m_file.open(self.m_path, std::ios::out | std::ios::app);
        if (!self.m_file.is_open())
            return;

        char timestamp[32];
        log_detail::format_line_timestamp(timestamp, sizeof(timestamp));
        self.m_file << '[' << timestamp << "] === log end ===\n";
        self.m_file.close();
    }

    std::mutex m_queueMutex;
    std::mutex m_fileMutex;
    std::condition_variable m_queueCv;
    std::condition_variable m_drainedCv;
    std::deque<std::string> m_pendingLines;
    std::thread m_writer;
    bool m_stop = true;
    bool m_acceptLogs = false;
    bool m_writerBusy = false;
    bool m_flushWanted = false;
    bool m_shareable = false;
    bool m_hasSession = false;
    size_t m_droppedLines = 0;
    std::string m_path;
    std::ofstream m_file;
};

}
