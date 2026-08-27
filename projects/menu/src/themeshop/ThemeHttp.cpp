#include "ThemeHttp.hpp"

#include "core/DebugLog.hpp"

#include <curl/curl.h>
#include <curlpp/Easy.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <switch.h>

#include <cstdio>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {

std::mutex g_themeHttpMutex;
bool g_nifmInitialized = false;
bool g_socketInitialized = false;
bool g_curlInitialized = false;

constexpr int kRequestAttemptCount = 2;

std::string resultToString(Result rc) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)rc);
    return buf;
}

bool runtimeInitializedLocked() {
    return g_nifmInitialized && g_socketInitialized && g_curlInitialized;
}

void shutdownRuntimeLocked() {
    if (g_curlInitialized) {
        curl_global_cleanup();
        g_curlInitialized = false;
    }
    if (g_socketInitialized) {
        socketExit();
        g_socketInitialized = false;
    }
    if (g_nifmInitialized) {
        nifmExit();
        g_nifmInitialized = false;
    }
}

bool initializeRuntimeLocked() {
    if (runtimeInitializedLocked())
        return true;

    if (!g_nifmInitialized) {
        Result rc = nifmInitialize(NifmServiceType_User);
        if (R_FAILED(rc)) {
            DebugLog::log("[themeshop] nifmInitialize failed: %s", resultToString(rc).c_str());
            shutdownRuntimeLocked();
            return false;
        }
        g_nifmInitialized = true;
    }

    if (!g_socketInitialized) {
        Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) {
            DebugLog::log("[themeshop] socketInitializeDefault failed: %s", resultToString(rc).c_str());
            shutdownRuntimeLocked();
            return false;
        }
        g_socketInitialized = true;
    }

    if (!g_curlInitialized) {
        CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            DebugLog::log("[themeshop] curl_global_init failed: %s", curl_easy_strerror(rc));
            shutdownRuntimeLocked();
            return false;
        }
        g_curlInitialized = true;
    }

    DebugLog::log("[themeshop] http runtime ready");
    return true;
}

void ensureInternetConnectionReady(const std::string& url) {
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    u32 strength = 0;
    Result rc = nifmGetInternetConnectionStatus(nullptr, &strength, &status);
    if (R_FAILED(rc)) {
        throw std::runtime_error("nifmGetInternetConnectionStatus failed: " + resultToString(rc));
    }
    if (status != NifmInternetConnectionStatus_Connected) {
        throw std::runtime_error("Internet connection is not ready for " + url);
    }
}

void configureRequest(curlpp::Easy& request,
                      const std::string& url,
                      std::ostringstream& response,
                      const std::list<std::string>& headers,
                      const themeshop::http::ProgressCallback& onProgress = {}) {
    request.setOpt<curlpp::options::Url>(url);
    request.setOpt<curlpp::options::FollowLocation>(true);
    request.setOpt<curlpp::options::NoSignal>(true);
    request.setOpt<curlpp::options::ConnectTimeout>(12L);
    request.setOpt<curlpp::options::Timeout>(30L);
    request.setOpt<curlpp::options::IpResolve>((long)CURL_IPRESOLVE_V4);
    request.setOpt<curlpp::options::UserAgent>(std::string("SwitchU/") + SWITCHU_VERSION);
    if (!headers.empty()) {
        request.setOpt<curlpp::options::HttpHeader>(headers);
    }
    request.setOpt<curlpp::options::WriteStream>(&response);
    if (onProgress) {
        request.setOpt<curlpp::options::NoProgress>(false);
        request.setOpt<curlpp::options::ProgressFunction>(
            [onProgress](double downloadTotal, double downloaded, double, double) {
                onProgress(downloaded > 0.0 ? static_cast<std::uint64_t>(downloaded) : 0,
                           downloadTotal > 0.0 ? static_cast<std::uint64_t>(downloadTotal) : 0);
                return 0;
            });
    }
}

std::vector<std::uint8_t> performRequestBytes(const std::string& url,
                                              const std::list<std::string>& headers,
                                              const themeshop::http::ProgressCallback& onProgress) {
    std::ostringstream response;
    curlpp::Easy request;
    configureRequest(request, url, response, headers, onProgress);
    request.perform();

    long statusCode = curlpp::infos::ResponseCode::get(request);
    std::string body = response.str();
    if (statusCode < 200 || statusCode >= 300) {
        std::string detail = body.substr(0, 240);
        std::replace(detail.begin(), detail.end(), '\n', ' ');
        std::replace(detail.begin(), detail.end(), '\r', ' ');
        throw std::runtime_error("HTTP error " + std::to_string(statusCode)
                                 + (detail.empty() ? std::string() : ": " + detail));
    }

    return std::vector<std::uint8_t>(body.begin(), body.end());
}

std::vector<std::uint8_t> performBytes(const std::string& url,
                                       const std::list<std::string>& headers,
                                       const themeshop::http::ProgressCallback& onProgress = {}) {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);

    std::string lastError = "Theme Shop HTTP request failed";
    for (int attempt = 1; attempt <= kRequestAttemptCount; ++attempt) {
        try {
            if (!initializeRuntimeLocked()) {
                throw std::runtime_error("Theme Shop HTTP runtime is unavailable");
            }

            ensureInternetConnectionReady(url);
            auto bytes = performRequestBytes(url, headers, onProgress);
            if (attempt > 1) {
                DebugLog::log("[themeshop] request recovered on retry %d: %s", attempt, url.c_str());
            }
            return bytes;
        } catch (const std::exception& ex) {
            lastError = ex.what();
            DebugLog::log("[themeshop] request failed (%d/%d): %s -> %s",
                          attempt,
                          kRequestAttemptCount,
                          url.c_str(),
                          ex.what());
        } catch (...) {
            lastError = "Unknown HTTP error";
            DebugLog::log("[themeshop] request failed (%d/%d): %s -> unknown error",
                          attempt,
                          kRequestAttemptCount,
                          url.c_str());
        }

        if (attempt < kRequestAttemptCount) {
            shutdownRuntimeLocked();
        }
    }

    throw std::runtime_error(lastError);
}

} // namespace

namespace themeshop::http {

bool initialize() {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);
    return initializeRuntimeLocked();
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);
    shutdownRuntimeLocked();
}

bool isInitialized() {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);
    return runtimeInitializedLocked();
}

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers,
                                   const ProgressCallback& onProgress) {
    return performBytes(url, headers, onProgress);
}

std::string getText(const std::string& url,
                    const std::list<std::string>& headers) {
    auto bytes = performBytes(url, headers);
    return std::string(bytes.begin(), bytes.end());
}

} // namespace themeshop::http
