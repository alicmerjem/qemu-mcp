#pragma once

#include <string>
#include <vector>
#include <sys/types.h>

struct QemuLaunchParams {
    std::string iso_path = "image.iso";
    std::string machine_type;
    int memory_mb = 128;
    int gdb_port = 1234;
    std::vector<std::string> extra_args;
};

class QemuInstance {
public:
    QemuInstance() = default;
    ~QemuInstance();

    QemuInstance(const QemuInstance&) = delete;
    QemuInstance& operator=(const QemuInstance&) = delete;

    void launch(const QemuLaunchParams& params);
    void terminate();
    bool is_alive() const;

    pid_t pid() const { return pid_; }
    int gdb_port() const { return gdb_port_; }
    const std::string& qmp_socket_path() const { return qmp_socket_path_; }
    const std::string& qmp_events_socket_path() const { return qmp_events_socket_path_; }
    const std::string& serial_log_path() const { return serial_log_path_; }
    const std::string& work_dir() const { return work_dir_; }

private:
    pid_t pid_ = -1;
    int gdb_port_ = 0;
    std::string qmp_socket_path_;
    std::string qmp_events_socket_path_;
    std::string serial_log_path_;
    std::string work_dir_;

    std::vector<std::string> build_argv(const QemuLaunchParams& params) const;
};