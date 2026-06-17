#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace nxui {

namespace {

class JsonFlatParser {
public:
    JsonFlatParser(const std::string& src,
                   std::unordered_map<std::string, std::string>& out)
        : m_src(src), m_out(out) {}

    bool parse() {
        skipWs();
        if (!consume('{')) return false;
        parseObject("");
        return true;
    }

private:
    bool parseObject(const std::string& prefix) {
        skipWs();
        if (peek() == '}') {
            ++m_pos;
            return true;
        }

        while (m_pos < m_src.size()) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;

            skipWs();
            if (!consume(':')) return false;
            skipWs();

            const char c = peek();
            const std::string fullKey = prefix.empty() ? key : (prefix + "." + key);

            if (c == '"') {
                std::string value;
                if (!parseString(value)) return false;
                m_out[fullKey] = value;
            } else if (c == '{') {
                ++m_pos;
                if (!parseObject(fullKey)) return false;
            } else {
                skipValue();
            }

            skipWs();
            if (consume(',')) continue;
            if (consume('}')) return true;
            return false;
        }
        return false;
    }

    void skipValue() {
        int depthObj = 0;
        int depthArr = 0;
        bool inString = false;
        bool escaped = false;

        while (m_pos < m_src.size()) {
            char c = m_src[m_pos];
            if (inString) {
                ++m_pos;
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }

            if (c == '"') {
                inString = true;
                ++m_pos;
                continue;
            }
            if (c == '{') { ++depthObj; ++m_pos; continue; }
            if (c == '}') {
                if (depthObj == 0 && depthArr == 0) return;
                --depthObj;
                ++m_pos;
                continue;
            }
            if (c == '[') { ++depthArr; ++m_pos; continue; }
            if (c == ']') { --depthArr; ++m_pos; continue; }
            if (c == ',' && depthObj == 0 && depthArr == 0) return;

            ++m_pos;
        }
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return false;
        out.clear();

        while (m_pos < m_src.size()) {
            char c = m_src[m_pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (m_pos >= m_src.size()) return false;
                char e = m_src[m_pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    void skipWs() {
        while (m_pos < m_src.size() && std::isspace((unsigned char)m_src[m_pos]))
            ++m_pos;
    }

    char peek() const {
        if (m_pos >= m_src.size()) return '\0';
        return m_src[m_pos];
    }

    bool consume(char expected) {
        if (peek() != expected) return false;
        ++m_pos;
        return true;
    }

    const std::string& m_src;
    std::unordered_map<std::string, std::string>& m_out;
    size_t m_pos = 0;
};

} // namespace

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

bool I18n::initialize(const std::string& basePath, const std::string& fallbackTag) {
    m_basePath = basePath;
    m_fallbackTag = normalizeTag(fallbackTag);
    if (m_fallbackTag.empty()) m_fallbackTag = "en-US";

    m_fallbackMap.clear();
    if (!loadJsonMapFromFile(buildLanguagePath(m_fallbackTag), m_fallbackMap)) {
        m_fallbackTag = "en-US";
        m_fallbackMap.clear();
        loadJsonMapFromFile(buildLanguagePath(m_fallbackTag), m_fallbackMap);
    }

    return setLanguageAuto();
}

bool I18n::setLanguageAuto() {
    return setLanguage(detectSystemLanguageTag());
}

bool I18n::setLanguage(const std::string& tag) {
    const std::string previousTag = m_activeTag;

    std::string normalized = normalizeTag(tag);
    if (normalized.empty() || normalized == "auto") {
        normalized = detectSystemLanguageTag();
    }

    std::unordered_map<std::string, std::string> nextMap;
    if (!loadJsonMapFromFile(buildLanguagePath(normalized), nextMap)) {
        normalized = m_fallbackTag;
        if (!loadJsonMapFromFile(buildLanguagePath(normalized), nextMap)) {
            nextMap.clear();
        }
    }

    m_activeTag = normalized;
    m_activeMap = std::move(nextMap);

    if (m_activeTag != previousTag)
        notifyLanguageChanged();

    return !m_activeMap.empty() || !m_fallbackMap.empty();
}

std::string I18n::tr(std::string_view key, std::string_view fallback) const {
    auto it = m_activeMap.find(std::string(key));
    if (it != m_activeMap.end()) return it->second;

    auto fb = m_fallbackMap.find(std::string(key));
    if (fb != m_fallbackMap.end()) return fb->second;

    if (!fallback.empty()) return std::string(fallback);
    return std::string(key);
}

int I18n::addLanguageChangedListener(LanguageChangedCb cb) {
    if (!cb) return -1;
    int id = m_nextListenerId++;
    m_languageListeners[id] = std::move(cb);
    return id;
}

void I18n::removeLanguageChangedListener(int id) {
    if (id < 0) return;
    m_languageListeners.erase(id);
}

void I18n::notifyLanguageChanged() {
    auto listeners = m_languageListeners;
    for (auto& kv : listeners) {
        if (kv.second) kv.second();
    }
}

std::string I18n::normalizeTag(const std::string& tag) {
    if (tag.empty()) return "";

    std::string out;
    out.reserve(tag.size());

    for (char c : tag) {
        if (c == '_') out.push_back('-');
        else if (!std::isspace((unsigned char)c)) out.push_back(c);
    }

    if (out.size() == 2) {
        out[0] = (char)std::tolower((unsigned char)out[0]);
        out[1] = (char)std::tolower((unsigned char)out[1]);
        return out;
    }

    auto dash = out.find('-');
    if (dash != std::string::npos && dash + 2 < out.size()) {
        std::string lang = out.substr(0, dash);
        std::string region = out.substr(dash + 1);
        std::transform(lang.begin(), lang.end(), lang.begin(), [](char ch) {
            return (char)std::tolower((unsigned char)ch);
        });
        std::transform(region.begin(), region.end(), region.begin(), [](char ch) {
            return (char)std::toupper((unsigned char)ch);
        });
        return lang + "-" + region;
    }

    return out;
}

std::string I18n::buildLanguagePath(const std::string& normalizedTag) const {
    return m_basePath + "/" + normalizedTag + ".json";
}

bool I18n::loadJsonMapFromFile(const std::string& path,
                               std::unordered_map<std::string, std::string>& out) const {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    out.clear();
    JsonFlatParser parser(src, out);
    return parser.parse();
}

static std::vector<std::string> scanI18nLanguageTags() {
    std::vector<std::string> tags;
    const char* basePath = "romfs:/i18n";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(basePath, ec)) {
        if (ec)
            break;

        const std::string name = entry.path().filename().string();
        if (name.size() <= 5 || name.substr(name.size() - 5) != ".json")
            continue;

        const std::string tag = name.substr(0, name.size() - 5);
        if (!tag.empty())
            tags.push_back(tag);
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

std::vector<std::string> I18n::supportedLanguageTags() {
    std::vector<std::string> tags = scanI18nLanguageTags();
    tags.erase(std::remove_if(tags.begin(), tags.end(), [](const std::string& tag) {
        return tag == "auto";
    }), tags.end());
    tags.insert(tags.begin(), "auto");
    return tags;
}

std::string I18n::detectSystemLanguageTag() {
    u64 langCode = 0;
    if (R_FAILED(setGetSystemLanguage(&langCode)))
        return "en-US";

    SetLanguage lang = SetLanguage_ENUS;
    if (R_FAILED(setMakeLanguage(langCode, &lang)))
        return "en-US";

    switch (lang) {
        case SetLanguage_JA:    return "ja-JP";
        case SetLanguage_ENUS:  return "en-US";
        case SetLanguage_FR:    return "fr-FR";
        case SetLanguage_DE:    return "de-DE";
        case SetLanguage_IT:    return "it-IT";
        case SetLanguage_ES:    return "es-ES";
        case SetLanguage_ZHCN:  return "zh-CN";
        case SetLanguage_KO:    return "ko-KR";
        case SetLanguage_NL:    return "nl-NL";
        case SetLanguage_PT:    return "pt-PT";
        case SetLanguage_RU:    return "ru-RU";
        case SetLanguage_ZHTW:  return "zh-TW";
        case SetLanguage_ENGB:  return "en-GB";
        case SetLanguage_FRCA:  return "fr-CA";
        case SetLanguage_ES419: return "es-419";
        case SetLanguage_ZHHANS:return "zh-CN";
        case SetLanguage_ZHHANT:return "zh-TW";
        case SetLanguage_PTBR:  return "pt-BR";
        default:                return "en-US";
    }
}

} // namespace nxui
