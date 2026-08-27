#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

namespace themeshop::http {

using ProgressCallback = std::function<void(std::uint64_t downloaded,
                                            std::uint64_t total)>;

bool initialize();
void shutdown();
bool isInitialized();

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers = {},
                                   const ProgressCallback& onProgress = {});

std::string getText(const std::string& url,
                    const std::list<std::string>& headers = {});

} // namespace themeshop::http
