#pragma once

#include <string>
#include <utility>
#include <variant>

namespace bz {

/// Human-readable failure. `message` is shown to the user as is,
/// so it is written in the UI language by the code that creates it.
struct Error {
    std::string message;
};

/// Minimal stand-in for C++23 std::expected: either a value or an Error.
template <class T>
class Expected {
public:
    Expected(T value) : myData(std::move(value)) {}
    Expected(Error error) : myData(std::move(error)) {}

    bool HasValue() const { return std::holds_alternative<T>(myData); }
    explicit operator bool() const { return HasValue(); }

    T& Value() { return std::get<T>(myData); }
    const T& Value() const { return std::get<T>(myData); }
    T* operator->() { return &Value(); }
    const T* operator->() const { return &Value(); }
    T& operator*() { return Value(); }
    const T& operator*() const { return Value(); }

    const Error& GetError() const { return std::get<Error>(myData); }

private:
    std::variant<T, Error> myData;
};

/// Result of an operation that produces no value.
class Status {
public:
    Status() = default;
    Status(Error error) : myError(std::move(error)), myFailed(true) {}

    static Status Ok() { return {}; }

    bool IsOk() const { return !myFailed; }
    explicit operator bool() const { return IsOk(); }
    const Error& GetError() const { return myError; }

private:
    Error myError;
    bool  myFailed = false;
};

} // namespace bz
