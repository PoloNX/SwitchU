#include "TextEntryScreen.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kInset = 26.f;
constexpr float kKeyGap = 8.f;
constexpr float kKeyboardTop = 166.f;   // from the panel top
constexpr float kRowHeight = 56.f;
constexpr float kFieldTop = 92.f;
constexpr float kFieldHeight = 58.f;

int sequenceLength(unsigned char lead) {
    if ((lead & 0x80u) == 0x00u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

int countCodepoints(const std::string& text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += static_cast<std::size_t>(sequenceLength(static_cast<unsigned char>(text[i])));
        ++count;
    }
    return count;
}

} // namespace

TextEntryScreen::TextEntryScreen() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    setFocusable(true);
    // Frosted, not clear, and the frost is made in onRender rather than by the
    // panel. Neither flag here can produce it: setBlurEnabled() captures the
    // scene into OFF_SCENE and blurs the OFF_SHARP_A/B pair, then draws the
    // unblurred capture, and the liquid-glass path samples that same sharp
    // capture -- which is why the keyboard read as clear glass over the
    // settings overlay even once the capture behind it was correct. onRender
    // blurs the capture itself, the way the settings overlay and the folder
    // transition already do.
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setBlurEnabled(false);
    setPanelOpacity(0.94f);
    setCornerRadius(26.f);
    setBorderWidth(1.f);
    setRect({kPanelX, kPanelY, kPanelW, kPanelH});
    buildLayout();
}

void TextEntryScreen::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    if (!m_theme)
        return;
    // Lighter than it was. The tint used to be the only thing hiding what was
    // behind the keys, so it had to be near solid; the blurred backdrop does
    // that job now, and leaving the tint that heavy would just cover the frost
    // with a flat plate.
    setBaseColor(m_theme->panelBase.withAlpha(
        std::clamp(m_theme->panelBase.a * 0.92f, 0.30f, 0.52f)));
    setBorderColor(m_theme->panelBorder.withAlpha(
        std::clamp(m_theme->panelBorder.a * 0.92f, 0.14f, 0.42f)));
    setHighlightColor(m_theme->panelHighlight.withAlpha(
        std::clamp(m_theme->panelHighlight.a * 0.92f, 0.05f, 0.18f)));
    setLiquidGlassShade(m_theme->mode == nxui::ThemeMode::Dark ? 0.08f : -0.03f);
}

void TextEntryScreen::buildLayout() {
    auto letter = [](const char* lower, const char* upper) {
        Key key;
        key.lower = lower;
        key.upper = upper;
        return key;
    };
    auto action = [](Action what, int span) {
        Key key;
        key.action = what;
        key.span = span;
        return key;
    };

    const std::vector<Key> numberRow = {
        letter("1", "1"), letter("2", "2"), letter("3", "3"), letter("4", "4"),
        letter("5", "5"), letter("6", "6"), letter("7", "7"), letter("8", "8"),
        letter("9", "9"), letter("0", "0")};
    const std::string language = nxui::I18n::instance().activeLanguageTag();
    const bool french = language.rfind("fr", 0) == 0;
    const bool german = language.rfind("de", 0) == 0;
    const bool russian = language.rfind("ru", 0) == 0;
    const bool spanish = language.rfind("es", 0) == 0;
    const bool italian = language.rfind("it", 0) == 0;
    const bool portuguese = language.rfind("pt", 0) == 0;

    m_letters.clear();
    m_letters.push_back(numberRow);
    if (russian) {
        m_letters.push_back({letter("й", "Й"), letter("ц", "Ц"), letter("у", "У"), letter("к", "К"),
                             letter("е", "Е"), letter("н", "Н"), letter("г", "Г"), letter("ш", "Ш"),
                             letter("щ", "Щ"), letter("з", "З")});
        m_letters.push_back({letter("ф", "Ф"), letter("ы", "Ы"), letter("в", "В"), letter("а", "А"),
                             letter("п", "П"), letter("р", "Р"), letter("о", "О"), letter("л", "Л"),
                             letter("д", "Д"), letter("ж", "Ж")});
        m_letters.push_back({letter("я", "Я"), letter("ч", "Ч"), letter("с", "С"), letter("м", "М"),
                             letter("и", "И"), letter("т", "Т"), letter("ь", "Ь"), letter("б", "Б"),
                             letter("ю", "Ю"), letter("ё", "Ё")});
    } else if (french) {
        m_letters.push_back({letter("a", "A"), letter("z", "Z"), letter("e", "E"), letter("r", "R"),
                             letter("t", "T"), letter("y", "Y"), letter("u", "U"), letter("i", "I"),
                             letter("o", "O"), letter("p", "P")});
        m_letters.push_back({letter("q", "Q"), letter("s", "S"), letter("d", "D"), letter("f", "F"),
                             letter("g", "G"), letter("h", "H"), letter("j", "J"), letter("k", "K"),
                             letter("l", "L"), letter("m", "M")});
        m_letters.push_back({letter("w", "W"), letter("x", "X"), letter("c", "C"), letter("v", "V"),
                             letter("b", "B"), letter("n", "N"), letter("é", "É"), letter("è", "È"),
                             letter("à", "À"), letter("ç", "Ç")});
    } else {
        m_letters.push_back({letter("q", "Q"), letter("w", "W"), letter("e", "E"), letter("r", "R"),
                             letter("t", "T"), letter(german ? "z" : "y", german ? "Z" : "Y"),
                             letter("u", "U"), letter("i", "I"), letter("o", "O"), letter("p", "P")});
        const char* localeLower = german ? "ü" : (spanish ? "ñ" : (italian ? "ò" : (portuguese ? "ç" : "'")));
        const char* localeUpper = german ? "Ü" : (spanish ? "Ñ" : (italian ? "Ò" : (portuguese ? "Ç" : "\"")));
        m_letters.push_back({letter("a", "A"), letter("s", "S"), letter("d", "D"), letter("f", "F"),
                             letter("g", "G"), letter("h", "H"), letter("j", "J"), letter("k", "K"),
                             letter("l", "L"), letter(localeLower, localeUpper)});
        m_letters.push_back({letter(german ? "y" : "z", german ? "Y" : "Z"),
                             letter("x", "X"), letter("c", "C"), letter("v", "V"),
                             letter("b", "B"), letter("n", "N"), letter("m", "M"),
                             letter(german ? "ö" : ",", german ? "Ö" : ";"),
                             letter(german ? "ä" : ".", german ? "Ä" : ":"),
                             letter(german ? "ß" : "-", german ? "ẞ" : "_")});
    }

    // Accented vowels sit beside the punctuation rather than behind a third
    // page: Portuguese needs them for ordinary folder names.
    m_symbols = {
        {letter("!", "!"), letter("@", "@"), letter("#", "#"), letter("$", "$"),
         letter("%", "%"), letter("&", "&"), letter("*", "*"), letter("(", "("),
         letter(")", ")"), letter("_", "_")},
        {letter("+", "+"), letter("=", "="), letter("/", "/"), letter("\\", "\\"),
         letter(":", ":"), letter(";", ";"), letter("\"", "\""), letter("'", "'"),
         letter("~", "~"), letter("|", "|")},
        {letter("?", "?"), letter("<", "<"), letter(">", ">"), letter("[", "["),
         letter("]", "]"), letter("{", "{"), letter("}", "}"), letter("^", "^"),
         letter("`", "`"), letter("°", "°")},
        russian
            ? std::vector<Key>{letter("ъ", "Ъ"), letter("э", "Э"), letter("ё", "Ё"), letter("№", "№"),
                               letter(",", ";"), letter(".", ":"), letter("-", "_"), letter("!", "!"),
                               letter("?", "?"), letter("…", "…")}
            : french
                ? std::vector<Key>{letter("é", "É"), letter("è", "È"), letter("à", "À"), letter("ç", "Ç"),
                                   letter("ù", "Ù"), letter("ê", "Ê"), letter("â", "Â"), letter("î", "Î"),
                                   letter("ô", "Ô"), letter("û", "Û")}
            : german
                ? std::vector<Key>{letter("ä", "Ä"), letter("ö", "Ö"), letter("ü", "Ü"), letter("ß", "ẞ"),
                                   letter("é", "É"), letter("è", "È"), letter(",", ";"), letter(".", ":"),
                                   letter("-", "_"), letter("?", "?")}
            : spanish
                ? std::vector<Key>{letter("á", "Á"), letter("é", "É"), letter("í", "Í"), letter("ó", "Ó"),
                                   letter("ú", "Ú"), letter("ü", "Ü"), letter("ñ", "Ñ"), letter("¿", "¿"),
                                   letter("¡", "¡"), letter("ç", "Ç")}
            : italian
                ? std::vector<Key>{letter("à", "À"), letter("è", "È"), letter("é", "É"), letter("ì", "Ì"),
                                   letter("í", "Í"), letter("ò", "Ò"), letter("ó", "Ó"), letter("ù", "Ù"),
                                   letter("ú", "Ú"), letter("ç", "Ç")}
            : portuguese
                ? std::vector<Key>{letter("á", "Á"), letter("é", "É"), letter("í", "Í"), letter("ó", "Ó"),
                                   letter("ú", "Ú"), letter("ã", "Ã"), letter("õ", "Õ"), letter("â", "Â"),
                                   letter("ê", "Ê"), letter("ô", "Ô")}
                : std::vector<Key>{letter(",", ";"), letter(".", ":"), letter("-", "_"), letter("'", "\""),
                                   letter("@", "@"), letter("&", "&"), letter("/", "\\"), letter("?", "?"),
                                   letter("!", "!"), letter("€", "€")},
    };

    const std::vector<Key> actionRow = {
        action(Action::Shift, 2), action(Action::Page, 2), action(Action::Space, 2),
        action(Action::Backspace, 2), action(Action::Accept, 2),
    };
    m_letters.push_back(actionRow);
    m_symbols.push_back(actionRow);
}

const std::vector<std::vector<TextEntryScreen::Key>>& TextEntryScreen::rows() const {
    return m_page == 0 ? m_letters : m_symbols;
}

void TextEntryScreen::show(const Request& request) {
    DebugLog::log("[textentry] TextEntryScreen::show title=%s already-active=%d",
                  request.title.c_str(), m_active);
    if (m_active)
        return;
    // The UI language can change after this screen was constructed.
    buildLayout();
    m_request = request;
    m_text = request.initial;
    m_active = true;
    m_animatingOut = false;
    m_accepted = false;
    m_shift = false;
    m_page = 0;
    m_row = 1;
    m_column = 0;
    m_caretTime = 0.f;
    m_touchRow = m_touchColumn = -1;
    m_waitingForTouchRelease = true;
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, 0.18f, nxui::Easing::outCubic);
    m_backdropReady = false;
    setRect({kPanelX, kPanelY, kPanelW, kPanelH});
    setVisible(true);
    setupActions();
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(request.title + ". " +
            i18n.tr("text_entry.opened",
                    "On-screen keyboard. Directional pad to choose a key, A to type, "
                    "X to erase, Plus to confirm, B to cancel."));
    }
}

void TextEntryScreen::hide(bool accepted) {
    DebugLog::log("[textentry] TextEntryScreen::hide accepted=%d active=%d animatingOut=%d",
                  accepted, m_active, m_animatingOut);
    if (!m_active || m_animatingOut)
        return;
    m_accepted = accepted;
    m_animatingOut = true;
    if (m_closeSfxCb) m_closeSfxCb();
    m_alpha.set(0.f, 0.15f, nxui::Easing::outCubic);
    clearActions();
}

void TextEntryScreen::setupActions() {
    clearActions();
    // Directions go through addDirectionAction, not addAction(Button::DLeft):
    // Application dispatches a held or pressed direction to the focused widget
    // through FocusManager::navigate, which only consults the direction map.
    // Registering them as plain button actions is why the first build of this
    // screen answered touch and ignored the d-pad completely.
    addDirectionAction(nxui::FocusDirection::UP, [this]() { moveSelection(0, -1); });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() { moveSelection(0, 1); });
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() { moveSelection(-1, 0); });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() { moveSelection(1, 0); });

    addAction(static_cast<std::uint64_t>(nxui::Button::A), [this]() { pressSelected(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() { hide(false); });
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() { backspace(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::Y), [this]() {
        m_shift = !m_shift;
        if (m_keySfxCb) m_keySfxCb();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::L), [this]() { togglePage(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::R), [this]() { togglePage(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::Plus), [this]() { hide(true); });
}

nxui::Rect TextEntryScreen::keyRect(int row, int column) const {
    const auto& all = rows();
    if (row < 0 || row >= static_cast<int>(all.size()))
        return {};
    const auto& keys = all[static_cast<std::size_t>(row)];
    if (column < 0 || column >= static_cast<int>(keys.size()))
        return {};

    const nxui::Rect panel = rect();
    const float usable = panel.width - kInset * 2.f;
    const float unit = (usable - kKeyGap * (kColumns - 1)) / kColumns;
    const float top = panel.y + kKeyboardTop + row * (kRowHeight + kKeyGap);

    int spanBefore = 0;
    for (int i = 0; i < column; ++i)
        spanBefore += std::max(1, keys[static_cast<std::size_t>(i)].span);
    const int span = std::max(1, keys[static_cast<std::size_t>(column)].span);

    const float x = panel.x + kInset + spanBefore * (unit + kKeyGap);
    const float width = unit * span + kKeyGap * (span - 1);
    return {x, top, width, kRowHeight};
}

void TextEntryScreen::togglePage() {
    m_page = m_page == 0 ? 1 : 0;
    const auto& all = rows();
    m_row = std::clamp(m_row, 0, static_cast<int>(all.size()) - 1);
    m_column = std::clamp(m_column, 0,
        static_cast<int>(all[static_cast<std::size_t>(m_row)].size()) - 1);
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::moveSelection(int dx, int dy) {
    if (!m_active || m_animatingOut)
        return;
    const auto& all = rows();
    if (all.empty())
        return;

    if (dy != 0) {
        const auto& from = all[static_cast<std::size_t>(m_row)];
        // Column indices differ per row because the action row uses wide keys.
        // Carry the position across by span so the selection lands under the key
        // the player was looking at rather than at a fixed index.
        int spanBefore = 0;
        for (int i = 0; i < m_column; ++i)
            spanBefore += std::max(1, from[static_cast<std::size_t>(i)].span);
        const int centre = spanBefore +
            std::max(1, from[static_cast<std::size_t>(m_column)].span) / 2;

        m_row = (m_row + dy + static_cast<int>(all.size())) % static_cast<int>(all.size());
        const auto& to = all[static_cast<std::size_t>(m_row)];
        int walked = 0;
        m_column = static_cast<int>(to.size()) - 1;
        for (std::size_t i = 0; i < to.size(); ++i) {
            const int span = std::max(1, to[i].span);
            if (centre < walked + span) {
                m_column = static_cast<int>(i);
                break;
            }
            walked += span;
        }
    }

    if (dx != 0) {
        const int count = static_cast<int>(all[static_cast<std::size_t>(m_row)].size());
        m_column = (m_column + dx + count) % count;
    }

    if (m_navigateSfxCb) m_navigateSfxCb();
    announceSelection();
}

void TextEntryScreen::pressSelected() {
    if (!m_active || m_animatingOut)
        return;
    const auto& all = rows();
    if (m_row < 0 || m_row >= static_cast<int>(all.size()))
        return;
    const auto& keys = all[static_cast<std::size_t>(m_row)];
    if (m_column < 0 || m_column >= static_cast<int>(keys.size()))
        return;
    pressKey(keys[static_cast<std::size_t>(m_column)]);
}

void TextEntryScreen::pressKey(const Key& key) {
    switch (key.action) {
        case Action::Shift:
            m_shift = !m_shift;
            if (m_keySfxCb) m_keySfxCb();
            return;
        case Action::Page:
            togglePage();
            return;
        case Action::Space:
            appendText(" ");
            return;
        case Action::Backspace:
            backspace();
            return;
        case Action::Accept:
            hide(true);
            return;
        case Action::None:
            break;
    }
    appendText(m_shift ? key.upper : key.lower);
}

void TextEntryScreen::appendText(const std::string& utf8) {
    if (utf8.empty())
        return;
    if (textLength() >= std::max(1, m_request.maxLength)) {
        if (m_accessibilityCb)
            m_accessibilityCb(nxui::I18n::instance().tr("text_entry.full",
                                                        "Maximum length reached"));
        return;
    }
    m_text += utf8;
    m_caretTime = 0.f;
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::backspace() {
    if (!m_active || m_animatingOut || m_text.empty())
        return;
    // Remove a whole character, not a byte: an accented vowel is two bytes and
    // half of one is not text.
    std::size_t cut = m_text.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(m_text[cut]) & 0xC0u) == 0x80u)
        --cut;
    m_text.erase(cut);
    m_caretTime = 0.f;
    if (m_keySfxCb) m_keySfxCb();
}

int TextEntryScreen::textLength() const {
    return countCodepoints(m_text);
}

std::string TextEntryScreen::displayText() const {
    if (!m_request.password)
        return m_text;
    std::string masked;
    const int count = textLength();
    masked.reserve(static_cast<std::size_t>(count) * 3);
    for (int i = 0; i < count; ++i)
        masked += "•";
    return masked;
}

std::string TextEntryScreen::keyLabel(const Key& key) const {
    auto& i18n = nxui::I18n::instance();
    switch (key.action) {
        case Action::Shift:     return i18n.tr("text_entry.shift", "Shift");
        case Action::Page:      return m_page == 0 ? i18n.tr("text_entry.symbols", "?12#")
                                                   : i18n.tr("text_entry.letters", "ABC");
        case Action::Space:     return i18n.tr("text_entry.space", "Space");
        case Action::Backspace: return i18n.tr("text_entry.erase", "Erase");
        case Action::Accept:    return i18n.tr("button.ok", "OK");
        case Action::None:      break;
    }
    return m_shift ? key.upper : key.lower;
}

void TextEntryScreen::announceSelection() {
    if (!m_accessibilityCb)
        return;
    const auto& all = rows();
    if (m_row < 0 || m_row >= static_cast<int>(all.size()))
        return;
    const auto& keys = all[static_cast<std::size_t>(m_row)];
    if (m_column < 0 || m_column >= static_cast<int>(keys.size()))
        return;
    m_accessibilityCb(keyLabel(keys[static_cast<std::size_t>(m_column)]));
}

void TextEntryScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut)
        return;
    if (m_waitingForTouchRelease) {
        if (input.touchUp() || !input.isTouching())
            m_waitingForTouchRelease = false;
        return;
    }
    const auto& all = rows();

    if (input.touchDown()) {
        m_touchRow = m_touchColumn = -1;
        for (int row = 0; row < static_cast<int>(all.size()); ++row) {
            const auto& keys = all[static_cast<std::size_t>(row)];
            for (int column = 0; column < static_cast<int>(keys.size()); ++column) {
                if (keyRect(row, column).contains(input.touchX(), input.touchY())) {
                    m_touchRow = row;
                    m_touchColumn = column;
                    m_row = row;
                    m_column = column;
                    return;
                }
            }
        }
        return;
    }

    if (input.touchUp() && m_touchRow >= 0) {
        const int row = m_touchRow;
        const int column = m_touchColumn;
        m_touchRow = m_touchColumn = -1;
        if (row < static_cast<int>(all.size())) {
            const auto& keys = all[static_cast<std::size_t>(row)];
            if (column < static_cast<int>(keys.size()) &&
                keyRect(row, column).contains(input.touchX(), input.touchY()))
                pressKey(keys[static_cast<std::size_t>(column)]);
        }
    }
}

void TextEntryScreen::onUpdate(float dt) {
    if (!isActive())
        return;
    m_alpha.update(dt);
    m_caretTime += dt;
    setOpacity(m_alpha.value());
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_active = false;
        m_animatingOut = false;
        setVisible(false);
        // Copied before the callbacks run: accepting usually rebuilds the grid,
        // which can destroy this screen's owner mid-call.
        const bool accepted = m_accepted;
        const std::string value = m_text;
        auto accept = m_acceptCb;
        auto cancel = m_cancelCb;
        // Never log the value: this screen also edits API keys in password
        // mode, and diagnostic logs must not become a credential store.
        DebugLog::log("[textentry] close accepted=%d length=%d hasAcceptCb=%d hasCancelCb=%d",
                      accepted, countCodepoints(value), (bool)accept, (bool)cancel);
        if (accepted) {
            if (accept) accept(value);
        } else if (cancel) {
            cancel();
        }
        DebugLog::log("[textentry] onUpdate close transition callback returned");
    }
}

void TextEntryScreen::onRender(nxui::Renderer& ren) {
    if (!isActive() || !m_theme || !m_font || !m_smallFont)
        return;
    // Dim behind the panel first, then let GlassWidget draw the panel itself so
    // this reads as the same glass the settings and power overlays use.
    // Blur what is actually behind the panel, once per opening. The scene
    // capture the glass path samples is half resolution, shared with every
    // other glass widget in the frame, and never blurred: with it the keyboard
    // showed a sharp copy of the screen through itself. This is the same
    // capture-blur-cache the settings overlay uses, and it runs after
    // everything below has been drawn, so it also picks up the overlay the
    // keyboard was opened from rather than the wallpaper behind it.
    const float alpha = m_alpha.value();
    if (!m_backdropReady) {
        ren.captureToOffscreen(false);
        ren.applyBlur(4.f, 3);
        ren.copyOffscreen(0, 2);
        m_backdropReady = true;
    }
    ren.drawRect({0.f, 0.f, 1280.f, 720.f},
                 nxui::Color::black().withAlpha(0.62f * alpha));
    ren.drawOffscreenRounded(2, rect(), 26.f,
                             nxui::Color::white().withAlpha(alpha));
    nxui::GlassWidget::onRender(ren);
}

void TextEntryScreen::onContentRender(nxui::Renderer& ren) {
    const float alpha = m_alpha.value();
    const nxui::Rect panel = rect();

    ren.drawText(m_request.title, {panel.x + 30.f, panel.y + 22.f}, m_font,
                 m_theme->textPrimary.withAlpha(alpha), 0.96f);
    if (!m_request.guide.empty())
        ren.drawText(m_request.guide, {panel.x + 30.f, panel.y + 58.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(0.86f * alpha), 0.70f);

    const nxui::Rect field{panel.x + kInset, panel.y + kFieldTop,
                           panel.width - kInset * 2.f, kFieldHeight};
    ren.drawLiquidGlass(2, field, 14.f,
                        m_theme->panelBase.withAlpha(0.16f), alpha, 0.06f);
    ren.drawRoundedRectOutline(field, m_theme->panelBorder.withAlpha(0.42f * alpha), 14.f, 1.2f);

    char counter[32]{};
    std::snprintf(counter, sizeof(counter), "%d/%d", textLength(),
                  std::max(1, m_request.maxLength));
    const nxui::Vec2 counterSize = m_smallFont->measure(counter);
    const float counterWidth = counterSize.x * 0.64f;
    ren.drawText(counter, {field.right() - 16.f - counterWidth, field.y + 20.f},
                 m_smallFont, m_theme->textSecondary.withAlpha(0.70f * alpha), 0.64f);

    const std::string shown = displayText();
    constexpr float kFieldScale = 0.84f;
    const nxui::Vec2 measured = m_font->measure(shown);
    const float textX = field.x + 16.f;
    const float textY = field.y + (field.height - measured.y * kFieldScale) * 0.5f;
    if (!shown.empty()) {
        // The counter owns the right edge; long input is clipped rather than
        // drawn through it.
        ren.pushClipRect({field.x, field.y,
                          field.width - counterWidth - 28.f, field.height});
        ren.drawText(shown, {textX, textY}, m_font,
                     m_theme->textPrimary.withAlpha(alpha), kFieldScale);
        ren.popClipRect();
    }
    if (std::fmod(m_caretTime, 1.0f) < 0.55f) {
        const float caretX = std::min(textX + measured.x * kFieldScale + 2.f,
                                      field.right() - counterWidth - 30.f);
        ren.drawRect({caretX, field.y + 12.f, 2.f, field.height - 24.f},
                     m_theme->cursorNormal.withAlpha(0.92f * alpha));
    }

    const auto& all = rows();
    for (int row = 0; row < static_cast<int>(all.size()); ++row) {
        const auto& keys = all[static_cast<std::size_t>(row)];
        for (int column = 0; column < static_cast<int>(keys.size()); ++column) {
            const Key& key = keys[static_cast<std::size_t>(column)];
            const nxui::Rect r = keyRect(row, column);
            const bool selected = row == m_row && column == m_column;
            const bool shiftLit = m_shift && key.action == Action::Shift;
            const bool accept = key.action == Action::Accept;

            // Every key is its own refracting panel. Selection is a color wash
            // over the glass, never a replacement flat tile.
            ren.drawLiquidGlass(2, r, 12.f,
                m_theme->panelBase.withAlpha(0.14f), alpha, 0.05f);
            nxui::Color wash = nxui::Color::transparent();
            if (accept) wash = m_theme->cursorNormal.withAlpha(0.18f * alpha);
            if (shiftLit) wash = m_theme->cursorNormal.withAlpha(0.25f * alpha);
            if (selected) wash = m_theme->cursorNormal.withAlpha(0.46f * alpha);
            if (wash.a > 0.001f) ren.drawRoundedRect(r, wash, 12.f);
            ren.drawRoundedRectOutline(r,
                (selected ? m_theme->textPrimary : m_theme->panelBorder)
                    .withAlpha((selected ? 0.68f : 0.30f) * alpha), 12.f, 1.f);

            const std::string label = keyLabel(key);
            const bool wide = key.action != Action::None;
            const float labelScale = wide ? 0.60f : 0.76f;
            nxui::Font* labelFont = wide ? m_smallFont : m_font;
            const nxui::Vec2 size = labelFont->measure(label);
            ren.drawText(label,
                {r.x + (r.width - size.x * labelScale) * 0.5f,
                 r.y + (r.height - size.y * labelScale) * 0.5f},
                labelFont,
                (selected ? m_theme->textPrimary : m_theme->textSecondary).withAlpha(alpha),
                labelScale);
        }
    }

    // Below the last key row, inside the panel. The first build drew this over
    // the action row because the rows overflowed the panel entirely.
    const float keysBottom = panel.y + kKeyboardTop +
        static_cast<float>(all.size()) * (kRowHeight + kKeyGap);
    const std::string hint = nxui::I18n::instance().tr("text_entry.hint",
        "A: type   X: erase   Y: shift   L/R: symbols   Plus: confirm   B: cancel");
    const nxui::Vec2 hintSize = m_smallFont->measure(hint);
    ren.drawText(hint,
                 {panel.x + (panel.width - hintSize.x * 0.64f) * 0.5f, keysBottom + 6.f},
                 m_smallFont, m_theme->textSecondary.withAlpha(0.78f * alpha), 0.64f);
}
