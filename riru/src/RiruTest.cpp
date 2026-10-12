// Riru Test — LeviLaunchroid native test mod.
//
// Does nothing except add a "Riru Test" module + "Riru" button via ModMenu.
// Offset-free by default (RIRU_USE_HOOKS 0). Optional true-StartScreen hook
// scaffold is gated and must only be enabled with Actions-verified offsets
// from offsets.json (see reverse.yml). Untested addresses must never ship.
//
// Verified against:
// - LiteLDev/LeviLaunchroid docs/api/mod.md + docs/api/mod-menu.md
// - examples/full-cpp-mod/src/FullCppMod.cpp
// - preloader-android 0.2.3 include/pl/Mod.hpp + ModMenu.hpp

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <android/log.h>

#include <filesystem>
#include <string_view>

// Set to 1 only after reverse.yml gives a TESTED signature+offset for your
// exact libminecraftpe.so SHA256, and you have validated it in Actions.
// Default 0 = ModMenu-only test button (safe, no game addresses used).
#ifndef RIRU_USE_HOOKS
#define RIRU_USE_HOOKS 0
#endif

namespace {

constexpr const char *kLogTag = "RiruTest";
constexpr const char *kModuleId = "riru.main";
constexpr const char *kModuleName = "Riru Test";
constexpr const char *kButtonId = "riru.test_button";
constexpr const char *kButtonName = "Riru Test Button";

void logInfo(const char *msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

#if RIRU_USE_HOOKS
// ---- Optional hook scaffold (OFF by default) ----
// Fill these ONLY from .github/workflows/reverse.yml artifact offsets.json
// with status == "verified" for your lib SHA256 + minecraft version.
// Example (DO NOT USE without verification):
//   constexpr std::string_view kStartScreenSignature = "48 8B ?? ?? 89 ...";
//   constexpr std::string_view kTargetModule = "libminecraftpe.so";
// Handling: resolveSignature() -> 0 means "not found, stay in ModMenu-only
// mode". Never call a 0 address. Test before using them (null-check +
// version-gate + try in Actions emulator/host validator first).
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>
#endif

class RiruTest {
public:
  static RiruTest &instance() {
    static RiruTest mod;
    return mod;
  }

  RiruTest() : mSelf(*ll::mod::NativeMod::current()) {}

  [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

  bool load() {
    auto &self = getSelf();
    std::error_code ec;
    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
      self.getLogger().error("Riru: failed to create config dir: {}", ec.message());
      // Non-fatal for a test mod; still return true so enable() runs.
    }
    self.getLogger().info("Riru Test load: id={} name={}", self.getId(), self.getName());
    logInfo("Riru load");
    return true;
  }

  bool enable() {
    auto &self = getSelf();

    const bool moduleOk =
        pl::modmenu::ModuleBuilder(kModuleId, kModuleName)
            .modId(self.getId())
            .description("Riru test module. Does nothing except add a button.")
            .defaultEnabled(true)
            .onToggle(onToggle)
            .registerModule();

    if (!moduleOk) {
      self.getLogger().error("Riru: ModuleBuilder failed for {}", kModuleId);
      return false;
    }

    // The test button: visible floating button labeled "Riru".
    // Appears via launcher ModMenu overlay / in-game HUD layer, including
    // while the Minecraft main menu (StartScreen/PlayScreen) is shown.
    // onEvent does nothing except log (testing purpose only).
    const bool buttonOk =
        pl::modmenu::ButtonBuilder(kButtonId, kButtonName)
            .moduleId(kModuleId)
            .modId(self.getId())
            .label("Riru")
            .behavior(pl::modmenu::ButtonBehavior::Click)
            .onEvent(onButtonEvent)
            .registerButton();

    if (!buttonOk) {
      self.getLogger().error("Riru: ButtonBuilder failed for {}", kButtonId);
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    self.getLogger().info("Riru Test enable: module={} button={} registered", kModuleId, kButtonId);
    logInfo("Riru enable: Riru Test button added");

#if RIRU_USE_HOOKS
    if (!installHooks(self)) {
      self.getLogger().error("Riru: hook install failed, staying ModMenu-only");
      // Do not fail enable() for a test mod; ModMenu button still works.
    }
#endif
    return true;
  }

  bool disable() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
#if RIRU_USE_HOOKS
    resetHooks();
#endif
    getSelf().getLogger().info("Riru Test disable");
    return true;
  }

  bool unload() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
#if RIRU_USE_HOOKS
    resetHooks();
#endif
    getSelf().getLogger().info("Riru Test unload");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;

#if RIRU_USE_HOOKS
  pl::memory::HookHandle mHook;
#endif

  static void onToggle(std::string_view moduleId, bool enabled) {
    instance().handleToggle(moduleId, enabled);
  }

  static void onButtonEvent(std::string_view buttonId, pl::modmenu::ButtonEvent event, float value) {
    instance().handleButton(buttonId, event, value);
  }

  void handleToggle(std::string_view moduleId, bool enabled) {
    (void)moduleId;
    getSelf().getLogger().info("Riru module {} {}", moduleId, enabled ? "enabled" : "disabled");
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "toggle %.*s = %s",
                        (int)moduleId.size(), moduleId.data(), enabled ? "true" : "false");
  }

  void handleButton(std::string_view buttonId, pl::modmenu::ButtonEvent event, float value) {
    // Intentionally does nothing except log — testing purpose only.
    (void)value;
    getSelf().getLogger().info("Riru button event: {} {}", buttonId, (int)event);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "button %.*s event %d value %.2f",
                        (int)buttonId.size(), buttonId.data(), (int)event, (double)value);
  }

#if RIRU_USE_HOOKS
  bool installHooks(ll::mod::NativeMod &self) {
    // TEMPLATE ONLY — replace signature + logic with reverse.yml verified data.
    // Steps required before use:
    //  1. reverse.yml -> offsets.json entry status==verified for this SHA256.
    //  2. Copy pattern + expected RVA here, keep version-gate.
    //  3. Build with -DRIRU_USE_HOOKS=1 in Actions, run validator job.
    //  4. If resolve returns 0 or hook() != 0, abort hook, keep ModMenu-only.
    (void)self;
    // Example (placeholder, NOT a real StartScreen address):
    // uintptr_t target = pl::memory::resolveSignature("48 8B ?? ?? 89", "libminecraftpe.so");
    // if (target == 0) return false;
    // ... install HookHandle with exact signature match ...
    return false; // default: no hook installed
  }

  void resetHooks() { mHook.reset(); }
#endif
};

} // namespace

PL_REGISTER_MOD(RiruTest, RiruTest::instance())
