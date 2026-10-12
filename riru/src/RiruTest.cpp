// Riru Test v1.0.5 — HBUI asset probe (log-only, no patching).
//
// Goal: put a "Riru" button between Play and Settings IN the vanilla main
// menu with identical UI/texture. The 1.26.52 menu is HBUI (HTML/JS via
// CoHTML): lib paths confirmed in libminecraftpe.so .rodata:
//   /hbui/index.html, /hbui/gameplay.html, hbui/routes.json.
// The game imports AAssetManager_open/read/getLength/close (verified in
// .dynstr), i.e. HBUI files load through Android assets. Those are STABLE
// NDK exports from libandroid.so — resolvable with dlopen+dlsym, NO game
// signatures needed.
//
// v1.0.5 installs hooks on the 4 AAsset functions and LOGS every asset open
// whose name contains "hbui" (plus read sizes). It changes NO bytes.
// Read back: adb logcat -s RiruTest | grep RIRU_HBUI
// v1.0.6 (next) will serve patched /hbui/index.html (original bytes + an
// injected script that clones the Play button node into a "Riru" button
// between Play and Settings) — only after the probe proves the path.
//
// ModMenu baseline from v1.0.4 kept as-is (proven crash-free: preloader
// 5d108da headers + all callbacks set + c++_shared + ZIP levipack).

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

#include <android/asset_manager.h>
#include <android/log.h>

#include <dlfcn.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

namespace {

constexpr const char *kLogTag = "RiruTest";
constexpr const char *kModuleId = "riru.main";
constexpr const char *kModuleName = "Riru Test";
constexpr const char *kButtonId = "riru.test_button";
constexpr const char *kButtonName = "Riru Test Button";

void logInfo(const char *msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

// ---- HBUI asset probe (stable libandroid.so hooks, log-only) ----
namespace assetprobe {

using OpenFn = AAsset *(*)(AAssetManager *, const char *, int);
using ReadFn = int (*)(AAsset *, void *, size_t);
using LengthFn = off_t (*)(AAsset *);
using CloseFn = void (*)(AAsset *);

OpenFn g_openOrig = nullptr;
ReadFn g_readOrig = nullptr;
LengthFn g_lengthOrig = nullptr;
CloseFn g_closeOrig = nullptr;

pl::memory::HookHandle g_openHook;
pl::memory::HookHandle g_readHook;
pl::memory::HookHandle g_lengthHook;
pl::memory::HookHandle g_closeHook;

std::mutex g_mutex;
std::unordered_set<AAsset *> g_tracked;
long g_openCount = 0;

bool isInteresting(const char *name) {
  if (!name) {
    return false;
  }
  return std::strstr(name, "hbui") != nullptr ||
         std::strstr(name, "routes.json") != nullptr ||
         std::strstr(name, "index.html") != nullptr;
}

AAsset *openDetour(AAssetManager *mgr, const char *filename, int mode) {
  AAsset *asset = g_openOrig ? g_openOrig(mgr, filename, mode) : nullptr;
  if (isInteresting(filename)) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      ++g_openCount;
      if (asset) {
        g_tracked.insert(asset);
      }
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI open name=%s asset=%p",
                        filename ? filename : "(null)",
                        (const void *)asset);
  }
  return asset;
}

int readDetour(AAsset *asset, void *buf, size_t count) {
  int n = g_readOrig ? g_readOrig(asset, buf, count) : -1;
  bool tracked = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tracked = asset && g_tracked.count(asset) != 0;
  }
  if (tracked) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI read asset=%p req=%zu got=%d",
                        (const void *)asset, count, n);
  }
  return n;
}

off_t lengthDetour(AAsset *asset) {
  off_t len = g_lengthOrig ? g_lengthOrig(asset) : 0;
  bool tracked = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tracked = asset && g_tracked.count(asset) != 0;
  }
  if (tracked) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI length asset=%p len=%ld", (const void *)asset,
                        (long)len);
  }
  return len;
}

void closeDetour(AAsset *asset) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (asset && g_tracked.erase(asset) != 0) {
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI close asset=%p", (const void *)asset);
    }
  }
  if (g_closeOrig) {
    g_closeOrig(asset);
  }
}

void *libSymbol(void *lib, const char *name) {
  if (!lib || !name) {
    return nullptr;
  }
  return dlsym(lib, name);
}

// Returns number of hooks installed (0-4). Never crashes: every step guarded.
int install() {
  void *lib = dlopen("libandroid.so", RTLD_NOLOAD | RTLD_NOW);
  if (!lib) {
    lib = dlopen("libandroid.so", RTLD_NOW);
  }
  if (!lib) {
    logInfo("RIRU_HBUI probe: libandroid.so unavailable");
    return 0;
  }
  int n = 0;
  auto tryHook = [&](const char *sym, void *detour, void **orig,
                     pl::memory::HookHandle &slot) {
    void *target = libSymbol(lib, sym);
    if (!target || !detour || !orig) {
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI probe: %s not found", sym);
      return;
    }
    slot = pl::memory::HookHandle(target, detour, orig);
    if (slot.installed()) {
      ++n;
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI probe: hooked %s -> %p", sym, target);
    } else {
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI probe: hook FAILED for %s", sym);
    }
  };
  tryHook("AAssetManager_open", (void *)openDetour,
          (void **)&g_openOrig, g_openHook);
  tryHook("AAsset_read", (void *)readDetour, (void **)&g_readOrig,
          g_readHook);
  tryHook("AAsset_getLength", (void *)lengthDetour,
          (void **)&g_lengthOrig, g_lengthHook);
  tryHook("AAsset_close", (void *)closeDetour, (void **)&g_closeOrig,
          g_closeHook);
  // NOTE: dlclose skipped on purpose — hooks reference this library's
  // addresses for the rest of the process lifetime.
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "RIRU_HBUI probe installed %d/4 (log-only)", n);
  return n;
}

void reset() {
  g_openHook.reset();
  g_readHook.reset();
  g_lengthHook.reset();
  g_closeHook.reset();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_tracked.clear();
}

} // namespace assetprobe

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
    self.getLogger().info("Riru Test load v1.0.5: id={} name={}", self.getId(),
                          self.getName());
    logInfo("Riru load v1.0.5");
    const int hooks = assetprobe::install();
    self.getLogger().info("Riru HBUI probe: {}/4 hooks", hooks);
    return true;
  }

  bool enable() {
    auto &self = getSelf();
    logInfo("Riru enable v1.0.5 step1: register module + callbacks");
    const bool moduleOk =
        pl::modmenu::ModuleBuilder(kModuleId, kModuleName)
            .defaultEnabled(true)
            .onToggle([](std::string_view, bool) {})
            .onConfigChanged([](std::string_view, std::string_view, std::string_view) {})
            .onKeybind([](std::string_view, std::string_view, bool) {})
            .registerModule();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.5 step1 module=%d",
                        (int)moduleOk);
    if (!moduleOk) {
      self.getLogger().error("Riru v1.0.5: module register failed");
      return false;
    }

    logInfo("Riru enable v1.0.5 step2: register button + event");
    const bool buttonOk =
        pl::modmenu::ButtonBuilder(kButtonId, kButtonName)
            .moduleId(kModuleId)
            .label("Riru")
            .onEvent([](std::string_view, pl::modmenu::ButtonEvent, float) {})
            .registerButton();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.5 step2 button=%d",
                        (int)buttonOk);
    if (!buttonOk) {
      self.getLogger().error("Riru v1.0.5: button register failed");
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    self.getLogger().info("Riru Test enable v1.0.5 OK");
    logInfo("Riru enable v1.0.5 OK");
    return true;
  }

  bool disable() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    logInfo("Riru disable v1.0.5");
    return true;
  }

  bool unload() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    assetprobe::reset();
    logInfo("Riru unload v1.0.5");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
};

} // namespace

PL_REGISTER_MOD(RiruTest, RiruTest::instance())
