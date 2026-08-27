// What went wrong during startup, for the surfaces that did not exist yet.
//
// Some failures happen before the web server is up, so there is no browser to
// push them to and no SSE stream to push down. Until now they went only to a
// serial console nobody was watching. This holds them until something can ask.
//
// Deliberately a boot record, not a running log: it is frozen once startup
// finishes, so what it reports is exactly the set of problems this boot hit and
// does not change again until the device restarts. `log_ring.h` is the running
// log; this is not that.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "json_escape.h"

namespace autolee {

struct BootIssue {
  // Stable machine-readable identifier, e.g. "nvs-erased". The UI keys wording
  // off this; `detail` carries the run-specific part.
  char code[24];
  char detail[176];
};

class BootReport {
 public:
  // Eight is well past the number of things that can independently go wrong in
  // app_main(). Overflow is counted rather than silently dropped, so a report
  // can never quietly claim to be complete when it is not.
  static constexpr size_t kCapacity = 8;

  void add(const char *code, const char *detail) {
    if (frozen_) return;  // a late writer is a bug, not a boot issue
    if (count_ >= kCapacity) {
      dropped_++;
      return;
    }
    BootIssue &it = items_[count_++];
    snprintf(it.code, sizeof(it.code), "%s", code ? code : "");
    snprintf(it.detail, sizeof(it.detail), "%s", detail ? detail : "");
  }

  // Called once app_main() has finished bringing everything up. After this the
  // report is immutable, which is what lets the UI say the list will not change
  // until restart.
  void freeze() { frozen_ = true; }

  bool frozen() const { return frozen_; }
  size_t size() const { return count_; }
  size_t dropped() const { return dropped_; }
  const BootIssue &at(size_t i) const { return items_[i]; }

  int toJson(char *out, size_t n) const {
    size_t o = 0;
    int w = snprintf(out, n, "{\"frozen\":%s,\"dropped\":%u,\"issues\":[",
                     frozen_ ? "true" : "false", (unsigned)dropped_);
    if (w < 0) return w;
    o = (size_t)w;
    for (size_t i = 0; i < count_ && o < n; i++) {
      char code[sizeof(items_[i].code) * 2];
      char detail[sizeof(items_[i].detail) * 2];
      jsonEscape(items_[i].code, code, sizeof(code));
      jsonEscape(items_[i].detail, detail, sizeof(detail));
      w = snprintf(out + o, n - o, "%s{\"code\":\"%s\",\"detail\":\"%s\"}", i ? "," : "", code,
                   detail);
      if (w < 0) return w;
      o += (size_t)w;
    }
    // Written unconditionally so a truncated buffer still returns the length
    // the caller needs to detect the truncation, matching buildStateJson().
    w = snprintf(out + (o < n ? o : n - 1), o < n ? n - o : 1, "]}");
    if (w < 0) return w;
    return (int)(o + (size_t)w);
  }

 private:
  BootIssue items_[kCapacity]{};
  size_t count_ = 0;
  size_t dropped_ = 0;
  bool frozen_ = false;
};

}  // namespace autolee
