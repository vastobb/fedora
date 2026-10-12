// Riru Test v1.0.4 — bisection result applied.
//
// v1.0.0/v1.0.1 crashed deterministically in
//   pl::modmenu::registerModule (SIGSEGV fault 0x10, tombstones 2026-10-12,
//   POCO F7, launcher 1.5.27, mod Riru(1)/libRiru(1).so).
// Shared-STL rebuild did NOT fix it, and preloader headers 0.2.3 == device
// submodule 5d108da for ModMenu, so this build bisects: enable() makes the
// SMALLEST legal ModMenu calls (no modId — owner fills it like BedrockTools,
// no description, no onToggle, no configs; button with no behavior/event
// overrides) with a log line before/after each call. The next tombstone (if
// any) will pinpoint the exact call. Signature probing is compiled in but NOT
// called yet (one variable at a time).
//
// Packaging (v1.0.3): .levipack is a ZIP with manifest.json + libRiru.so at
// ROOT — Levi FileHandler.isValidZipModPackage reads ZipInputStream and
// rejects TARs ("Invalid ZIP Mod Package", seen 2026-10-12).

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <android/log.h>

#include <filesystem>

namespace {

constexpr const char *kLogTag = "RiruTest";
constexpr const char *kModuleId = "riru.main";
constexpr const char *kModuleName = "Riru Test";
constexpr const char *kButtonId = "riru.test_button";
constexpr const char *kButtonName = "Riru Test Button";

void logInfo(const char *msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

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
    self.getLogger().info("Riru Test load v1.0.4: id={} name={}", self.getId(),
                          self.getName());
    logInfo("Riru load v1.0.4");
    return true;
  }

  bool enable() {
    auto &self = getSelf();
    logInfo("Riru enable v1.0.4 step1: register module + callbacks");
    // v1.0.4: all three callbacks set to no-ops. Device libpreloader.so
    // registerModule unconditionally clones the std::functions (faulting
    // LDR x9,[x9,#0x10] at +1484 when the manager ptr is null = empty).
    // BedrockTools always sets all three and never crashes; v1.0.0-v1.0.3
    // left onConfigChanged/onKeybind (and in v1.0.3 onToggle too) empty.
    const bool moduleOk =
        pl::modmenu::ModuleBuilder(kModuleId, kModuleName)
            .defaultEnabled(true)
            .onToggle([](std::string_view, bool) {})
            .onConfigChanged([](std::string_view, std::string_view, std::string_view) {})
            .onKeybind([](std::string_view, std::string_view, bool) {})
            .registerModule();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.4 step1 module=%d",
                        (int)moduleOk);
    if (!moduleOk) {
      self.getLogger().error("Riru v1.0.3: minimal module register failed");
      return false;
    }

    logInfo("Riru enable v1.0.4 step2: register button + event");
    const bool buttonOk =
        pl::modmenu::ButtonBuilder(kButtonId, kButtonName)
            .moduleId(kModuleId)
            .label("Riru")
            .onEvent([](std::string_view, pl::modmenu::ButtonEvent, float) {})
            .registerButton();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.4 step2 button=%d",
                        (int)buttonOk);
    if (!buttonOk) {
      self.getLogger().error("Riru v1.0.3: minimal button register failed");
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    self.getLogger().info("Riru Test enable v1.0.4 OK");
    logInfo("Riru enable v1.0.4 OK");
    return true;
  }

  bool disable() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    logInfo("Riru disable v1.0.4");
    return true;
  }

  bool unload() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    logInfo("Riru unload v1.0.4");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
};

} // namespace

PL_REGISTER_MOD(RiruTest, RiruTest::instance())
