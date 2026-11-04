#pragma once

#include "IStreamParser.h"

#include <memory>

namespace vofa::protocol {

enum class ProtocolKind {
    RawData,
    JustFloat,
    FireWater,
    Image,
};

[[nodiscard]] std::unique_ptr<IStreamParser> makeParser(ProtocolKind kind);

} // namespace vofa::protocol
