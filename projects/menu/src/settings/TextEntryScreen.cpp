#include "TextEntryScreen.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

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

std::size_t byteOffsetForCodepoint(const std::string& text, int codepointIndex) {
    if (codepointIndex <= 0)
        return 0;
    int count = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (count == codepointIndex)
            return i;
        i += static_cast<std::size_t>(sequenceLength(static_cast<unsigned char>(text[i])));
        ++count;
    }
    return text.size();
}

std::string utf8Prefix(const std::string& text, int codepoints) {
    return text.substr(0, byteOffsetForCodepoint(text, std::max(0, codepoints)));
}

} // namespace

TextEntryScreen::TextEntryScreen() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    setFocusable(true);
    // Frosted backdrop is applied in onRender (same path as settings overlays).
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setBlurEnabled(false);
    setPanelOpacity(0.94f);
    setCornerRadius(18.f);
    setBorderWidth(1.f);
    applyPanelRect();
    buildLayout();
}

void TextEntryScreen::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    if (!m_theme)
        return;
    setBaseColor(m_theme->panelBase.withAlpha(
        std::clamp(m_theme->panelBase.a * 0.92f, 0.30f, 0.52f)));
    setBorderColor(m_theme->panelBorder.withAlpha(
        std::clamp(m_theme->panelBorder.a * 0.92f, 0.14f, 0.42f)));
    setHighlightColor(m_theme->panelHighlight.withAlpha(
        std::clamp(m_theme->panelHighlight.a * 0.92f, 0.05f, 0.18f)));
    setLiquidGlassShade(m_theme->mode == nxui::ThemeMode::Dark ? 0.08f : -0.03f);
}

void TextEntryScreen::setCanvasSize(float width, float height) {
    m_canvasW = width > 1.f ? width : 1280.f;
    m_canvasH = height > 1.f ? height : 720.f;
    if (m_active || m_animatingOut)
        applyPanelRect();
}

TextEntryScreen::Metrics TextEntryScreen::metrics() const {
    Metrics m;
    if (isPortraitCanvas()) {
        // Squish into 720-wide; compact hugs content + centers, full fills the canvas.
        m.panelX = 12.f;
        m.panelW = m_canvasW - 24.f;
        m.inset = 12.f;
        m.keyGap = 4.f;
        m.keyRadius = 10.f;
        m.boardPad = 0.f;
        const int rowCount = std::max(1, static_cast<int>(rows().size()));
        constexpr float kHintH = 32.f;
        constexpr float kBottomPad = 14.f;
        if (m_fullLayout) {
            constexpr float kEdge = 6.f;
            m.panelX = kEdge;
            m.panelY = kEdge;
            m.panelW = m_canvasW - kEdge * 2.f;
            m.panelH = m_canvasH - kEdge * 2.f;
            // Title + chips stay pinned; field / keys / hint center in the rest.
            constexpr float kTitleBand = 88.f;
            constexpr float kFieldToBoard = 16.f;
            m.fieldHeight = 56.f;
            m.rowHeight = 78.f;
            const float gaps = static_cast<float>(std::max(0, rowCount - 1)) * m.keyGap;
            float boardH = static_cast<float>(rowCount) * m.rowHeight + gaps;
            float contentH = m.fieldHeight + kFieldToBoard + boardH + kHintH;
            const float avail = m.panelH - kTitleBand - kBottomPad;
            if (contentH + 48.f < avail) {
                const float grow = std::min(
                    18.f, (avail - contentH - 48.f) / static_cast<float>(rowCount));
                m.rowHeight = std::min(96.f, m.rowHeight + grow);
                boardH = static_cast<float>(rowCount) * m.rowHeight + gaps;
                contentH = m.fieldHeight + kFieldToBoard + boardH + kHintH;
            }
            const float contentTop =
                kTitleBand + std::max(0.f, (avail - contentH) * 0.5f);
            m.fieldTop = contentTop;
            m.keyboardTop = contentTop + m.fieldHeight + kFieldToBoard;
        } else {
            m.rowHeight = 56.f;
            m.fieldTop = 86.f;
            m.fieldHeight = 48.f;
            m.keyboardTop = 148.f;
            const float boardH = static_cast<float>(rowCount) * m.rowHeight
                + static_cast<float>(std::max(0, rowCount - 1)) * m.keyGap;
            m.panelH = m.keyboardTop + boardH + kHintH + kBottomPad;
            m.panelY = std::max(12.f, (m_canvasH - m.panelH) * 0.5f);
        }
        return m;
    }
    if (m_fullLayout) {
        // Large glass sheet — nearly full screen, still a rounded panel so the
        // frosted look matches the original keyboard language.
        m.panelX = 28.f;
        m.panelY = 18.f;
        m.panelW = 1224.f;
        m.panelH = 684.f;
        m.inset = 24.f;
        m.keyGap = 5.f;
        m.rowHeight = 86.f;
        m.fieldTop = 88.f;
        m.fieldHeight = 64.f;
        m.keyboardTop = 168.f;
        m.keyRadius = 12.f;
        m.boardPad = 0.f;
    } else {
        // Compact glass card — denser/taller than the first keyboard, same look.
        m.panelX = 64.f;
        m.panelY = 70.f;
        m.panelW = 1152.f;
        m.panelH = 580.f;
        m.inset = 26.f;
        m.keyGap = 6.f;
        m.rowHeight = 70.f;
        m.fieldTop = 90.f;
        m.fieldHeight = 58.f;
        m.keyboardTop = 162.f;
        m.keyRadius = 12.f;
        m.boardPad = 0.f;
    }
    return m;
}

void TextEntryScreen::applyPanelRect() {
    const Metrics m = metrics();
    setRect({m.panelX, m.panelY, m.panelW, m.panelH});
    setCornerRadius(26.f);
}

void TextEntryScreen::setFullLayout(bool full) {
    if (m_fullLayout == full)
        return;
    m_fullLayout = full;
    applyPanelRect();
}

void TextEntryScreen::setGlassStyle(bool glass) {
    if (m_glassStyle == glass)
        return;
    m_glassStyle = glass;
    m_backdropReady = false;
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

    // Space and backspace get the wide slots; OK stays reachable on the right.
    // Cancel lives as a chrome chip so the board can stay dense.
    const std::vector<Key> actionRow = {
        action(Action::Shift, 1), action(Action::Page, 1), action(Action::Space, 4),
        action(Action::Backspace, 2), action(Action::Accept, 2),
    };
    m_letters.push_back(actionRow);
    m_symbols.push_back(actionRow);
}

const std::vector<std::vector<TextEntryScreen::Key>>& TextEntryScreen::rows() const {
    return m_page == 0 ? m_letters : m_symbols;
}

void TextEntryScreen::show(const Request& request) {
    DebugLog::log("[textentry] TextEntryScreen::show title=%s already-active=%d full=%d",
                  request.title.c_str(), m_active, m_fullLayout);
    if (m_active)
        return;
    buildLayout();
    m_request = request;
    m_text = request.initial;
    m_active = true;
    m_animatingOut = false;
    m_accepted = false;
    m_shift = false;
    m_shiftLock = false;
    m_passwordRevealed = false;
    m_focusField = false;
    m_caret = textLength();
    m_selAnchor = -1;
    m_page = 0;
    m_row = 1;
    m_column = 0;
    m_caretTime = 0.f;
    m_touchRow = m_touchColumn = -1;
    m_touchOnStyleToggle = false;
    m_touchOnSizeToggle = false;
    m_touchOnCancelChip = false;
    m_touchOnClear = false;
    m_touchOnReveal = false;
    m_touchOnField = false;
    m_fieldSelecting = false;
    m_waitingForTouchRelease = true;
    m_backspaceHeld = false;
    m_backspaceHoldTime = 0.f;
    m_backspaceRepeatLeft = 0.f;
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, 0.18f, nxui::Easing::outCubic);
    m_backdropReady = false;
    applyPanelRect();
    setVisible(true);
    setupActions();
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(request.title + ". " +
            i18n.tr("text_entry.opened",
                    "On-screen keyboard. Up into the text field to move the caret. "
                    "A to type, X to erase, Plus to confirm, B to cancel."));
    }
}

void TextEntryScreen::hide(bool accepted) {
    DebugLog::log("[textentry] TextEntryScreen::hide accepted=%d active=%d animatingOut=%d",
                  accepted, m_active, m_animatingOut);
    if (!m_active || m_animatingOut)
        return;
    m_accepted = accepted;
    m_animatingOut = true;
    m_backspaceHeld = false;
    if (m_closeSfxCb) m_closeSfxCb();
    m_alpha.set(0.f, 0.15f, nxui::Easing::outCubic);
    clearActions();
}

void TextEntryScreen::setupActions() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::UP, [this]() { moveSelection(0, -1); });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() { moveSelection(0, 1); });
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() { moveSelection(-1, 0); });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() { moveSelection(1, 0); });

    addAction(static_cast<std::uint64_t>(nxui::Button::A), [this]() {
        if (m_focusField) {
            leaveTextField();
            return;
        }
        pressSelected();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() { hide(false); });
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() {
        backspace();
        beginBackspaceHold();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::Y), [this]() {
        if (m_focusField) {
            // Y while editing text: select all (handy before replace/clear).
            selectAll();
            if (m_keySfxCb) m_keySfxCb();
            return;
        }
        Key shiftKey;
        shiftKey.action = Action::Shift;
        shiftKey.span = 1;
        pressKey(shiftKey);
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::L), [this]() {
        if (m_focusField) {
            moveCaret(-1, true);
            return;
        }
        togglePage();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::R), [this]() {
        if (m_focusField) {
            moveCaret(+1, true);
            return;
        }
        togglePage();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::Plus), [this]() { hide(true); });
    addAction(static_cast<std::uint64_t>(nxui::Button::Minus), [this]() {
        // Password fields: Minus toggles show/hide (size still via the Size chip).
        if (m_request.password) {
            togglePasswordReveal();
            return;
        }
        toggleSizeMode();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::ZR), [this]() { toggleGlassStyle(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::ZL), [this]() {
        if (!m_text.empty())
            clearText();
    });
}

nxui::Rect TextEntryScreen::keyRect(int row, int column) const {
    const auto& all = rows();
    if (row < 0 || row >= static_cast<int>(all.size()))
        return {};
    const auto& keys = all[static_cast<std::size_t>(row)];
    if (column < 0 || column >= static_cast<int>(keys.size()))
        return {};

    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    const float usable = panel.width - m.inset * 2.f;
    const float unit = (usable - m.keyGap * (kColumns - 1)) / kColumns;
    const float top = panel.y + m.keyboardTop + row * (m.rowHeight + m.keyGap);

    int spanBefore = 0;
    for (int i = 0; i < column; ++i)
        spanBefore += std::max(1, keys[static_cast<std::size_t>(i)].span);
    const int span = std::max(1, keys[static_cast<std::size_t>(column)].span);

    const float x = panel.x + m.inset + spanBefore * (unit + m.keyGap);
    const float width = unit * span + m.keyGap * (span - 1);
    return {x, top, width, m.rowHeight};
}

nxui::Rect TextEntryScreen::keyboardBoardRect() const {
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    const auto& all = rows();
    const float rowsH = static_cast<float>(all.size()) * m.rowHeight
        + static_cast<float>(std::max(0, static_cast<int>(all.size()) - 1)) * m.keyGap;
    return {panel.x + m.inset, panel.y + m.keyboardTop,
            panel.width - m.inset * 2.f, rowsH};
}

nxui::Rect TextEntryScreen::styleToggleRect() const {
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    // Leftmost of the three chrome chips: Style | Size | Cancel
    // Portrait chips need more width so "Flat"/"Compact"/"Cancel" stay readable.
    const float chipW = isPortraitCanvas() ? 118.f : 106.f;
    const float gap = isPortraitCanvas() ? 8.f : 14.f;
    const float span = chipW * 3.f + gap * 2.f;
    return {panel.right() - m.inset - span, panel.y + 18.f, chipW, 40.f};
}

nxui::Rect TextEntryScreen::sizeToggleRect() const {
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    const float chipW = isPortraitCanvas() ? 118.f : 106.f;
    const float gap = isPortraitCanvas() ? 8.f : 14.f;
    const float span = chipW * 2.f + gap;
    return {panel.right() - m.inset - span, panel.y + 18.f, chipW, 40.f};
}

nxui::Rect TextEntryScreen::cancelChipRect() const {
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    const float chipW = isPortraitCanvas() ? 118.f : 106.f;
    return {panel.right() - m.inset - chipW, panel.y + 18.f, chipW, 40.f};
}

void TextEntryScreen::togglePage() {
    m_page = m_page == 0 ? 1 : 0;
    const auto& all = rows();
    m_row = std::clamp(m_row, 0, static_cast<int>(all.size()) - 1);
    m_column = std::clamp(m_column, 0,
        static_cast<int>(all[static_cast<std::size_t>(m_row)].size()) - 1);
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::toggleSizeMode() {
    m_fullLayout = !m_fullLayout;
    applyPanelRect();
    m_backdropReady = false;
    if (m_keySfxCb) m_keySfxCb();
    if (m_layoutCb) m_layoutCb(m_fullLayout);
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(m_fullLayout
            ? i18n.tr("text_entry.layout_full", "Full keyboard")
            : i18n.tr("text_entry.layout_compact", "Compact keyboard"));
    }
}

void TextEntryScreen::toggleGlassStyle() {
    m_glassStyle = !m_glassStyle;
    m_backdropReady = false;
    if (m_keySfxCb) m_keySfxCb();
    if (m_styleCb) m_styleCb(m_glassStyle);
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(m_glassStyle
            ? i18n.tr("text_entry.style_glass", "Glass keyboard")
            : i18n.tr("text_entry.style_flat", "Flat keyboard"));
    }
}

void TextEntryScreen::moveSelection(int dx, int dy) {
    if (!m_active || m_animatingOut)
        return;

    if (m_focusField) {
        if (dy > 0) {
            leaveTextField();
            return;
        }
        if (dx != 0) {
            // Hold Shift (or Y-shifted state) extends the selection.
            moveCaret(dx, m_shift || m_shiftLock);
            return;
        }
        return;
    }

    const auto& all = rows();
    if (all.empty())
        return;

    if (dy < 0 && m_row == 0) {
        enterTextField();
        return;
    }

    if (dy != 0) {
        const auto& from = all[static_cast<std::size_t>(m_row)];
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
            // First tap = one-shot shift; second tap while shifted = caps lock;
            // third tap clears.
            if (m_shiftLock) {
                m_shift = false;
                m_shiftLock = false;
            } else if (m_shift) {
                m_shiftLock = true;
            } else {
                m_shift = true;
            }
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
            beginBackspaceHold();
            return;
        case Action::Accept:
            hide(true);
            return;
        case Action::Cancel:
            hide(false);
            return;
        case Action::None:
            break;
    }
    appendText(m_shift ? key.upper : key.lower);
    if (m_shift && !m_shiftLock)
        m_shift = false;
}

void TextEntryScreen::appendText(const std::string& utf8) {
    if (utf8.empty())
        return;
    if (hasSelection())
        deleteSelection();
    if (textLength() >= std::max(1, m_request.maxLength)) {
        if (m_accessibilityCb)
            m_accessibilityCb(nxui::I18n::instance().tr("text_entry.full",
                                                        "Maximum length reached"));
        return;
    }
    const std::size_t at = byteOffsetForCodepoint(m_text, m_caret);
    m_text.insert(at, utf8);
    ++m_caret;
    m_caret = std::clamp(m_caret, 0, textLength());
    clearSelection();
    m_caretTime = 0.f;
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::backspace() {
    if (!m_active || m_animatingOut)
        return;
    if (hasSelection()) {
        deleteSelection();
        if (m_keySfxCb) m_keySfxCb();
        return;
    }
    if (m_text.empty() || m_caret <= 0)
        return;
    const std::size_t end = byteOffsetForCodepoint(m_text, m_caret);
    const std::size_t begin = byteOffsetForCodepoint(m_text, m_caret - 1);
    m_text.erase(begin, end - begin);
    --m_caret;
    m_caretTime = 0.f;
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::clearText() {
    if (m_text.empty())
        return;
    m_text.clear();
    m_caret = 0;
    clearSelection();
    m_caretTime = 0.f;
    if (m_keySfxCb) m_keySfxCb();
}

void TextEntryScreen::deleteSelection() {
    if (!hasSelection())
        return;
    const int start = selectionStart();
    const int end = selectionEnd();
    const std::size_t b0 = byteOffsetForCodepoint(m_text, start);
    const std::size_t b1 = byteOffsetForCodepoint(m_text, end);
    m_text.erase(b0, b1 - b0);
    m_caret = start;
    clearSelection();
    m_caretTime = 0.f;
}

void TextEntryScreen::enterTextField() {
    m_focusField = true;
    m_caretTime = 0.f;
    if (m_navigateSfxCb) m_navigateSfxCb();
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr(
            "text_entry.field_focus",
            "Text field. Left and Right move the caret. L and R select. Y select all. A returns to keys."));
}

void TextEntryScreen::leaveTextField() {
    if (!m_focusField)
        return;
    m_focusField = false;
    m_row = 0;
    m_column = 0;
    if (m_navigateSfxCb) m_navigateSfxCb();
    announceSelection();
}

void TextEntryScreen::clearSelection() {
    m_selAnchor = -1;
}

bool TextEntryScreen::hasSelection() const {
    return m_selAnchor >= 0 && m_selAnchor != m_caret;
}

int TextEntryScreen::selectionStart() const {
    if (!hasSelection())
        return m_caret;
    return std::min(m_selAnchor, m_caret);
}

int TextEntryScreen::selectionEnd() const {
    if (!hasSelection())
        return m_caret;
    return std::max(m_selAnchor, m_caret);
}

void TextEntryScreen::selectAll() {
    if (m_text.empty()) {
        clearSelection();
        m_caret = 0;
        return;
    }
    m_selAnchor = 0;
    m_caret = textLength();
    m_caretTime = 0.f;
}

void TextEntryScreen::setCaret(int codepointIndex, bool extendSelection) {
    const int len = textLength();
    const int next = std::clamp(codepointIndex, 0, len);
    if (extendSelection) {
        if (m_selAnchor < 0)
            m_selAnchor = m_caret;
    } else {
        clearSelection();
    }
    m_caret = next;
    m_caretTime = 0.f;
}

void TextEntryScreen::moveCaret(int delta, bool extendSelection) {
    setCaret(m_caret + delta, extendSelection);
    if (m_navigateSfxCb) m_navigateSfxCb();
}

void TextEntryScreen::togglePasswordReveal() {
    if (!m_request.password)
        return;
    m_passwordRevealed = !m_passwordRevealed;
    if (m_keySfxCb) m_keySfxCb();
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(m_passwordRevealed
            ? i18n.tr("text_entry.password_shown", "Password visible")
            : i18n.tr("text_entry.password_hidden", "Password hidden"));
    }
}

nxui::Rect TextEntryScreen::textFieldRect() const {
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    return {panel.x + m.inset, panel.y + m.fieldTop,
            panel.width - m.inset * 2.f, m.fieldHeight};
}

float TextEntryScreen::counterScale() const {
    // Match SteamGridDB body/hint readability (~0.95 on smallFont).
    return 0.92f;
}

float TextEntryScreen::hintScale() const {
    return 0.90f;
}

float TextEntryScreen::guideScale() const {
    return 0.92f;
}

float TextEntryScreen::fieldTrailingReserve() const {
    char counter[32]{};
    std::snprintf(counter, sizeof(counter), "%d/%d", textLength(),
                  std::max(1, m_request.maxLength));
    float reserve = 16.f;
    if (m_smallFont)
        reserve += m_smallFont->measure(counter).x * counterScale() + 10.f;
    if (!m_text.empty())
        reserve += 34.f; // clear button
    if (m_request.password)
        reserve += 58.f; // show/hide pill
    return reserve;
}

nxui::Rect TextEntryScreen::clearButtonRect() const {
    if (m_text.empty())
        return {};
    const nxui::Rect field = textFieldRect();
    char counter[32]{};
    std::snprintf(counter, sizeof(counter), "%d/%d", textLength(),
                  std::max(1, m_request.maxLength));
    const float counterW = m_smallFont
        ? m_smallFont->measure(counter).x * counterScale() : 36.f;
    const float size = 28.f;
    return {field.right() - 16.f - counterW - 8.f - size,
            field.y + (field.height - size) * 0.5f, size, size};
}

nxui::Rect TextEntryScreen::revealButtonRect() const {
    if (!m_request.password)
        return {};
    const nxui::Rect clear = clearButtonRect();
    const nxui::Rect field = textFieldRect();
    const float height = 28.f;
    const float width = 52.f;
    char counter[32]{};
    std::snprintf(counter, sizeof(counter), "%d/%d", textLength(),
                  std::max(1, m_request.maxLength));
    const float counterW = m_smallFont
        ? m_smallFont->measure(counter).x * counterScale() : 36.f;
    const float x = clear.width > 0.f
        ? clear.x - 6.f - width
        : field.right() - 16.f - counterW - 8.f - width;
    return {x, field.y + (field.height - height) * 0.5f, width, height};
}

int TextEntryScreen::caretFromFieldX(float x) const {
    if (!m_font)
        return textLength();
    const nxui::Rect field = textFieldRect();
    const float fieldScale = m_fullLayout ? 0.92f : 0.84f;
    const float textX = field.x + 16.f;
    const std::string shown = displayText();
    const int len = textLength();
    float bestDist = 1e9f;
    int best = 0;
    for (int i = 0; i <= len; ++i) {
        const std::string prefix = displayPrefix(i);
        const float caretX = textX + m_font->measure(prefix).x * fieldScale;
        const float dist = std::abs(caretX - x);
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void TextEntryScreen::beginBackspaceHold() {
    m_backspaceHeld = true;
    m_backspaceHoldTime = 0.f;
    m_backspaceRepeatLeft = 0.35f;
}

void TextEntryScreen::updateBackspaceHold(float dt, bool held) {
    if (!m_backspaceHeld)
        return;
    if (!held) {
        m_backspaceHeld = false;
        return;
    }
    m_backspaceHoldTime += dt;
    m_backspaceRepeatLeft -= dt;
    if (m_backspaceHoldTime < 0.35f || m_backspaceRepeatLeft > 0.f)
        return;
    backspace();
    // Accelerate slightly while held.
    m_backspaceRepeatLeft = std::max(0.04f, 0.10f - m_backspaceHoldTime * 0.01f);
}

int TextEntryScreen::textLength() const {
    return countCodepoints(m_text);
}

std::string TextEntryScreen::displayText() const {
    return displayPrefix(textLength());
}

std::string TextEntryScreen::displayPrefix(int codepoints) const {
    const int count = std::clamp(codepoints, 0, textLength());
    if (!m_request.password || m_passwordRevealed)
        return utf8Prefix(m_text, count);
    std::string masked;
    masked.reserve(static_cast<std::size_t>(count) * 3);
    for (int i = 0; i < count; ++i)
        masked += "•";
    return masked;
}

std::string TextEntryScreen::keyLabel(const Key& key) const {
    auto& i18n = nxui::I18n::instance();
    switch (key.action) {
        case Action::Shift:
            return m_shiftLock ? i18n.tr("text_entry.caps", "CAPS")
                               : i18n.tr("text_entry.shift", "Shift");
        case Action::Page:      return m_page == 0 ? i18n.tr("text_entry.symbols", "?12#")
                                                   : i18n.tr("text_entry.letters", "ABC");
        case Action::Space:     return i18n.tr("text_entry.space", "Space");
        case Action::Backspace: return i18n.tr("text_entry.backspace", "←");
        case Action::Accept:    return i18n.tr("button.ok", "OK");
        case Action::Cancel:    return i18n.tr("button.cancel", "Cancel");
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

    // Hold-to-repeat backspace from X or a held backspace key.
    bool touchBackspaceHeld = false;
    if (input.isTouching() && m_touchRow >= 0 && m_touchRow < static_cast<int>(all.size())) {
        const auto& keys = all[static_cast<std::size_t>(m_touchRow)];
        if (m_touchColumn >= 0 && m_touchColumn < static_cast<int>(keys.size())
            && keys[static_cast<std::size_t>(m_touchColumn)].action == Action::Backspace
            && keyRect(m_touchRow, m_touchColumn).contains(input.touchX(), input.touchY()))
            touchBackspaceHeld = true;
    }
    if (!(input.isHeld(nxui::Button::X) || touchBackspaceHeld))
        m_backspaceHeld = false;

    if (m_fieldSelecting && input.isTouching()) {
        setCaret(caretFromFieldX(input.touchX()), true);
    }

    if (input.touchDown()) {
        m_touchRow = m_touchColumn = -1;
        m_touchOnStyleToggle = false;
        m_touchOnSizeToggle = false;
        m_touchOnCancelChip = false;
        m_touchOnClear = false;
        m_touchOnReveal = false;
        m_touchOnField = false;
        m_fieldSelecting = false;
        const nxui::Rect clear = clearButtonRect();
        const nxui::Rect reveal = revealButtonRect();
        if (clear.width > 0.f && clear.contains(input.touchX(), input.touchY())) {
            m_touchOnClear = true;
            return;
        }
        if (reveal.width > 0.f && reveal.contains(input.touchX(), input.touchY())) {
            m_touchOnReveal = true;
            return;
        }
        if (styleToggleRect().contains(input.touchX(), input.touchY())) {
            m_touchOnStyleToggle = true;
            return;
        }
        if (sizeToggleRect().contains(input.touchX(), input.touchY())) {
            m_touchOnSizeToggle = true;
            return;
        }
        if (cancelChipRect().contains(input.touchX(), input.touchY())) {
            m_touchOnCancelChip = true;
            return;
        }
        if (textFieldRect().contains(input.touchX(), input.touchY())) {
            m_touchOnField = true;
            m_fieldSelecting = true;
            m_focusField = true;
            setCaret(caretFromFieldX(input.touchX()), false);
            return;
        }
        m_focusField = false;
        for (int row = 0; row < static_cast<int>(all.size()); ++row) {
            const auto& keys = all[static_cast<std::size_t>(row)];
            for (int column = 0; column < static_cast<int>(keys.size()); ++column) {
                if (!keyRect(row, column).contains(input.touchX(), input.touchY()))
                    continue;
                m_touchRow = row;
                m_touchColumn = column;
                m_row = row;
                m_column = column;
                // Backspace fires on press so hold-to-repeat can continue while
                // the finger stays down (other keys still commit on release).
                if (keys[static_cast<std::size_t>(column)].action == Action::Backspace)
                    pressKey(keys[static_cast<std::size_t>(column)]);
                return;
            }
        }
        return;
    }

    if (input.touchUp()) {
        if (m_touchOnClear) {
            m_touchOnClear = false;
            if (clearButtonRect().contains(input.touchX(), input.touchY()))
                clearText();
            return;
        }
        if (m_touchOnReveal) {
            m_touchOnReveal = false;
            if (revealButtonRect().contains(input.touchX(), input.touchY()))
                togglePasswordReveal();
            return;
        }
        if (m_touchOnField) {
            m_touchOnField = false;
            m_fieldSelecting = false;
            return;
        }
        if (m_touchOnStyleToggle) {
            m_touchOnStyleToggle = false;
            if (styleToggleRect().contains(input.touchX(), input.touchY()))
                toggleGlassStyle();
            return;
        }
        if (m_touchOnSizeToggle) {
            m_touchOnSizeToggle = false;
            if (sizeToggleRect().contains(input.touchX(), input.touchY()))
                toggleSizeMode();
            return;
        }
        if (m_touchOnCancelChip) {
            m_touchOnCancelChip = false;
            if (cancelChipRect().contains(input.touchX(), input.touchY()))
                hide(false);
            return;
        }
        if (m_touchRow >= 0) {
            const int row = m_touchRow;
            const int column = m_touchColumn;
            m_touchRow = m_touchColumn = -1;
            if (row < static_cast<int>(all.size())) {
                const auto& keys = all[static_cast<std::size_t>(row)];
                if (column < static_cast<int>(keys.size())
                    && keys[static_cast<std::size_t>(column)].action != Action::Backspace
                    && keyRect(row, column).contains(input.touchX(), input.touchY()))
                    pressKey(keys[static_cast<std::size_t>(column)]);
            }
            if (!input.isHeld(nxui::Button::X))
                m_backspaceHeld = false;
        }
    }
}

void TextEntryScreen::onUpdate(float dt) {
    if (!isActive())
        return;
    m_alpha.update(dt);
    m_caretTime += dt;
    if (m_backspaceHeld)
        updateBackspaceHold(dt, true);
    setOpacity(m_alpha.value());
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_active = false;
        m_animatingOut = false;
        setVisible(false);
        const bool accepted = m_accepted;
        const std::string value = m_text;
        auto accept = m_acceptCb;
        auto cancel = m_cancelCb;
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
    const float alpha = m_alpha.value();
    if (!m_backdropReady) {
        ren.captureToOffscreen(false);
        ren.applyBlur(4.f, 3);
        ren.copyOffscreen(0, 2);
        m_backdropReady = true;
    }
    ren.drawRect({0.f, 0.f, m_canvasW, m_canvasH},
                 nxui::Color::black().withAlpha((m_glassStyle ? 0.62f : 0.72f) * alpha));
    if (m_glassStyle) {
        ren.drawOffscreenRounded(2, rect(), 26.f,
                                 nxui::Color::white().withAlpha(alpha));
        nxui::GlassWidget::onRender(ren);
    } else {
        // Flat: opaque panel for maximum contrast / readability.
        ren.drawRoundedRect(rect(), m_theme->panelBase.withAlpha(0.96f * alpha), 26.f);
        ren.drawRoundedRectOutline(rect(), m_theme->panelBorder.withAlpha(0.40f * alpha), 26.f, 1.2f);
        onContentRender(ren);
    }
}

void TextEntryScreen::onContentRender(nxui::Renderer& ren) {
    const float alpha = m_alpha.value();
    const Metrics m = metrics();
    const nxui::Rect panel = rect();
    auto& i18n = nxui::I18n::instance();

    ren.drawText(m_request.title, {panel.x + 30.f, panel.y + 22.f}, m_font,
                 m_theme->textPrimary.withAlpha(alpha), m_fullLayout ? 1.02f : 0.96f);
    if (!m_request.guide.empty())
        ren.drawText(m_request.guide, {panel.x + 30.f, panel.y + 56.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(0.86f * alpha), guideScale());

    auto drawChip = [&](const nxui::Rect& r, const std::string& label) {
        if (m_glassStyle)
            ren.drawLiquidGlass(2, r, 10.f, m_theme->panelBase.withAlpha(0.16f), alpha, 0.05f);
        else
            ren.drawRoundedRect(r, m_theme->panelHighlight.withAlpha(0.20f * alpha), 10.f);
        ren.drawRoundedRectOutline(r, m_theme->panelBorder.withAlpha(0.42f * alpha), 10.f, 1.f);
        const float scale = 0.66f;
        const nxui::Vec2 size = m_smallFont->measure(label);
        ren.drawText(label,
            {r.x + (r.width - size.x * scale) * 0.5f,
             r.y + (r.height - size.y * scale) * 0.5f},
            m_smallFont, m_theme->textPrimary.withAlpha(alpha), scale);
    };
    // Short chip labels (dedicated keys) — a11y announcements keep the long forms.
    drawChip(styleToggleRect(),
             m_glassStyle ? i18n.tr("text_entry.chip_flat", "Flat")
                          : i18n.tr("text_entry.chip_glass", "Glass"));
    drawChip(sizeToggleRect(),
             m_fullLayout ? i18n.tr("text_entry.chip_compact", "Compact")
                          : i18n.tr("text_entry.chip_full", "Full"));
    drawChip(cancelChipRect(), i18n.tr("button.cancel", "Cancel"));

    const nxui::Rect field = textFieldRect();
    if (m_glassStyle) {
        ren.drawLiquidGlass(2, field, 14.f,
                            m_theme->panelBase.withAlpha(0.16f), alpha, 0.06f);
    } else {
        ren.drawRoundedRect(field, m_theme->panelBase.withAlpha(0.40f * alpha), 14.f);
    }
    const float fieldOutline = m_focusField ? 0.78f : 0.42f;
    ren.drawRoundedRectOutline(
        field,
        (m_focusField ? m_theme->cursorNormal : m_theme->panelBorder)
            .withAlpha(fieldOutline * alpha),
        14.f, m_focusField ? 1.8f : 1.2f);

    char counter[32]{};
    std::snprintf(counter, sizeof(counter), "%d/%d", textLength(),
                  std::max(1, m_request.maxLength));
    const float cScale = counterScale();
    const nxui::Vec2 counterSize = m_smallFont->measure(counter);
    const float counterWidth = counterSize.x * cScale;
    ren.drawText(counter, {field.right() - 16.f - counterWidth,
                           field.y + (field.height - counterSize.y * cScale) * 0.5f},
                 m_smallFont, m_theme->textSecondary.withAlpha(0.78f * alpha), cScale);

    const nxui::Rect clear = clearButtonRect();
    if (clear.width > 0.f) {
        ren.drawCircle({clear.x + clear.width * 0.5f, clear.y + clear.height * 0.5f},
                       clear.width * 0.42f,
                       m_theme->panelHighlight.withAlpha(0.28f * alpha), 16);
        const float cx = clear.x + clear.width * 0.5f;
        const float cy = clear.y + clear.height * 0.5f;
        const float arm = clear.width * 0.22f;
        const nxui::Color xInk = m_theme->textPrimary.withAlpha(0.88f * alpha);
        ren.drawLine({cx - arm, cy - arm}, {cx + arm, cy + arm}, xInk, 2.2f);
        ren.drawLine({cx + arm, cy - arm}, {cx - arm, cy + arm}, xInk, 2.2f);
    }

    const nxui::Rect reveal = revealButtonRect();
    if (reveal.width > 0.f) {
        ren.drawRoundedRect(reveal, m_theme->panelHighlight.withAlpha(0.28f * alpha), 10.f);
        const std::string eye = m_passwordRevealed ? "Hide" : "Show";
        const float eyeScale = 0.52f;
        const nxui::Vec2 eyeSize = m_smallFont->measure(eye);
        ren.drawText(eye,
            {reveal.x + (reveal.width - eyeSize.x * eyeScale) * 0.5f,
             reveal.y + (reveal.height - eyeSize.y * eyeScale) * 0.5f},
            m_smallFont, m_theme->textPrimary.withAlpha(0.88f * alpha), eyeScale);
    }

    const std::string shown = displayText();
    const float fieldScale = m_fullLayout ? 0.92f : 0.84f;
    const float textX = field.x + 16.f;
    const float reserve = fieldTrailingReserve();
    const float clipW = std::max(8.f, field.width - reserve);
    const nxui::Vec2 measuredFull = m_font->measure(shown.empty() ? "Ay" : shown);
    const float textY = field.y + (field.height - measuredFull.y * fieldScale) * 0.5f;

    ren.pushClipRect({field.x + 4.f, field.y, clipW - 4.f, field.height});
    if (hasSelection() && !shown.empty()) {
        const float sel0 = textX + m_font->measure(displayPrefix(selectionStart())).x * fieldScale;
        const float sel1 = textX + m_font->measure(displayPrefix(selectionEnd())).x * fieldScale;
        ren.drawRect({sel0, field.y + 10.f, std::max(2.f, sel1 - sel0), field.height - 20.f},
                     m_theme->cursorNormal.withAlpha(0.34f * alpha));
    }
    if (!shown.empty()) {
        ren.drawText(shown, {textX, textY}, m_font,
                     m_theme->textPrimary.withAlpha(alpha), fieldScale);
    }
    if (std::fmod(m_caretTime, 1.0f) < 0.55f) {
        const float caretX = textX + m_font->measure(displayPrefix(m_caret)).x * fieldScale + 1.f;
        ren.drawRect({caretX, field.y + 12.f, 2.f, field.height - 24.f},
                     m_theme->cursorNormal.withAlpha(0.92f * alpha));
    }
    ren.popClipRect();

    const auto& all = rows();
    for (int row = 0; row < static_cast<int>(all.size()); ++row) {
        const auto& keys = all[static_cast<std::size_t>(row)];
        for (int column = 0; column < static_cast<int>(keys.size()); ++column) {
            const Key& key = keys[static_cast<std::size_t>(column)];
            const nxui::Rect r = keyRect(row, column);
            const bool selected = !m_focusField && row == m_row && column == m_column;
            const bool shiftLit = (m_shift || m_shiftLock) && key.action == Action::Shift;
            const bool accept = key.action == Action::Accept;

            if (m_glassStyle) {
                ren.drawLiquidGlass(2, r, m.keyRadius,
                    m_theme->panelBase.withAlpha(0.14f), alpha, 0.05f);
                nxui::Color wash = nxui::Color::transparent();
                if (accept) wash = m_theme->cursorNormal.withAlpha(0.18f * alpha);
                if (shiftLit) wash = m_theme->cursorNormal.withAlpha(0.25f * alpha);
                if (selected) wash = m_theme->cursorNormal.withAlpha(0.46f * alpha);
                if (wash.a > 0.001f) ren.drawRoundedRect(r, wash, m.keyRadius);
            } else {
                nxui::Color fill = m_theme->panelHighlight.withAlpha(0.18f * alpha);
                if (accept) fill = m_theme->cursorNormal.withAlpha(0.32f * alpha);
                else if (shiftLit) fill = m_theme->cursorNormal.withAlpha(0.28f * alpha);
                if (selected) fill = m_theme->cursorNormal.withAlpha(0.55f * alpha);
                ren.drawRoundedRect(r, fill, m.keyRadius);
            }
            ren.drawRoundedRectOutline(r,
                (selected ? m_theme->textPrimary : m_theme->panelBorder)
                    .withAlpha((selected ? 0.68f : 0.30f) * alpha), m.keyRadius, 1.f);

            const std::string label = keyLabel(key);
            const bool wide = key.action != Action::None;
            const float labelScale = wide
                ? (m_fullLayout ? 0.68f : 0.60f)
                : (m_fullLayout ? 0.90f : 0.80f);
            nxui::Font* labelFont = (wide && key.action != Action::Backspace)
                ? m_smallFont : m_font;
            const nxui::Vec2 size = labelFont->measure(label);
            ren.drawText(label,
                {r.x + (r.width - size.x * labelScale) * 0.5f,
                 r.y + (r.height - size.y * labelScale) * 0.5f},
                labelFont,
                (selected ? m_theme->textPrimary : m_theme->textSecondary).withAlpha(alpha),
                labelScale);
        }
    }

    const nxui::Rect board = keyboardBoardRect();
    const float hScale = hintScale();
    const float hintY = board.bottom() + 6.f;
    if (hintY + 28.f < panel.bottom() - 4.f) {
        std::string hint;
        if (m_focusField) {
            hint = m_request.password
                ? i18n.tr("text_entry.hint_field_password",
                    "←/→ caret · L/R select · Y all · − show/hide · ZL clear · A keys · + OK · B cancel")
                : i18n.tr("text_entry.hint_field",
                    "←/→ caret · L/R select · Y all · ZL clear · A keys · + OK · B cancel");
        } else if (m_request.password) {
            hint = i18n.tr("text_entry.hint_password",
                "↑ field · A type · X erase · Y shift · − show/hide · ZL clear · + OK · B cancel");
        } else {
            hint = i18n.tr("text_entry.hint",
                "↑ field · A type · X erase · Y shift · L/R symbols · − size · ZR style · + OK · B cancel");
        }
        const nxui::Vec2 hintSize = m_smallFont->measure(hint);
        // Shrink slightly if a long localization would overflow the panel.
        float scale = hScale;
        const float maxW = panel.width - m.inset * 2.f;
        if (hintSize.x * scale > maxW)
            scale = std::max(0.72f, maxW / std::max(1.f, hintSize.x));
        ren.drawText(hint,
                     {panel.x + (panel.width - hintSize.x * scale) * 0.5f, hintY},
                     m_smallFont, m_theme->textSecondary.withAlpha(0.86f * alpha), scale);
    }
}
