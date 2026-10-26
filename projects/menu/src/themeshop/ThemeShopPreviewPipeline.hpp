#pragma once

// Phase 2 skeleton: community preview disk cache + shared decode budgets.
// ThemeShopScreen still owns GPU upload / HTTP prime; local vs remote adapters
// grow here next so Themes/Icons presenters can share one path.

#include <cstdint>
#include <string>
#include <vector>

namespace themeshop::preview {

constexpr int kGridMaxSide = 384;   // keep GPU under ~64MB image budget
constexpr int kDetailMaxSide = 640;
// Cap GPU cover uploads per frame. 2 fills a cold 3x2 shelf quickly without the
// Frame Tap hitch that a burst caused; still polite for capture/stream.
constexpr int kMaxUploadsPerFrame = 2;

constexpr const char* kCommunityDiskDir = "sdmc:/config/SwitchU/community_preview_cache";

std::string communityDiskPath(const std::string& url);
bool loadCommunityDiskCache(const std::string& url, std::vector<std::uint8_t>& outBytes);
void saveCommunityDiskCache(const std::string& url, const std::vector<std::uint8_t>& bytes);

} // namespace themeshop::preview
