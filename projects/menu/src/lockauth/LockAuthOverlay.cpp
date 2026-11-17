#include "LockAuthOverlay.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace switchu::lockauth {
namespace {

constexpr float kPadKey = 72.f;
constexpr float kPadGap = 10.f;
constexpr char kKbRows[4][11] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
};

const char* comboLabel(std::uint32_t b) {
    using B = nxui::Button;
    if (b == static_cast<std::uint32_t>(B::A)) return "A";
    if (b == static_cast<std::uint32_t>(B::B)) return "B";
    if (b == static_cast<std::uint32_t>(B::X)) return "X";
    if (b == static_cast<std::uint32_t>(B::Y)) return "Y";
    if (b == static_cast<std::uint32_t>(B::L)) return "L";
    if (b == static_cast<std::uint32_t>(B::R)) return "R";
    if (b == static_cast<std::uint32_t>(B::ZL)) return "ZL";
    if (b == static_cast<std::uint32_t>(B::ZR)) return "ZR";
    if (b == static_cast<std::uint32_t>(B::DUp)) return "Up";
    if (b == static_cast<std::uint32_t>(B::DDown)) return "Down";
    if (b == static_cast<std::uint32_t>(B::DLeft)) return "Left";
    if (b == static_cast<std::uint32_t>(B::DRight)) return "Right";
    if (b == static_cast<std::uint32_t>(B::Minus)) return "-";
    return "?";
}

} // namespace

LockAuthOverlay::LockAuthOverlay() {
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setBlurEnabled(false);
    setFrameworkTouchEnabled(false);
    // Swallow FocusManager actions — real handling is in handleInput().
    // Without these, D-pad can fall through spatial nav / parent layers.
    auto swallow = []() {};
    for (auto b : {nxui::Button::A, nxui::Button::B, nxui::Button::X, nxui::Button::Y,
                   nxui::Button::Plus, nxui::Button::Minus,
                   nxui::Button::DUp, nxui::Button::DDown,
                   nxui::Button::DLeft, nxui::Button::DRight,
                   nxui::Button::L, nxui::Button::R,
                   nxui::Button::ZL, nxui::Button::ZR}) {
        addAction(static_cast<std::uint64_t>(b), swallow);
    }
}

void LockAuthOverlay::setFonts(nxui::Font* body, nxui::Font* small) {
    m_font = body;
    m_smallFont = small;
}

void LockAuthOverlay::setCanvasSize(float w, float h) {
    m_canvasW = w > 1.f ? w : 1280.f;
    m_canvasH = h > 1.f ? h : 720.f;
    setRect({0.f, 0.f, m_canvasW, m_canvasH});
}

void LockAuthOverlay::resetEntry() {
    m_pin.clear();
    m_password.clear();
    m_pattern.clear();
    m_patternDrawing = false;
    m_combo.clear();
    m_error.clear();
    m_errorAge = 0.f;
    m_shake = 0.f;
}

void LockAuthOverlay::beginUnlock(Mode mode, bool resumeIntent) {
    m_active = true;
    setVisible(true);
    m_purpose = Purpose::Unlock;
    m_mode = mode;
    m_resumeIntent = resumeIntent;
    m_enrollConfirm = false;
    m_enrollFirst.clear();
    m_pinLen = m_store ? m_store->pinLength() : kDefaultPinLength;
    m_waitRelease = true;
    m_padFocus = 4;
    m_patternFocus = 4;
    m_kbRow = 0;
    m_kbCol = 0;
    resetEntry();
}

void LockAuthOverlay::beginEnroll(Mode mode) {
    m_active = true;
    setVisible(true);
    m_purpose = Purpose::Enroll;
    m_mode = mode;
    m_resumeIntent = false;
    m_enrollConfirm = false;
    m_enrollFirst.clear();
    m_pinLen = kDefaultPinLength;
    m_waitRelease = true;
    m_padFocus = 4;
    m_patternFocus = 4;
    m_kbRow = 0;
    m_kbCol = 0;
    resetEntry();
}

void LockAuthOverlay::beginVerify(Mode mode) {
    m_active = true;
    setVisible(true);
    m_purpose = Purpose::Verify;
    m_mode = mode;
    m_resumeIntent = false;
    m_enrollConfirm = false;
    m_enrollFirst.clear();
    m_pinLen = m_store ? m_store->pinLength() : kDefaultPinLength;
    m_waitRelease = true;
    m_padFocus = 4;
    m_patternFocus = 4;
    m_kbRow = 0;
    m_kbCol = 0;
    resetEntry();
}

void LockAuthOverlay::cancel() {
    if (!m_active) return;
    m_active = false;
    setVisible(false);
    resetEntry();
    if (m_onDone) m_onDone(PendingAction::Cancelled);
}

bool LockAuthOverlay::rateLimited() const {
    return m_lockoutLeft > 0.f;
}

void LockAuthOverlay::showError(const std::string& msg) {
    ++m_failCount;
    if (m_failCount >= kFailLimit) {
        m_lockoutLeft = kFailLockoutSec;
        m_failCount = 0;
    }
    if (m_onSfx) m_onSfx(false);
    resetEntry();
    m_error = msg;
    m_errorAge = 0.f;
    m_shake = 1.f;
}

void LockAuthOverlay::onSuccess() {
    m_failCount = 0;
    m_lockoutLeft = 0.f;
    if (m_onSfx) m_onSfx(true);
    m_active = false;
    setVisible(false);
    PendingAction action = PendingAction::UnlockHome;
    if (m_purpose == Purpose::Enroll)
        action = PendingAction::EnrollDone;
    else if (m_purpose == Purpose::Verify)
        action = PendingAction::VerifyOk;
    else if (m_resumeIntent)
        action = PendingAction::UnlockResume;
    if (m_onDone) m_onDone(action);
}

std::string LockAuthOverlay::titleText() const {
    auto& i18n = nxui::I18n::instance();
    if (m_purpose == Purpose::Enroll) {
        if (!m_enrollConfirm) {
            switch (m_mode) {
                case Mode::Pin:
                    return i18n.tr("lockauth.enroll_pin", "Choose a PIN");
                case Mode::Password:
                    return i18n.tr("lockauth.enroll_password",
                                   "Choose a password (4+ characters)");
                case Mode::Pattern:
                    return i18n.tr("lockauth.enroll_pattern", "Draw a pattern");
                case Mode::Combo:
                    return i18n.tr("lockauth.enroll_combo", "Enter a button combo");
                default: break;
            }
        }
        return i18n.tr("lockauth.confirm_again", "Enter it again");
    }
    if (m_purpose == Purpose::Verify)
        return i18n.tr("lockauth.verify", "Confirm Sign-in");
    return i18n.tr("lockauth.unlock", "Sign in");
}

std::string LockAuthOverlay::hintText() const {
    auto& i18n = nxui::I18n::instance();
    if (rateLimited()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      i18n.tr("lockauth.try_again_in", "Try again in %.0fs").c_str(),
                      std::ceil(m_lockoutLeft));
        return buf;
    }
    if (!m_error.empty() && m_errorAge < 2.5f)
        return m_error;
    switch (m_mode) {
        case Mode::Pin:
            return i18n.tr("lockauth.hint_pin", "Enter your PIN");
        case Mode::Password:
            return i18n.tr("lockauth.hint_password",
                          "Password · at least 4 characters · OK to confirm");
        case Mode::Pattern:
            return i18n.tr("lockauth.hint_pattern", "Connect at least 4 dots");
        case Mode::Combo:
            return i18n.tr("lockauth.hint_combo", "Press buttons, then + to submit");
        default:
            return {};
    }
}

void LockAuthOverlay::handlePinKey(int key) {
    if (rateLimited()) return;
    if (key == -1) {
        if (!m_pin.empty()) m_pin.pop_back();
        return;
    }
    if (key == -2) {
        submitPin();
        return;
    }
    if (key < 0 || key > 9) return;
    if (static_cast<int>(m_pin.size()) >= m_pinLen) return;
    m_pin.push_back(static_cast<char>('0' + key));
    if (static_cast<int>(m_pin.size()) == m_pinLen)
        submitPin();
}

void LockAuthOverlay::submitPin() {
    if (static_cast<int>(m_pin.size()) < kMinPinLength) {
        showError(nxui::I18n::instance().tr("lockauth.too_short", "Too short"));
        return;
    }
    const auto secret = secretFromPin(m_pin);
    if (m_purpose == Purpose::Enroll) {
        if (!m_enrollConfirm) {
            m_enrollFirst = secret;
            m_pinLen = static_cast<int>(m_pin.size());
            m_enrollConfirm = true;
            resetEntry();
            return;
        }
        if (secret != m_enrollFirst) {
            m_enrollConfirm = false;
            m_enrollFirst.clear();
            showError(nxui::I18n::instance().tr("lockauth.mismatch", "Didn't match — try again"));
            return;
        }
        onSuccess();
        return;
    }
    if (!m_store || !m_store->verify(secret)) {
        showError(nxui::I18n::instance().tr("lockauth.wrong", "Incorrect"));
        return;
    }
    onSuccess();
}

void LockAuthOverlay::submitPassword() {
    if (static_cast<int>(m_password.size()) < kMinPasswordLength) {
        showError(nxui::I18n::instance().tr(
            "lockauth.password_too_short", "Need at least 4 characters"));
        return;
    }
    const auto secret = secretFromPassword(m_password);
    if (m_purpose == Purpose::Enroll) {
        if (!m_enrollConfirm) {
            m_enrollFirst = secret;
            m_enrollConfirm = true;
            resetEntry();
            return;
        }
        if (secret != m_enrollFirst) {
            m_enrollConfirm = false;
            m_enrollFirst.clear();
            showError(nxui::I18n::instance().tr("lockauth.mismatch", "Didn't match — try again"));
            return;
        }
        onSuccess();
        return;
    }
    if (!m_store || !m_store->verify(secret)) {
        showError(nxui::I18n::instance().tr("lockauth.wrong", "Incorrect"));
        return;
    }
    onSuccess();
}

void LockAuthOverlay::patternAddCell(int cell) {
    if (cell < 0 || cell >= kPatternCells || rateLimited()) return;
    if (!m_pattern.empty() && m_pattern.back() == static_cast<std::uint8_t>(cell))
        return;
    for (auto c : m_pattern)
        if (c == static_cast<std::uint8_t>(cell))
            return; // no revisit
    m_pattern.push_back(static_cast<std::uint8_t>(cell));
}

bool LockAuthOverlay::patternComplete() const {
    const int n = static_cast<int>(m_pattern.size());
    if (n < kMinPatternLen)
        return false;
    if (m_purpose == Purpose::Enroll && m_enrollConfirm)
        return n == static_cast<int>(m_enrollFirst.size());
    if (m_purpose == Purpose::Unlock || m_purpose == Purpose::Verify)
        return n == std::clamp(m_pinLen, kMinPatternLen, kMaxPatternLen);
    // First enroll pass: length is chosen by the user (+ / lift). Auto only at max.
    return n >= kMaxPatternLen;
}

void LockAuthOverlay::submitPattern() {
    if (static_cast<int>(m_pattern.size()) < kMinPatternLen) {
        showError(nxui::I18n::instance().tr("lockauth.pattern_short", "Connect more dots"));
        return;
    }
    const auto secret = secretFromPattern(m_pattern);
    if (m_purpose == Purpose::Enroll) {
        if (!m_enrollConfirm) {
            m_enrollFirst = secret;
            m_pinLen = static_cast<int>(m_pattern.size());
            m_enrollConfirm = true;
            resetEntry();
            return;
        }
        if (secret != m_enrollFirst) {
            m_enrollConfirm = false;
            m_enrollFirst.clear();
            showError(nxui::I18n::instance().tr("lockauth.mismatch", "Didn't match — try again"));
            return;
        }
        onSuccess();
        return;
    }
    if (!m_store || !m_store->verify(secret)) {
        showError(nxui::I18n::instance().tr("lockauth.wrong", "Incorrect"));
        return;
    }
    onSuccess();
}

void LockAuthOverlay::comboAdd(std::uint32_t button) {
    if (rateLimited()) return;
    if (static_cast<int>(m_combo.size()) >= kMaxComboLen) return;
    m_combo.push_back(button);
}

void LockAuthOverlay::submitCombo() {
    if (static_cast<int>(m_combo.size()) < kMinComboLen) {
        showError(nxui::I18n::instance().tr("lockauth.combo_short", "Need more presses"));
        return;
    }
    const auto secret = secretFromCombo(m_combo);
    if (m_purpose == Purpose::Enroll) {
        if (!m_enrollConfirm) {
            m_enrollFirst = secret;
            m_enrollConfirm = true;
            resetEntry();
            return;
        }
        if (secret != m_enrollFirst) {
            m_enrollConfirm = false;
            m_enrollFirst.clear();
            showError(nxui::I18n::instance().tr("lockauth.mismatch", "Didn't match — try again"));
            return;
        }
        onSuccess();
        return;
    }
    if (!m_store || !m_store->verify(secret)) {
        showError(nxui::I18n::instance().tr("lockauth.wrong", "Incorrect"));
        return;
    }
    onSuccess();
}

nxui::Rect LockAuthOverlay::padKeyRect(int index) const {
    const float gridW = 3.f * kPadKey + 2.f * kPadGap;
    const float gridH = 4.f * kPadKey + 3.f * kPadGap;
    const float ox = (m_canvasW - gridW) * 0.5f;
    const float oy = m_canvasH * 0.42f;
    const int row = index / 3;
    const int col = index % 3;
    return {ox + col * (kPadKey + kPadGap), oy + row * (kPadKey + kPadGap), kPadKey, kPadKey};
}

nxui::Rect LockAuthOverlay::patternCellRect(int index) const {
    const float size = 56.f;
    const float gap = 36.f;
    const float grid = 3.f * size + 2.f * gap;
    const float ox = (m_canvasW - grid) * 0.5f;
    const float oy = m_canvasH * 0.38f;
    const int row = index / 3;
    const int col = index % 3;
    return {ox + col * (size + gap), oy + row * (size + gap), size, size};
}

int LockAuthOverlay::hitPadKey(float x, float y) const {
    for (int i = 0; i < 12; ++i) {
        const auto r = padKeyRect(i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
            return i;
    }
    return -1;
}

int LockAuthOverlay::hitPatternCell(float x, float y) const {
    for (int i = 0; i < kPatternCells; ++i) {
        const auto r = patternCellRect(i);
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        const float dx = x - cx;
        const float dy = y - cy;
        if (dx * dx + dy * dy <= (r.width * 0.55f) * (r.width * 0.55f))
            return i;
    }
    return -1;
}

void LockAuthOverlay::handleInput(nxui::Input& input) {
    if (!m_active) return;

    // Drop the press that opened us (Theme Shop A / Lock Unlock) so it cannot
    // type a digit, place a pattern cell, or cancel on the same frame.
    if (m_waitRelease) {
        using B = nxui::Button;
        const bool stillHeld =
            input.isHeld(B::A) || input.isHeld(B::B) || input.isHeld(B::X) ||
            input.isHeld(B::Y) || input.isHeld(B::Plus) || input.isHeld(B::Minus) ||
            input.isHeld(B::DUp) || input.isHeld(B::DDown) ||
            input.isHeld(B::DLeft) || input.isHeld(B::DRight) ||
            input.isHeld(B::L) || input.isHeld(B::R) ||
            input.isHeld(B::ZL) || input.isHeld(B::ZR) ||
            input.isTouching();
        if (stillHeld)
            return;
        m_waitRelease = false;
    }

    if (input.isDown(nxui::Button::B) && m_mode != Mode::Combo) {
        // Combo uses B as a secret digit — cancel via Minus instead.
        cancel();
        return;
    }
    if (m_mode == Mode::Combo && input.isDown(nxui::Button::Minus)) {
        cancel();
        return;
    }

    if (rateLimited()) return;

    if (m_mode == Mode::Pin) {
        if (input.touchDown()) {
            const int hit = hitPadKey(input.touchX(), input.touchY());
            if (hit >= 0) {
                m_padFocus = hit;
                if (hit < 9) handlePinKey(hit + 1);
                else if (hit == 9) handlePinKey(-1);
                else if (hit == 10) handlePinKey(0);
                else handlePinKey(-2);
            }
        }
        if (input.isDown(nxui::Button::DLeft)) m_padFocus = (m_padFocus % 3 == 0) ? m_padFocus + 2 : m_padFocus - 1;
        if (input.isDown(nxui::Button::DRight)) m_padFocus = (m_padFocus % 3 == 2) ? m_padFocus - 2 : m_padFocus + 1;
        if (input.isDown(nxui::Button::DUp)) m_padFocus = (m_padFocus < 3) ? m_padFocus + 9 : m_padFocus - 3;
        if (input.isDown(nxui::Button::DDown)) m_padFocus = (m_padFocus >= 9) ? m_padFocus - 9 : m_padFocus + 3;
        m_padFocus = std::clamp(m_padFocus, 0, 11);
        if (input.isDown(nxui::Button::A)) {
            const int hit = m_padFocus;
            if (hit < 9) handlePinKey(hit + 1);
            else if (hit == 9) handlePinKey(-1);
            else if (hit == 10) handlePinKey(0);
            else handlePinKey(-2);
        }
        return;
    }

    if (m_mode == Mode::Password) {
        // Compact QWERTY for controller + touch.
        const int rowLens[4] = {10, 10, 9, 7};
        if (input.isDown(nxui::Button::DLeft))
            m_kbCol = (m_kbCol <= 0) ? rowLens[m_kbRow] - 1 : m_kbCol - 1;
        if (input.isDown(nxui::Button::DRight))
            m_kbCol = (m_kbCol + 1) % rowLens[m_kbRow];
        if (input.isDown(nxui::Button::DUp))
            m_kbRow = (m_kbRow + 3) % 4;
        if (input.isDown(nxui::Button::DDown))
            m_kbRow = (m_kbRow + 1) % 4;
        m_kbCol = std::clamp(m_kbCol, 0, rowLens[m_kbRow] - 1);
        if (input.isDown(nxui::Button::A)) {
            const char ch = kKbRows[m_kbRow][m_kbCol];
            if (m_password.size() < 64)
                m_password.push_back(ch);
        }
        if (input.isDown(nxui::Button::Y) && !m_password.empty())
            m_password.pop_back();
        if (input.isDown(nxui::Button::Plus) || input.isDown(nxui::Button::X))
            submitPassword();
        if (input.touchDown()) {
            // Simple: tap bottom OK / backspace zones approximated by pad area.
            const float midY = m_canvasH * 0.78f;
            if (input.touchY() > midY) {
                if (input.touchX() < m_canvasW * 0.5f && !m_password.empty())
                    m_password.pop_back();
                else
                    submitPassword();
            } else {
                // Map touch onto keyboard roughly
                const float top = m_canvasH * 0.40f;
                const float keyH = 48.f;
                const int row = static_cast<int>((input.touchY() - top) / keyH);
                if (row >= 0 && row < 4) {
                    const int len = rowLens[row];
                    const float keyW = 48.f;
                    const float ox = (m_canvasW - len * keyW) * 0.5f;
                    const int col = static_cast<int>((input.touchX() - ox) / keyW);
                    if (col >= 0 && col < len && m_password.size() < 64)
                        m_password.push_back(kKbRows[row][col]);
                }
            }
        }
        return;
    }

    if (m_mode == Mode::Pattern) {
        if (input.touchDown() || input.isTouching()) {
            const int cell = hitPatternCell(input.touchX(), input.touchY());
            if (cell >= 0) {
                if (input.touchDown())
                    m_patternDrawing = true;
                if (m_patternDrawing)
                    patternAddCell(cell);
            }
        }
        if (input.touchUp() && m_patternDrawing) {
            m_patternDrawing = false;
            submitPattern();
        }
        if (input.isDown(nxui::Button::DLeft) && m_patternFocus % 3 > 0) --m_patternFocus;
        if (input.isDown(nxui::Button::DRight) && m_patternFocus % 3 < 2) ++m_patternFocus;
        if (input.isDown(nxui::Button::DUp) && m_patternFocus >= 3) m_patternFocus -= 3;
        if (input.isDown(nxui::Button::DDown) && m_patternFocus < 6) m_patternFocus += 3;
        if (input.isDown(nxui::Button::A)) {
            const auto before = m_pattern.size();
            patternAddCell(m_patternFocus);
            // PIN-like: last A that completes the known path submits (no + needed).
            if (m_pattern.size() > before && patternComplete())
                submitPattern();
        }
        if (input.isDown(nxui::Button::Plus) || input.isDown(nxui::Button::X))
            submitPattern();
        if (input.isDown(nxui::Button::Y)) {
            m_pattern.clear();
        }
        return;
    }

    if (m_mode == Mode::Combo) {
        using B = nxui::Button;
        // Y clears the draft — omit it from the secret alphabet.
        const B keys[] = {B::A, B::B, B::X, B::L, B::R, B::ZL, B::ZR,
                          B::DUp, B::DDown, B::DLeft, B::DRight};
        for (B b : keys) {
            if (input.isDown(b))
                comboAdd(static_cast<std::uint32_t>(b));
        }
        if (input.isDown(nxui::Button::Plus))
            submitCombo();
        if (input.isDown(nxui::Button::Y))
            m_combo.clear();
    }
}

void LockAuthOverlay::update(float dt) {
    if (!m_active) return;
    if (m_lockoutLeft > 0.f)
        m_lockoutLeft = std::max(0.f, m_lockoutLeft - dt);
    if (m_shake > 0.f)
        m_shake = std::max(0.f, m_shake - dt * 4.f);
    m_errorAge += dt;
}

void LockAuthOverlay::drawChrome(nxui::Renderer& ren, float opacity) {
    ren.drawRect({0.f, 0.f, m_canvasW, m_canvasH},
                 nxui::Color(0.02f, 0.03f, 0.05f, 0.72f * opacity));
    auto* font = m_font;
    auto* small = m_smallFont ? m_smallFont : m_font;
    if (!font) return;
    const float shakeX = m_shake > 0.f
        ? std::sin(m_shake * 40.f) * 8.f * m_shake
        : 0.f;
    const auto title = titleText();
    const auto tw = font->measure(title);
    font->draw(ren, title,
               {(m_canvasW - tw.x) * 0.5f + shakeX, m_canvasH * 0.18f},
               nxui::Color(1.f, 1.f, 1.f, opacity));
    const auto hint = hintText();
    if (small && !hint.empty()) {
        const auto hw = small->measure(hint);
        small->draw(ren, hint,
                    {(m_canvasW - hw.x) * 0.5f + shakeX, m_canvasH * 0.24f},
                    nxui::Color(0.85f, 0.88f, 0.92f, 0.9f * opacity));
    }
}

void LockAuthOverlay::drawPin(nxui::Renderer& ren, float opacity) {
    const float shakeX = m_shake > 0.f ? std::sin(m_shake * 40.f) * 8.f * m_shake : 0.f;
    // Dots
    const float dot = 14.f;
    const float gap = 18.f;
    const float dotsW = m_pinLen * dot + (m_pinLen - 1) * gap;
    float dx = (m_canvasW - dotsW) * 0.5f + shakeX;
    const float dy = m_canvasH * 0.32f;
    for (int i = 0; i < m_pinLen; ++i) {
        const bool filled = i < static_cast<int>(m_pin.size());
        ren.drawCircle({dx + dot * 0.5f, dy + dot * 0.5f}, dot * 0.5f,
                       filled ? nxui::Color(1.f, 1.f, 1.f, opacity)
                              : nxui::Color(1.f, 1.f, 1.f, 0.25f * opacity));
        dx += dot + gap;
    }
    // Prefer glyphs already in menu fonts — ⌫ renders as tofu.
    const char* labels[12] = {"1","2","3","4","5","6","7","8","9","<-","0","OK"};
    for (int i = 0; i < 12; ++i) {
        auto r = padKeyRect(i);
        r.x += shakeX;
        const bool focus = (i == m_padFocus);
        ren.drawRoundedRect(r,
                            focus ? nxui::Color(0.25f, 0.45f, 0.75f, 0.85f * opacity)
                                  : nxui::Color(0.12f, 0.14f, 0.18f, 0.75f * opacity),
                            16.f);
        if (m_font) {
            const auto tw = m_font->measure(labels[i]);
            m_font->draw(ren, labels[i],
                         {r.x + (r.width - tw.x) * 0.5f,
                          r.y + (r.height - tw.y) * 0.5f},
                         nxui::Color(1.f, 1.f, 1.f, opacity));
        }
    }
}

void LockAuthOverlay::drawPassword(nxui::Renderer& ren, float opacity) {
    const float fieldW = 420.f;
    const float fieldH = 52.f;
    const float fx = (m_canvasW - fieldW) * 0.5f;
    const float fy = m_canvasH * 0.32f;
    ren.drawRoundedRect({fx, fy, fieldW, fieldH},
                        nxui::Color(0.1f, 0.12f, 0.16f, 0.85f * opacity), 14.f);
    std::string masked(m_password.size(), '*');
    if (m_font) {
        const auto tw = m_font->measure(masked.empty() ? "—" : masked);
        m_font->draw(ren, masked.empty() ? "—" : masked,
                     {fx + 16.f, fy + (fieldH - tw.y) * 0.5f},
                     nxui::Color(1.f, 1.f, 1.f, opacity));
    }
    const int rowLens[4] = {10, 10, 9, 7};
    const float keyW = 48.f;
    const float keyH = 44.f;
    const float top = m_canvasH * 0.42f;
    for (int row = 0; row < 4; ++row) {
        const int len = rowLens[row];
        const float ox = (m_canvasW - len * keyW) * 0.5f;
        for (int col = 0; col < len; ++col) {
            const bool focus = (row == m_kbRow && col == m_kbCol);
            const nxui::Rect r{ox + col * keyW, top + row * (keyH + 6.f), keyW - 4.f, keyH};
            ren.drawRoundedRect(r,
                                focus ? nxui::Color(0.25f, 0.45f, 0.75f, 0.9f * opacity)
                                      : nxui::Color(0.14f, 0.16f, 0.2f, 0.8f * opacity),
                                10.f);
            if (m_smallFont) {
                char s[2] = {kKbRows[row][col], 0};
                const auto tw = m_smallFont->measure(s);
                m_smallFont->draw(ren, s,
                                  {r.x + (r.width - tw.x) * 0.5f,
                                   r.y + (r.height - tw.y) * 0.5f},
                                  nxui::Color(1.f, 1.f, 1.f, opacity));
            }
        }
    }
    if (m_smallFont) {
        const auto tip = nxui::I18n::instance().tr(
            "lockauth.password_controls", "A type · Y delete · + / X OK · B cancel");
        const auto tw = m_smallFont->measure(tip);
        m_smallFont->draw(ren, tip, {(m_canvasW - tw.x) * 0.5f, m_canvasH * 0.82f},
                          nxui::Color(0.8f, 0.85f, 0.9f, 0.85f * opacity));
    }
}

void LockAuthOverlay::drawPattern(nxui::Renderer& ren, float opacity) {
    // Lines
    for (std::size_t i = 1; i < m_pattern.size(); ++i) {
        const auto a = patternCellRect(m_pattern[i - 1]);
        const auto b = patternCellRect(m_pattern[i]);
        ren.drawLine({a.x + a.width * 0.5f, a.y + a.height * 0.5f},
                     {b.x + b.width * 0.5f, b.y + b.height * 0.5f},
                     nxui::Color(0.45f, 0.7f, 1.f, 0.85f * opacity), 4.f);
    }
    for (int i = 0; i < kPatternCells; ++i) {
        const auto r = patternCellRect(i);
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        bool on = false;
        for (auto c : m_pattern)
            if (c == static_cast<std::uint8_t>(i)) on = true;
        const bool focus = (i == m_patternFocus);
        if (focus)
            ren.drawCircle({cx, cy}, r.width * 0.42f,
                           nxui::Color(1.f, 1.f, 1.f, 0.18f * opacity));
        ren.drawCircle({cx, cy}, r.width * 0.22f,
                       on ? nxui::Color(0.5f, 0.75f, 1.f, opacity)
                          : nxui::Color(1.f, 1.f, 1.f, (focus ? 0.55f : 0.28f) * opacity));
    }
    if (m_smallFont) {
        const auto tip = nxui::I18n::instance().tr(
            "lockauth.pattern_controls",
            "Touch draw · A add (auto on last) · + done · Y clear · B cancel");
        const auto tw = m_smallFont->measure(tip);
        m_smallFont->draw(ren, tip, {(m_canvasW - tw.x) * 0.5f, m_canvasH * 0.82f},
                          nxui::Color(0.8f, 0.85f, 0.9f, 0.85f * opacity));
    }
}

void LockAuthOverlay::drawCombo(nxui::Renderer& ren, float opacity) {
    std::string seq;
    for (std::size_t i = 0; i < m_combo.size(); ++i) {
        if (i) seq += " · ";
        seq += comboLabel(m_combo[i]);
    }
    if (seq.empty())
        seq = "—";
    if (m_font) {
        const auto tw = m_font->measure(seq);
        m_font->draw(ren, seq, {(m_canvasW - tw.x) * 0.5f, m_canvasH * 0.42f},
                     nxui::Color(1.f, 1.f, 1.f, opacity));
    }
    if (m_smallFont) {
        const auto tip = nxui::I18n::instance().tr(
            "lockauth.combo_controls", "A/B/X · L/R/ZL/ZR · D-pad · + submit · Y clear · − cancel");
        const auto tw = m_smallFont->measure(tip);
        m_smallFont->draw(ren, tip, {(m_canvasW - tw.x) * 0.5f, m_canvasH * 0.55f},
                          nxui::Color(0.8f, 0.85f, 0.9f, 0.85f * opacity));
    }
}

void LockAuthOverlay::render(nxui::Renderer& ren) {
    if (!m_active) return;
    const float opacity = 1.f;
    drawChrome(ren, opacity);
    switch (m_mode) {
        case Mode::Pin: drawPin(ren, opacity); break;
        case Mode::Password: drawPassword(ren, opacity); break;
        case Mode::Pattern: drawPattern(ren, opacity); break;
        case Mode::Combo: drawCombo(ren, opacity); break;
        default: break;
    }
}

} // namespace switchu::lockauth
