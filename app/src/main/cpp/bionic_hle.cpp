// Host implementations of the 32-bit bionic libc/libm/liblog imports.
//
// Guest memory is host memory (base + guest address), so string and file
// functions call the host libc on translated pointers. Structures whose layout
// differs between 32-bit bionic and the host (stat, dirent, tm, FILE, timeval
// is fine) are converted explicitly. Hot pure functions (memcpy, malloc...) are
// not here: they run as guest code from native/fastlibc.c.
#include <algorithm>
#include <cctype>
#include <climits>
#include <cerrno>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <set>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include "engine.h"
#include "fastlibc_blob.h"
#include "log.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace nin {
namespace {

// bionic (OpenBSD) ctype flags
constexpr uint8_t kU = 1, kL = 2, kN = 4, kS = 8, kP = 16, kC = 32, kX = 64, kB = 128;
constexpr uint32_t kFileSize = 84;  // sizeof(FILE) on 32-bit bionic

// 32-bit ARM Linux open(2) flags that differ between architectures.
constexpr uint32_t kArmODirectory = 040000, kArmONofollow = 0100000, kArmODirect = 0200000,
                   kArmOLargefile = 0400000;

struct Mapping {
  uint32_t addr, length;
  bool file;
};

struct Bionic {
  Engine& e;
  Guest& g;
  uint32_t errnoAddr = 0;
  std::map<uint32_t, FILE*> files;  // guest FILE* -> host FILE*
  std::map<uint32_t, int> stdio;    // __sF entries -> 0/1/2
  std::map<uint32_t, DIR*> dirs;    // guest DIR* handle -> host DIR*
  std::set<int> fds;                // open(2) descriptors not yet closed
  std::map<uint32_t, uint32_t> tls;
  uint32_t nextKey = 1;
  uint32_t mmapTop = kMmap;
  std::vector<Mapping> maps;
  uint32_t localeC = 0, tzName = 0, tmBuffer = 0;
  std::map<int, uint32_t> strerrors;
  std::string lineBuffer[3];
  bool tracing = false;

  explicit Bionic(Engine& engine) : e(engine), g(engine.guest()) {}

  void setErrno(int v) { g.w32(errnoAddr, uint32_t(v)); }
  void hostErrno() { setErrno(errno); }

  // Engine diagnostics that nothing reads back while typing: the crash
  // breadcrumb it rewrites several times per frame and its rolling memory log
  // (written to a temporary file and renamed, every few frames). Writes go to
  // /dev/null and the files never exist otherwise, which keeps this I/O off the
  // device's storage. Learning, dictionary and settings files are untouched.
  static bool diagnosticFile(const std::string& p) {
    for (const char* name : {"/memtracklatest.trackey", "/shinglogs/mninmem.logs", "/shinglogs/mninmem.logs.temp"}) {
      size_t n = std::strlen(name);
      if (p.size() >= n && p.compare(p.size() - n, n, name) == 0) return true;
    }
    return false;
  }
  static constexpr const char* kHidden = "/nonexistent/nin-shim-hidden";

  // /proc/<pid>/maps as a 32-bit ARM kernel prints it, for the engine's
  // CallStack::dump (its assertion handler): it looks up each return address
  // in the executable mappings and does not handle a missing file.
  static bool procMaps(const std::string& p) {
    if (p.rfind("/proc/", 0) != 0 || p.size() < 11 || p.compare(p.size() - 5, 5, "/maps") != 0) return false;
    std::string pid = p.substr(6, p.size() - 11);
    return pid == "self" || (!pid.empty() && pid.find_first_not_of("0123456789") == std::string::npos);
  }
  std::string mapsText;
  const std::string& procMapsText() {
    if (mapsText.empty()) {
      auto line = [&](uint32_t lo, uint32_t hi, const char* name) {
        char buf[160];
        int n = std::snprintf(buf, sizeof buf, "%08x-%08x r-xp 00000000 00:00 0", lo, hi);
        mapsText += buf + std::string(size_t(std::max(1, 49 - n)), ' ') + name + "\n";  // name at column 49
      };
      line(kImageBase, g.imageEnd(), "/data/app/com.jormy.nin/lib/arm/libgl2jni.so");
      line(kStubs, kStubs + 0x10000, "[anon:nin-shim imports]");
      line(kNative, kNative + 0x10000, "[anon:nin-shim native]");
      line(kKuser, kKuser + 0x1000, "[vectors]");
    }
    return mapsText;
  }

  // Host path for a guest path. `writing`: opened for writing (fopen/open).
  std::string path(uint32_t guest_path, bool writing = false) {
    std::string p = g.str(guest_path);
    static const bool traceFiles = std::getenv("NIN_TRACE_FILES") != nullptr;
    if (traceFiles && tracing) NIN_LOGI("file: %s (from %s)", p.c_str(), g.symbolize(g.reg(kLR)).c_str());
    // The engine reads /proc for memory accounting and crash reports; this
    // 64-bit host process's values do not fit its 32-bit parsing, so /proc is
    // hidden (as in the validated desktop harness).
    if (p.rfind("/proc/", 0) == 0) return kHidden;
    if (diagnosticFile(p)) return writing ? "/dev/null" : kHidden;
    const std::string& root = e.platform().sandbox_root;
    if (root.empty()) return p;
    if (p.rfind("/dev/urandom", 0) == 0 || p.rfind("/dev/random", 0) == 0) return p;
    if (p.rfind("/proc/", 0) == 0) return p;
    if (p.empty() || p[0] != '/') p = "/" + p;
    return root + p;
  }

  timeval now() {
    timeval tv{};
    if (e.platform().virtual_clock) {
      double t = e.platform().virtual_now;
      tv.tv_sec = time_t(t);
      tv.tv_usec = suseconds_t(std::llround((t - double(tv.tv_sec)) * 1e6));
      if (tv.tv_usec >= 1000000) tv.tv_sec++, tv.tv_usec -= 1000000;
    } else {
      gettimeofday(&tv, nullptr);
    }
    return tv;
  }

  void logLine(int prio, const std::string& tag, const std::string& text) {
    if (!e.platform().echo_engine_logs) return;
#ifdef __ANDROID__
    __android_log_write(prio, tag.c_str(), text.c_str());
#else
    (void)prio;
    std::fprintf(stderr, "[%s] %s\n", tag.c_str(), text.c_str());
#endif
  }

  void writeStream(int which, const char* data, size_t n) {
    std::string& buf = lineBuffer[which];
    buf.append(data, n);
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      logLine(which == 2 ? 6 : 4, which == 2 ? "nin-stderr" : "nin-stdout", buf.substr(0, nl));
      buf.erase(0, nl + 1);
    }
  }

  uint32_t newFile(FILE* f) {
    uint32_t gf = g.malloc(kFileSize);
    std::memset(g.host(gf), 0, kFileSize);
    int16_t flags = 0x0010, fd = int16_t(fileno(f));
    std::memcpy(g.host(gf + 12), &flags, 2);
    std::memcpy(g.host(gf + 14), &fd, 2);
    files[gf] = f;
    return gf;
  }

  FILE* hostFile(uint32_t gf) {
    auto it = files.find(gf);
    if (it == files.end()) g.fatal("unknown FILE* passed by engine");
    return it->second;
  }

  size_t writeTo(uint32_t gf, const char* data, size_t n) {
    auto s = stdio.find(gf);
    if (s != stdio.end()) {
      writeStream(s->second, data, n);
      return n;
    }
    return fwrite(data, 1, n, hostFile(gf));
  }

  void writeStat(uint32_t p, const struct stat& st) {
    uint8_t* d = g.host(p);
    std::memset(d, 0, 104);
    auto put32 = [&](int off, uint32_t v) { std::memcpy(d + off, &v, 4); };
    auto put64 = [&](int off, uint64_t v) { std::memcpy(d + off, &v, 8); };
    put64(0, st.st_dev);
    put32(12, uint32_t(st.st_ino));
    put32(16, st.st_mode);
    put32(20, uint32_t(st.st_nlink));
    put32(24, st.st_uid);
    put32(28, st.st_gid);
    put64(32, st.st_rdev);
    put64(48, uint64_t(st.st_size));
    put32(56, uint32_t(st.st_blksize));
    put64(64, uint64_t(st.st_blocks));
    put32(72, uint32_t(st.st_atim.tv_sec)); put32(76, uint32_t(st.st_atim.tv_nsec));
    put32(80, uint32_t(st.st_mtim.tv_sec)); put32(84, uint32_t(st.st_mtim.tv_nsec));
    put32(88, uint32_t(st.st_ctim.tv_sec)); put32(92, uint32_t(st.st_ctim.tv_nsec));
    put64(96, st.st_ino);
  }

  void writeTm(uint32_t p, const struct tm& t) {
    int32_t v[9] = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    std::memcpy(g.host(p), v, sizeof v);
    int32_t gmtoff = int32_t(t.tm_gmtoff);
    std::memcpy(g.host(p + 36), &gmtoff, 4);
    if (!tzName) tzName = g.allocCString(t.tm_zone ? t.tm_zone : "UTC");
    g.w32(p + 40, tzName);
  }

  struct tm readTm(uint32_t p) {
    struct tm t{};
    int32_t v[9];
    std::memcpy(v, g.host(p), sizeof v);
    t.tm_sec = v[0]; t.tm_min = v[1]; t.tm_hour = v[2]; t.tm_mday = v[3]; t.tm_mon = v[4];
    t.tm_year = v[5]; t.tm_wday = v[6]; t.tm_yday = v[7]; t.tm_isdst = v[8];
    return t;
  }

  int openFlags(uint32_t f) {
    int h = int(f & 03);
    if (f & 0100) h |= O_CREAT;
    if (f & 0200) h |= O_EXCL;
    if (f & 01000) h |= O_TRUNC;
    if (f & 02000) h |= O_APPEND;
    if (f & 04000) h |= O_NONBLOCK;
    if (f & 02000000) h |= O_CLOEXEC;
    if (f & kArmODirectory) h |= O_DIRECTORY;
    if (f & kArmONofollow) h |= O_NOFOLLOW;
    (void)kArmODirect;
    (void)kArmOLargefile;
    return h;
  }

  uint32_t mmapGuest(uint32_t length, uint32_t prot, uint32_t flags, int fd, uint32_t offset);
};

// ---------------------------------------------------------------- printf
template <class T>
void appendFormatted(std::string& out, const std::string& spec, T value) {
  char small[256];
  int n = std::snprintf(small, sizeof small, spec.c_str(), value);
  if (n < 0) return;
  if (size_t(n) < sizeof small) {
    out.append(small, size_t(n));
    return;
  }
  std::vector<char> big(size_t(n) + 1);
  std::snprintf(big.data(), big.size(), spec.c_str(), value);
  out.append(big.data(), size_t(n));
}

std::string cFormat(Guest& g, const char* fmt, Args& a) {
  std::string out;
  for (const char* p = fmt; *p;) {
    if (*p != '%') {
      out += *p++;
      continue;
    }
    p++;
    if (*p == '%') {
      out += '%';
      p++;
      continue;
    }
    std::string spec = "%";
    while (*p && std::strchr("-+ #0", *p)) spec += *p++;
    if (*p == '*') {
      spec += std::to_string(a.i32());
      p++;
    } else {
      while (std::isdigit(uint8_t(*p))) spec += *p++;
    }
    if (*p == '.') {
      spec += *p++;
      if (*p == '*') {
        spec += std::to_string(std::max(a.i32(), 0));
        p++;
      } else {
        while (std::isdigit(uint8_t(*p))) spec += *p++;
      }
    }
    std::string len;
    while (*p && std::strchr("hlLqjzt", *p)) len += *p++;
    char conv = *p ? *p++ : 0;
    bool wide = len == "ll" || len == "q" || len == "L" || len == "j";
    switch (conv) {
      case 'd':
      case 'i': {
        long long v = wide ? a.i64() : a.i32();
        if (len == "h") v = int16_t(v);
        if (len == "hh") v = int8_t(v);
        appendFormatted(out, spec + "lld", v);
        break;
      }
      case 'u':
      case 'x':
      case 'X':
      case 'o': {
        unsigned long long v = wide ? a.u64() : a.u32();
        if (len == "h") v &= 0xffff;
        if (len == "hh") v &= 0xff;
        appendFormatted(out, spec + "ll" + conv, v);
        break;
      }
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        appendFormatted(out, spec + conv, a.f64());
        break;
      case 'c':
        appendFormatted(out, spec + "c", int(a.u32() & 0xff));
        break;
      case 's': {
        uint32_t s = a.u32();
        appendFormatted(out, spec + "s", s ? g.cstr(s) : "(null)");
        break;
      }
      case 'p':
        appendFormatted(out, std::string("0x%x"), a.u32());
        break;
      case 'n':
        a.u32();
        break;
      default:
        g.fatal(std::string("unsupported printf conversion in: ") + fmt);
    }
  }
  return out;
}

uint8_t ctypeFlags(int c) {
  if (c >= 128) return 0;
  uint8_t f = 0;
  if (std::isupper(c)) f |= kU;
  if (std::islower(c)) f |= kL;
  if (std::isdigit(c)) f |= kN;
  if (std::strchr(" \t\n\v\f\r", c) && c) f |= kS;
  if (c >= 33 && c <= 126 && !std::isalnum(c)) f |= kP;
  if (c < 32 || c == 127) f |= kC;
  if (std::isxdigit(c)) f |= kX;
  if (c == 32) f |= kB;
  return f;
}

uint32_t Bionic::mmapGuest(uint32_t length, uint32_t prot, uint32_t flags, int fd, uint32_t offset) {
  const uint32_t size = (length + kRegionAlign - 1) & ~(kRegionAlign - 1);
  if (mmapTop + size > kMmap + kMmapSize) g.fatal("guest mmap arena exhausted");
  uint32_t addr = mmapTop;
  mmapTop += size;
  const bool anonymous = (flags & 0x20) || fd < 0;
  if (!anonymous) {
    const long page = sysconf(_SC_PAGESIZE);
    if (offset % uint32_t(page) == 0) {
      int hflags = MAP_FIXED | ((flags & 0x1) ? MAP_SHARED : MAP_PRIVATE);
      // Same protection as requested: a read-only fd cannot back a writable shared map.
      int hprot = PROT_READ | ((prot & 0x2) ? PROT_WRITE : 0);
      void* r = ::mmap(g.host(addr), length, hprot, hflags, fd, off_t(offset));
      if (r == MAP_FAILED) {
        hostErrno();
        return 0xffffffff;
      }
      maps.push_back({addr, size, true});
      return addr;
    }
    // Unaligned offset for this host's page size: private copy (not coherent).
    NIN_LOGW("mmap offset %u not host-page aligned; using a private copy", offset);
    pread(fd, g.host(addr), length, off_t(offset));
  }
  maps.push_back({addr, size, false});
  return addr;
}

void unimplemented(Guest& g, const char* name) {
  g.fatal(std::string("engine called unsupported import ") + name + " from " +
          g.symbolize(g.reg(kLR)));
}

}  // namespace

static Bionic* g_bionic = nullptr;
void setFileTracing(bool on) {
  if (g_bionic) g_bionic->tracing = on;
}

void installBionic(Engine& e) {
  auto* b = new Bionic(e);  // lives as long as the engine
  g_bionic = b;
  e.onShutdown([b] {
    for (auto& [gf, f] : b->files) std::fclose(f);  // flushes what the engine wrote
    for (auto& [handle, d] : b->dirs) ::closedir(d);
    for (int fd : b->fds) ::close(fd);
    if (g_bionic == b) g_bionic = nullptr;
    delete b;
  });
  Guest& g = e.guest();
  auto def = [&](const char* name, Thunk t) { g.setThunk(name, std::move(t)); };

  // --- data imports ------------------------------------------------------
  if (uint32_t guard = g.importData("__stack_chk_guard")) g.w32(guard, 0x13572468);
  {
    uint8_t table[257];
    table[0] = 0;
    for (int c = 0; c < 256; c++) table[c + 1] = ctypeFlags(c);
    uint32_t t = g.allocBytes(table, sizeof table);
    if (uint32_t p = g.importData("_ctype_")) g.w32(p, t);
    int16_t lower[257], upper[257];
    lower[0] = upper[0] = -1;
    for (int c = 0; c < 256; c++) {
      lower[c + 1] = int16_t(c < 128 ? std::tolower(c) : c);
      upper[c + 1] = int16_t(c < 128 ? std::toupper(c) : c);
    }
    if (uint32_t p = g.importData("_tolower_tab_")) g.w32(p, g.allocBytes(lower, sizeof lower));
    if (uint32_t p = g.importData("_toupper_tab_")) g.w32(p, g.allocBytes(upper, sizeof upper));
  }
  if (uint32_t sf = g.importData("__sF")) {
    for (int i = 0; i < 3; i++) {
      uint32_t f = sf + kFileSize * uint32_t(i);
      int16_t flags = i == 0 ? 0x0004 : 0x0008, fd = int16_t(i);
      std::memcpy(g.host(f + 12), &flags, 2);
      std::memcpy(g.host(f + 14), &fd, 2);
      b->stdio[f] = i;
    }
  }
  b->errnoAddr = g.malloc(16);

  // --- guest-native fast paths (native/fastlibc.c) --------------------------
  const std::pair<const char*, uint32_t> native[] = {
      {"memcpy", kFastlibcMemcpy}, {"__aeabi_memcpy", kFastlibcMemcpy},
      {"memmove", kFastlibcMemmove}, {"__aeabi_memmove", kFastlibcMemmove},
      {"memset", kFastlibcMemset}, {"__aeabi_memset", kFastlibcAeabiMemset},
      {"strlen", kFastlibcStrlen}, {"strcmp", kFastlibcStrcmp}, {"strcoll", kFastlibcStrcmp},
      {"strncmp", kFastlibcStrncmp}, {"memcmp", kFastlibcMemcmp},
      {"pthread_once", kFastlibcPthreadOnce}, {"tolower", kFastlibcTolower},
      {"toupper", kFastlibcToupper}, {"malloc", kFastlibcMalloc}, {"free", kFastlibcFree},
      {"calloc", kFastlibcCalloc}, {"realloc", kFastlibcRealloc},
      {"pthread_mutex_init", kFastlibcReturn0}, {"pthread_mutex_lock", kFastlibcReturn0},
      {"pthread_mutex_unlock", kFastlibcReturn0}, {"pthread_mutex_destroy", kFastlibcReturn0},
      {"pthread_mutex_trylock", kFastlibcReturn0}, {"pthread_mutexattr_init", kFastlibcReturn0},
      {"pthread_mutexattr_settype", kFastlibcReturn0}, {"pthread_mutexattr_destroy", kFastlibcReturn0},
      {"pthread_cond_broadcast", kFastlibcReturn0}, {"sem_init", kFastlibcReturn0},
      {"sem_destroy", kFastlibcReturn0}, {"sem_post", kFastlibcReturn0},
  };
  for (auto& [name, addr] : native) g.routeToNative(name, addr);

  // --- process ---------------------------------------------------------------
  def("abort", [](Guest& g, Args&) { g.fatal("engine called abort() from " + g.symbolize(g.reg(kLR))); });
  def("exit", [](Guest& g, Args& a) { g.fatal("engine called exit(" + std::to_string(a.i32()) + ")"); });
  def("raise", [](Guest& g, Args& a) { g.fatal("engine raised signal " + std::to_string(a.i32())); });
  def("__stack_chk_fail", [](Guest& g, Args&) { g.fatal("stack protector tripped in engine"); });
  def("__assert2", [](Guest& g, Args& a) {
    std::string file = g.str(a.u32());
    int line = a.i32();
    std::string func = g.str(a.u32()), expr = g.str(a.u32());
    g.fatal("engine assertion " + expr + " failed at " + file + ":" + std::to_string(line) + " in " + func);
  });
  def("__errno", [b](Guest& g, Args&) { g.ret32(b->errnoAddr); });
  def("__cxa_atexit", [](Guest& g, Args&) { g.ret32(0); });
  def("__cxa_finalize", [](Guest& g, Args&) { g.ret32(0); });
  def("getpid", [](Guest& g, Args&) { g.ret32(uint32_t(getpid())); });
  def("geteuid", [](Guest& g, Args&) { g.ret32(uint32_t(geteuid())); });
  def("getenv", [](Guest& g, Args&) { g.ret32(0); });
  def("sysconf", [](Guest& g, Args& a) {
    int32_t n = a.i32();  // bionic numbering
    g.ret32(n == 0x27 || n == 0x28 ? 4096 : (n == 0x60 || n == 0x61) ? 4 : 0);
  });
  def("setlocale", [b](Guest& g, Args&) {
    if (!b->localeC) b->localeC = g.allocCString("C");
    g.ret32(b->localeC);
  });
  def("bsd_signal", [](Guest& g, Args&) { g.ret32(0); });
  def("kill", [](Guest& g, Args&) { g.ret32(0); });
  def("__gnu_Unwind_Find_exidx", [](Guest& g, Args& a) {
    a.u32();
    uint32_t pcount = a.u32();
    auto [start, count] = g.exidx();
    g.w32(pcount, count);
    g.ret32(start);
  });
  for (const char* name : {"dlopen", "dlsym", "dlclose", "dlerror"})
    def(name, [](Guest& g, Args&) { g.ret32(0); });
  def("getrlimit", [](Guest& g, Args& a) {
    a.u32();
    uint32_t p = a.u32();
    g.w32(p, 8u << 20);
    g.w32(p + 4, 0xffffffff);
    g.ret32(0);
  });
  def("getrusage", [](Guest& g, Args& a) {
    a.u32();
    std::memset(g.host(a.u32()), 0, 72);
    g.ret32(0);
  });
  def("sysinfo", [](Guest& g, Args& a) {
    uint32_t p = a.u32();
    std::memset(g.host(p), 0, 64);
    g.w32(p + 16, 2u << 30);
    g.w32(p + 20, 1u << 30);
    g.w32(p + 52, 1);
    g.ret32(0);
  });
  def("uname", [](Guest& g, Args& a) {
    uint32_t p = a.u32();
    const char* fields[] = {"Linux", "localhost", "3.4.0", "#1 SMP", "armv7l", "localdomain"};
    for (int i = 0; i < 6; i++) {
      std::memset(g.host(p + 65 * uint32_t(i)), 0, 65);
      std::strcpy(reinterpret_cast<char*>(g.host(p + 65 * uint32_t(i))), fields[i]);
    }
    g.ret32(0);
  });
  def("gethostname", [](Guest& g, Args& a) {
    uint32_t p = a.u32(), n = a.u32();
    std::strncpy(reinterpret_cast<char*>(g.host(p)), "localhost", n);
    g.ret32(0);
  });
  def("isatty", [](Guest& g, Args&) { g.ret32(0); });

  // --- setjmp/longjmp (r4-r11, sp, lr, d8-d15) ---------------------------------
  def("setjmp", [](Guest& g, Args&) {
    uint32_t buf = g.reg(kR0);
    const int core[] = {kR4, kR5, kR6, kR7, kR8, kR9, kR10, kR11, kSP, kLR};
    for (int i = 0; i < 10; i++) g.w32(buf + 4 * uint32_t(i), g.reg(core[i]));
    for (int i = 0; i < 8; i++) {
      uint64_t d = g.dreg(8 + i);
      std::memcpy(g.host(buf + 40 + 8 * uint32_t(i)), &d, 8);
    }
    g.ret32(0);
  });
  def("longjmp", [](Guest& g, Args&) {
    uint32_t buf = g.reg(kR0), value = g.reg(kR1);
    const int core[] = {kR4, kR5, kR6, kR7, kR8, kR9, kR10, kR11, kSP, kLR};
    for (int i = 0; i < 10; i++) g.setReg(core[i], g.r32(buf + 4 * uint32_t(i)));
    for (int i = 0; i < 8; i++) {
      uint64_t d;
      std::memcpy(&d, g.host(buf + 40 + 8 * uint32_t(i)), 8);
      g.setDreg(8 + i, d);
    }
    g.ret32(value ? value : 1);  // the stub's `bx lr` returns into setjmp's caller
  });

  // --- memory -------------------------------------------------------------------
  def("mmap", [b](Guest& g, Args& a) {
    a.u32();
    uint32_t length = a.u32(), prot = a.u32(), flags = a.u32();
    int fd = a.i32();
    uint32_t offset = a.u32();
    g.ret32(b->mmapGuest(length, prot, flags, fd, offset));
  });
  def("munmap", [b](Guest& g, Args& a) {
    uint32_t p = a.u32();
    for (auto it = b->maps.begin(); it != b->maps.end(); ++it)
      if (it->addr == p) {
        // Replace the pages with fresh anonymous memory; keep the guest mapping.
        ::mmap(g.host(it->addr), it->length, PROT_READ | PROT_WRITE,
               MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        b->maps.erase(it);
        break;
      }
    g.ret32(0);
  });
  def("msync", [b](Guest& g, Args& a) {
    uint32_t p = a.u32(), n = a.u32(), flags = a.u32();
    const long page = sysconf(_SC_PAGESIZE);
    uint32_t start = p & ~uint32_t(page - 1);
    ::msync(g.host(start), n + (p - start), (flags & 4) ? MS_SYNC : MS_ASYNC);
    g.ret32(0);
  });

  // --- strings (pointer-translated host libc) --------------------------------
  def("strcpy", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32();
    std::strcpy(reinterpret_cast<char*>(g.host(d)), g.cstr(s));
    g.ret32(d);
  });
  def("strncpy", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32(), n = a.u32();
    std::strncpy(reinterpret_cast<char*>(g.host(d)), g.cstr(s), n);
    g.ret32(d);
  });
  def("strcat", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32();
    std::strcat(reinterpret_cast<char*>(g.host(d)), g.cstr(s));
    g.ret32(d);
  });
  def("strchr", [](Guest& g, Args& a) {
    uint32_t s = a.u32();
    int c = a.i32();
    const char* r = std::strchr(g.cstr(s), c);
    g.ret32(r ? g.guestAddr(r) : 0);
  });
  def("strrchr", [](Guest& g, Args& a) {
    uint32_t s = a.u32();
    int c = a.i32();
    const char* r = std::strrchr(g.cstr(s), c);
    g.ret32(r ? g.guestAddr(r) : 0);
  });
  def("strstr", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), n = a.u32();
    const char* r = std::strstr(g.cstr(s), g.cstr(n));
    g.ret32(r ? g.guestAddr(r) : 0);
  });
  def("memchr", [](Guest& g, Args& a) {
    uint32_t s = a.u32();
    int c = a.i32();
    uint32_t n = a.u32();
    const void* r = std::memchr(g.host(s), c, n);
    g.ret32(r ? g.guestAddr(r) : 0);
  });
  def("strdup", [](Guest& g, Args& a) { g.ret32(g.allocCString(g.str(a.u32()))); });
  def("strcasecmp", [](Guest& g, Args& a) {
    uint32_t x = a.u32(), y = a.u32();
    g.ret32(uint32_t(strcasecmp(g.cstr(x), g.cstr(y))));
  });
  def("strxfrm", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32(), n = a.u32();
    size_t len = std::strlen(g.cstr(s));
    if (len < n) std::memcpy(g.host(d), g.cstr(s), len + 1);
    g.ret32(uint32_t(len));
  });
  def("strerror", [b](Guest& g, Args& a) {  // static strings in libc: one copy per errno value
    int e = a.i32();
    uint32_t& s = b->strerrors[e];
    if (!s) s = g.allocCString(std::strerror(e));
    g.ret32(s);
  });
  def("strtol", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), endp = a.u32();
    int base = a.i32();
    char* end = nullptr;
    long long v = std::strtoll(g.cstr(s), &end, base);
    v = std::clamp<long long>(v, INT32_MIN, INT32_MAX);
    if (endp) g.w32(endp, g.guestAddr(end));
    g.ret32(uint32_t(int32_t(v)));
  });
  def("strtoul", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), endp = a.u32();
    int base = a.i32();
    char* end = nullptr;
    unsigned long long v = std::strtoull(g.cstr(s), &end, base);
    if (endp) g.w32(endp, g.guestAddr(end));
    g.ret32(v > 0xffffffffull ? 0xffffffffu : uint32_t(v));
  });
  def("strtoll", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), endp = a.u32();
    int base = a.i32();
    char* end = nullptr;
    long long v = std::strtoll(g.cstr(s), &end, base);
    if (endp) g.w32(endp, g.guestAddr(end));
    g.ret64(uint64_t(v));
  });
  def("strtod", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), endp = a.u32();
    char* end = nullptr;
    double v = std::strtod(g.cstr(s), &end);
    if (endp) g.w32(endp, g.guestAddr(end));
    g.retD(v);
  });
  def("atoi", [](Guest& g, Args& a) { g.ret32(uint32_t(std::atoi(g.cstr(a.u32())))); });
  def("atol", [](Guest& g, Args& a) { g.ret32(uint32_t(std::atoi(g.cstr(a.u32())))); });
  def("qsort", [](Guest& g, Args& a) {
    uint32_t base = a.u32(), n = a.u32(), size = a.u32(), cmp = a.u32();
    // Stable insertion sort through the guest comparator (inputs are small).
    std::vector<uint8_t> tmp(size);
    for (uint32_t i = 1; i < n; i++) {
      std::memcpy(tmp.data(), g.host(base + i * size), size);
      uint32_t key = g.malloc(size);
      std::memcpy(g.host(key), tmp.data(), size);
      uint32_t j = i;
      while (j > 0 && int32_t(g.call(cmp, {base + (j - 1) * size, key})) > 0) {
        std::memcpy(g.host(base + j * size), g.host(base + (j - 1) * size), size);
        j--;
      }
      std::memcpy(g.host(base + j * size), tmp.data(), size);
      g.free(key);
    }
  });

  // --- ctype / wide characters -------------------------------------------------
  def("isalnum", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 0 && c < 128 && std::isalnum(c)); });
  def("isalpha", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 0 && c < 128 && std::isalpha(c)); });
  def("isdigit", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= '0' && c <= '9'); });
  def("islower", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 'a' && c <= 'z'); });
  def("isupper", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 'A' && c <= 'Z'); });
  def("isspace", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c == ' ' || (c >= 9 && c <= 13)); });
  def("isprint", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 32 && c <= 126); });
  def("isgraph", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 33 && c <= 126); });
  def("towlower", [](Guest& g, Args& a) { g.ret32(uint32_t(std::towlower(wint_t(a.u32())))); });
  def("towupper", [](Guest& g, Args& a) { g.ret32(uint32_t(std::towupper(wint_t(a.u32())))); });
  def("iswspace", [](Guest& g, Args& a) { g.ret32(std::iswspace(wint_t(a.u32())) != 0); });
  def("iswalnum", [](Guest& g, Args& a) { g.ret32(std::iswalnum(wint_t(a.u32())) != 0); });
  def("wctype", [](Guest& g, Args&) { g.ret32(0); });
  def("iswctype", [](Guest& g, Args&) { g.ret32(0); });
  def("btowc", [](Guest& g, Args& a) { int c = a.i32(); g.ret32(c >= 0 && c < 128 ? uint32_t(c) : 0xffffffffu); });
  def("wctob", [](Guest& g, Args& a) { uint32_t c = a.u32(); g.ret32(c < 128 ? c : 0xffffffffu); });
  def("mbrtowc", [](Guest& g, Args& a) {
    uint32_t pwc = a.u32(), s = a.u32();
    if (!s) return g.ret32(0);
    uint8_t c = *g.host(s);
    if (pwc) g.w32(pwc, c);
    g.ret32(c ? 1 : 0);
  });
  def("wcrtomb", [](Guest& g, Args& a) {
    uint32_t s = a.u32(), wc = a.u32();
    if (s) g.w8(s, uint8_t(wc < 256 ? wc : '?'));
    g.ret32(1);
  });
  def("wcslen", [](Guest& g, Args& a) {
    uint32_t p = a.u32(), n = 0;
    while (g.r32(p + 4 * n)) n++;
    g.ret32(n);
  });
  def("wmemcpy", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32(), n = a.u32();
    std::memmove(g.host(d), g.host(s), 4ull * n);
    g.ret32(d);
  });
  def("wmemmove", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), s = a.u32(), n = a.u32();
    std::memmove(g.host(d), g.host(s), 4ull * n);
    g.ret32(d);
  });
  def("wmemset", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), c = a.u32(), n = a.u32();
    for (uint32_t i = 0; i < n; i++) g.w32(d + 4 * i, c);
    g.ret32(d);
  });
  def("wmemcmp", [](Guest& g, Args& a) {
    uint32_t x = a.u32(), y = a.u32(), n = a.u32();
    for (uint32_t i = 0; i < n; i++) {
      int32_t u = int32_t(g.r32(x + 4 * i)), v = int32_t(g.r32(y + 4 * i));
      if (u != v) return g.ret32(u < v ? 0xffffffffu : 1u);
    }
    g.ret32(0);
  });
  def("wmemchr", [](Guest& g, Args& a) {
    uint32_t p = a.u32(), c = a.u32(), n = a.u32();
    for (uint32_t i = 0; i < n; i++)
      if (g.r32(p + 4 * i) == c) return g.ret32(p + 4 * i);
    g.ret32(0);
  });

  // --- formatting / stdio ------------------------------------------------------
  def("sprintf", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), fmt = a.u32();
    std::string s = cFormat(g, g.cstr(fmt), a);
    std::memcpy(g.host(d), s.c_str(), s.size() + 1);
    g.ret32(uint32_t(s.size()));
  });
  def("snprintf", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), n = a.u32(), fmt = a.u32();
    std::string s = cFormat(g, g.cstr(fmt), a);
    if (n) {
      size_t k = std::min<size_t>(s.size(), n - 1);
      std::memcpy(g.host(d), s.data(), k);
      g.w8(d + uint32_t(k), 0);
    }
    g.ret32(uint32_t(s.size()));
  });
  def("vsprintf", [](Guest& g, Args& a) {
    uint32_t d = a.u32(), fmt = a.u32(), va = a.u32();
    Args v(g, va, true);
    std::string s = cFormat(g, g.cstr(fmt), v);
    std::memcpy(g.host(d), s.c_str(), s.size() + 1);
    g.ret32(uint32_t(s.size()));
  });
  def("printf", [b](Guest& g, Args& a) {
    std::string s = cFormat(g, g.cstr(a.u32()), a);
    b->writeStream(1, s.data(), s.size());
    g.ret32(uint32_t(s.size()));
  });
  def("fprintf", [b](Guest& g, Args& a) {
    uint32_t f = a.u32(), fmt = a.u32();
    std::string s = cFormat(g, g.cstr(fmt), a);
    g.ret32(uint32_t(b->writeTo(f, s.data(), s.size())));
  });
  def("fputs", [b](Guest& g, Args& a) {
    uint32_t s = a.u32(), f = a.u32();
    b->writeTo(f, g.cstr(s), std::strlen(g.cstr(s)));
    g.ret32(1);
  });
  auto fputc = [b](Guest& g, Args& a) {
    uint32_t c = a.u32(), f = a.u32();
    char ch = char(c & 0xff);
    b->writeTo(f, &ch, 1);
    g.ret32(c & 0xff);
  };
  def("fputc", fputc);
  def("putc", fputc);
  def("fwrite", [b](Guest& g, Args& a) {
    uint32_t p = a.u32(), size = a.u32(), n = a.u32(), f = a.u32();
    size_t w = b->writeTo(f, g.cstr(p), size_t(size) * n);
    g.ret32(size ? uint32_t(w / size) : 0);
  });
  def("fflush", [b](Guest& g, Args& a) {
    uint32_t f = a.u32();
    if (f && !b->stdio.count(f)) fflush(b->hostFile(f));
    g.ret32(0);
  });
  def("setvbuf", [](Guest& g, Args&) { g.ret32(0); });
  def("__android_log_print", [b](Guest& g, Args& a) {
    int prio = a.i32();
    std::string tag = g.str(a.u32());
    std::string s = cFormat(g, g.cstr(a.u32()), a);
    b->logLine(prio, "nin:" + tag, s);
    g.ret32(1);
  });
  def("__android_log_write", [b](Guest& g, Args& a) {
    int prio = a.i32();
    std::string tag = g.str(a.u32());
    b->logLine(prio, "nin:" + tag, g.str(a.u32()));
    g.ret32(1);
  });
  def("fopen", [b](Guest& g, Args& a) {
    uint32_t name = a.u32();
    std::string mode = g.str(a.u32());
    if (Bionic::procMaps(g.str(name)) && mode.find_first_of("wa+") == std::string::npos) {
      const std::string& text = b->procMapsText();
      FILE* f = fmemopen(const_cast<char*>(text.data()), text.size(), "r");
      if (!f) {
        b->hostErrno();
        return g.ret32(0);
      }
      return g.ret32(b->newFile(f));
    }
    std::string p = b->path(name, mode.find_first_of("wa+") != std::string::npos);
    if (p == Bionic::kHidden) {  // /proc is polled ~50 times per frame: skip the syscall
      b->setErrno(ENOENT);
      return g.ret32(0);
    }
    static const bool mkdirs = std::getenv("NIN_SANDBOX_MKDIRS") != nullptr;  // match the Python harness
    if (mkdirs && mode.find_first_of("wa") != std::string::npos) {
      std::string cmd = "mkdir -p \"" + p.substr(0, p.rfind('/')) + "\"";
      if (std::system(cmd.c_str()) != 0) {}
    }
    FILE* f = std::fopen(p.c_str(), mode.c_str());
    if (!f) {
      b->hostErrno();
      return g.ret32(0);
    }
    g.ret32(b->newFile(f));
  });
  def("fclose", [b](Guest& g, Args& a) {
    uint32_t f = a.u32();
    auto it = b->files.find(f);
    if (it != b->files.end()) {
      std::fclose(it->second);
      b->files.erase(it);
      g.free(f);
    }
    g.ret32(0);
  });
  def("fread", [b](Guest& g, Args& a) {
    uint32_t p = a.u32(), size = a.u32(), n = a.u32(), f = a.u32();
    g.ret32(uint32_t(std::fread(g.host(p), size, n, b->hostFile(f))));
  });
  def("fseek", [b](Guest& g, Args& a) {
    uint32_t f = a.u32();
    int32_t off = a.i32();
    int whence = a.i32();
    g.ret32(uint32_t(std::fseek(b->hostFile(f), off, whence)));
  });
  def("ftell", [b](Guest& g, Args& a) { g.ret32(uint32_t(std::ftell(b->hostFile(a.u32())))); });
  def("fgets", [b](Guest& g, Args& a) {
    uint32_t p = a.u32();
    int n = a.i32();
    uint32_t f = a.u32();
    g.ret32(std::fgets(reinterpret_cast<char*>(g.host(p)), n, b->hostFile(f)) ? p : 0);
  });
  auto fgetc = [b](Guest& g, Args& a) { g.ret32(uint32_t(std::fgetc(b->hostFile(a.u32())))); };
  def("fgetc", fgetc);
  def("getc", fgetc);
  def("ungetc", [b](Guest& g, Args& a) {
    int c = a.i32();
    g.ret32(uint32_t(std::ungetc(c, b->hostFile(a.u32()))));
  });
  def("sscanf", [](Guest& g, Args&) { unimplemented(g, "sscanf"); });

  // --- file descriptors ----------------------------------------------------------
  def("open", [b](Guest& g, Args& a) {
    uint32_t name = a.u32(), flags = a.u32(), mode = a.u32();
    std::string p = b->path(name, (flags & 3) != 0);  // O_WRONLY / O_RDWR
    int fd = ::open(p.c_str(), b->openFlags(flags), mode ? mode : 0644);
    if (fd < 0) b->hostErrno();
    else b->fds.insert(fd);
    g.ret32(uint32_t(fd));
  });
  def("close", [b](Guest& g, Args& a) {
    int fd = a.i32();
    b->fds.erase(fd);
    g.ret32(uint32_t(::close(fd)));
  });
  def("read", [b](Guest& g, Args& a) {
    int fd = a.i32();
    uint32_t p = a.u32(), n = a.u32();
    ssize_t r = ::read(fd, g.host(p), n);
    if (r < 0) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("write", [b](Guest& g, Args& a) {
    int fd = a.i32();
    uint32_t p = a.u32(), n = a.u32();
    if (fd == 1 || fd == 2) {
      b->writeStream(fd, g.cstr(p), n);
      return g.ret32(n);
    }
    ssize_t r = ::write(fd, g.host(p), n);
    if (r < 0) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("writev", [b](Guest& g, Args& a) {
    int fd = a.i32();
    uint32_t iov = a.u32();
    int count = a.i32();
    std::string data;
    for (int i = 0; i < count; i++)
      data.append(g.cstr(g.r32(iov + 8 * uint32_t(i))), g.r32(iov + 8 * uint32_t(i) + 4));
    if (fd == 1 || fd == 2) {
      b->writeStream(fd, data.data(), data.size());
      return g.ret32(uint32_t(data.size()));
    }
    g.ret32(uint32_t(::write(fd, data.data(), data.size())));
  });
  def("lseek", [](Guest& g, Args& a) {
    int fd = a.i32();
    int32_t off = a.i32();
    int whence = a.i32();
    g.ret32(uint32_t(::lseek(fd, off, whence)));
  });
  def("ftruncate", [](Guest& g, Args& a) {
    int fd = a.i32();
    int32_t len = a.i32();
    g.ret32(uint32_t(::ftruncate(fd, len)));
  });
  def("fsync", [](Guest& g, Args& a) { g.ret32(uint32_t(::fsync(a.i32()))); });
  def("fdatasync", [](Guest& g, Args& a) { g.ret32(uint32_t(::fsync(a.i32()))); });
  def("flock", [](Guest& g, Args& a) {
    int fd = a.i32(), op = a.i32();
    g.ret32(uint32_t(::flock(fd, op)));
  });
  def("fcntl", [](Guest& g, Args& a) {
    int fd = a.i32(), cmd = a.i32();
    uint32_t arg = a.u32();
    if (cmd >= 1 && cmd <= 4) return g.ret32(uint32_t(::fcntl(fd, cmd, arg)));
    g.ret32(0);  // advisory locks: single process, always granted
  });
  auto statFn = [b](bool follow) {
    return [b, follow](Guest& g, Args& a) {
      std::string p = b->path(a.u32());
      uint32_t out = a.u32();
      struct stat st{};
      int r = follow ? ::stat(p.c_str(), &st) : ::lstat(p.c_str(), &st);
      if (r != 0) {
        b->hostErrno();
        return g.ret32(0xffffffffu);
      }
      b->writeStat(out, st);
      g.ret32(0);
    };
  };
  def("stat", statFn(true));
  def("lstat", statFn(false));
  def("fstat", [b](Guest& g, Args& a) {
    int fd = a.i32();
    uint32_t out = a.u32();
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
      b->hostErrno();
      return g.ret32(0xffffffffu);
    }
    b->writeStat(out, st);
    g.ret32(0);
  });
  def("access", [b](Guest& g, Args& a) {
    std::string p = b->path(a.u32());
    int mode = a.i32();
    int r = ::access(p.c_str(), mode);
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("mkdir", [b](Guest& g, Args& a) {
    std::string p = b->path(a.u32());
    uint32_t mode = a.u32();
    int r = ::mkdir(p.c_str(), mode ? mode : 0755);
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("rmdir", [b](Guest& g, Args& a) {
    int r = ::rmdir(b->path(a.u32()).c_str());
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("unlink", [b](Guest& g, Args& a) {
    int r = ::unlink(b->path(a.u32()).c_str());
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("remove", [b](Guest& g, Args& a) {
    int r = std::remove(b->path(a.u32()).c_str());
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("rename", [b](Guest& g, Args& a) {
    uint32_t fromName = a.u32(), toName = a.u32();
    if (Bionic::diagnosticFile(g.str(fromName)) && Bionic::diagnosticFile(g.str(toName)))
      return g.ret32(0);  // the memory log's temp -> log rotation: nothing on disk to move
    std::string from = b->path(fromName), to = b->path(toName);
    int r = std::rename(from.c_str(), to.c_str());
    if (r) b->hostErrno();
    g.ret32(uint32_t(r));
  });
  def("opendir", [b](Guest& g, Args& a) {
    DIR* d = ::opendir(b->path(a.u32()).c_str());
    if (!d) {
      b->hostErrno();
      return g.ret32(0);
    }
    uint32_t handle = g.malloc(288);  // handle + dirent buffer (at +8)
    b->dirs[handle] = d;
    g.ret32(handle);
  });
  def("readdir", [b](Guest& g, Args& a) {
    uint32_t handle = a.u32();
    auto it = b->dirs.find(handle);
    if (it == b->dirs.end()) return g.ret32(0);
    struct dirent* ent = ::readdir(it->second);
    if (!ent) return g.ret32(0);
    uint32_t out = handle + 8;  // struct dirent {u64 ino; s64 off; u16 reclen; u8 type; char name[256]}
    uint64_t ino = ent->d_ino;
    int64_t off = 0;
    uint16_t reclen = 280;
    std::memcpy(g.host(out), &ino, 8);
    std::memcpy(g.host(out + 8), &off, 8);
    std::memcpy(g.host(out + 16), &reclen, 2);
    g.w8(out + 18, ent->d_type);
    std::strncpy(reinterpret_cast<char*>(g.host(out + 19)), ent->d_name, 255);
    g.w8(out + 19 + 255, 0);
    g.ret32(out);
  });
  def("closedir", [b](Guest& g, Args& a) {
    uint32_t handle = a.u32();
    auto it = b->dirs.find(handle);
    if (it != b->dirs.end()) {
      ::closedir(it->second);
      b->dirs.erase(it);
      g.free(handle);
    }
    g.ret32(0);
  });
  def("getcwd", [](Guest& g, Args& a) {
    uint32_t p = a.u32();
    std::strcpy(reinterpret_cast<char*>(g.host(p)), "/");
    g.ret32(p);
  });
  for (const char* name : {"chdir", "fchmod", "fchown", "utimes"})
    def(name, [](Guest& g, Args&) { g.ret32(0); });

  // --- time ----------------------------------------------------------------------
  def("gettimeofday", [b](Guest& g, Args& a) {
      uint32_t tv = a.u32();
      timeval t = b->now();
      if (tv) {
        g.w32(tv, uint32_t(t.tv_sec));
        g.w32(tv + 4, uint32_t(t.tv_usec));
      }
      g.ret32(0);
    });
  def("time", [b](Guest& g, Args& a) {
    uint32_t t = a.u32();
    uint32_t s = uint32_t(b->now().tv_sec);
    if (t) g.w32(t, s);
    g.ret32(s);
  });
  def("clock", [b](Guest& g, Args&) {
    if (b->e.platform().virtual_clock) {
      timeval t = b->now();
      return g.ret32(uint32_t((t.tv_sec - 1577836800) * 1000000 + t.tv_usec));
    }
    g.ret32(uint32_t(::clock()));
  });
  def("times", [b](Guest& g, Args& a) {
    uint32_t p = a.u32();
    if (p) std::memset(g.host(p), 0, 16);
    timeval t = b->now();
    g.ret32(uint32_t(t.tv_sec * 100 + t.tv_usec / 10000));
  });
  auto tmFn = [b](bool local) {
    return [b, local](Guest& g, Args& a) {
      time_t t = time_t(g.r32(a.u32()));
      struct tm out{};
      if (local && !b->e.platform().virtual_clock)
        localtime_r(&t, &out);
      else
        gmtime_r(&t, &out);
      if (!b->tmBuffer) b->tmBuffer = g.malloc(44);  // like bionic: one static struct tm for both
      b->writeTm(b->tmBuffer, out);
      g.ret32(b->tmBuffer);
    };
  };
  def("localtime", tmFn(true));
  def("gmtime", tmFn(false));
  def("mktime", [b](Guest& g, Args& a) {
    struct tm t = b->readTm(a.u32());
    g.ret32(uint32_t(b->e.platform().virtual_clock ? timegm(&t) : mktime(&t)));
  });
  def("strftime", [b](Guest& g, Args& a) {
    uint32_t d = a.u32(), n = a.u32(), fmt = a.u32(), tmp = a.u32();
    struct tm t = b->readTm(tmp);
    std::vector<char> buf(std::max<uint32_t>(n, 1));
    size_t r = std::strftime(buf.data(), buf.size(), g.cstr(fmt), &t);
    if (r) std::memcpy(g.host(d), buf.data(), r + 1);
    g.ret32(uint32_t(r));
  });
  def("nanosleep", [](Guest& g, Args&) { g.ret32(0); });
  def("sleep", [](Guest& g, Args&) { g.ret32(0); });

  // --- threads (single emulated thread) ---------------------------------------------
  def("pthread_self", [](Guest& g, Args&) { g.ret32(1); });
  def("pthread_key_create", [b](Guest& g, Args& a) {
    uint32_t p = a.u32();
    g.w32(p, b->nextKey++);
    g.ret32(0);
  });
  def("pthread_key_delete", [](Guest& g, Args&) { g.ret32(0); });
  def("pthread_getspecific", [b](Guest& g, Args& a) {
    auto it = b->tls.find(a.u32());
    g.ret32(it == b->tls.end() ? 0 : it->second);
  });
  def("pthread_setspecific", [b](Guest& g, Args& a) {
    uint32_t k = a.u32();
    b->tls[k] = a.u32();
    g.ret32(0);
  });
  def("pthread_create", [](Guest& g, Args& a) {
    a.u32();
    a.u32();
    uint32_t fn = a.u32();
    NIN_LOGW("engine requested a thread (%s); refused (single-threaded emulation)",
             g.symbolize(fn).c_str());
    g.ret32(11);  // EAGAIN
  });
  def("pthread_cond_wait", [](Guest& g, Args&) { unimplemented(g, "pthread_cond_wait (would block)"); });
  def("sem_wait", [](Guest& g, Args&) { unimplemented(g, "sem_wait (would block)"); });

  // --- random ------------------------------------------------------------------------
  def("srand48", [](Guest& g, Args& a) { srand48(long(a.u32())); });
  def("lrand48", [](Guest& g, Args&) { g.ret32(uint32_t(lrand48())); });

  // --- math -------------------------------------------------------------------------------
#define F1(name) def(#name, [](Guest& g, Args& a) { g.retF(::name(a.f32())); })
#define F2(name) def(#name, [](Guest& g, Args& a) { float x = a.f32(), y = a.f32(); g.retF(::name(x, y)); })
#define D1(name) def(#name, [](Guest& g, Args& a) { g.retD(::name(a.f64())); })
#define D2(name) def(#name, [](Guest& g, Args& a) { double x = a.f64(), y = a.f64(); g.retD(::name(x, y)); })
  F1(sqrtf); F1(sinf); F1(cosf); F1(tanf); F1(acosf); F1(atanf); F1(floorf); F1(ceilf); F1(logf);
  F2(powf); F2(atan2f); F2(fmodf);
  D1(sqrt); D1(sin); D1(cos); D1(tan); D1(acos); D1(asin); D1(atan); D1(floor); D1(ceil); D1(exp);
  D1(exp2); D1(log); D1(log10); D1(fabs); D1(sinh); D1(cosh); D1(tanh); D1(acosh); D1(asinh); D1(atanh);
  D2(pow); D2(fmod);
#undef F1
#undef F2
#undef D1
#undef D2
  def("frexp", [](Guest& g, Args& a) {
    double x = a.f64();
    uint32_t p = a.u32();
    int ex = 0;
    double m = std::frexp(x, &ex);
    g.w32(p, uint32_t(ex));
    g.retD(m);
  });
  def("ldexp", [](Guest& g, Args& a) {
    double x = a.f64();
    int ex = a.i32();
    g.retD(std::ldexp(x, ex));
  });

  // --- network / processes: not used by the keyboard paths ------------------------------
  for (const char* name : {"socket", "connect", "bind", "listen", "accept", "send", "sendto", "recv",
                           "recvmsg", "setsockopt", "getpeername", "select", "poll", "pipe", "dup2",
                           "fork", "execvp", "waitpid", "popen", "pclose", "system", "symlink",
                           "readlink", "mremap", "gethostbyname"})
    def(name, [name](Guest& g, Args&) {
      NIN_LOGW("engine called %s; not supported, returning failure", name);
      g.ret32(0xffffffffu);
    });
}

}  // namespace nin
