#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <functional>
#include <cstdint>
#include <cstddef>

/// A circular focusable user-avatar button using the same liquid-glass
/// rendering as GlossyIcon (they both extend GlassWidget).
class UserAvatarButton : public nxui::GlassWidget {
public:
    using ActivateCallback = std::function<void()>;

    UserAvatarButton() {
        setCornerRadius(28.f);
        setLiquidGlassEnabled(true);
        setBlurEnabled(false);
    }

    /// Load the avatar JPEG/PNG from a memory buffer.
    /// The texture is owned by the button.
    void loadAvatar(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                    const void* data, std::size_t size) {
        m_ownTex.loadFromMemory(gpu, ren,
                                static_cast<const uint8_t*>(data), size);
    }

    void setUid(AccountUid uid)               { m_uid = uid; }
    AccountUid uid() const                    { return m_uid; }
    void setNickname(const std::string& n)   { m_nickname = n; }
    const std::string& nickname() const       { return m_nickname; }
    void setOnActivate(ActivateCallback cb)   { m_onActivate = std::move(cb); }

    bool isFocusable() const override { return true; }
    void onFocusGained() override     { m_focused = true; }
    void onFocusLost()   override     { m_focused = false; }

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    nxui::Texture    m_ownTex;
    AccountUid       m_uid      = {};
    std::string      m_nickname;
    bool             m_focused  = false;
    ActivateCallback m_onActivate;
};
