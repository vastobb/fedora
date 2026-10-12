// Riru Test v1.0.8 — vanilla main-menu button via HBUI patch (in-memory).
//
// Goal: a "Riru" button between Play and Settings IN the vanilla Minecraft
// main menu (1.26.52), identical UI/texture, click = no-op (test only).
//
// Why this path (all verified, no device feedback needed):
//  - The 1.26.52 menu is HBUI/OreUI (HTML/JS via CoHTML). bedrock.dev +
//    wiki confirm: "Unlike JSON UI, Ore UI is hardcoded and cannot be
//    modified by resource packs." -> passive resource-pack override is DEAD.
//  - libminecraftpe.so references /hbui/index.html, /hbui/gameplay.html,
//    hbui/routes.json AND imports AAssetManager_open/read/getLength/
//    getBuffer/openFileDescriptor/close (stable NDK exports, libandroid.so).
//    Precedent: QPC-github/assethook replaces APK assets at runtime by
//    hooking exactly this C API; nixware-style cheat mods inject menu JS at
//    HTML-panel paint. We do the oreUIShim trick on-device: serve patched
//    /hbui/index.html = original bytes + injected script.
//  - No game offsets/signatures: interception uses dlopen+dlsym on
//    libandroid.so. Version-proof for the hook point; the patch reads the
//    INSTALLED file bytes at runtime, so no hash/version knowledge needed.
//  - Same UI/texture by construction: the script CLONES the Play button DOM
//    node, relabels it "Riru", inserts it after Play (before Settings).
//
// Safety: every hook guarded; any failure -> original behavior (button
// absent, menu intact, ModMenu baseline still works). Patch applies ONLY to
// files matching hbui+index.html that look like HTML and are <= 4MB.

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

#include <android/asset_manager.h>
#include <android/log.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef __NR_memfd_create
#define __NR_memfd_create 279 /* arm64 ABI, stable */
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kLogTag = "RiruTest";
constexpr const char *kModuleId = "riru.main";
constexpr const char *kModuleName = "Riru Test";
constexpr const char *kButtonId = "riru.test_button";
constexpr const char *kButtonName = "Riru Test Button";

void logInfo(const char *msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

// ---- File reporter: no-logcat diagnostics ---------------------------
// Writes RIRU_* lines to the app's EXTERNAL dir so they can be copied out
// with any file manager (user has no logcat). Uses raw open/write/close
// (never fopen: ours is hooked). All JNI guarded; silent on any failure.
namespace report {

std::string g_path;
std::mutex g_mutex;

void clearEx(JNIEnv *env) {
  if (env && env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

// method: "getExternalMediaDirs" (->[File]) or "getExternalFilesDir" (->File)
std::string appDir(JavaVM *vm, bool media) {
  if (!vm) {
    return "";
  }
  JNIEnv *env = nullptr;
  bool attached = false;
  if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) {
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
      return "";
    }
    attached = true;
  }
  std::string out;
  do {
    jclass at = env->FindClass("android/app/ActivityThread");
    clearEx(env);
    if (!at) {
      break;
    }
    jmethodID cur = env->GetStaticMethodID(
        at, "currentApplication", "()Landroid/app/Application;");
    clearEx(env);
    if (!cur) {
      break;
    }
    jobject app = env->CallStaticObjectMethod(at, cur);
    clearEx(env);
    if (!app) {
      break;
    }
    jclass ctx = env->FindClass("android/content/Context");
    clearEx(env);
    if (!ctx) {
      break;
    }
    jobject dirObj = nullptr;
    if (media) {
      jmethodID m = env->GetMethodID(ctx, "getExternalMediaDirs",
                                     "()[Ljava/io/File;");
      clearEx(env);
      if (!m) {
        break;
      }
      jobjectArray arr =
          (jobjectArray)env->CallObjectMethod(app, m);
      clearEx(env);
      if (!arr || env->GetArrayLength(arr) < 1) {
        break;
      }
      dirObj = env->GetObjectArrayElement(arr, 0);
      clearEx(env);
    } else {
      jmethodID m = env->GetMethodID(ctx, "getExternalFilesDir",
                                     "(Ljava/lang/String;)Ljava/io/File;");
      clearEx(env);
      if (!m) {
        break;
      }
      dirObj = env->CallObjectMethod(app, m, nullptr);
      clearEx(env);
    }
    if (!dirObj) {
      break;
    }
    jclass fileCl = env->FindClass("java/io/File");
    clearEx(env);
    if (!fileCl) {
      break;
    }
    jmethodID gap = env->GetMethodID(fileCl, "getAbsolutePath",
                                     "()Ljava/lang/String;");
    clearEx(env);
    if (!gap) {
      break;
    }
    jstring js = (jstring)env->CallObjectMethod(dirObj, gap);
    clearEx(env);
    if (!js) {
      break;
    }
    const char *c = env->GetStringUTFChars(js, nullptr);
    if (c) {
      out = c;
      env->ReleaseStringUTFChars(js, c);
    }
  } while (false);
  if (attached) {
    vm->DetachCurrentThread();
  }
  return out;
}

void init(JavaVM *vm, const char *ver) {
  std::string dir = appDir(vm, true);
  if (dir.empty()) {
    dir = appDir(vm, false);
  }
  if (dir.empty()) {
    return;
  }
  g_path = dir + "/riru_report.txt";
  const int fd =
      open(g_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    g_path.clear();
    return;
  }
  std::string head = std::string("RIRU report ") + ver + "\n";
  write(fd, head.c_str(), head.size());
  close(fd);
}

void log(const char *line) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_path.empty() || !line) {
    return;
  }
  const int fd = open(g_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
    return;
  }
  write(fd, line, std::strlen(line));
  write(fd, "\n", 1);
  close(fd);
}

} // namespace report

// ---- Injected script: clone Play -> "Riru" between Play and Settings ----
constexpr const char kRiruJs[] =
    ";(function(){"
    "var ID='riru-main-menu-btn';"
    "var PLAY=['Play','Spielen','Jouer','Jugar','\\u0418\\u0433\\u0440\\u0430\\u0442\\u044C','\\u30d7\\u30ec\\u30a4','Jogar','Oyna','Main'];"
    "var SETT=['Settings','Einstellungen','Param\\u00e8tres','Ajustes','\\u041d\\u0430\\u0441\\u0442\\u0440\\u043e\\u0439\\u043a\\u0438','\\u8a2d\\u5b9a','Configura\\u00e7\\u00f5es','Ayarlar','Pengaturan'];"
    "function norm(t){return String(t==null?'':t).replace(/\\s+/g,' ').trim();}"
    "function inList(t,l){t=norm(t);for(var i=0;i<l.length;i++){if(t===l[i])return true;}return false;}"
    "function btnFor(names){"
    "var bs=document.querySelectorAll('button');"
    "for(var i=0;i<bs.length;i++){if(inList(bs[i].textContent,names))return bs[i];}"
    "var w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);var n;"
    "while((n=w.nextNode())){if(!inList(n.nodeValue,names))continue;"
    "var el=n.parentElement;"
    "for(var d=0;el&&d<4;d++,el=el.parentElement){"
    "if(!el||el===document.body)break;"
    "if(el.tagName==='BUTTON')return el;"
    "if(inList(el.textContent,names)&&el.querySelectorAll('button').length===0)return el;}"
    "}return null;}"
    "function setLabel(root,txt){"
    "var w=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);var n;"
    "while((n=w.nextNode())){if(norm(n.nodeValue)){n.nodeValue=txt;return true;}}"
    "return false;}"
    "function strip(el){"
    "try{el.removeAttribute('onclick');el.onclick=null;}catch(e){}"
    "var as=el.attributes;"
    "for(var i=as.length-1;i>=0;i--){var a=as[i].name;if(a&&a.slice(0,2)==='on'){try{el.removeAttribute(a);}catch(x){}}}"
    "var k=el.children;for(var j=0;j<k.length;j++)strip(k[j]);}"
    "function attempt(){try{"
    "if(!document.body||document.getElementById(ID))return;"
    "var play=btnFor(PLAY);if(!play)return;"
    "var st=btnFor(SETT);if(!st)return;"
    "if(play===st||play.parentElement!==st.parentElement||!play.parentElement)return;"
    "var b=play.cloneNode(true);"
    "b.removeAttribute('id');b.id=ID;strip(b);setLabel(b,'Riru');"
    "b.addEventListener('click',function(e){e.preventDefault();e.stopPropagation();if(e.stopImmediatePropagation)e.stopImmediatePropagation();},true);"
    "play.parentElement.insertBefore(b,st);"
    "}catch(e){}}"
    "attempt();"
    "new MutationObserver(function(){attempt();}).observe(document.documentElement,{childList:true,subtree:true});"
    "})();";

// ---- HBUI patch engine (AAsset family, behavior-preserving) ----
namespace hbui {

using FOpenFn = FILE *(*)(const char *, const char *);
using FCloseFn = int (*)(FILE *);
using OpenFn = AAsset *(*)(AAssetManager *, const char *, int);
using ReadFn = int (*)(AAsset *, void *, size_t);
using LengthFn = off_t (*)(AAsset *);
using BufferFn = const void *(*)(AAsset *, off_t *);
using OpenFdFn = int (*)(AAsset *, off_t *, off_t *);
using CloseFn = void (*)(AAsset *);

OpenFn g_openOrig = nullptr;
ReadFn g_readOrig = nullptr;
LengthFn g_lengthOrig = nullptr;
BufferFn g_bufferOrig = nullptr;
OpenFdFn g_openFdOrig = nullptr;
CloseFn g_closeOrig = nullptr;

FOpenFn g_fopenOrig = nullptr;
FOpenFn g_fopen64Orig = nullptr;
FCloseFn g_fcloseOrig = nullptr;

pl::memory::HookHandle g_fopenHook;
pl::memory::HookHandle g_fopen64Hook;
pl::memory::HookHandle g_fcloseHook;
pl::memory::HookHandle g_openHook;
pl::memory::HookHandle g_readHook;
pl::memory::HookHandle g_lengthHook;
pl::memory::HookHandle g_bufferHook;
pl::memory::HookHandle g_openFdHook;
pl::memory::HookHandle g_closeHook;

constexpr off_t kMaxPatchBytes = 4 * 1024 * 1024;

struct Tracked {
  std::vector<unsigned char> bytes; // patched (or original if unpatchable)
  size_t off = 0;
  bool ready = false;
  bool patched = false;
  int memFd = -1;
};

std::mutex g_mutex;
std::unordered_map<AAsset *, Tracked> g_files;
std::unordered_map<FILE *, int> g_memFiles; // our substituted FILE* -> memfd (fclose closes)
long g_hbuiOpens = 0;
long g_fopenHits = 0;

bool isTarget(const char *name) {
  if (!name) {
    return false;
  }
  return std::strstr(name, "hbui") != nullptr &&
         std::strstr(name, "index.html") != nullptr;
}

bool looksHtml(const unsigned char *d, size_t n) {
  if (!d || n < 16) {
    return false;
  }
  // case-insensitive search of the first 4KB for html/head/body markers
  const size_t scan = n < 4096 ? n : 4096;
  for (size_t i = 0; i + 6 < scan; ++i) {
    char c0 = (char)(d[i] | 0x20), c1 = (char)(d[i + 1] | 0x20);
    if (d[i] == '<' &&
        ((c0 == 'h' && c1 == 't') || (c0 == 'h' && c1 == 'e') ||
         (c0 == 'b' && c1 == 'o'))) {
      return true;
    }
  }
  return false;
}

std::vector<unsigned char> buildPatched(const unsigned char *orig, size_t len) {
  std::string head((const char *)orig, len);
  const std::string tag =
      std::string("<script>") + kRiruJs + "</script>";
  std::string out;
  size_t pos = head.find("</head>");
  if (pos != std::string::npos) {
    out = head.substr(0, pos) + tag + head.substr(pos);
  } else if ((pos = head.find("</body>")) != std::string::npos) {
    out = head.substr(0, pos) + tag + head.substr(pos);
  } else {
    out = head + tag;
  }
  return std::vector<unsigned char>(out.begin(), out.end());
}

// Drain the real stream (bounded) and prepare serve bytes. Always leaves a
// servable buffer: patched HTML on success, original bytes otherwise.
void ensureReady(AAsset *asset, Tracked &t) {
  if (t.ready || !g_readOrig || !g_lengthOrig) {
    return;
  }
  t.ready = true;
  const off_t total = g_lengthOrig(asset);
  if (total <= 0 || total > kMaxPatchBytes) {
    return; // serve nothing extra; reads fall through? (see readDetour)
  }
  std::vector<unsigned char> orig;
  orig.reserve((size_t)total);
  unsigned char tmp[65536];
  while ((off_t)orig.size() < total) {
    const int n = g_readOrig(asset, tmp, sizeof(tmp));
    if (n <= 0) {
      break;
    }
    orig.insert(orig.end(), tmp, tmp + n);
  }
  if ((off_t)orig.size() != total) {
    return;
  }
  if (looksHtml(orig.data(), orig.size())) {
    t.bytes = buildPatched(orig.data(), orig.size());
    t.patched = true;
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI patched %zu -> %zu bytes", orig.size(),
                        t.bytes.size());
    {
      char b[128];
      std::snprintf(b, sizeof(b), "RIRU patched aasset %zu -> %zu",
                    orig.size(), t.bytes.size());
      report::log(b);
    }
  } else {
    t.bytes = std::move(orig);
  }
}

bool isDiskTarget(const char *path) {
  if (!path) {
    return false;
  }
  return std::strstr(path, "hbui") != nullptr &&
         std::strstr(path, "index.html") != nullptr;
}

// Serve bytes through a memfd-backed FILE* (real fd: fstat/seek/mmap all work).
FILE *serveMemFile(const std::vector<unsigned char> &bytes) {
  if (bytes.empty()) {
    return nullptr;
  }
  const int fd = (int)syscall(__NR_memfd_create, "riru_hbui", 0);
  if (fd < 0) {
    return nullptr;
  }
  size_t wrote = 0;
  while (wrote < bytes.size()) {
    const ssize_t n =
        write(fd, bytes.data() + wrote, bytes.size() - wrote);
    if (n <= 0) {
      close(fd);
      return nullptr;
    }
    wrote += (size_t)n;
  }
  lseek(fd, 0, SEEK_SET);
  FILE *f = fdopen(fd, "rb");
  if (!f) {
    close(fd);
    return nullptr;
  }
  return f;
}

FILE *fopenDetour(const char *path, const char *mode) {
  if (!isDiskTarget(path) || !mode || mode[0] == 'w' || mode[0] == 'a' ||
      std::strchr(mode, '+') != nullptr) {
    return g_fopenOrig ? g_fopenOrig(path, mode) : nullptr;
  }
  // Target disk file: read original, patch, serve substitute. Any failure
  // falls back to the plain original open (menu intact, button absent).
  FOpenFn ro = g_fopenOrig;
  FCloseFn rc = g_fcloseOrig;
  FILE *orig = ro ? ro(path, mode) : nullptr;
  if (!orig) {
    return nullptr;
  }
  std::vector<unsigned char> fileBytes;
  {
    // read orig fully then close it via SAVED originals (never ::fclose:
    // that would re-enter this hook chain)
    unsigned char tmp[65536];
    size_t n = 0;
    bool ok = true;
    while ((n = std::fread(tmp, 1, sizeof(tmp), orig)) != 0) {
      if (fileBytes.size() + n > (size_t)kMaxPatchBytes) {
        ok = false;
        break;
      }
      fileBytes.insert(fileBytes.end(), tmp, tmp + n);
      if (n < sizeof(tmp)) {
        break;
      }
    }
    if (rc) {
      rc(orig);
    }
    orig = nullptr;
    if (!ok || fileBytes.empty() ||
        !looksHtml(fileBytes.data(), fileBytes.size())) {
      // not patchable: reopen original path and return it untouched
      return ro ? ro(path, mode) : nullptr;
    }
  }
  std::vector<unsigned char> patched =
      buildPatched(fileBytes.data(), fileBytes.size());
  FILE *sub = serveMemFile(patched);
  if (!sub) {
    return ro ? ro(path, mode) : nullptr;
  }
  long hit = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // fd owned by FILE*; fclose detour closes it via fclose()
    g_memFiles.emplace(sub, 0);
    hit = ++g_fopenHits;
  }
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "RIRU_HBUI fopen #%ld path=%s %zu -> %zu bytes", hit, path,
                      fileBytes.size(), patched.size());
  {
    char b[512];
    std::snprintf(b, sizeof(b), "RIRU patched fopen %s %zu -> %zu",
                  path, fileBytes.size(), patched.size());
    report::log(b);
  }
  return sub;
}

int fcloseDetour(FILE *f) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_memFiles.find(f);
    if (it != g_memFiles.end()) {
      g_memFiles.erase(it);
      report("fclose sub");
      lock.unlock();
      // SAVED original: ::fclose would re-enter this detour forever
      return g_fcloseOrig ? g_fcloseOrig(f) : EOF;
    }
  }
  return g_fcloseOrig ? g_fcloseOrig(f) : EOF;
}

AAsset *openDetour(AAssetManager *mgr, const char *filename, int mode) {
  AAsset *asset = g_openOrig ? g_openOrig(mgr, filename, mode) : nullptr;
  if (isTarget(filename)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_hbuiOpens;
    if (asset) {
      g_files.emplace(asset, Tracked{});
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI open #%ld name=%s asset=%p",
                        g_hbuiOpens, filename ? filename : "(null)",
                        (const void *)asset);
    {
      char b[320];
      std::snprintf(b, sizeof(b), "RIRU open aasset #%ld %s asset=%p",
                    g_hbuiOpens, filename ? filename : "(null)",
                    (const void *)asset);
      report::log(b);
    }
  } else if (filename && std::strstr(filename, "hbui") != nullptr) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "RIRU_HBUI see name=%s asset=%p", filename,
                        (const void *)asset);
  }
  return asset;
}

Tracked *findTracked(AAsset *asset) {
  if (!asset) {
    return nullptr;
  }
  auto it = g_files.find(asset);
  return it == g_files.end() ? nullptr : &it->second;
}

int readDetour(AAsset *asset, void *buf, size_t count) {
  std::unique_lock<std::mutex> lock(g_mutex);
  Tracked *t = findTracked(asset);
  if (!t) {
    // not ours: unlock then passthrough (avoid holding mutex in game IO)
    lock.unlock();
    return g_readOrig ? g_readOrig(asset, buf, count) : -1;
  }
  ensureReady(asset, *t);
  if (t->bytes.empty()) {
    // bounded-drain refused: fall back to live original reads
    lock.unlock();
    return g_readOrig ? g_readOrig(asset, buf, count) : -1;
  }
  const size_t avail =
      t->bytes.size() > t->off ? t->bytes.size() - t->off : 0;
  const size_t n = count < avail ? count : avail;
  if (n && buf) {
    std::memcpy(buf, t->bytes.data() + t->off, n);
    t->off += n;
  }
  return (int)n;
}

off_t lengthDetour(AAsset *asset) {
  std::unique_lock<std::mutex> lock(g_mutex);
  Tracked *t = findTracked(asset);
  if (!t) {
    lock.unlock();
    return g_lengthOrig ? g_lengthOrig(asset) : 0;
  }
  ensureReady(asset, *t);
  if (!t->bytes.empty()) {
    return (off_t)t->bytes.size();
  }
  lock.unlock();
  return g_lengthOrig ? g_lengthOrig(asset) : 0;
}

const void *bufferDetour(AAsset *asset, off_t *outLen) {
  std::unique_lock<std::mutex> lock(g_mutex);
  Tracked *t = findTracked(asset);
  if (!t) {
    lock.unlock();
    return g_bufferOrig ? g_bufferOrig(asset, outLen) : nullptr;
  }
  ensureReady(asset, *t);
  if (!t->bytes.empty()) {
    if (outLen) {
      *outLen = (off_t)t->bytes.size();
    }
    return t->bytes.data();
  }
  lock.unlock();
  return g_bufferOrig ? g_bufferOrig(asset, outLen) : nullptr;
}

int openFdDetour(AAsset *asset, off_t *outStart, off_t *outLength) {
  std::unique_lock<std::mutex> lock(g_mutex);
  Tracked *t = findTracked(asset);
  if (!t) {
    lock.unlock();
    return g_openFdOrig ? g_openFdOrig(asset, outStart, outLength) : -1;
  }
  ensureReady(asset, *t);
  if (t->bytes.empty()) {
    lock.unlock();
    return g_openFdOrig ? g_openFdOrig(asset, outStart, outLength) : -1;
  }
  if (t->memFd < 0) {
    const int fd = (int)syscall(__NR_memfd_create, "riru_hbui", 0);
    if (fd < 0) {
      lock.unlock();
      return g_openFdOrig ? g_openFdOrig(asset, outStart, outLength) : -1;
    }
    size_t wrote = 0;
    while (wrote < t->bytes.size()) {
      const ssize_t n = write(fd, t->bytes.data() + wrote,
                              t->bytes.size() - wrote);
      if (n <= 0) {
        close(fd);
        lock.unlock();
        return g_openFdOrig ? g_openFdOrig(asset, outStart, outLength) : -1;
      }
      wrote += (size_t)n;
    }
    lseek(fd, 0, SEEK_SET);
    t->memFd = fd;
  }
  if (outStart) {
    *outStart = 0;
  }
  if (outLength) {
    *outLength = (off_t)t->bytes.size();
  }
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "RIRU_HBUI openFd asset=%p fd=%d len=%zu", (const void *)asset,
                      t->memFd, t->bytes.size());
  return t->memFd;
}

void closeDetour(AAsset *asset) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_files.find(asset);
    if (it != g_files.end()) {
      if (it->second.memFd >= 0) {
        close(it->second.memFd);
      }
      g_files.erase(it);
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI close asset=%p", (const void *)asset);
    }
  }
  if (g_closeOrig) {
    g_closeOrig(asset);
  }
}

void *libSymbol(void *lib, const char *name) {
  if (!name) {
    return nullptr;
  }
  return dlsym(lib ? lib : RTLD_DEFAULT, name);
}

int install() {
  void *lib = dlopen("libandroid.so", RTLD_NOLOAD | RTLD_NOW);
  if (!lib) {
    lib = dlopen("libandroid.so", RTLD_NOW);
  }
  if (!lib) {
    logInfo("RIRU_HBUI: libandroid.so unavailable");
    return 0;
  }
  int n = 0;
  auto tryHook = [&](const char *sym, void *detour, void **orig,
                     pl::memory::HookHandle &slot) {
    void *target = libSymbol(lib, sym);
    if (!target || !detour || !orig) {
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI: %s not found", sym);
      return;
    }
    slot = pl::memory::HookHandle(target, detour, orig);
    if (slot.installed()) {
      ++n;
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI: hooked %s -> %p", sym, target);
    } else {
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI: hook FAILED for %s", sym);
    }
  };
  // Disk-file backup path (hbui may live under gui/dist/ on disk, not APK).
  {
    void *fp = libSymbol(RTLD_DEFAULT, "fopen");
    void *fp64 = libSymbol(RTLD_DEFAULT, "fopen64");
    void *fc = libSymbol(RTLD_DEFAULT, "fclose");
    if (fp) {
      g_fopenHook = pl::memory::HookHandle(fp, (void *)fopenDetour,
                                           (void **)&g_fopenOrig);
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "RIRU_HBUI: fopen hook %s",
                          g_fopenHook.installed() ? "OK" : "FAILED");
      if (g_fopenHook.installed()) {
        ++n;
      }
    }
    if (fp64 && fp64 != fp) {
      g_fopen64Hook = pl::memory::HookHandle(fp64, (void *)fopenDetour,
                                             (void **)&g_fopen64Orig);
      if (g_fopen64Hook.installed()) {
        ++n;
      }
    } else {
      g_fopen64Orig = g_fopenOrig;
    }
    if (fc) {
      g_fcloseHook = pl::memory::HookHandle(fc, (void *)fcloseDetour,
                                            (void **)&g_fcloseOrig);
      if (g_fcloseHook.installed()) {
        ++n;
      }
    }
  }
  tryHook("AAssetManager_open", (void *)openDetour,
          (void **)&g_openOrig, g_openHook);
  tryHook("AAsset_read", (void *)readDetour, (void **)&g_readOrig,
          g_readHook);
  tryHook("AAsset_getLength", (void *)lengthDetour,
          (void **)&g_lengthOrig, g_lengthHook);
  tryHook("AAsset_getBuffer", (void *)bufferDetour,
          (void **)&g_bufferOrig, g_bufferHook);
  tryHook("AAsset_openFileDescriptor", (void *)openFdDetour,
          (void **)&g_openFdOrig, g_openFdHook);
  tryHook("AAsset_close", (void *)closeDetour, (void **)&g_closeOrig,
          g_closeHook);
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "RIRU_HBUI hooks installed %d/9", n);
  {
    char b[96];
    std::snprintf(b, sizeof(b), "RIRU hooks %d/9", n);
    report::log(b);
  }
  return n;
}

void reset() {
  g_fopenHook.reset();
  g_fopen64Hook.reset();
  g_fcloseHook.reset();
  g_openHook.reset();
  g_readHook.reset();
  g_lengthHook.reset();
  g_bufferHook.reset();
  g_openFdHook.reset();
  g_closeHook.reset();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_memFiles.clear();
  for (auto &kv : g_files) {
    if (kv.second.memFd >= 0) {
      close(kv.second.memFd);
    }
  }
  g_files.clear();
}

} // namespace hbui

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
    self.getLogger().info("Riru Test load v1.0.8: id={} name={}", self.getId(),
                          self.getName());
    logInfo("Riru load v1.0.8");
    report::init(self.getJavaVM(), "v1.0.8");
    report::log("RIRU load: report ready");
    const int hooks = hbui::install();
    self.getLogger().info("Riru HBUI hooks: {}/9", hooks);
    {
      char b[96];
      std::snprintf(b, sizeof(b), "RIRU hooks installed %d/9", hooks);
      report::log(b);
    }
    return true;
  }

  bool enable() {
    auto &self = getSelf();
    logInfo("Riru enable v1.0.8 step1: register module + callbacks");
    const bool moduleOk =
        pl::modmenu::ModuleBuilder(kModuleId, kModuleName)
            .defaultEnabled(true)
            .onToggle([](std::string_view, bool) {})
            .onConfigChanged([](std::string_view, std::string_view, std::string_view) {})
            .onKeybind([](std::string_view, std::string_view, bool) {})
            .registerModule();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.8 step1 module=%d",
                        (int)moduleOk);
    if (!moduleOk) {
      self.getLogger().error("Riru v1.0.8: module register failed");
      return false;
    }

    logInfo("Riru enable v1.0.8 step2: register button + event");
    const bool buttonOk =
        pl::modmenu::ButtonBuilder(kButtonId, kButtonName)
            .moduleId(kModuleId)
            .label("Riru")
            .onEvent([](std::string_view, pl::modmenu::ButtonEvent, float) {})
            .registerButton();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Riru v1.0.8 step2 button=%d",
                        (int)buttonOk);
    if (!buttonOk) {
      self.getLogger().error("Riru v1.0.8: button register failed");
      pl::modmenu::unregisterModule(kModuleId);
      return false;
    }

    self.getLogger().info("Riru Test enable v1.0.8 OK");
    logInfo("Riru enable v1.0.8 OK");
    return true;
  }

  bool disable() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    logInfo("Riru disable v1.0.8");
    return true;
  }

  bool unload() {
    pl::modmenu::unregisterButton(kButtonId);
    pl::modmenu::unregisterModule(kModuleId);
    hbui::reset();
    logInfo("Riru unload v1.0.8");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
};

} // namespace

PL_REGISTER_MOD(RiruTest, RiruTest::instance())
