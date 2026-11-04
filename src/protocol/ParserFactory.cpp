#include "ParserFactory.h"

#include "FireWaterParser.h"
#include "ImageParser.h"
#include "JustFloatParser.h"
#include "RawParser.h"

namespace vofa::protocol {

std::unique_ptr<IStreamParser> makeParser(ProtocolKind kind)
{
    switch (kind) {
    case ProtocolKind::RawData:
        return std::make_unique<RawParser>();
    case ProtocolKind::JustFloat:
        return std::make_unique<JustFloatParser>();
    case ProtocolKind::FireWater:
        return std::make_unique<FireWaterParser>();
    case ProtocolKind::Image:
        return std::make_unique<ImageParser>();
    }
    return std::make_unique<RawParser>();
}

} // namespace vofa::protocol
