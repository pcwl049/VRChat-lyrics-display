#include "music_source.h"
#include "moekoe_client.h"
#include "netease_client.h"

namespace moekoe {

std::unique_ptr<MusicSource> createMusicSource(MusicPlatform platform) {
    switch (platform) {
        case MusicPlatform::MoeKoeMusic:
            return std::make_unique<MoeKoeClient>();
        case MusicPlatform::NeteaseCloudMusic:
            return std::make_unique<NeteaseClient>();
        default:
            return nullptr;
    }
}

} // namespace moekoe
