#include "boot_log.h"

#include <time.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace nin {
namespace {

struct Line {
  uint32_t seq;
  BootLine kind;
  uint32_t ms;
  std::string text;
};

constexpr size_t kMaxLines = 400;
std::atomic<bool> g_active{false};
std::mutex g_lock;
std::deque<Line> g_lines;
uint32_t g_nextSeq = 0;
double g_startMs = 0;
double g_endMs = 0;

double nowMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

}  // namespace

void bootLogBegin() {
  std::lock_guard<std::mutex> lock(g_lock);
  g_lines.clear();
  g_startMs = nowMs();
  g_active = true;
}

void bootLogEnd() {
  std::lock_guard<std::mutex> lock(g_lock);
  g_endMs = nowMs();
  g_active = false;
}

bool bootLogging() { return g_active.load(std::memory_order_relaxed); }

void bootLog(BootLine kind, const std::string& text) {
  if (!bootLogging()) return;
  std::lock_guard<std::mutex> lock(g_lock);
  // One line per entry: the engine sometimes logs several at once.
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    if (end > start || kind == BootLine::Step)
      g_lines.push_back({g_nextSeq++, kind, uint32_t(nowMs() - g_startMs), text.substr(start, end - start)});
    if (g_lines.size() > kMaxLines) g_lines.pop_front();
    start = end + 1;
  }
}

std::string bootLogRead(uint32_t next) {
  std::lock_guard<std::mutex> lock(g_lock);
  const double elapsed = (g_active ? nowMs() : g_endMs) - g_startMs;
  std::string out = std::to_string(g_nextSeq) + '\t' + std::to_string(uint32_t(elapsed)) + "\n";
  for (const Line& line : g_lines) {
    if (line.seq < next) continue;
    out += char(line.kind);
    out += '\t';
    out += std::to_string(line.ms);
    out += '\t';
    for (char c : line.text) out += (c == '\t' || c == '\r') ? ' ' : c;
    out += '\n';
  }
  return out;
}

}  // namespace nin
