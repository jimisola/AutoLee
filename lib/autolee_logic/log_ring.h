// ============================================================================
//  autolee_logic/log_ring.h
//  Fixed-capacity line ring buffer for the web log — pure, host-testable.
//  Mirrors the logBuf/logHead/logSerial ring in src/main.cpp:
//   - lines are copied with truncation to LINELEN-1 chars (NUL-terminated)
//   - head wraps modulo LINES; serial counts total lines ever pushed
//
//  Thread-safety: in the firmware, push() is called from both pump_task and
//  HTTP request handlers, while serial()/size()/at() are read concurrently
//  from sse_task (and potentially other HTTP handlers). This header must
//  stay buildable on a plain Linux host for host_test/ (no FreeRTOS there).
//
//  On-target (ESP_PLATFORM defined) every public method takes an ESP-IDF
//  portMUX critical section: this disables interrupts (and therefore
//  preemption) for the duration, so a spin-waiter can never starve behind a
//  holder that got preempted. A plain std::atomic_flag busy-wait would NOT
//  be safe here: the ESP32-C6 is single-core, so if a lower-priority task
//  (HTTP/sse_task) is preempted mid-critical-section by pump_task calling
//  webLog(), pump_task would spin forever waiting for a task the scheduler
//  can never run again - a hard hang that trips the task watchdog mid-motion.
//  On the host build, a std::atomic_flag spin is fine (single-threaded test
//  binary, no real contention, no preemption hazard).
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#else
#include <atomic>
#endif

namespace autolee {

#ifdef ESP_PLATFORM
// Disables interrupts for the critical section - safe against the
// single-core preemption hazard described above (see header comment).
class SpinLock {
 public:
  void lock() { portENTER_CRITICAL(&mux_); }
  void unlock() { portEXIT_CRITICAL(&mux_); }

 private:
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
#else
// Host build only: single-threaded test binary, no preemption hazard.
class SpinLock {
 public:
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
    }
  }
  void unlock() { flag_.clear(std::memory_order_release); }

 private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
#endif

class SpinLockGuard {
 public:
  explicit SpinLockGuard(SpinLock &lock) : lock_(lock) { lock_.lock(); }
  ~SpinLockGuard() { lock_.unlock(); }
  SpinLockGuard(const SpinLockGuard &) = delete;
  SpinLockGuard &operator=(const SpinLockGuard &) = delete;

 private:
  SpinLock &lock_;
};

template <uint16_t LINES, uint16_t LINELEN>
class LogRing {
 public:
  LogRing() = default;

  // SpinLock is not copyable, so LogRing isn't either — callers that used to
  // reassign a fresh instance (e.g. `g_log = LogRing<...>();` to clear it)
  // must use clear() instead (see below).
  LogRing(const LogRing &) = delete;
  LogRing &operator=(const LogRing &) = delete;

  void push(const char *line) {
    SpinLockGuard g(lock_);
    std::strncpy(buf_[head_], line, LINELEN - 1);
    buf_[head_][LINELEN - 1] = '\0';
    head_ = (head_ + 1) % LINES;
    serial_++;
  }

  // Resets the ring to empty. Equivalent to what re-assigning a
  // default-constructed LogRing used to do, but safe to call while other
  // tasks may be pushing/reading concurrently.
  void clear() {
    SpinLockGuard g(lock_);
    head_ = 0;
    serial_ = 0;
  }

  // Total lines ever pushed (monotonic; used to detect new lines for SSE).
  uint32_t serial() const {
    SpinLockGuard g(lock_);
    return serial_;
  }
  uint16_t head() const {
    SpinLockGuard g(lock_);
    return head_;
  }

  // Number of lines currently retained (<= LINES).
  uint16_t size() const {
    SpinLockGuard g(lock_);
    return serial_ < LINES ? (uint16_t)serial_ : LINES;
  }

  // Access retained lines oldest->newest by index [0, size()). head_/serial_
  // are read together under the lock so the computed slot is always
  // consistent with a single point in time (no torn index math from a
  // concurrent push() landing mid-computation).
  const char *at(uint16_t i) const {
    SpinLockGuard g(lock_);
    uint16_t start = (serial_ < LINES) ? 0 : head_;  // oldest slot
    return buf_[(start + i) % LINES];
  }

  static constexpr uint16_t capacity() { return LINES; }
  static constexpr uint16_t lineLen() { return LINELEN; }

 private:
  char buf_[LINES][LINELEN] = {};
  uint16_t head_ = 0;
  uint32_t serial_ = 0;
  mutable SpinLock lock_;
};

}  // namespace autolee
