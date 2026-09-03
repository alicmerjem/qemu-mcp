#include "qemu_instance.hpp"

#include <stdexcept>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

namespace {

std::string make_work_dir(pid_t tag) {
    std::ostringstream oss;
    oss << "/tmp/qemu-mcp-" << tag;
    std::string dir = oss.str();
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        throw std::runtime_error("Failed to create work dir " + dir + ": " + strerror(errno));
    }
    return dir;
}

}

QemuInstance::~QemuInstance() {
    terminate();
}

std::vector<std::string> QemuInstance::build_argv(const QemuLaunchParams& params) const {
    std::vector<std::string> argv;
    argv.push_back("qemu-system-x86_64");

    argv.push_back("-cdrom");
    argv.push_back(params.iso_path);

    if (!params.machine_type.empty()) {
        argv.push_back("-machine");
        argv.push_back(params.machine_type);
    }

    argv.push_back("-m");
    argv.push_back(std::to_string(params.memory_mb));

    argv.push_back("-S");

    argv.push_back("-gdb");
    argv.push_back("tcp::" + std::to_string(params.gdb_port));

    argv.push_back("-qmp");
    argv.push_back("unix:" + qmp_socket_path_ + ",server,nowait");

    argv.push_back("-serial");
    argv.push_back("file:" + serial_log_path_);

    argv.push_back("-display");
    argv.push_back("none");

    for (const auto& extra : params.extra_args) {
        argv.push_back(extra);
    }

    return argv;
}

void QemuInstance::launch(const QemuLaunchParams& params) {
    if (is_alive()) {
        throw std::runtime_error("QEMU instance already running (pid " + std::to_string(pid_) + "); terminate it first");
    }
    if (params.iso_path.empty()) {
        throw std::runtime_error("iso_path is required");
    }

    gdb_port_ = params.gdb_port;
    work_dir_ = make_work_dir(getpid());
    qmp_socket_path_ = work_dir_ + "/qmp.sock";
    serial_log_path_ = work_dir_ + "/serial.log";

    int fd = open(serial_log_path_.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to create serial log " + serial_log_path_ + ": " + strerror(errno));
    }
    close(fd);

    std::vector<std::string> argv_strings = build_argv(params);

    std::vector<char*> argv;
    argv.reserve(argv_strings.size() + 1);
    for (auto& s : argv_strings) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error(std::string("fork() failed: ") + strerror(errno));
    }

    if (child == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }

    pid_ = child;
}

bool QemuInstance::is_alive() const {
    if (pid_ <= 0) return false;
    int status = 0;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    return false;
}

void QemuInstance::terminate() {
    if (pid_ <= 0) return;

    if (is_alive()) {
        kill(pid_, SIGTERM);
        for (int i = 0; i < 20 && is_alive(); ++i) {
            usleep(50 * 1000);
        }
        if (is_alive()) {
            kill(pid_, SIGKILL);
        }
    }

    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = -1;
}