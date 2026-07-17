// ============================================================================
//  autolee_logic/log_ring.h
//  Fixed-capacity line ring buffer for the web log — pure, host-testable.
//  Mirrors the logBuf/logHead/logSerial ring in AutoLee/AutoLee.ino:
//   - lines are copied with truncation to LINELEN-1 chars (NUL-terminated)
//   - head wraps modulo LINES; serial counts total lines ever pushed
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>

namespace autolee {

template <uint16_t LINES, uint16_t LINELEN>
class LogRing {
 public:
  void push(const char* line) {
    std::strncpy(buf_[head_], line, LINELEN - 1);
    buf_[head_][LINELEN - 1] = '\0';
    head_ = (head_ + 1) % LINES;
    serial_++;
  }

  // Total lines ever pushed (monotonic; used to detect new lines for SSE).
  uint32_t serial() const { return serial_; }
  uint16_t head() const { return head_; }

  // Number of lines currently retained (<= LINES).
  uint16_t size() const { return serial_ < LINES ? (uint16_t)serial_ : LINES; }

  // Access retained lines oldest->newest by index [0, size()).
  const char* at(uint16_t i) const {
    uint16_t start = (serial_ < LINES) ? 0 : head_;  // oldest slot
    return buf_[(start + i) % LINES];
  }

  static constexpr uint16_t capacity() { return LINES; }
  static constexpr uint16_t lineLen() { return LINELEN; }

 private:
  char buf_[LINES][LINELEN] = {};
  uint16_t head_ = 0;
  uint32_t serial_ = 0;
};

}  // namespace autolee
