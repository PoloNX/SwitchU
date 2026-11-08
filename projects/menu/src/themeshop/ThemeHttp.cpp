#include "ThemeHttp.hpp"

#include "core/DebugLog.hpp"

#include <curl/curl.h>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {

std::mutex g_themeHttpMutex;
bool g_nifmInitialized = false;
bool g_socketInitialized = false;
bool g_curlInitialized = false;
// DNS answers and open connections shared by every request. Requests are
// serialized by g_themeHttpMutex, so the share needs no lock callbacks. Art
// downloads hit the same two hosts back to back; without this each one paid a
// fresh DNS lookup and TLS handshake.
CURLSH* g_share = nullptr;

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
    if (g_share) {
        curl_share_cleanup(g_share);
        g_share = nullptr;
    }
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

    if (!g_share) {
        g_share = curl_share_init();
        if (g_share) {
            curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
            curl_share_setopt(g_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        }
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

bool isAbortRequested(const themeshop::http::AbortCheck& shouldAbort) {
    return shouldAbort && shouldAbort();
}

void configureRequest(curlpp::Easy& request,
                      const std::string& url,
                      std::ostringstream& response,
                      const std::list<std::string>& headers,
                      const themeshop::http::ProgressCallback& onProgress = {},
                      const themeshop::http::AbortCheck& shouldAbort = {}) {
    request.setOpt<curlpp::options::Url>(url);
    if (g_share)
        curl_easy_setopt(request.getHandle(), CURLOPT_SHARE, g_share);
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
    if (onProgress || shouldAbort) {
        request.setOpt<curlpp::options::NoProgress>(false);
        request.setOpt<curlpp::options::ProgressFunction>(
            [onProgress, shouldAbort](double downloadTotal, double downloaded, double, double) {
                if (isAbortRequested(shouldAbort))
                    return 1; // Non-zero aborts the curl transfer immediately.
                if (onProgress) {
                    onProgress(downloaded > 0.0 ? static_cast<std::uint64_t>(downloaded) : 0,
                               downloadTotal > 0.0 ? static_cast<std::uint64_t>(downloadTotal) : 0);
                }
                return 0;
            });
    }
}

std::vector<std::uint8_t> performRequestBytes(const std::string& url,
                                              const std::list<std::string>& headers,
                                              const themeshop::http::ProgressCallback& onProgress,
                                              const themeshop::http::AbortCheck& shouldAbort) {
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    std::ostringstream response;
    curlpp::Easy request;
    configureRequest(request, url, response, headers, onProgress, shouldAbort);
    try {
        request.perform();
    } catch (const curlpp::LibcurlRuntimeError& ex) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        const char* detail = ex.what();
        std::string msg = "HTTPS failed";
        if (detail && detail[0] != '\0') {
            msg += ": ";
            msg += detail;
        } else {
            msg += ": ";
            msg += curl_easy_strerror(ex.whatCode());
        }
        throw std::runtime_error(msg);
    } catch (const std::exception& ex) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        const char* detail = ex.what();
        if (detail && detail[0] != '\0')
            throw std::runtime_error(detail);
        throw std::runtime_error("HTTPS request failed");
    } catch (...) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        throw std::runtime_error("HTTPS request failed");
    }

    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

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
                                       const themeshop::http::ProgressCallback& onProgress = {},
                                       const themeshop::http::AbortCheck& shouldAbort = {}) {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);

    std::string lastError = "Theme Shop HTTP request failed";
    for (int attempt = 1; attempt <= kRequestAttemptCount; ++attempt) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        try {
            if (!initializeRuntimeLocked()) {
                throw std::runtime_error("Theme Shop HTTP runtime is unavailable");
            }

            ensureInternetConnectionReady(url);
            auto bytes = performRequestBytes(url, headers, onProgress, shouldAbort);
            if (attempt > 1) {
                DebugLog::log("[themeshop] request recovered on retry %d: %s", attempt, url.c_str());
            }
            return bytes;
        } catch (const std::exception& ex) {
            lastError = ex.what() ? ex.what() : "";
            if (lastError.empty())
                lastError = "HTTPS request failed";
            if (lastError == "Cancelled" || isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
            DebugLog::log("[themeshop] request failed (%d/%d): %s -> %s",
                          attempt,
                          kRequestAttemptCount,
                          url.c_str(),
                          lastError.c_str());
        } catch (...) {
            if (isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
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

std::string extractHttpDateHeader(const std::string& headerBlock) {
    // curl header callback delivers one header line at a time or a buffer; we
    // accumulate full blocks and scan case-insensitively for "Date:".
    std::string lower = headerBlock;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr const char* kKey = "date:";
    const auto pos = lower.find(kKey);
    if (pos == std::string::npos)
        return {};
    std::size_t start = pos + 5;
    while (start < headerBlock.size()
           && (headerBlock[start] == ' ' || headerBlock[start] == '\t'))
        ++start;
    std::size_t end = start;
    while (end < headerBlock.size()
           && headerBlock[end] != '\r' && headerBlock[end] != '\n')
        ++end;
    while (end > start && (headerBlock[end - 1] == ' ' || headerBlock[end - 1] == '\t'))
        --end;
    return headerBlock.substr(start, end - start);
}

std::uint64_t performUtcUnixTimeRequest(const std::string& url,
                                        const themeshop::http::AbortCheck& shouldAbort) {
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    std::string headerAccum;
    std::ostringstream body;
    curlpp::Easy request;
    request.setOpt<curlpp::options::Url>(url);
    request.setOpt<curlpp::options::NoBody>(true); // HEAD
    request.setOpt<curlpp::options::FollowLocation>(true);
    request.setOpt<curlpp::options::NoSignal>(true);
    request.setOpt<curlpp::options::ConnectTimeout>(12L);
    request.setOpt<curlpp::options::Timeout>(20L);
    request.setOpt<curlpp::options::IpResolve>((long)CURL_IPRESOLVE_V4);
    request.setOpt<curlpp::options::UserAgent>(std::string("SwitchU/") + SWITCHU_VERSION);
    // Clock may be years off; cert validity checks then fail. This endpoint is
    // only used to read the Date header for bootstrapping the console clock.
    request.setOpt<curlpp::options::SslVerifyPeer>(false);
    request.setOpt<curlpp::options::SslVerifyHost>(0L);
    request.setOpt<curlpp::options::WriteStream>(&body);
    request.setOpt<curlpp::options::HeaderFunction>(
        [&headerAccum, &shouldAbort](char* ptr, size_t size, size_t nmemb) -> size_t {
            if (isAbortRequested(shouldAbort))
                return 0; // abort
            const size_t total = size * nmemb;
            headerAccum.append(ptr, total);
            return total;
        });

    try {
        request.perform();
    } catch (...) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        throw;
    }
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    const std::string dateValue = extractHttpDateHeader(headerAccum);
    if (dateValue.empty())
        throw std::runtime_error("No Date header from " + url);

    const time_t parsed = curl_getdate(dateValue.c_str(), nullptr);
    if (parsed < 0)
        throw std::runtime_error("Could not parse Date header: " + dateValue);

    // Sanity: reject absurd results (before 2020 / after ~2100).
    constexpr time_t kMin = 1577836800;  // 2020-01-01 UTC
    constexpr time_t kMax = 4102444800;  // 2100-01-01 UTC
    if (parsed < kMin || parsed > kMax)
        throw std::runtime_error("Date header out of range: " + dateValue);

    DebugLog::log("[themeshop] clock sync Date '%s' -> %lld from %s",
                  dateValue.c_str(),
                  static_cast<long long>(parsed),
                  url.c_str());
    return static_cast<std::uint64_t>(parsed);
}

std::uint64_t performUtcUnixTime(const themeshop::http::AbortCheck& shouldAbort) {
    static const char* kUrls[] = {
        "https://www.cloudflare.com/",
        "https://www.google.com/",
        "https://1.1.1.1/",
    };

    std::lock_guard<std::mutex> lk(g_themeHttpMutex);
    std::string lastError = "Clock sync request failed";

    for (const char* url : kUrls) {
        for (int attempt = 1; attempt <= kRequestAttemptCount; ++attempt) {
            if (isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
            try {
                if (!initializeRuntimeLocked())
                    throw std::runtime_error("Theme Shop HTTP runtime is unavailable");
                ensureInternetConnectionReady(url);
                return performUtcUnixTimeRequest(url, shouldAbort);
            } catch (const std::exception& ex) {
                lastError = ex.what();
                if (lastError == "Cancelled" || isAbortRequested(shouldAbort))
                    throw std::runtime_error("Cancelled");
                DebugLog::log("[themeshop] clock sync failed (%d/%d): %s -> %s",
                              attempt, kRequestAttemptCount, url, ex.what());
            } catch (...) {
                if (isAbortRequested(shouldAbort))
                    throw std::runtime_error("Cancelled");
                lastError = "Unknown clock sync error";
                DebugLog::log("[themeshop] clock sync failed (%d/%d): %s -> unknown error",
                              attempt, kRequestAttemptCount, url);
            }
            if (attempt < kRequestAttemptCount)
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

bool isInternetReadyForHttp() {
    std::lock_guard<std::mutex> lk(g_themeHttpMutex);
    if (!initializeRuntimeLocked())
        return false;

    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    u32 strength = 0;
    const Result rc = nifmGetInternetConnectionStatus(nullptr, &strength, &status);
    if (R_FAILED(rc)) {
        DebugLog::log("[themeshop] isInternetReady nifm status rc=%s",
                      resultToString(rc).c_str());
        return false;
    }
    return status == NifmInternetConnectionStatus_Connected;
}

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers,
                                   const ProgressCallback& onProgress,
                                   const AbortCheck& shouldAbort) {
    return performBytes(url, headers, onProgress, shouldAbort);
}

std::string getText(const std::string& url,
                    const std::list<std::string>& headers,
                    const AbortCheck& shouldAbort) {
    auto bytes = performBytes(url, headers, {}, shouldAbort);
    return std::string(bytes.begin(), bytes.end());
}

std::uint64_t fetchUtcUnixTime(const AbortCheck& shouldAbort) {
    return performUtcUnixTime(shouldAbort);
}

} // namespace themeshop::http
