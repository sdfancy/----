#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace spray::protocol {

struct ProtocolError {
    QString message;
};

template <typename T>
struct ProtocolResult {
    std::optional<T> value;
    std::optional<ProtocolError> error;

    static ProtocolResult ok(T parsed)
    {
        return {std::move(parsed), std::nullopt};
    }

    static ProtocolResult fail(QString message)
    {
        return {std::nullopt, ProtocolError{std::move(message)}};
    }

    bool hasValue() const
    {
        return value.has_value();
    }
};

} // namespace spray::protocol
