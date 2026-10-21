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

DecodedImage decode(const std::string& source, int outputWidth, int outputHeight,
                    bool fill);
bool prepare(const std::string& source, int outputWidth, int outputHeight, bool fill);
void remove(const std::string& source, int outputWidth, int outputHeight);

// Scale to fit inside maxWidth×maxHeight (no letterbox). Cache key is
// source.fit{maxW}x{maxH}.rgba-cache; payload size is the tight output.
DecodedImage decodeFit(const std::string& source, int maxWidth, int maxHeight);
bool prepareFit(const std::string& source, int maxWidth, int maxHeight);
void removeFit(const std::string& source, int maxWidth, int maxHeight);

} // namespace steamgriddb::artwork
