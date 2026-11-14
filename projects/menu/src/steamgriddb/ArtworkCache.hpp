#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace steamgriddb::artwork {

struct DecodedImage {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
};

// focusX/focusY: cover-crop anchor in [0,1] (0.5 = center). Values <0.5 bias
// toward the left/top of the source when the fill crop has leftover on that axis.
DecodedImage decode(const std::string& source, int outputWidth, int outputHeight,
                    bool fill, float focusX = 0.5f, float focusY = 0.5f);
bool prepare(const std::string& source, int outputWidth, int outputHeight, bool fill,
             float focusX = 0.5f, float focusY = 0.5f);
void remove(const std::string& source, int outputWidth, int outputHeight);

// Scale to fit inside maxWidth×maxHeight (no letterbox). Cache key is
// source.fit{maxW}x{maxH}.rgba-cache; payload size is the tight output.
DecodedImage decodeFit(const std::string& source, int maxWidth, int maxHeight);
bool prepareFit(const std::string& source, int maxWidth, int maxHeight);
void removeFit(const std::string& source, int maxWidth, int maxHeight);

} // namespace steamgriddb::artwork
