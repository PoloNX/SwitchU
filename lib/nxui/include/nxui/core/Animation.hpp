#pragma once
#include <nxui/core/Types.hpp>
#include <functional>
#include <vector>
#include <memory>
#include <cmath>

namespace nxui {

// Easing functions
namespace Easing {
    inline float linear(float t) { return t; }
    inline float inQuad(float t)    { return t*t; }
    inline float outQuad(float t)   { return 1-(1-t)*(1-t); }
    inline float inOutQuad(float t) { return t<.5f?2*t*t:1-std::pow(-2*t+2,2)/2; }
    inline float inCubic(float t)   { return t*t*t; }
    inline float outCubic(float t)  { return 1-std::pow(1-t,3); }
    inline float inOutCubic(float t){ return t<.5f?4*t*t*t:1-std::pow(-2*t+2,3)/2; }
    inline float outExpo(float t)   { return t==1?1:1-std::pow(2,-10*t); }
    inline float inOutExpo(float t) {
        if (t<=0) return 0; if (t>=1) return 1;
        return t<.5f ? std::pow(2,20*t-10)/2 : (2-std::pow(2,-20*t+10))/2;
    }
    inline float inBack(float t) {
        const float c1=1.70158f, c3=c1+1;
        return c3*t*t*t - c1*t*t;
    }
    inline float outBack(float t) {
        const float c1=1.70158f, c3=c1+1;
        return 1+c3*std::pow(t-1,3)+c1*std::pow(t-1,2);
    }
    inline float inOutBack(float t) {
        const float c1=1.70158f, c2=c1*1.525f;
        return t<.5f
            ? (std::pow(2*t,2)*((c2+1)*2*t-c2))/2
            : (std::pow(2*t-2,2)*((c2+1)*(2*t-2)+c2)+2)/2;
    }
    inline float outElastic(float t) {
        if (t<=0) return 0; if (t>=1) return 1;
        const float c4 = 2.f*3.14159265f/3.f;
        return std::pow(2,-10*t)*std::sin((t*10-0.75f)*c4)+1;
    }
    inline float outBounce(float t) {
        if (t<1/2.75f) return 7.5625f*t*t;
        if (t<2/2.75f) { t-=1.5f/2.75f; return 7.5625f*t*t+.75f; }
        if (t<2.5f/2.75f) { t-=2.25f/2.75f; return 7.5625f*t*t+.9375f; }
        t-=2.625f/2.75f; return 7.5625f*t*t+.984375f;
    }
}
using EasingFunc = float(*)(float);

// Base class
class Animation {
public:
    virtual ~Animation() = default;
    virtual void update(float dt) = 0;
    virtual bool isFinished() const = 0;
    virtual void reset() = 0;
    void stop()  { m_running = false; }
    void start() { m_running = true; }
    bool isRunning() const { return m_running; }
protected:
    bool m_running = false;
};

// Tween<T>
template<typename T>
class Tween : public Animation {
public:
    Tween(T* target, T from, T to, float dur, EasingFunc ease = Easing::linear)
        : m_target(target), m_from(from), m_to(to), m_dur(dur), m_ease(ease) {}

    void update(float dt) override {
        if (!m_running || isFinished() || !m_valid) return;
        if (m_delay > 0.f) {
            m_delay -= dt;
            if (m_delay > 0.f) return;
            dt = -m_delay;
            m_delay = 0.f;
        }
        m_elapsed += dt;
        float t = std::min(m_elapsed / m_dur, 1.f);
        *m_target = doLerp(m_from, m_to, m_ease(t));
        if (t >= 1.f && !m_completed) {
            m_completed = true;
            if (m_onComplete) m_onComplete();
        }
    }
    bool isFinished() const override { return (m_delay <= 0.f && m_elapsed >= m_dur) || !m_valid; }
    void reset() override { m_elapsed = 0; m_completed = false; if (m_valid) *m_target = m_from; }
    Tween& onComplete(VoidCallback cb) { m_onComplete = cb; return *this; }

    Tween& delay(float d) { m_delay = d; return *this; }

    /// Mark the tween's target pointer as invalid (owner destroyed).
    void invalidate() { m_valid = false; m_running = false; }

private:
    T* m_target; T m_from, m_to;
    float m_dur, m_elapsed = 0, m_delay = 0; EasingFunc m_ease;
    VoidCallback m_onComplete;
    bool m_valid = true;
    bool m_completed = false;
    static float doLerp(float a, float b, float t) { return a+(b-a)*t; }
    static Vec2 doLerp(const Vec2& a, const Vec2& b, float t) { return Vec2::lerp(a,b,t); }
    static Color doLerp(const Color& a, const Color& b, float t) { return Color::lerp(a,b,t); }
    static Rect doLerp(const Rect& a, const Rect& b, float t) { return Rect::lerp(a,b,t); }
};

class Delay : public Animation {
public:
    Delay(float seconds, VoidCallback cb) : m_remaining(seconds), m_cb(std::move(cb)) {}
    void update(float dt) override {
        if (!m_running || m_fired) return;
        m_remaining -= dt;
        if (m_remaining <= 0.f) { m_fired = true; if (m_cb) m_cb(); }
    }
    bool isFinished() const override { return m_fired; }
    void reset() override { m_fired = false; }
private:
    float m_remaining;
    VoidCallback m_cb;
    bool m_fired = false;
};

// AnimationManager (singleton)
class AnimationManager {
public:
    static AnimationManager& instance();
    void update(float dt);
    void add(std::shared_ptr<Animation> a);
    template<typename T>
    std::shared_ptr<Tween<T>> tween(T* target, T from, T to, float dur, EasingFunc e = Easing::linear) {
        auto t = std::make_shared<Tween<T>>(target, from, to, dur, e);
        t->start(); m_anims.push_back(t); return t;
    }
    std::shared_ptr<Delay> after(float seconds, VoidCallback cb) {
        auto d = std::make_shared<Delay>(seconds, std::move(cb));
        d->start(); m_anims.push_back(d); return d;
    }
    void clear();
private:
    AnimationManager() = default;
    std::vector<std::shared_ptr<Animation>> m_anims;
};

// AnimatedProperty<T>
template<typename T>
class AnimatedProperty {
public:
    AnimatedProperty(T init = T{}) : m_val(init), m_target(init) {}
    ~AnimatedProperty() { if (m_anim) std::static_pointer_cast<Tween<T>>(m_anim)->invalidate(); }

    // Non-copyable (raw pointer to m_val would be wrong in a copy)
    AnimatedProperty(const AnimatedProperty&) = delete;
    AnimatedProperty& operator=(const AnimatedProperty&) = delete;
    AnimatedProperty(AnimatedProperty&&) = default;
    AnimatedProperty& operator=(AnimatedProperty&&) = default;

    void setDuration(float d) { m_dur = d; }
    void setEasing(EasingFunc e) { m_ease = e; }

    void animateTo(T v) { set(v, m_dur, m_ease); }
    void set(T v, float dur = .3f, EasingFunc e = Easing::outCubic, float delay = 0.f) {
        if (m_anim) std::static_pointer_cast<Tween<T>>(m_anim)->invalidate();
        m_target = v;
        if (dur > 0) {
            auto tw = std::make_shared<Tween<T>>(&m_val, m_val, v, dur, e);
            if (delay > 0.f) tw->delay(delay);
            m_anim = tw;
            m_anim->start();
            AnimationManager::instance().add(m_anim);
        } else m_val = v;
    }
    void onComplete(VoidCallback cb) {
        if (m_anim) std::static_pointer_cast<Tween<T>>(m_anim)->onComplete(std::move(cb));
    }
    void setImmediate(T v) { if (m_anim) std::static_pointer_cast<Tween<T>>(m_anim)->invalidate(); m_val = m_target = v; }

    void update(float dt) { if (m_anim && m_anim->isRunning()) m_anim->update(dt); }
    T value()  const { return m_val; }
    T target() const { return m_target; }
    operator T() const { return m_val; }
private:
    T m_val, m_target;
    float m_dur = .3f;
    EasingFunc m_ease = Easing::outCubic;
    std::shared_ptr<Animation> m_anim;
};

using AnimatedFloat = AnimatedProperty<float>;
using AnimatedRect  = AnimatedProperty<Rect>;

} // namespace nxui
