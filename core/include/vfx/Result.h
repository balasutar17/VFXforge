// VFX Forge core: error handling without exceptions crossing the API.
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace vfx {

// An error has two parts: a message an artist can read, and optional
// technical detail that belongs in a log file.
struct Error {
    std::string message;
    std::string detail;
};

inline Error makeError(std::string message, std::string detail = {}) {
    return Error{std::move(message), std::move(detail)};
}

template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : state_(std::move(value)) {}
    Result(Error error) : state_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(state_); }
    explicit operator bool() const { return ok(); }

    T& value() { return std::get<T>(state_); }
    const T& value() const { return std::get<T>(state_); }
    const Error& error() const { return std::get<Error>(state_); }

private:
    std::variant<T, Error> state_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), failed_(true) {}

    bool ok() const { return !failed_; }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return error_; }

private:
    Error error_;
    bool failed_ = false;
};

using Status = Result<void>;

}  // namespace vfx
