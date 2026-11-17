#pragma once

#include "LockAuthStore.hpp"
#include "LockAuthTypes.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/Theme.hpp>
#include <nxui/widgets/GlassWidget.hpp>

#include <functional>
#include <string>
#include <vector>

namespace switchu::lockauth {

/// Full-screen Lock auth / enroll UI (PIN, password, pattern, combo).
class LockAuthOverlay : public nxui::GlassWidget {
public:
    enum class Purpose {
        Unlock,   // verify → success callback
        Enroll,   // enter twice → enroll callback
        Verify,   // verify current before settings change
    };

    enum class PendingAction {
        None,
        UnlockHome,
        UnlockResume,
        EnrollDone,
        VerifyOk,
        Cancelled,
    };

    using DoneCb = std::function<void(PendingAction action)>;

    LockAuthOverlay();

    void setFonts(nxui::Font* body, nxui::Font* small);
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setCanvasSize(float w, float h);
    void setStore(LockAuthStore* store) { m_store = store; }
    void setOnDone(DoneCb cb) { m_onDone = std::move(cb); }
    void setOnSfx(std::function<void(bool success)> cb) { m_onSfx = std::move(cb); }

    void beginUnlock(Mode mode, bool resumeIntent);
    void beginEnroll(Mode mode);
    void beginVerify(Mode mode);

    bool isActive() const { return m_active; }
    Purpose purpose() const { return m_purpose; }
    Mode mode() const { return m_mode; }
    bool resumeIntent() const { return m_resumeIntent; }

    /// Enrolled secret available after EnrollDone (caller persists via store).
    const SecretBytes& pendingSecret() const { return m_enrollFirst; }
    int pendingPinLength() const { return m_pinLen; }

    void cancel();
    void handleInput(nxui::Input& input);
    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void resetEntry();
    void showError(const std::string& msg);
    void onSuccess();
    void submitPin();
    void submitPassword();
    void submitPattern();
    void submitCombo();
    void handlePinKey(int key); // 0-9, -1 backspace, -2 ok
    void patternAddCell(int cell);
    void comboAdd(std::uint32_t button);
    void drawPin(nxui::Renderer& ren, float opacity);
    void drawPassword(nxui::Renderer& ren, float opacity);
    void drawPattern(nxui::Renderer& ren, float opacity);
    void drawCombo(nxui::Renderer& ren, float opacity);
    void drawChrome(nxui::Renderer& ren, float opacity);
    nxui::Rect padKeyRect(int index) const; // 0-11 (1-9, clear, 0, ok)
    nxui::Rect patternCellRect(int cell) const;
    int hitPadKey(float x, float y) const;
    int hitPatternCell(float x, float y) const;
    std::string titleText() const;
    std::string hintText() const;
    bool rateLimited() const;

    LockAuthStore* m_store = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    DoneCb m_onDone;
    std::function<void(bool)> m_onSfx;

    bool m_active = false;
    Purpose m_purpose = Purpose::Unlock;
    Mode m_mode = Mode::Pin;
    bool m_resumeIntent = false;
    bool m_enrollConfirm = false;

    float m_canvasW = 1280.f;
    float m_canvasH = 720.f;
    float m_shake = 0.f;
    float m_errorAge = 0.f;
    float m_lockoutLeft = 0.f;
    int m_failCount = 0;
    std::string m_error;
    /// Ignore the A/press that opened this overlay until controls are released.
    bool m_waitRelease = false;

    // PIN
    std::string m_pin;
    int m_pinLen = kDefaultPinLength;
    int m_padFocus = 4; // center "5" — empty until the user presses A

    // Password
    std::string m_password;
    int m_kbRow = 0;
    int m_kbCol = 0;

    // Pattern
    std::vector<std::uint8_t> m_pattern;
    bool m_patternDrawing = false;
    int m_patternFocus = 4;

    // Combo
    std::vector<std::uint32_t> m_combo;

    SecretBytes m_enrollFirst;
};

} // namespace switchu::lockauth
