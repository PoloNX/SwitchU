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

} // namespace steamgriddb::artwork
