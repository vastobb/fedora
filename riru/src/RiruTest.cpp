// Riru Test v1.0.2 — LeviLaunchroid native test mod.
//
// What it does:
//  1. Registers a "Riru Test" module + "Riru" button via ModMenu (safe baseline,
//     needs no game addresses; built with ANDROID_STL=c++_shared, see docs/CRASH_FIX.md).
//  2. Probes 7 BedrockTools-derived byte signatures against the loaded
//     libminecraftpe.so and LOGS the resolved addresses (test-before-use).
//     It installs NO game hooks yet. Send the logcat lines to advance to hooks.
//
// How Levi expects a .so mod (verified):
//  - Package dir <modsDir>/<modId>/ with manifest.json {type:"preload-native",
//    entry:"libRiru.so"}; dir name == mod id.
//  - libpreloader.so dlopens the cached copy, finds PLGetModRegistration
//    (emitted by PL_REGISTER_MOD below), drives load()/enable()/disable()/unload().
//  - Hooking mechanism (preloader-android): GlossHook-based inline hooks via
//    pl::memory::hook(target, detour, &original) + byte-pattern search via
//    pl::memory::resolveSignature(pat, "libminecraftpe.so"). BedrockTools
//    (Unknown3111/BedrockTools) wraps exactly these two APIs; riru copies that
//    methodology with patterns re-tested against 1.26.52 (see docs/HOOKING.md).
//
// Verified against:
// - LiteLDev/LeviLaunchroid docs/api/mod.md + mod-menu.md + full-cpp-mod example
// - preloader-android 0.2.3 (Mod.hpp, ModMenu.hpp, memory/Hook.hpp, memory/Signature.hpp)
// - Unknown3111/BedrockTools Signatures.cpp patterns, re-scanned with python
//   against /emulated/libminecraftpe.so (1.26.52, SHA256 5f5baf8e...): 7/8 match.

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Signature.hpp>

#include <android/log.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Real hooks stay OFF until probed addresses are confirmed from device logcat.
// v1.0.2 = probe + log only (user rule: find correct from lib, test before using).
#ifndef RIRU_USE_HOOKS
#define RIRU_USE_HOOKS 0
#endif

namespace {

constexpr const char *kLogTag = "RiruTest";
constexpr const char *kModuleId = "riru.main";
constexpr const char *kModuleName = "Riru Test";
constexpr const char *kButtonId = "riru.test_button";
constexpr const char *kButtonName = "Riru Test Button";
constexpr const char *kTargetLib = "libminecraftpe.so";

void logInfo(const char *msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

// BedrockTools-derived patterns re-tested (python scan) against 1.26.52 lib:
// .text-relative first-match offsets observed locally:
//   NormalTick 0x46018f8, VersionString 0x1c49ba4, ScreenViewRender 0x35c34a0,
//   DrawText 0x4d8a3fc, ContainerOpen 0xb1de68, ChatOpen 0x3548b68, GetFov 0x4ddf504.
// ClientInstanceUpdate did NOT match 1.26.52 -> omitted (needs Ghidra re-find).
struct SigProbe {
  const char *name;
  const char *pattern;
};

constexpr SigProbe kSigProbes[] = {
    {"NormalTick",
     "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 "
     "F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9 "
     "A8 03 1E F8 08 C0 78 39"},
    {"VersionString",
     "FF 03 01 D1 FD 7B 02 A9 F4 4F 03 A9 FD 83 00 91 54 D0 3B D5 F3 03 08 AA "
     "88 16 40 F9 A8 83 1F F8 E8 03 00 91"},
    {"ScreenViewRender",
     "EC 0F 17 FC EB 2B 01 6D E9 23 02 6D FD 7B 03 A9 FC 6F 04 A9 FA 67 05 A9 "
     "F8 5F 06 A9 F6 57 07 A9 F4 4F 08 A9 FD C3 00 91 FF 03 09 D1 48 D0 3B D5 "
     "FC 03 00 AA E8 0F 00 F9 08 15 40 F9 A8 83 1B F8"},
    {"DrawText",
     "FF C3 00 D1 FD 7B 01 A9 F3 13 00 F9 FD 43 00 91 09 A8 48 A9 E8 03 05 2A "
     "F3 03 00 AA 3F 01 0A EB"},
    {"ContainerOpen",
     "FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 F3 03 00 AA"},
    {"ChatOpen",
     "FF C3 02 D1 FD 7B 08 A9 F6 57 09 A9 F4 4F 0A A9 FD 03 02 91 56 D0 3B D5 "
     "F3 03 00 AA C8 16 40 F9 A8 83 1F F8 14 6C 44 F9"},
    {"GetFov",
     "EA 0F 1C FC E9 A3 00 6D FD FB 01 A9 F5 17 00 F9 F4 4F 03 A9 FD 63 00 91 "
     "08 40 20 1E F4 03 01 2A F3 03 00 AA"},
};

#if RIRU_USE_HOOKS
#include <pl/memory/Hook.hpp>
// Future: hooks get installed here only after probe addresses are confirmed
// from this device's logcat (see probeSignatures output) + version gate.
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
    }
    self.getLogger().info("Riru Test load: id={} name={}", self.getId(), self.getName());
    logInfo("Riru load v1.0.2");
    probeSignatures(self);
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

    // Baseline test button (Levi overlay, draggable). The true Minecraft
    // main-menu button (between Play and Settings) is tracked in
    // docs/MAIN_MENU_BUTTON.md and needs the HBUI files from 1.26.52 first.
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
    return true;
  }

  bool disable() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    getSelf().getLogger().info("Riru Test disable");
    return true;
  }

  bool unload() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    getSelf().getLogger().info("Riru Test unload");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;

  // Resolve-only probe: proves offsets on THIS device's lib, installs nothing.
  // Read back with: adb logcat -s RiruTest | grep RIRU_SIG
  void probeSignatures(ll::mod::NativeMod &self) {
    std::vector<std::string> patterns;
    patterns.reserve(sizeof(kSigProbes) / sizeof(kSigProbes[0]));
    for (const auto &p : kSigProbes) {
      patterns.emplace_back(p.pattern);
    }
    auto resolved = pl::memory::resolveSignatures(patterns, kTargetLib);
    for (std::size_t i = 0; i < patterns.size(); ++i) {
      std::uintptr_t addr = 0;
      const auto it = resolved.find(patterns[i]);
      if (it != resolved.end()) {
        addr = it->second;
      }
      char line[160];
      std::snprintf(line, sizeof(line), "RIRU_SIG %s = 0x%zx %s",
                    kSigProbes[i].name, addr, addr ? "FOUND" : "NOT_FOUND");
      __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line);
      self.getLogger().info("Riru sig {} -> {:x} {}", kSigProbes[i].name, addr,
                            addr ? "FOUND" : "NOT_FOUND");
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_SIG done: no hooks installed (probe only)");
  }

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
};

} // namespace

PL_REGISTER_MOD(RiruTest, RiruTest::instance())
