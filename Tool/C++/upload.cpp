#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include "puff.h"
}

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kVersion[] = L"3.0.0";
constexpr wchar_t kUserAgent[] = L"Firmware-Transfer/3.0 (C++; WinHTTP)";
constexpr std::uint64_t kDefaultMaximumFirmwareBytes = 16ull * 1024ull * 1024ull;
constexpr std::uint64_t kHardMaximumFirmwareBytes = 512ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumResponseBytes = 1024u * 1024u;

enum ExitCode : int {
    success = 0,
    usage_error = 2,
    input_error = 3,
    network_error = 4,
    endpoint_error = network_error,
    upload_error = network_error,
    protocol_error = 5,
    response_error = protocol_error,
    integrity_error = 6,
    upload_rejected = 7,
    post_verify_error = 8,
    reboot_timeout = post_verify_error,
    firmware_mismatch = post_verify_error,
    local_io_error = 9,
    internal_error = local_io_error,
    cancelled = 130,
};

class AppError final : public std::runtime_error {
public:
    AppError(
        ExitCode code,
        std::string message,
        bool address_retryable = false,
        bool watch_transient = false)
        : std::runtime_error(std::move(message)),
          code_(code),
          address_retryable_(address_retryable),
          watch_transient_(watch_transient) {}

    [[nodiscard]] ExitCode code() const noexcept { return code_; }
    [[nodiscard]] bool address_retryable() const noexcept { return address_retryable_; }
    [[nodiscard]] bool watch_transient() const noexcept { return watch_transient_; }

private:
    ExitCode code_;
    bool address_retryable_;
    bool watch_transient_;
};

class WinHttpHandle final {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) noexcept : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                WinHttpCloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] HINTERNET release() noexcept {
        HINTERNET value = handle_;
        handle_ = nullptr;
        return value;
    }

private:
    HINTERNET handle_;
};

class Winsock final {
public:
    Winsock() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw AppError(endpoint_error, "Winsock initialization failed: " + std::to_string(result));
        }
        active_ = true;
    }

    ~Winsock() {
        if (active_) {
            WSACleanup();
        }
    }

    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;

private:
    bool active_ = false;
};

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw AppError(internal_error, "Text is too long to encode.");
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw AppError(internal_error, "UTF-8 conversion failed.");
    }
    std::string output(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            output.data(), required, nullptr, nullptr) != required) {
        throw AppError(internal_error, "UTF-8 conversion failed.");
    }
    return output;
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw AppError(input_error, "Text is too long to decode.");
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) {
        throw AppError(input_error, "Input is not valid UTF-8.");
    }
    std::wstring output(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            output.data(), required) != required) {
        throw AppError(input_error, "Input is not valid UTF-8.");
    }
    return output;
}

std::string windows_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (size && buffer) {
        std::wstring text(buffer, size);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
        message = wide_to_utf8(text);
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "Windows error " + std::to_string(error);
    } else {
        message += " (" + std::to_string(error) + ")";
    }
    return message;
}

[[noreturn]] void throw_last_error(
    ExitCode code,
    std::string_view operation,
    bool address_retryable = false) {
    throw AppError(
        code,
        std::string(operation) + ": " + windows_message(GetLastError()),
        address_retryable);
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string sanitize_console_text(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x20 || character == 0x7f ||
            (character >= 0x80 && character <= 0x9f)) {
            output.push_back('?');
            continue;
        }
        // Strip UTF-8 encoded C1 controls as well as their invalid single-byte form.
        if (character == 0xc2 && index + 1 < value.size()) {
            const unsigned char next = static_cast<unsigned char>(value[index + 1]);
            if (next >= 0x80 && next <= 0x9f) {
                output.push_back('?');
                ++index;
                continue;
            }
        }
        output.push_back(static_cast<char>(character));
    }
    return output;
}

bool is_hex_md5(std::string_view value) {
    return value.size() == 32 &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

std::string basename_utf8(std::string value) {
    const std::size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) {
        value.erase(0, slash + 1);
    }
    return value;
}

struct Console {
    bool color = false;
    bool error_color = false;
    bool live_output = false;
    mutable bool progress_visible = false;
    mutable unsigned last_redirected_percent = 101;

    static Console configure(bool force_color, bool disable_color) {
        Console console;
        SetConsoleOutputCP(CP_UTF8);

        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD output_mode = 0;
        if (output != INVALID_HANDLE_VALUE && output != nullptr &&
            GetConsoleMode(output, &output_mode)) {
            console.live_output = true;
            if (!disable_color &&
                SetConsoleMode(output, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                console.color = true;
            }
        }

        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        DWORD error_mode = 0;
        if (error != INVALID_HANDLE_VALUE && error != nullptr &&
            GetConsoleMode(error, &error_mode) && !disable_color &&
            SetConsoleMode(error, error_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            console.error_color = true;
        }

        if (force_color) {
            console.color = true;
            console.error_color = true;
            console.live_output = true;
        }
        return console;
    }

    [[nodiscard]] std::string paint(std::string_view code, std::string_view text) const {
        if (!color) {
            return std::string(text);
        }
        return "\x1b[" + std::string(code) + "m" + std::string(text) + "\x1b[0m";
    }

    [[nodiscard]] std::string paint_error(std::string_view code, std::string_view text) const {
        if (!error_color) {
            return std::string(text);
        }
        return "\x1b[" + std::string(code) + "m" + std::string(text) + "\x1b[0m";
    }

    void clear_progress() const {
        if (!progress_visible) {
            return;
        }
        if (color) {
            std::cout << "\r\x1b[2K\x1b]9;4;0;0\x07" << std::flush;
        } else if (live_output) {
            std::cout << "\r" << std::string(80, ' ') << "\r" << std::flush;
        } else {
            std::cout << std::flush;
        }
        progress_visible = false;
        last_redirected_percent = 101;
    }

    void progress(std::string_view message, unsigned percent) const {
        const unsigned bounded = std::min(percent, 100u);
        progress_visible = true;
        if (!live_output) {
            const unsigned bucket = bounded / 10;
            if (last_redirected_percent != bucket) {
                std::cout << message << " " << bounded << "%\n";
                last_redirected_percent = bucket;
            }
            return;
        }
        std::cout << (color ? "\r\x1b[2K" : "\r") << paint("90", message) << " "
                  << paint("1;36", std::to_string(bounded) + "%") << std::flush;
        if (color) {
            std::cout << "\x1b]9;4;1;" << bounded << "\x07" << std::flush;
        }
    }
};

struct Options {
    fs::path firmware;
    fs::path manifest;
    fs::path backup;
    fs::path download;
    std::wstring endpoint;
    std::wstring authorization;
    int connect_timeout_ms = 20000;
    int request_timeout_ms = 10000;
    int reboot_timeout_ms = 30000;
    int poll_interval_ms = 750;
    int initial_wait_ms = 750;
    int watch_poll_ms = 250;
    int debounce_ms = 1000;
    std::uint64_t maximum_firmware_bytes = kDefaultMaximumFirmwareBytes;
    std::uint64_t maximum_download_bytes = kDefaultMaximumFirmwareBytes;
    bool validate_only = false;
    bool watch = false;
    bool force = false;
    bool verify = true;
    bool resume = true;
    bool quiet = false;
    bool force_color = false;
    bool no_color = false;
    bool help = false;
    bool version = false;
    bool self_test = false;
};

std::atomic<bool> watch_stop_requested{false};

BOOL WINAPI watch_console_handler(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        watch_stop_requested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}

class WatchConsoleHandler final {
public:
    WatchConsoleHandler() {
        watch_stop_requested.store(false, std::memory_order_relaxed);
        installed_ = SetConsoleCtrlHandler(watch_console_handler, TRUE) != FALSE;
        if (!installed_) {
            throw_last_error(internal_error, "Could not install Ctrl+C handler");
        }
    }

    ~WatchConsoleHandler() {
        if (installed_) {
            SetConsoleCtrlHandler(watch_console_handler, FALSE);
        }
    }

    WatchConsoleHandler(const WatchConsoleHandler&) = delete;
    WatchConsoleHandler& operator=(const WatchConsoleHandler&) = delete;

private:
    bool installed_ = false;
};

bool wait_interruptibly(int milliseconds) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!watch_stop_requested.load(std::memory_order_relaxed)) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            return true;
        }
        std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(50)));
    }
    return false;
}

struct FileIdentity {
    std::uintmax_t size = 0;
    std::int64_t modified_ticks = 0;

    friend bool operator==(const FileIdentity& left, const FileIdentity& right) {
        return left.size == right.size && left.modified_ticks == right.modified_ticks;
    }
};

struct BuildIdentity {
    FileIdentity firmware;
    FileIdentity manifest;

    friend bool operator==(const BuildIdentity& left, const BuildIdentity& right) {
        return left.firmware == right.firmware && left.manifest == right.manifest;
    }
};

class WatchDebouncer final {
public:
    explicit WatchDebouncer(std::int64_t debounce_ms) : debounce_ms_(debounce_ms) {}

    bool observe(
        const std::optional<BuildIdentity>& candidate,
        std::int64_t now_ms) {
        if (!candidate) {
            has_observed_ = false;
            return false;
        }
        if (!has_observed_ || !(observed_ == *candidate)) {
            observed_ = *candidate;
            has_observed_ = true;
            stable_since_ms_ = now_ms;
            return false;
        }
        if (already_attempted(*candidate)) {
            return false;
        }
        return now_ms - stable_since_ms_ >= debounce_ms_;
    }

    void mark_attempted(const BuildIdentity& candidate) {
        if (!already_attempted(candidate)) {
            attempted_.push_back(candidate);
        }
    }

    [[nodiscard]] bool already_attempted(const BuildIdentity& candidate) const {
        return std::find(attempted_.begin(), attempted_.end(), candidate) !=
            attempted_.end();
    }

private:
    std::int64_t debounce_ms_;
    std::int64_t stable_since_ms_ = 0;
    BuildIdentity observed_{};
    bool has_observed_ = false;
    std::vector<BuildIdentity> attempted_;
};

std::int64_t monotonic_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int parse_positive_int(const std::wstring& text, std::wstring_view option, int maximum) {
    if (text.empty()) {
        throw AppError(usage_error, wide_to_utf8(option) + " requires a value.");
    }
    std::size_t consumed = 0;
    long long parsed = 0;
    try {
        parsed = std::stoll(text, &consumed, 10);
    } catch (...) {
        throw AppError(usage_error, wide_to_utf8(option) + " requires an integer.");
    }
    if (consumed != text.size() || parsed <= 0 || parsed > maximum) {
        throw AppError(usage_error, wide_to_utf8(option) + " is outside the supported range.");
    }
    return static_cast<int>(parsed);
}

std::uint64_t parse_positive_size(
    const std::wstring& text,
    std::wstring_view option,
    std::uint64_t maximum) {
    if (text.empty()) {
        throw AppError(usage_error, wide_to_utf8(option) + " requires a value.");
    }
    std::size_t consumed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(text, &consumed, 10);
    } catch (...) {
        throw AppError(usage_error, wide_to_utf8(option) + " requires an integer.");
    }
    if (consumed != text.size() || parsed == 0 || parsed > maximum) {
        throw AppError(usage_error, wide_to_utf8(option) + " is outside the supported range.");
    }
    return static_cast<std::uint64_t>(parsed);
}

std::optional<std::wstring> environment_value(const wchar_t* name) {
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_SUCCESS || error == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        throw AppError(
            internal_error,
            "Could not read environment variable: " + windows_message(error));
    }
    std::vector<wchar_t> value(required);
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0 || copied >= required) {
        throw AppError(
            internal_error,
            "Could not read environment variable: " + windows_message(GetLastError()));
    }
    std::wstring result(value.data(), copied);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

void validate_authorization(const std::wstring& value) {
    if (value.empty()) {
        return;
    }
    if (value.size() > 8192 ||
        std::any_of(value.begin(), value.end(), [](wchar_t character) {
            return character == L'\r' || character == L'\n' ||
                character < 0x20 || character == 0x7f;
        })) {
        throw AppError(usage_error, "Authorization contains invalid characters.");
    }
}

Options parse_arguments(int argc, wchar_t* argv[]) {
    Options options;
    std::vector<std::wstring> positional;
    std::optional<std::wstring> explicit_authorization;
    std::optional<std::wstring> explicit_bearer;

    auto require_value = [&](int& index, std::wstring_view option) -> std::wstring {
        if (index + 1 >= argc) {
            throw AppError(usage_error, wide_to_utf8(option) + " requires a value.");
        }
        return argv[++index];
    };

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            options.help = true;
        } else if (argument == L"--version") {
            options.version = true;
        } else if (argument == L"--self-test") {
            options.self_test = true;
        } else if (argument == L"--validate-only") {
            options.validate_only = true;
        } else if (argument == L"--check") {
            options.validate_only = true;
        } else if (argument == L"--watch") {
            options.watch = true;
        } else if (argument == L"--immediate" || argument == L"-i") {
            options.watch = false;
        } else if (argument == L"--force") {
            options.force = true;
        } else if (argument == L"--no-verify") {
            options.verify = false;
        } else if (argument == L"--no-resume") {
            options.resume = false;
        } else if (argument == L"--quiet") {
            options.quiet = true;
        } else if (argument == L"--no-color") {
            options.no_color = true;
        } else if (argument == L"--color") {
            options.force_color = true;
        } else if (argument == L"--endpoint" || argument == L"--url" || argument == L"-e") {
            options.endpoint = require_value(index, argument);
        } else if (argument.rfind(L"--endpoint=", 0) == 0) {
            options.endpoint = argument.substr(11);
        } else if (argument.rfind(L"--url=", 0) == 0) {
            options.endpoint = argument.substr(6);
        } else if (argument == L"--md5" || argument == L"--manifest") {
            options.manifest = require_value(index, argument);
        } else if (argument.rfind(L"--manifest=", 0) == 0) {
            options.manifest = argument.substr(11);
        } else if (argument.rfind(L"--md5=", 0) == 0) {
            options.manifest = argument.substr(6);
        } else if (argument == L"--backup") {
            options.backup = require_value(index, argument);
        } else if (argument.rfind(L"--backup=", 0) == 0) {
            options.backup = argument.substr(9);
        } else if (argument == L"--download") {
            options.download = require_value(index, argument);
        } else if (argument.rfind(L"--download=", 0) == 0) {
            options.download = argument.substr(11);
        } else if (argument == L"--authorization") {
            explicit_authorization = require_value(index, argument);
        } else if (argument.rfind(L"--authorization=", 0) == 0) {
            explicit_authorization = argument.substr(16);
        } else if (argument == L"--bearer") {
            explicit_bearer = require_value(index, argument);
        } else if (argument.rfind(L"--bearer=", 0) == 0) {
            explicit_bearer = argument.substr(9);
        } else if (argument == L"--max-firmware-size") {
            options.maximum_firmware_bytes = parse_positive_size(
                require_value(index, argument), argument, kHardMaximumFirmwareBytes);
        } else if (argument == L"--max-download-size") {
            options.maximum_download_bytes = parse_positive_size(
                require_value(index, argument), argument, kHardMaximumFirmwareBytes);
        } else if (argument == L"--connect-timeout-ms") {
            options.connect_timeout_ms = parse_positive_int(
                require_value(index, argument), argument, 10 * 60 * 1000);
        } else if (argument == L"--request-timeout-ms" || argument == L"--timeout") {
            options.request_timeout_ms = parse_positive_int(
                require_value(index, argument), argument, 60 * 60 * 1000);
        } else if (argument == L"--reboot-timeout-ms" || argument == L"--verify-timeout") {
            options.reboot_timeout_ms = parse_positive_int(
                require_value(index, argument), argument, 60 * 60 * 1000);
        } else if (argument == L"--poll-interval-ms" || argument == L"--verify-interval") {
            options.poll_interval_ms = parse_positive_int(
                require_value(index, argument), argument, 60 * 1000);
        } else if (argument == L"--initial-wait-ms") {
            options.initial_wait_ms = parse_positive_int(
                require_value(index, argument), argument, 60 * 1000);
        } else if (argument == L"--watch-poll-ms") {
            options.watch_poll_ms = parse_positive_int(
                require_value(index, argument), argument, 60 * 1000);
        } else if (argument == L"--debounce-ms") {
            options.debounce_ms = parse_positive_int(
                require_value(index, argument), argument, 10 * 60 * 1000);
        } else if (!argument.empty() && argument.front() == L'-') {
            throw AppError(usage_error, "Unknown option: " + wide_to_utf8(argument));
        } else {
            positional.push_back(argument);
        }
    }

    if (options.help || options.version || options.self_test) {
        return options;
    }
    if (!options.download.empty()) {
        if (!positional.empty()) {
            throw AppError(usage_error, "--download does not accept a firmware input.");
        }
        if (options.watch || options.validate_only || !options.backup.empty() || options.force) {
            throw AppError(
                usage_error,
                "--download cannot be combined with --watch, --check, --backup, or --force.");
        }
    } else if (positional.size() != 1) {
        throw AppError(usage_error, "Provide exactly one firmware .bin path.");
    }
    if (options.watch && options.validate_only) {
        throw AppError(usage_error, "--watch and --validate-only cannot be combined.");
    }
    if (options.validate_only && !options.backup.empty()) {
        throw AppError(usage_error, "--backup cannot be combined with --check.");
    }
    if (options.download.empty()) {
        options.firmware = fs::path(positional.front());
        if (options.manifest.empty()) {
            options.manifest = fs::path(positional.front() + L".md5");
        }
    }
    if (options.endpoint.empty()) {
        if (const auto value = environment_value(L"UPDATE_API")) {
            options.endpoint = *value;
        }
    }
    if (!options.validate_only && options.endpoint.empty()) {
        throw AppError(
            usage_error,
            "Provide --endpoint URL or set the UPDATE_API environment variable.");
    }

    if (explicit_authorization && explicit_bearer) {
        throw AppError(usage_error, "Use only one of --authorization and --bearer.");
    }
    if (!explicit_authorization && !explicit_bearer) {
        explicit_authorization = environment_value(L"UPDATE_AUTHORIZATION");
        const auto bearer = environment_value(L"UPDATE_BEARER_TOKEN");
        const auto token = environment_value(L"UPDATE_TOKEN");
        if (bearer && token && *bearer != *token) {
            throw AppError(
                usage_error,
                "UPDATE_BEARER_TOKEN and UPDATE_TOKEN disagree; provide one credential.");
        }
        explicit_bearer = bearer ? bearer : token;
        if (explicit_authorization && explicit_bearer) {
            throw AppError(
                usage_error,
                "UPDATE_AUTHORIZATION cannot be combined with a bearer-token environment variable.");
        }
    }
    if (explicit_bearer) {
        if (explicit_bearer->empty()) {
            throw AppError(usage_error, "Bearer token must not be empty.");
        }
        options.authorization = L"Bearer " + *explicit_bearer;
    } else if (explicit_authorization) {
        options.authorization = *explicit_authorization;
    }
    validate_authorization(options.authorization);
    return options;
}

void print_usage() {
    std::cout
        << "Firmware transfer tool (native C++) " << wide_to_utf8(kVersion) << "\n\n"
        << "Usage:\n"
        << "  FirmwareTransferCpp.exe [options] firmware.bin\n"
        << "  FirmwareTransferCpp.exe --download PATH [options]\n\n"
        << "Endpoint:\n"
        << "  -e, --endpoint, --url URL   OTA endpoint (or UPDATE_API)\n"
        << "      --authorization VALUE   Complete Authorization value (or UPDATE_AUTHORIZATION)\n"
        << "      --bearer TOKEN          Bearer token (or UPDATE_BEARER_TOKEN / UPDATE_TOKEN)\n"
        << "      --manifest, --md5 PATH  Strict manifest (default: firmware.bin.md5)\n\n"
        << "Operation:\n"
        << "      --watch                 Upload each newest stable build continuously\n"
        << "  -i, --immediate             Explicit one-shot upload (the C++ default)\n"
        << "      --watch-poll-ms N       Watch polling interval (default: 250)\n"
        << "      --debounce-ms N         Required unchanged interval (default: 1000)\n"
        << "      --check, --validate-only  Validate gzip and both hashes offline\n"
        << "      --force                 Upload even when the raw hash is already installed\n"
        << "      --backup PATH           Verified atomic pre-upload firmware backup\n"
        << "      --download PATH         Verified atomic download without upload\n"
        << "      --no-resume             Ignore an existing PATH.part download\n"
        << "      --no-verify             Explicitly disable post-reboot verification\n"
        << "      --connect-timeout-ms N  Connect timeout (default: 20000)\n"
        << "      --request-timeout-ms N  Request timeout (default: 10000)\n"
        << "      --reboot-timeout-ms N   Post-hash deadline (default: 30000)\n"
        << "      --poll-interval-ms N    Verification interval (default: 750)\n"
        << "      --initial-wait-ms N     Initial reboot delay (default: 750)\n"
        << "      --max-firmware-size N   Compressed/raw safety limit in bytes\n"
        << "      --max-download-size N   Download safety limit in bytes\n\n"
        << "Display:\n"
        << "      --quiet                 Suppress informational output\n"
        << "      --color                 Force VT100 color\n"
        << "      --no-color              Disable VT100 color\n"
        << "  -h, --help                  Show this help\n"
        << "      --version               Show version\n\n"
        << "Exit codes: 0 success/no-op, 2 usage, 3 local input/manifest, 4 network,\n"
        << "            5 device protocol, 6 integrity, 7 upload rejected,\n"
        << "            8 post-verify, 9 local I/O/internal, 130 interrupted.\n";
}

std::optional<FileIdentity> read_file_identity(
    const fs::path& path,
    std::string& reason,
    std::string_view label) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error || !fs::is_regular_file(status)) {
        reason = "Waiting for " + std::string(label) + ": " +
            sanitize_console_text(wide_to_utf8(path.wstring()));
        return std::nullopt;
    }

    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        reason = "Waiting to read " + std::string(label) + " metadata.";
        return std::nullopt;
    }
    const fs::file_time_type modified = fs::last_write_time(path, error);
    if (error) {
        reason = "Waiting to read " + std::string(label) + " timestamp.";
        return std::nullopt;
    }

    return FileIdentity{
        size,
        static_cast<std::int64_t>(modified.time_since_epoch().count()),
    };
}

std::optional<BuildIdentity> observe_build(
    const Options& options,
    std::string& reason) {
    const fs::path compressed = fs::path(options.firmware.wstring() + L".gz");
    const fs::path local_header = options.firmware.parent_path() / L"~local.h";
    std::error_code error;
    const bool compressed_exists = fs::exists(compressed, error);
    if (error) {
        reason = "Waiting to inspect compressed build state.";
        return std::nullopt;
    }
    if (compressed_exists) {
        reason = "Build is still compressing (.gz is present).";
        return std::nullopt;
    }
    error.clear();
    const bool local_header_exists = fs::exists(local_header, error);
    if (error) {
        reason = "Waiting to inspect active build state.";
        return std::nullopt;
    }
    if (local_header_exists) {
        reason = "Build is still active (~local.h is present).";
        return std::nullopt;
    }

    const auto firmware = read_file_identity(
        options.firmware, reason, "firmware");
    if (!firmware) {
        return std::nullopt;
    }
    const auto manifest = read_file_identity(
        options.manifest, reason, "MD5 manifest");
    if (!manifest) {
        return std::nullopt;
    }
    reason = "Waiting for build files to remain unchanged.";
    return BuildIdentity{*firmware, *manifest};
}

std::vector<std::uint8_t> read_firmware(
    const fs::path& path,
    std::uint64_t maximum_bytes = kDefaultMaximumFirmwareBytes) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error || !fs::is_regular_file(status)) {
        throw AppError(input_error, "Firmware is not a readable regular file: " + wide_to_utf8(path.wstring()));
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        throw AppError(input_error, "Could not read firmware size: " + error.message());
    }
    if (size == 0) {
        throw AppError(input_error, "Firmware file is empty.");
    }
    if (size > maximum_bytes ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<DWORD>::max())) {
        throw AppError(input_error, "Firmware exceeds the configured safety limit.");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw AppError(input_error, "Could not open firmware: " + wide_to_utf8(path.wstring()));
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != data.size()) {
        throw AppError(input_error, "Could not read the complete firmware file.");
    }
    return data;
}

std::string md5_bytes(const std::vector<std::uint8_t>& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> object;
    std::array<UCHAR, 16> digest{};

    auto cleanup = [&]() {
        if (hash) {
            BCryptDestroyHash(hash);
            hash = nullptr;
        }
        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
        }
    };

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0);
    if (status < 0) {
        cleanup();
        throw AppError(internal_error, "BCrypt could not open the MD5 provider.");
    }

    DWORD object_size = 0;
    DWORD received = 0;
    status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &received, 0);
    if (status < 0 || received != sizeof(object_size)) {
        cleanup();
        throw AppError(internal_error, "BCrypt could not query the MD5 object size.");
    }
    object.resize(object_size);

    status = BCryptCreateHash(
        algorithm, &hash, object.data(), static_cast<ULONG>(object.size()),
        nullptr, 0, 0);
    if (status < 0) {
        cleanup();
        throw AppError(internal_error, "BCrypt could not create an MD5 hash.");
    }

    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t remaining = data.size() - offset;
        const ULONG chunk = static_cast<ULONG>(
            std::min<std::size_t>(remaining, std::numeric_limits<ULONG>::max()));
        status = BCryptHashData(
            hash, const_cast<PUCHAR>(data.data() + offset), chunk, 0);
        if (status < 0) {
            cleanup();
            throw AppError(internal_error, "BCrypt failed while hashing firmware.");
        }
        offset += chunk;
    }

    status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) {
        cleanup();
        throw AppError(internal_error, "BCrypt could not finish the MD5 hash.");
    }
    cleanup();

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const UCHAR byte : digest) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

std::uint32_t read_little_u32(
    const std::vector<std::uint8_t>& data,
    std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) {
        throw AppError(input_error, "Firmware gzip trailer is truncated.");
    }
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint32_t crc32_bytes(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t value : data) {
        crc ^= value;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::vector<std::uint8_t> gunzip_firmware(
    const std::vector<std::uint8_t>& compressed,
    std::uint64_t maximum_bytes) {
    if (compressed.size() < 18 || compressed[0] != 0x1f ||
        compressed[1] != 0x8b || compressed[2] != 8) {
        throw AppError(input_error, "Firmware is not a valid gzip stream.");
    }
    const std::uint8_t flags = compressed[3];
    if ((flags & 0xe0u) != 0) {
        throw AppError(input_error, "Firmware gzip header uses reserved flags.");
    }

    std::size_t cursor = 10;
    const std::size_t trailer = compressed.size() - 8;
    const auto require_header = [&](std::size_t count) {
        if (cursor > trailer || count > trailer - cursor) {
            throw AppError(input_error, "Firmware gzip header is truncated.");
        }
    };
    if ((flags & 0x04u) != 0) {
        require_header(2);
        const std::size_t extra =
            static_cast<std::size_t>(compressed[cursor]) |
            (static_cast<std::size_t>(compressed[cursor + 1]) << 8);
        cursor += 2;
        require_header(extra);
        cursor += extra;
    }
    const auto skip_zero_terminated = [&]() {
        while (cursor < trailer && compressed[cursor] != 0) {
            ++cursor;
        }
        require_header(1);
        ++cursor;
    };
    if ((flags & 0x08u) != 0) {
        skip_zero_terminated();
    }
    if ((flags & 0x10u) != 0) {
        skip_zero_terminated();
    }
    if ((flags & 0x02u) != 0) {
        require_header(2);
        cursor += 2;
    }
    if (cursor >= trailer) {
        throw AppError(input_error, "Firmware gzip stream has no deflate payload.");
    }

    const std::uint32_t raw_size = read_little_u32(compressed, trailer + 4);
    if (raw_size == 0 || raw_size > maximum_bytes ||
        raw_size > std::numeric_limits<unsigned long>::max()) {
        throw AppError(input_error, "Raw firmware exceeds the configured safety limit.");
    }
    const std::size_t deflate_size = trailer - cursor;
    if (deflate_size > std::numeric_limits<unsigned long>::max()) {
        throw AppError(input_error, "Compressed firmware is too large to inflate safely.");
    }

    std::vector<std::uint8_t> raw(raw_size);
    unsigned long output_size = static_cast<unsigned long>(raw.size());
    unsigned long input_size = static_cast<unsigned long>(deflate_size);
    const int result = puff(
        raw.data(),
        &output_size,
        compressed.data() + cursor,
        &input_size);
    if (result != 0 || output_size != raw.size() || input_size != deflate_size) {
        throw AppError(
            input_error,
            "Firmware contains an invalid, truncated, or trailing deflate stream.");
    }
    if (crc32_bytes(raw) != read_little_u32(compressed, trailer)) {
        throw AppError(integrity_error, "Firmware gzip CRC32 verification failed.");
    }
    return raw;
}

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw AppError(input_error, "Could not open MD5 manifest: " + wide_to_utf8(path.wstring()));
    }
    std::ostringstream output;
    output << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw AppError(input_error, "Could not read MD5 manifest.");
    }
    std::string text = output.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }
    return text;
}

using Md5Manifest = std::map<std::string, std::string>;

Md5Manifest parse_md5_manifest(std::string_view text) {
    Md5Manifest entries;
    std::istringstream lines{std::string(text)};
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(lines, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            throw AppError(
                input_error,
                "Manifest must contain exactly two non-empty md5sum records.");
        }
        if (line.size() < 35 ||
            !is_hex_md5(std::string_view(line).substr(0, 32)) ||
            line[32] != ' ' || (line[33] != ' ' && line[33] != '*')) {
            throw AppError(
                input_error,
                "Manifest contains a malformed md5sum record on line " +
                    std::to_string(line_number) + ".");
        }

        std::string digest = lower_ascii(line.substr(0, 32));
        std::string name = line.substr(34);
        name = basename_utf8(std::move(name));
        if (name.empty() || name.find('\n') != std::string::npos ||
            name.find('\r') != std::string::npos) {
            throw AppError(
                input_error,
                "Invalid filename on manifest line " + std::to_string(line_number) + ".");
        }

        const auto existing = entries.find(name);
        if (existing != entries.end()) {
            throw AppError(
                input_error,
                "Manifest contains a duplicate record for \"" + name + "\".");
        }
        entries[name] = digest;
    }

    if (entries.size() != 2 || line_number != 2) {
        throw AppError(
            input_error,
            "Manifest must contain exactly two non-empty md5sum records.");
    }
    return entries;
}

struct FirmwareHashes {
    std::string compressed;
    std::string raw;
};

FirmwareHashes select_hashes(
    const Md5Manifest& manifest,
    const std::string& firmware_name) {
    const std::string raw_key = firmware_name;
    const std::string compressed_key = raw_key + " (compressed)";
    const auto raw = manifest.find(raw_key);
    const auto compressed = manifest.find(compressed_key);
    if (manifest.size() != 2 || raw == manifest.end() || compressed == manifest.end()) {
        throw AppError(
            input_error,
            "Manifest must name exactly \"" + firmware_name + "\" and \"" +
                firmware_name + " (compressed)\".");
    }
    return {compressed->second, raw->second};
}

struct ParsedEndpoint {
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring original_host;
    std::wstring connect_host;
    std::wstring host_header;
    std::wstring upload_path;
};

bool is_numeric_address(const std::wstring& host) {
    IN_ADDR ipv4{};
    if (InetPtonW(AF_INET, host.c_str(), &ipv4) == 1) {
        return true;
    }
    IN6_ADDR ipv6{};
    return InetPtonW(AF_INET6, host.c_str(), &ipv6) == 1;
}

ParsedEndpoint parse_endpoint(const std::wstring& url) {
    if (url.empty()) {
        throw AppError(endpoint_error, "OTA endpoint is empty.");
    }
    if (url.find(L'#') != std::wstring::npos) {
        throw AppError(endpoint_error, "OTA endpoint must not contain a URL fragment.");
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        throw_last_error(endpoint_error, "Invalid OTA endpoint");
    }
    if (components.nScheme != INTERNET_SCHEME_HTTP &&
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        throw AppError(endpoint_error, "OTA endpoint must use http:// or https://.");
    }
    if (components.dwHostNameLength == 0) {
        throw AppError(endpoint_error, "OTA endpoint has no hostname.");
    }
    if (components.dwUserNameLength || components.dwPasswordLength) {
        throw AppError(endpoint_error, "Credentials in the OTA endpoint URL are not supported.");
    }

    ParsedEndpoint endpoint;
    endpoint.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    endpoint.port = components.nPort;
    endpoint.original_host.assign(components.lpszHostName, components.dwHostNameLength);
    if (endpoint.original_host.find(L'%') != std::wstring::npos) {
        throw AppError(endpoint_error, "IPv6 scope identifiers are not supported in endpoint URLs.");
    }

    if (components.dwUrlPathLength) {
        endpoint.upload_path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    }
    if (endpoint.upload_path.empty()) {
        endpoint.upload_path = L"/";
    }
    if (components.dwExtraInfoLength) {
        endpoint.upload_path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    endpoint.connect_host = endpoint.original_host;
    const bool ipv6 = endpoint.original_host.find(L':') != std::wstring::npos;
    endpoint.host_header = ipv6
        ? L"[" + endpoint.original_host + L"]"
        : endpoint.original_host;
    const INTERNET_PORT default_port = endpoint.secure
        ? INTERNET_DEFAULT_HTTPS_PORT
        : INTERNET_DEFAULT_HTTP_PORT;
    if (endpoint.port != default_port) {
        endpoint.host_header += L":" + std::to_wstring(endpoint.port);
    }
    return endpoint;
}

std::wstring resolve_hostname_once(const std::wstring& hostname) {
    if (is_numeric_address(hostname)) {
        return hostname;
    }

    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    PADDRINFOW result = nullptr;
    const int lookup = GetAddrInfoW(hostname.c_str(), nullptr, &hints, &result);
    if (lookup != 0 || !result) {
        const std::string detail = lookup == 0
            ? "no addresses returned"
            : wide_to_utf8(gai_strerrorW(lookup));
        throw AppError(
            endpoint_error,
            "Could not resolve \"" + wide_to_utf8(hostname) + "\": " + detail);
    }

    PADDRINFOW selected = nullptr;
    for (PADDRINFOW item = result; item; item = item->ai_next) {
        if (item->ai_family == AF_INET) {
            selected = item;
            break;
        }
        if (!selected && item->ai_family == AF_INET6) {
            selected = item;
        }
    }
    if (!selected) {
        FreeAddrInfoW(result);
        throw AppError(endpoint_error, "Hostname did not resolve to an IPv4 or IPv6 address.");
    }

    std::array<wchar_t, NI_MAXHOST> numeric{};
    const int named = GetNameInfoW(
        selected->ai_addr, static_cast<socklen_t>(selected->ai_addrlen),
        numeric.data(), static_cast<DWORD>(numeric.size()),
        nullptr, 0, NI_NUMERICHOST);
    FreeAddrInfoW(result);
    if (named != 0) {
        throw AppError(
            endpoint_error,
            "Could not format resolved address: " + wide_to_utf8(gai_strerrorW(named)));
    }
    return numeric.data();
}

void apply_request_policy(HINTERNET request) {
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(
            request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) {
        throw_last_error(endpoint_error, "Could not disable HTTP redirects");
    }
}

void add_host_header(HINTERNET request, const std::wstring& host_header) {
    const std::wstring header = L"Host: " + host_header;
    if (!WinHttpAddRequestHeaders(
            request, header.c_str(), static_cast<DWORD>(header.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        throw_last_error(endpoint_error, "Could not set the original Host header");
    }
}

void add_authorization_header(
    HINTERNET request,
    const std::wstring& authorization) {
    if (authorization.empty()) {
        return;
    }
    const std::wstring header = L"Authorization: " + authorization;
    if (!WinHttpAddRequestHeaders(
            request,
            header.c_str(),
            static_cast<DWORD>(header.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        throw_last_error(endpoint_error, "Could not set Authorization header");
    }
}

void add_request_header(
    HINTERNET request,
    const std::wstring& name,
    const std::wstring& value) {
    const std::wstring header = name + L": " + value;
    if (!WinHttpAddRequestHeaders(
            request,
            header.c_str(),
            static_cast<DWORD>(header.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        throw_last_error(endpoint_error, "Could not set HTTP request header");
    }
}

DWORD query_status(HINTERNET request) {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
            WINHTTP_NO_HEADER_INDEX)) {
        throw_last_error(response_error, "Could not read HTTP status");
    }
    return status;
}

using HeaderMap = std::map<std::string, std::vector<std::string>>;

HeaderMap query_response_headers(HINTERNET request) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        nullptr,
        &bytes,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) {
        throw_last_error(response_error, "Could not size HTTP response headers");
    }
    std::vector<wchar_t> raw(bytes / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX,
            raw.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        throw_last_error(response_error, "Could not read HTTP response headers");
    }

    HeaderMap headers;
    std::wistringstream lines{std::wstring(raw.data())};
    std::wstring line;
    bool first = true;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (first) {
            first = false;
            continue;
        }
        const std::size_t colon = line.find(L':');
        if (colon == std::wstring::npos) {
            continue;
        }
        std::string name = lower_ascii(trim_ascii(wide_to_utf8(line.substr(0, colon))));
        std::string value = trim_ascii(wide_to_utf8(line.substr(colon + 1)));
        if (!name.empty()) {
            headers[name].push_back(value);
        }
    }
    return headers;
}

std::string read_response_body(
    HINTERNET request,
    std::size_t maximum_bytes = kMaximumResponseBytes) {
    std::string body;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            throw_last_error(response_error, "Could not query response body");
        }
        if (available == 0) {
            break;
        }
        if (body.size() > maximum_bytes ||
            available > maximum_bytes - body.size()) {
            throw AppError(protocol_error, "HTTP response exceeds the configured safety limit.");
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &received)) {
            throw_last_error(response_error, "Could not read response body");
        }
        body.resize(offset + received);
        if (received == 0) {
            break;
        }
    }
    return body;
}

std::string printable_response(std::string body) {
    constexpr std::size_t maximum = 512;
    if (body.size() > maximum) {
        body.resize(maximum);
        body += "...";
    }
    for (char& character : body) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return sanitize_console_text(body);
}

std::string multipart_boundary() {
    std::array<unsigned char, 18> random{};
    const NTSTATUS status = BCryptGenRandom(
        nullptr, random.data(), static_cast<ULONG>(random.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        throw AppError(internal_error, "Could not create a multipart boundary.");
    }
    std::ostringstream output;
    output << "------------------------";
    output << std::hex << std::setfill('0');
    for (const unsigned char value : random) {
        output << std::setw(2) << static_cast<unsigned>(value);
    }
    return output.str();
}

void write_http_data(HINTERNET request, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(size - offset, 64u * 1024u));
        DWORD written = 0;
        if (!WinHttpWriteData(request, bytes + offset, chunk, &written)) {
            throw_last_error(upload_error, "Could not send firmware");
        }
        if (written == 0) {
            throw AppError(upload_error, "HTTP connection stopped accepting firmware data.");
        }
        offset += written;
    }
}

struct HttpClient {
    WinHttpHandle session;
    WinHttpHandle connection;
    ParsedEndpoint endpoint;

    HttpClient(
        ParsedEndpoint value,
        int connect_timeout_ms,
        int request_timeout_ms,
        bool asynchronous = false)
        : endpoint(std::move(value)) {
        session = WinHttpHandle(WinHttpOpen(
            kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
            asynchronous ? WINHTTP_FLAG_ASYNC : 0));
        if (!session) {
            throw_last_error(endpoint_error, "Could not initialize WinHTTP");
        }
        if (!WinHttpSetTimeouts(
                session.get(),
                connect_timeout_ms,
                connect_timeout_ms,
                request_timeout_ms,
                request_timeout_ms)) {
            throw_last_error(endpoint_error, "Could not configure HTTP timeouts");
        }
        connection = WinHttpHandle(WinHttpConnect(
            session.get(), endpoint.connect_host.c_str(), endpoint.port, 0));
        if (!connection) {
            throw_last_error(endpoint_error, "Could not create HTTP connection");
        }
    }
};

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    HeaderMap headers;
};

HttpResponse request_buffer(
    HttpClient& client,
    std::wstring_view method,
    std::wstring_view path,
    const Options& options,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers = {},
    std::size_t maximum_response_bytes = kMaximumResponseBytes) {
    const DWORD flags = client.endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    const std::wstring method_text(method);
    const std::wstring path_text(path);
    WinHttpHandle request(WinHttpOpenRequest(
        client.connection.get(),
        method_text.c_str(),
        path_text.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!request) {
        throw_last_error(network_error, "Could not create HTTP request");
    }
    apply_request_policy(request.get());
    add_host_header(request.get(), client.endpoint.host_header);
    add_authorization_header(request.get(), options.authorization);
    for (const auto& [name, value] : headers) {
        add_request_header(request.get(), name, value);
    }

    if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        throw_last_error(network_error, "Could not send HTTP request", true);
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw_last_error(network_error, "Could not receive HTTP response", true);
    }

    HttpResponse response;
    response.status = query_status(request.get());
    response.headers = query_response_headers(request.get());
    if (method != L"HEAD") {
        response.body = read_response_body(request.get(), maximum_response_bytes);
    }
    return response;
}

HttpResponse upload_firmware(
    HttpClient& client,
    const Options& options,
    const fs::path& firmware_path,
    const std::vector<std::uint8_t>& firmware,
    const std::string& compressed_md5,
    const Console& console) {
    const std::string boundary = multipart_boundary();
    std::string filename = wide_to_utf8(firmware_path.filename().wstring());
    std::replace(filename.begin(), filename.end(), '"', '_');
    std::replace(filename.begin(), filename.end(), '\r', '_');
    std::replace(filename.begin(), filename.end(), '\n', '_');

    const std::string prefix =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"MD5\"\r\n\r\n" +
        compressed_md5 + "\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"firmware\"; filename=\"" +
        filename + "\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    const std::string suffix = "\r\n--" + boundary + "--\r\n";

    const std::uint64_t total =
        static_cast<std::uint64_t>(prefix.size()) +
        static_cast<std::uint64_t>(firmware.size()) +
        static_cast<std::uint64_t>(suffix.size());
    if (total > std::numeric_limits<DWORD>::max()) {
        throw AppError(input_error, "Multipart request is too large for WinHTTP.");
    }

    const DWORD flags = client.endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(
        client.connection.get(), L"POST", client.endpoint.upload_path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        throw_last_error(upload_error, "Could not create upload request");
    }
    apply_request_policy(request.get());
    add_host_header(request.get(), client.endpoint.host_header);
    add_authorization_header(request.get(), options.authorization);

    const std::wstring content_type =
        L"Content-Type: multipart/form-data; boundary=" + utf8_to_wide(boundary);
    if (!WinHttpSendRequest(
            request.get(), content_type.c_str(), static_cast<DWORD>(content_type.size()),
            WINHTTP_NO_REQUEST_DATA, 0, static_cast<DWORD>(total), 0)) {
        // No firmware body has been committed yet, so one DNS refresh/retry is safe.
        throw_last_error(upload_error, "Could not start firmware upload", true);
    }

    write_http_data(request.get(), prefix.data(), prefix.size());
    std::size_t sent = 0;
    while (sent < firmware.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(firmware.size() - sent, 32u * 1024u));
        DWORD written = 0;
        if (!WinHttpWriteData(
                request.get(), firmware.data() + sent, chunk, &written)) {
            throw_last_error(upload_error, "Firmware upload failed");
        }
        if (written == 0) {
            throw AppError(upload_error, "Firmware upload stopped before completion.");
        }
        sent += written;
        const unsigned percent = static_cast<unsigned>(
            (static_cast<std::uint64_t>(sent) * 100u) / firmware.size());
        if (!options.quiet) {
            console.progress("Uploading", percent);
        }
    }
    write_http_data(request.get(), suffix.data(), suffix.size());

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw_last_error(upload_error, "Device did not return an upload response");
    }
    HttpResponse response;
    response.status = query_status(request.get());
    response.body = read_response_body(request.get());
    response.headers = query_response_headers(request.get());
    if (!options.quiet) {
        console.clear_progress();
    }
    return response;
}

struct AsyncInfoContext {
    std::mutex mutex;
    std::condition_variable changed;
    bool completed = false;
    bool handle_closed = false;
    DWORD error = ERROR_SUCCESS;
    DWORD status = 0;
    std::string body;
    std::size_t read_offset = 0;
};

void complete_async_info(AsyncInfoContext& context, DWORD error) noexcept {
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        if (context.completed) {
            return;
        }
        context.error = error;
        context.completed = true;
    }
    context.changed.notify_all();
}

bool begin_async_info_data_query(HINTERNET request, AsyncInfoContext& context) noexcept {
    if (WinHttpQueryDataAvailable(request, nullptr)) {
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        return true;
    }
    complete_async_info(context, error);
    return false;
}

void CALLBACK async_info_callback(
    HINTERNET request,
    DWORD_PTR context_value,
    DWORD status,
    void* status_information,
    DWORD status_information_length) noexcept {
    auto* context = reinterpret_cast<AsyncInfoContext*>(context_value);
    if (!context) {
        return;
    }

    try {
        switch (status) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (!WinHttpReceiveResponse(request, nullptr)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    complete_async_info(*context, error);
                }
            }
            break;

        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
            DWORD http_status = 0;
            DWORD size = sizeof(http_status);
            if (!WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &http_status,
                    &size,
                    WINHTTP_NO_HEADER_INDEX)) {
                complete_async_info(*context, GetLastError());
                break;
            }
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                context->status = http_status;
            }
            (void)begin_async_info_data_query(request, *context);
            break;
        }

        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
            if (!status_information || status_information_length != sizeof(DWORD)) {
                complete_async_info(*context, ERROR_INVALID_DATA);
                break;
            }
            const DWORD available = *static_cast<const DWORD*>(status_information);
            if (available == 0) {
                complete_async_info(*context, ERROR_SUCCESS);
                break;
            }

            char* destination = nullptr;
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->completed) {
                    break;
                }
                if (context->body.size() > kMaximumResponseBytes ||
                    available > kMaximumResponseBytes - context->body.size()) {
                    context->error = ERROR_INSUFFICIENT_BUFFER;
                    context->completed = true;
                    context->changed.notify_all();
                    break;
                }
                context->read_offset = context->body.size();
                context->body.resize(context->read_offset + available);
                destination = context->body.data() + context->read_offset;
            }
            if (!WinHttpReadData(request, destination, available, nullptr)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    complete_async_info(*context, error);
                }
            }
            break;
        }

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                if (context->completed) {
                    break;
                }
                context->body.resize(context->read_offset + status_information_length);
            }
            (void)begin_async_info_data_query(request, *context);
            break;
        }

        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            if (status_information &&
                status_information_length >= sizeof(WINHTTP_ASYNC_RESULT)) {
                const auto* result =
                    static_cast<const WINHTTP_ASYNC_RESULT*>(status_information);
                complete_async_info(*context, result->dwError);
            } else {
                complete_async_info(*context, ERROR_WINHTTP_INTERNAL_ERROR);
            }
            break;

        case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            complete_async_info(*context, ERROR_WINHTTP_SECURE_FAILURE);
            break;

        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
            {
                std::lock_guard<std::mutex> lock(context->mutex);
                context->handle_closed = true;
            }
            context->changed.notify_all();
            break;

        default:
            break;
        }
    } catch (...) {
        complete_async_info(*context, ERROR_OUTOFMEMORY);
    }
}

bool retryable_network_error(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_OPERATION_CANCELLED:
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_WINHTTP_RESEND_REQUEST:
    case WSAECONNABORTED:
    case WSAECONNREFUSED:
    case WSAECONNRESET:
    case WSAEHOSTUNREACH:
    case WSAENETUNREACH:
    case WSAETIMEDOUT:
        return true;
    default:
        return false;
    }
}

HttpResponse get_info(
    HttpClient& client,
    std::wstring_view path,
    const Options& options,
    int response_timeout_ms) {
    const DWORD flags = client.endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    const std::wstring path_text(path);
    WinHttpHandle request(WinHttpOpenRequest(
        client.connection.get(), L"GET", path_text.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        throw_last_error(response_error, "Could not create firmware-information request");
    }
    apply_request_policy(request.get());
    add_host_header(request.get(), client.endpoint.host_header);
    add_authorization_header(request.get(), options.authorization);

    AsyncInfoContext context;
    DWORD_PTR context_pointer = reinterpret_cast<DWORD_PTR>(&context);
    if (!WinHttpSetOption(
            request.get(),
            WINHTTP_OPTION_CONTEXT_VALUE,
            &context_pointer,
            sizeof(context_pointer))) {
        throw_last_error(response_error, "Could not attach firmware-information request context");
    }

    constexpr DWORD callback_flags =
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
        WINHTTP_CALLBACK_FLAG_SECURE_FAILURE |
        WINHTTP_CALLBACK_FLAG_HANDLES;
    if (WinHttpSetStatusCallback(
            request.get(),
            async_info_callback,
            callback_flags,
            0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        throw_last_error(response_error, "Could not configure firmware-information callback");
    }

    if (!WinHttpSendRequest(
            request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, context_pointer)) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            complete_async_info(context, error);
        }
    }

    bool timed_out = false;
    {
        std::unique_lock<std::mutex> lock(context.mutex);
        timed_out = !context.changed.wait_for(
            lock,
            std::chrono::milliseconds(std::max(response_timeout_ms, 1)),
            [&context] { return context.completed; });
    }

    const HINTERNET raw_request = request.get();
    if (!WinHttpCloseHandle(raw_request)) {
        throw_last_error(response_error, "Could not close firmware-information request");
    }
    (void)request.release();
    {
        std::unique_lock<std::mutex> lock(context.mutex);
        context.changed.wait(lock, [&context] { return context.handle_closed; });
    }

    if (timed_out) {
        throw AppError(
            response_error,
            "Firmware-information request exceeded its deadline.",
            true);
    }

    DWORD operation_error = ERROR_SUCCESS;
    HttpResponse response;
    {
        std::lock_guard<std::mutex> lock(context.mutex);
        operation_error = context.error;
        response.status = context.status;
        response.body = std::move(context.body);
    }
    if (operation_error != ERROR_SUCCESS) {
        throw AppError(
            response_error,
            "Could not complete firmware-information request: " +
                windows_message(operation_error),
            retryable_network_error(operation_error));
    }
    return response;
}

using InfoMap = std::map<std::string, std::string>;

InfoMap parse_info_lines(std::string_view body) {
    InfoMap values;
    std::istringstream lines{std::string(body)};
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = lower_ascii(trim_ascii(line.substr(0, colon)));
        std::string value = trim_ascii(line.substr(colon + 1));
        if (!key.empty() && !value.empty()) {
            values[key] = value;
        }
    }
    return values;
}

std::string clean_reported_md5(std::string value) {
    value = lower_ascii(trim_ascii(std::move(value)));
    if (is_hex_md5(value)) {
        return value;
    }
    return {};
}

std::string hash_candidate(std::string_view value) {
    for (std::size_t index = 0; index + 32 <= value.size(); ++index) {
        const std::string_view candidate = value.substr(index, 32);
        if (!is_hex_md5(candidate)) {
            continue;
        }
        const bool left_ok =
            index == 0 ||
            !std::isxdigit(static_cast<unsigned char>(value[index - 1]));
        const bool right_ok =
            index + 32 == value.size() ||
            !std::isxdigit(static_cast<unsigned char>(value[index + 32]));
        if (left_ok && right_ok) {
            return lower_ascii(std::string(candidate));
        }
    }
    return {};
}

std::string parse_info_hash(std::string_view body) {
    const std::string text(body);
    const std::string trimmed = trim_ascii(text);
    if (!trimmed.empty() && trimmed.front() == '{') {
        const std::array<std::string_view, 5> json_keys = {
            "\"hash\"",
            "\"firmwareHash\"",
            "\"firmware_hash\"",
            "\"md5\"",
            "\"firmware md5\"",
        };
        for (const std::string_view key : json_keys) {
            std::size_t cursor = trimmed.find(key);
            while (cursor != std::string::npos) {
                const std::size_t colon = trimmed.find(':', cursor + key.size());
                if (colon == std::string::npos) {
                    break;
                }
                const std::size_t value_end =
                    std::min(trimmed.size(), colon + 256);
                const std::string hash = hash_candidate(
                    std::string_view(trimmed).substr(colon + 1, value_end - colon - 1));
                if (!hash.empty()) {
                    return hash;
                }
                cursor = trimmed.find(key, cursor + key.size());
            }
        }
    }

    const InfoMap info = parse_info_lines(text);
    for (const char* key : {"hash", "firmware hash", "firmware md5", "md5"}) {
        const auto value = info.find(key);
        if (value == info.end()) {
            continue;
        }
        const std::string hash = hash_candidate(value->second);
        if (!hash.empty()) {
            return hash;
        }
    }
    return {};
}

struct FirmwareInfo {
    std::string hash;
    std::wstring path;
    HttpResponse response;
};

FirmwareInfo fetch_firmware_info_once(
    const ParsedEndpoint& endpoint,
    const Options& options,
    int request_budget) {
    std::optional<AppError> first_failure;
    for (const std::wstring_view path : {L"/update/info", L"/info"}) {
        HttpClient client(endpoint, request_budget, request_budget, true);
        HttpResponse response = get_info(client, path, options, request_budget);
        if (response.status >= 200 && response.status < 300) {
            const std::string hash = parse_info_hash(response.body);
            if (!hash.empty()) {
                return {hash, std::wstring(path), std::move(response)};
            }
            if (!first_failure) {
                first_failure.emplace(
                    protocol_error,
                    wide_to_utf8(path) + " did not contain a valid firmware hash.");
            }
            continue;
        }
        if (path == L"/update/info" &&
            (response.status == 404 || response.status == 405 || response.status == 501)) {
            continue;
        }
        throw AppError(
            protocol_error,
            wide_to_utf8(path) + " returned HTTP " +
                std::to_string(response.status) +
                (response.body.empty()
                    ? std::string()
                    : ": " + printable_response(response.body)));
    }
    if (first_failure) {
        throw *first_failure;
    }
    throw AppError(protocol_error, "The device did not report a firmware hash.");
}

FirmwareInfo fetch_firmware_info(
    ParsedEndpoint& endpoint,
    const Options& options,
    const Console& console,
    bool& address_refreshed,
    int request_budget) {
    try {
        return fetch_firmware_info_once(endpoint, options, request_budget);
    } catch (const AppError& error) {
        if (!error.address_retryable() || address_refreshed ||
            is_numeric_address(endpoint.original_host)) {
            throw;
        }
        address_refreshed = true;
        if (!options.quiet) {
            console.clear_progress();
            std::cout << console.paint(
                "33",
                "Cached address failed; resolving the hostname once more.") << "\n";
        }
        endpoint.connect_host = resolve_hostname_once(endpoint.original_host);
        return fetch_firmware_info_once(endpoint, options, request_budget);
    }
}

const std::string* single_header(
    const HttpResponse& response,
    const std::string& name,
    bool required = false) {
    const auto found = response.headers.find(name);
    if (found == response.headers.end()) {
        if (required) {
            throw AppError(
                protocol_error,
                "Firmware response omitted the required " + name + " header.");
        }
        return nullptr;
    }
    if (found->second.size() != 1) {
        throw AppError(
            protocol_error,
            "Firmware response supplied multiple " + name + " headers.");
    }
    return &found->second.front();
}

std::uint64_t parse_content_length(
    const HttpResponse& response,
    std::string_view label) {
    const std::string* value = single_header(response, "content-length", true);
    if (!value || value->empty() ||
        !std::all_of(value->begin(), value->end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        throw AppError(
            protocol_error,
            std::string(label) + " supplied an invalid Content-Length.");
    }
    std::size_t consumed = 0;
    unsigned long long length = 0;
    try {
        length = std::stoull(*value, &consumed, 10);
    } catch (...) {
        throw AppError(
            protocol_error,
            std::string(label) + " supplied an invalid Content-Length.");
    }
    if (consumed != value->size()) {
        throw AppError(
            protocol_error,
            std::string(label) + " supplied an invalid Content-Length.");
    }
    return static_cast<std::uint64_t>(length);
}

std::vector<std::uint8_t> decode_base64(std::string_view input) {
    if (input.empty() || input.size() % 4 != 0) {
        return {};
    }
    const auto decode = [](unsigned char character) -> int {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26;
        if (character >= '0' && character <= '9') return character - '0' + 52;
        if (character == '+') return 62;
        if (character == '/') return 63;
        if (character == '=') return -2;
        return -1;
    };

    std::vector<std::uint8_t> output;
    output.reserve(input.size() / 4 * 3);
    for (std::size_t offset = 0; offset < input.size(); offset += 4) {
        std::array<int, 4> values{};
        for (std::size_t index = 0; index < 4; ++index) {
            values[index] = decode(static_cast<unsigned char>(input[offset + index]));
            if (values[index] == -1) {
                return {};
            }
        }
        const bool final = offset + 4 == input.size();
        if (values[0] < 0 || values[1] < 0 ||
            (values[2] == -2 && values[3] != -2) ||
            (!final && (values[2] == -2 || values[3] == -2))) {
            return {};
        }
        const unsigned combined =
            (static_cast<unsigned>(values[0]) << 18) |
            (static_cast<unsigned>(values[1]) << 12) |
            (static_cast<unsigned>(std::max(values[2], 0)) << 6) |
            static_cast<unsigned>(std::max(values[3], 0));
        output.push_back(static_cast<std::uint8_t>((combined >> 16) & 0xffu));
        if (values[2] != -2) {
            output.push_back(static_cast<std::uint8_t>((combined >> 8) & 0xffu));
        }
        if (values[3] != -2) {
            output.push_back(static_cast<std::uint8_t>(combined & 0xffu));
        }
    }
    return output;
}

std::string hex_bytes(const std::vector<std::uint8_t>& data) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t value : data) {
        output << std::setw(2) << static_cast<unsigned>(value);
    }
    return output.str();
}

std::string parse_header_md5(
    const HttpResponse& response,
    const std::string& name) {
    const std::string* value = single_header(response, name);
    if (!value) {
        return {};
    }
    if (name == "content-md5") {
        const std::vector<std::uint8_t> decoded = decode_base64(trim_ascii(*value));
        if (decoded.size() != 16) {
            throw AppError(protocol_error, "Content-MD5 is not a valid 16-byte digest.");
        }
        return hex_bytes(decoded);
    }
    if (name == "digest") {
        const std::string normalized = trim_ascii(*value);
        if (normalized.rfind("md5=", 0) != 0) {
            throw AppError(protocol_error, "Digest is not an exact md5 digest.");
        }
        const std::vector<std::uint8_t> decoded =
            decode_base64(std::string_view(normalized).substr(4));
        if (decoded.size() != 16) {
            throw AppError(protocol_error, "Digest is not a valid 16-byte md5 digest.");
        }
        return hex_bytes(decoded);
    }
    if (name == "etag") {
        std::string normalized = trim_ascii(*value);
        if (normalized.rfind("W/", 0) == 0) {
            normalized.erase(0, 2);
        }
        if (normalized.size() != 34 || normalized.front() != '"' ||
            normalized.back() != '"' ||
            !is_hex_md5(std::string_view(normalized).substr(1, 32))) {
            throw AppError(protocol_error, "ETag is not an exact firmware MD5.");
        }
        return lower_ascii(normalized.substr(1, 32));
    }
    const std::string normalized = lower_ascii(trim_ascii(*value));
    if (!is_hex_md5(normalized)) {
        throw AppError(protocol_error, "X-Firmware-MD5 is not an exact hexadecimal MD5.");
    }
    return normalized;
}

using ExpectedHashes = std::map<std::string, std::string>;

void add_expected_hash(
    ExpectedHashes& hashes,
    std::string source,
    std::string hash) {
    if (hash.empty()) {
        return;
    }
    for (const auto& [existing_source, existing_hash] : hashes) {
        if (existing_hash != hash) {
            throw AppError(
                integrity_error,
                source + " conflicts with " + existing_source + ".");
        }
    }
    hashes.emplace(std::move(source), std::move(hash));
}

void collect_download_hashes(
    const HttpResponse& response,
    ExpectedHashes& hashes,
    std::string_view source) {
    for (const char* name : {"x-firmware-md5", "content-md5", "digest", "etag"}) {
        const std::string hash = parse_header_md5(response, name);
        if (!hash.empty()) {
            add_expected_hash(
                hashes,
                std::string(source) + ":" + name,
                hash);
        }
    }
}

HttpResponse safe_request_buffer(
    ParsedEndpoint& endpoint,
    const Options& options,
    const Console& console,
    bool& address_refreshed,
    std::wstring_view method,
    std::wstring_view path,
    const std::vector<std::pair<std::wstring, std::wstring>>& headers,
    std::size_t maximum_response_bytes) {
    const auto perform = [&]() {
        HttpClient client(
            endpoint,
            options.connect_timeout_ms,
            options.request_timeout_ms);
        return request_buffer(
            client,
            method,
            path,
            options,
            headers,
            maximum_response_bytes);
    };
    try {
        return perform();
    } catch (const AppError& error) {
        if (!error.address_retryable() || address_refreshed ||
            is_numeric_address(endpoint.original_host)) {
            throw;
        }
        address_refreshed = true;
        if (!options.quiet) {
            std::cout << console.paint(
                "33",
                "Cached address failed; resolving the hostname once more.") << "\n";
        }
        endpoint.connect_host = resolve_hostname_once(endpoint.original_host);
        return perform();
    }
}

std::uintmax_t regular_file_size_or_zero(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            return 0;
        }
        throw AppError(local_io_error, "Could not inspect partial download: " + error.message());
    }
    if (!fs::exists(status)) {
        return 0;
    }
    if (!fs::is_regular_file(status)) {
        throw AppError(local_io_error, "Partial download path is not a regular file.");
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        throw AppError(local_io_error, "Could not read partial download size: " + error.message());
    }
    return size;
}

void truncate_file(const fs::path& path) {
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code directory_error;
        fs::create_directories(parent, directory_error);
        if (directory_error) {
            throw AppError(
                local_io_error,
                "Could not create download directory: " + directory_error.message());
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw AppError(local_io_error, "Could not create partial download.");
    }
}

void write_download_part(
    const fs::path& path,
    std::string_view body,
    bool append) {
    std::ofstream stream(
        path,
        std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!stream) {
        throw AppError(local_io_error, "Could not open partial download for writing.");
    }
    stream.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!stream) {
        throw AppError(local_io_error, "Could not write the complete partial download.");
    }
    stream.flush();
    if (!stream) {
        throw AppError(local_io_error, "Could not flush the partial download.");
    }
}

void remove_invalid_part(const fs::path& part) {
    std::error_code error;
    fs::remove(part, error);
    if (error) {
        throw AppError(
            local_io_error,
            "Could not remove invalid partial download: " + error.message());
    }
}

void publish_download_atomically(const fs::path& part, const fs::path& target) {
    if (!MoveFileExW(
            part.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw_last_error(local_io_error, "Could not atomically publish downloaded firmware");
    }
}

struct DownloadResult {
    fs::path path;
    std::uint64_t size = 0;
    std::string md5;
};

DownloadResult verified_download(
    ParsedEndpoint& endpoint,
    const Options& options,
    const Console& console,
    bool& address_refreshed,
    const fs::path& destination) {
    const fs::path target = fs::absolute(destination);
    const fs::path part(target.wstring() + L".part");
    const FirmwareInfo info = fetch_firmware_info(
        endpoint,
        options,
        console,
        address_refreshed,
        options.request_timeout_ms);
    ExpectedHashes expected{{info.path == L"/update/info" ? "/update/info" : "/info", info.hash}};

    const HttpResponse head = safe_request_buffer(
        endpoint,
        options,
        console,
        address_refreshed,
        L"HEAD",
        L"/firmware/download",
        {},
        1024);
    if (head.status < 200 || head.status >= 300) {
        throw AppError(
            protocol_error,
            "Firmware download metadata returned HTTP " + std::to_string(head.status) +
                (head.body.empty() ? std::string() : ": " + printable_response(head.body)));
    }
    const std::uint64_t total_length =
        parse_content_length(head, "Firmware download metadata");
    if (total_length == 0 || total_length > options.maximum_download_bytes ||
        total_length > std::numeric_limits<std::size_t>::max()) {
        throw AppError(protocol_error, "Firmware download size exceeds the configured safety limit.");
    }
    collect_download_hashes(head, expected, "HEAD");

    std::uint64_t offset = options.resume
        ? static_cast<std::uint64_t>(regular_file_size_or_zero(part))
        : 0;
    if (!options.resume || offset > total_length) {
        truncate_file(part);
        offset = 0;
    } else if (offset == 0 && !fs::exists(part)) {
        truncate_file(part);
    }

    try {
        if (offset < total_length) {
            std::vector<std::pair<std::wstring, std::wstring>> request_headers;
            if (offset > 0) {
                request_headers.emplace_back(
                    L"Range",
                    L"bytes=" + std::to_wstring(offset) + L"-");
            }
            const HttpResponse response = safe_request_buffer(
                endpoint,
                options,
                console,
                address_refreshed,
                L"GET",
                L"/firmware/download",
                request_headers,
                static_cast<std::size_t>(total_length));

            bool append = false;
            std::uint64_t expected_body_length = total_length;
            if (offset > 0 && response.status == 206) {
                const std::string* content_range =
                    single_header(response, "content-range", true);
                const std::string expected_range =
                    "bytes " + std::to_string(offset) + "-" +
                    std::to_string(total_length - 1) + "/" +
                    std::to_string(total_length);
                if (!content_range || *content_range != expected_range) {
                    throw AppError(
                        protocol_error,
                        "Range response Content-Range does not match the requested resume offset.");
                }
                append = true;
                expected_body_length = total_length - offset;
            } else if (response.status == 200) {
                offset = 0;
            } else {
                throw AppError(
                    protocol_error,
                    "Firmware download returned HTTP " + std::to_string(response.status) +
                        (response.body.empty()
                            ? std::string()
                            : ": " + printable_response(response.body)));
            }

            const std::uint64_t response_length =
                parse_content_length(response, "Firmware download");
            if (response_length != expected_body_length ||
                response.body.size() != expected_body_length) {
                throw AppError(
                    integrity_error,
                    "Firmware download length mismatch (expected " +
                        std::to_string(expected_body_length) + ", received " +
                        std::to_string(response.body.size()) + ").");
            }
            collect_download_hashes(response, expected, "GET");
            write_download_part(part, response.body, append);
        }

        const std::uintmax_t final_size = regular_file_size_or_zero(part);
        if (final_size != total_length) {
            throw AppError(
                integrity_error,
                "Partial firmware size mismatch (expected " +
                    std::to_string(total_length) + ", received " +
                    std::to_string(final_size) + ").");
        }
        const std::vector<std::uint8_t> downloaded =
            read_firmware(part, options.maximum_download_bytes);
        const std::string actual_md5 = md5_bytes(downloaded);
        for (const auto& [source, expected_md5] : expected) {
            if (actual_md5 != expected_md5) {
                throw AppError(
                    integrity_error,
                    "Downloaded firmware MD5 " + actual_md5 +
                        " does not match " + source + " " + expected_md5 + ".");
            }
        }
        publish_download_atomically(part, target);
        if (!options.quiet) {
            std::cout << console.paint("1;32", "✅ Verified firmware download: ")
                      << sanitize_console_text(wide_to_utf8(target.wstring()))
                      << " (" << total_length << " bytes, MD5 " << actual_md5 << ")\n";
        }
        return {target, total_length, actual_md5};
    } catch (const AppError& error) {
        if (error.code() == integrity_error) {
            remove_invalid_part(part);
        }
        throw;
    }
}

void verify_rebooted_firmware(
    ParsedEndpoint& endpoint,
    const std::string& expected_raw_md5,
    const Options& options,
    const Console& console,
    bool& address_refreshed) {
    if (!options.quiet) {
        std::cout << console.paint("90", "Waiting for the device to reboot...") << "\n";
    }
    if (options.watch) {
        if (!wait_interruptibly(options.initial_wait_ms)) {
            throw AppError(
                cancelled,
                "Stop requested after upload; firmware verification was interrupted.");
        }
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.initial_wait_ms));
    }

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(options.reboot_timeout_ms);
    bool received_info = false;
    std::string last_hash;
    std::string last_error;

    while (std::chrono::steady_clock::now() < deadline) {
        if (options.watch &&
            watch_stop_requested.load(std::memory_order_relaxed)) {
            throw AppError(
                cancelled,
                "Stop requested after upload; firmware verification was interrupted.");
        }
        const auto now = std::chrono::steady_clock::now();
        const auto remaining_before_request =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (remaining_before_request <= 0) {
            break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - started).count();
        const unsigned percent = static_cast<unsigned>(
            std::min<long long>(99, elapsed * 100 / options.reboot_timeout_ms));
        if (!options.quiet) {
            console.progress("Verifying firmware hash", percent);
        }

        try {
            // Async cancellation bounds the complete request, so this is a
            // whole-request budget rather than a per-socket-operation timeout.
            const int request_budget = static_cast<int>(std::max<long long>(
                1,
                std::min<long long>(
                    {options.connect_timeout_ms,
                     options.request_timeout_ms,
                     remaining_before_request,
                     2000})));
            const FirmwareInfo firmware_info = fetch_firmware_info(
                endpoint,
                options,
                console,
                address_refreshed,
                request_budget);
            if (std::chrono::steady_clock::now() >= deadline) {
                last_error = "firmware information completed after the reboot deadline";
                break;
            }
            last_hash = firmware_info.hash;
            received_info = true;
            if (last_hash == lower_ascii(expected_raw_md5)) {
                console.clear_progress();
                if (!options.quiet) {
                    std::cout << console.paint("1;32", "✅ Firmware hash matches.") << "\n";
                }
                return;
            }
        } catch (const AppError& error) {
            last_error = sanitize_console_text(error.what());
        } catch (const std::exception& error) {
            last_error = sanitize_console_text(error.what());
        }

        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds::zero()) {
            break;
        }
        const auto delay = std::min(
            std::chrono::milliseconds(options.poll_interval_ms),
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
        if (options.watch) {
            if (!wait_interruptibly(static_cast<int>(delay.count()))) {
                throw AppError(
                    cancelled,
                    "Stop requested after upload; firmware verification was interrupted.");
            }
        } else {
            std::this_thread::sleep_for(delay);
        }
    }

    console.clear_progress();
    if (received_info && !last_hash.empty()) {
        throw AppError(
            firmware_mismatch,
            "Device came back online, but firmware hash did not match. Expected " +
                expected_raw_md5 + ", received " + last_hash + ".");
    }
    throw AppError(
        reboot_timeout,
        "Timed out waiting for a valid firmware hash after reboot" +
            (last_error.empty() ? std::string(".") : ": " + last_error));
}

void validate_inputs(
    const Options& options,
    const Console& console,
    std::vector<std::uint8_t>& firmware,
    FirmwareHashes& hashes) {
    firmware = read_firmware(options.firmware, options.maximum_firmware_bytes);
    const std::vector<std::uint8_t> raw =
        gunzip_firmware(firmware, options.maximum_firmware_bytes);
    const Md5Manifest manifest = parse_md5_manifest(read_text_file(options.manifest));
    const std::string filename = wide_to_utf8(options.firmware.filename().wstring());
    hashes = select_hashes(manifest, filename);
    const std::string calculated_compressed = md5_bytes(firmware);
    const std::string calculated_raw = md5_bytes(raw);

    if (!options.quiet) {
        std::cout << "Firmware: "
                  << sanitize_console_text(wide_to_utf8(fs::absolute(options.firmware).wstring())) << "\n"
                  << "Compressed size: " << firmware.size() << " bytes\n"
                  << "Raw size: " << raw.size() << " bytes\n"
                  << "Compressed MD5: " << console.paint("36", calculated_compressed) << "\n"
                  << "Raw firmware MD5: " << console.paint("36", calculated_raw) << "\n";
    }

    if (calculated_compressed != hashes.compressed) {
        throw AppError(
            integrity_error,
            "Compressed firmware MD5 mismatch. Expected " + hashes.compressed +
                ", calculated " + calculated_compressed + ".");
    }
    if (calculated_raw != hashes.raw) {
        throw AppError(
            integrity_error,
            "Raw firmware MD5 mismatch. Expected " + hashes.raw +
                ", calculated " + calculated_raw + ".");
    }
    if (!options.quiet) {
        std::cout << console.paint(
            "1;32",
            "✅ Gzip stream and strict two-record manifest are valid.") << "\n";
    }
}

void expect_self_test(bool condition, std::string_view message) {
    if (!condition) {
        throw AppError(internal_error, "Self-test failed: " + std::string(message));
    }
}

void run_self_test() {
    const Options defaults;
    expect_self_test(
        defaults.connect_timeout_ms == 20000,
        "default allows a cold Windows mDNS lookup");

    const std::string fixture =
        "90EDAD71A57D0437412D2F3F9EAA46BF *folder/firmware.bin\n"
        "6fb510b13cdc013ad7a29b68f326f37c *firmware.bin (compressed)\r\n";
    const Md5Manifest manifest = parse_md5_manifest(fixture);
    const FirmwareHashes hashes = select_hashes(manifest, "firmware.bin");
    expect_self_test(hashes.raw == "90edad71a57d0437412d2f3f9eaa46bf", "raw MD5 selection");
    expect_self_test(hashes.compressed == "6fb510b13cdc013ad7a29b68f326f37c", "compressed MD5 selection");

    const ParsedEndpoint endpoint = parse_endpoint(L"http://device.local:8080/update?slot=0");
    expect_self_test(!endpoint.secure, "HTTP scheme");
    expect_self_test(endpoint.port == 8080, "custom port");
    expect_self_test(endpoint.original_host == L"device.local", "hostname");
    expect_self_test(endpoint.host_header == L"device.local:8080", "Host header");
    expect_self_test(endpoint.upload_path == L"/update?slot=0", "upload target");

    const InfoMap info = parse_info_lines(
        "hostname: rayanlamp\r\nfirmware hash: 90edad71a57d0437412d2f3f9eaa46bf\n");
    expect_self_test(info.at("hostname") == "rayanlamp", "info hostname");
    expect_self_test(
        clean_reported_md5(info.at("firmware hash")) ==
            "90edad71a57d0437412d2f3f9eaa46bf",
        "info firmware MD5");
    expect_self_test(
        clean_reported_md5("90edad71a57d0437412d2f3f9eaa46bfgarbage").empty(),
        "firmware MD5 must be exact");
    expect_self_test(
        parse_info_hash("{\"product\":\"fixture\",\"hash\":\"90edad71a57d0437412d2f3f9eaa46bf\"}") ==
            "90edad71a57d0437412d2f3f9eaa46bf",
        "JSON info firmware MD5");
    expect_self_test(
        sanitize_console_text("safe\x1b]0;unsafe\x07") == "safe?]0;unsafe?",
        "terminal control sanitization");

    bool conflict_rejected = false;
    try {
        (void)parse_md5_manifest(
            "00000000000000000000000000000000 *firmware.bin\n"
            "11111111111111111111111111111111 *firmware.bin\n");
    } catch (const AppError& error) {
        conflict_rejected = error.code() == input_error;
    }
    expect_self_test(conflict_rejected, "conflicting manifest records");

    const BuildIdentity build_a{{100, 10}, {50, 20}};
    const BuildIdentity build_b{{101, 11}, {50, 20}};
    const BuildIdentity build_c{{102, 12}, {51, 21}};
    WatchDebouncer watch_state(1000);
    expect_self_test(!watch_state.observe(build_a, 0), "watch begins debouncing");
    expect_self_test(!watch_state.observe(build_a, 999), "watch requires full debounce");
    expect_self_test(watch_state.observe(build_a, 1000), "stable watch build becomes ready");
    watch_state.mark_attempted(build_a);
    expect_self_test(!watch_state.observe(build_a, 5000), "attempted build is not replayed");
    expect_self_test(watch_state.already_attempted(build_a), "watch remembers attempted build");
    expect_self_test(!watch_state.observe(build_b, 5100), "new build starts a new debounce");
    expect_self_test(watch_state.observe(build_b, 6100), "new stable build is coalesced and ready");
    watch_state.mark_attempted(build_b);
    expect_self_test(!watch_state.observe(build_a, 6200), "reverted build starts observation");
    expect_self_test(!watch_state.observe(build_a, 8000), "older attempted build is never replayed");
    expect_self_test(!watch_state.observe(std::nullopt, 8050), "transient build resets stability");
    expect_self_test(!watch_state.observe(build_c, 8100), "post-transient build re-debounces");
    expect_self_test(watch_state.observe(build_c, 9100), "post-transient stable build is ready");

    std::cout << "All built-in parser and protocol self-tests passed.\n";
}

ParsedEndpoint resolve_endpoint(const Options& options, const Console& console) {
    ParsedEndpoint endpoint = parse_endpoint(options.endpoint);
    if (endpoint.secure && !is_numeric_address(endpoint.original_host)) {
        throw AppError(
            endpoint_error,
            "HTTPS hostname endpoints cannot be safely address-pinned: resolving to an IP "
            "would lose the certificate hostname/SNI. Use HTTP on the trusted local network "
            "or an HTTPS endpoint whose certificate is valid for a numeric address.");
    }
    if (!is_numeric_address(endpoint.original_host)) {
        if (!options.quiet) {
            std::cout << console.paint("33", "Resolving hostname once: ")
                      << sanitize_console_text(wide_to_utf8(endpoint.original_host)) << "\n";
        }
        endpoint.connect_host = resolve_hostname_once(endpoint.original_host);
        if (!options.quiet) {
            std::cout << console.paint("32", "Resolved and cached: ")
                      << sanitize_console_text(wide_to_utf8(endpoint.connect_host)) << "\n";
        }
    }
    if (!options.quiet) {
        std::cout << "Endpoint: "
                  << sanitize_console_text(wide_to_utf8(options.endpoint)) << "\n";
    }
    return endpoint;
}

void upload_current_build(
    const Options& options,
    const Console& console,
    ParsedEndpoint& endpoint,
    const std::optional<BuildIdentity>& expected_build = std::nullopt) {
    std::vector<std::uint8_t> firmware;
    FirmwareHashes hashes;
    validate_inputs(options, console, firmware, hashes);
    if (expected_build) {
        std::string reason;
        const auto current = observe_build(options, reason);
        if (!current || !(*current == *expected_build)) {
            throw AppError(
                input_error,
                "Build changed while it was being prepared; waiting for the newest stable files.",
                false,
                true);
        }
    }

    bool address_refreshed = false;
    const FirmwareInfo before = fetch_firmware_info(
        endpoint,
        options,
        console,
        address_refreshed,
        options.request_timeout_ms);
    if (!options.quiet) {
        std::cout << console.paint("90", "Device firmware before transfer: ")
                  << before.hash << "\n";
    }
    if (before.hash == hashes.raw && !options.force) {
        if (!options.quiet) {
            std::cout << console.paint(
                "1;32",
                "✅ Device already has this firmware; upload skipped. Use --force to override.")
                      << "\n";
        }
        return;
    }
    if (!options.backup.empty()) {
        if (!options.quiet) {
            std::cout << console.paint(
                "36",
                "Downloading a verified pre-update backup...") << "\n";
        }
        (void)verified_download(
            endpoint,
            options,
            console,
            address_refreshed,
            options.backup);
    }

    const auto perform_upload = [&]() {
        HttpClient client(
            endpoint, options.connect_timeout_ms, options.request_timeout_ms);
        return upload_firmware(
            client,
            options,
            options.firmware,
            firmware,
            hashes.compressed,
            console);
    };

    HttpResponse response;
    try {
        response = perform_upload();
    } catch (const AppError& error) {
        if (!error.address_retryable() || address_refreshed ||
            is_numeric_address(endpoint.original_host)) {
            throw;
        }
        address_refreshed = true;
        console.clear_progress();
        if (!options.quiet) {
            std::cout << console.paint(
                "33",
                "Cached address failed before upload; resolving the hostname once more.") << "\n";
        }
        endpoint.connect_host = resolve_hostname_once(endpoint.original_host);
        if (!options.quiet) {
            std::cout << console.paint("32", "Refreshed and cached the new address.") << "\n";
        }
        response = perform_upload();
    }

    if (response.status < 200 || response.status >= 300) {
        throw AppError(
            upload_rejected,
            "Upload returned HTTP " + std::to_string(response.status) +
                (response.body.empty() ? std::string() : ": " + printable_response(response.body)));
    }
    if (response.body != "ok!") {
        throw AppError(
            upload_rejected,
            "Upload response was not the exact expected \"ok!\" body. Received: \"" +
                printable_response(response.body) + "\".");
    }

    if (!options.quiet) {
        std::cout << console.paint("1;32", "✅ Upload accepted with exact ok! response.") << "\n";
    }
    if (options.verify) {
        verify_rebooted_firmware(endpoint, hashes.raw, options, console, address_refreshed);
    } else if (!options.quiet) {
        std::cout << console.paint(
            "1;33",
            "⚠ Post-reboot verification was explicitly disabled.") << "\n";
    }
}

int run_watch(
    const Options& options,
    const Console& console,
    ParsedEndpoint& endpoint) {
    const WatchConsoleHandler handler;
    WatchDebouncer debouncer(options.debounce_ms);
    std::string last_status;

    const auto report_status = [&](const std::string& status) {
        if (status == last_status) {
            return;
        }
        last_status = status;
        std::cout << console.paint("90", sanitize_console_text(status)) << "\n";
    };

    std::cout << console.paint("1;36", "Watching for stable firmware builds:") << "\n"
              << sanitize_console_text(wide_to_utf8(fs::absolute(options.firmware).wstring()))
              << "\n"
              << console.paint("90", "Press Ctrl+C to stop safely.") << "\n";

    while (!watch_stop_requested.load(std::memory_order_relaxed)) {
        std::string reason;
        const auto candidate = observe_build(options, reason);
        const bool ready = debouncer.observe(candidate, monotonic_milliseconds());

        if (!candidate) {
            report_status(reason);
        } else if (ready) {
            console.clear_progress();
            std::cout << console.paint("1;36", "Stable build detected; starting one-shot transaction.")
                      << "\n";
            bool mark_attempted = true;
            try {
                upload_current_build(options, console, endpoint, candidate);
                std::cout << console.paint("1;32", "Watch transaction completed.") << "\n";
            } catch (const AppError& error) {
                console.clear_progress();
                if (error.watch_transient()) {
                    mark_attempted = false;
                    std::cout << console.paint("33", sanitize_console_text(error.what())) << "\n";
                } else if (error.code() == cancelled) {
                    debouncer.mark_attempted(*candidate);
                    std::cout << console.paint(
                        "33",
                        "Stop requested. The current build will not be replayed automatically.")
                              << "\n";
                    return cancelled;
                } else {
                    std::cerr << console.paint_error("1;31", "Watch transaction failed: ")
                              << sanitize_console_text(error.what()) << "\n"
                              << "This exact build will not be replayed; waiting for newer files.\n";
                }
            } catch (const std::exception& error) {
                console.clear_progress();
                std::cerr << console.paint_error("1;31", "Watch transaction failed: ")
                          << sanitize_console_text(error.what()) << "\n"
                          << "This exact build will not be replayed; waiting for newer files.\n";
            }
            if (mark_attempted) {
                debouncer.mark_attempted(*candidate);
            }
            last_status.clear();
        } else if (debouncer.already_attempted(*candidate)) {
            report_status("Watching for a newer build.");
        } else {
            report_status(reason);
        }

        if (!wait_interruptibly(options.watch_poll_ms)) {
            break;
        }
    }

    console.clear_progress();
    std::cout << console.paint("1;32", "Watcher stopped safely.") << "\n";
    return cancelled;
}

int run(const Options& options, const Console& console) {
    if (options.help) {
        print_usage();
        return success;
    }
    if (options.version) {
        std::cout << wide_to_utf8(kVersion) << "\n";
        return success;
    }
    if (options.self_test) {
        run_self_test();
        return success;
    }
    if (options.validate_only) {
        std::vector<std::uint8_t> firmware;
        FirmwareHashes hashes;
        validate_inputs(options, console, firmware, hashes);
        if (!options.quiet) {
            std::cout << console.paint(
                "1;32",
                "Validation-only run completed; nothing was uploaded.") << "\n";
        }
        return success;
    }

    const Winsock winsock;
    ParsedEndpoint endpoint = resolve_endpoint(options, console);
    if (!options.download.empty()) {
        bool address_refreshed = false;
        (void)verified_download(
            endpoint,
            options,
            console,
            address_refreshed,
            options.download);
        return success;
    }
    if (options.watch) {
        return run_watch(options, console, endpoint);
    }
    upload_current_build(options, console, endpoint);
    return success;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    bool force_color = false;
    bool no_color = environment_value(L"NO_COLOR").has_value();
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--color") {
            force_color = true;
        } else if (std::wstring_view(argv[index]) == L"--no-color") {
            no_color = true;
        }
    }
    const Console console = Console::configure(force_color, no_color);

    try {
        const Options options = parse_arguments(argc, argv);
        return run(options, console);
    } catch (const AppError& error) {
        console.clear_progress();
        std::cerr << console.paint_error("1;31", "❌ Error: ")
                  << sanitize_console_text(error.what()) << "\n";
        if (error.code() == usage_error) {
            std::cerr << "Run with --help for usage.\n";
        }
        return static_cast<int>(error.code());
    } catch (const fs::filesystem_error& error) {
        console.clear_progress();
        std::cerr << console.paint_error("1;31", "❌ Filesystem error: ")
                  << sanitize_console_text(error.what()) << "\n";
        return local_io_error;
    } catch (const std::exception& error) {
        console.clear_progress();
        std::cerr << console.paint_error("1;31", "❌ Unexpected error: ")
                  << sanitize_console_text(error.what()) << "\n";
        return internal_error;
    }
}
