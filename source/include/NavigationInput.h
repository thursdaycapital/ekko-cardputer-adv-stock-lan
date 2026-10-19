#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct NativeNavigationKeys {
  bool left = false, right = false, up = false, down = false, enter = false, back = false;
};

// M5Cardputer's getKey()/isKeyPressed() changes printable characters under
// Shift, Ctrl and Caps Lock. Both legends identify the same physical key.
template <class IsPressed>
NativeNavigationKeys nativeNavigationKeys(IsPressed pressed, bool enter) {
  NativeNavigationKeys keys;
  keys.left = pressed(',') || pressed('<');
  keys.right = pressed('/') || pressed('?');
  keys.up = pressed(';') || pressed(':');
  keys.down = pressed('.') || pressed('>');
  keys.enter = enter;
  keys.back = pressed('`') || pressed('~'); // Physical top-left Esc key.
  return keys;
}

struct NativeNavigationStats {
  uint32_t left = 0, right = 0, up = 0, down = 0;
  uint32_t ok = 0, back = 0;
  uint32_t dropped = 0;
  size_t queued = 0;
};

// Per-key rising edges permit changing direction without first releasing a
// different held arrow. The fixed queue never allocates during audio polling.
class NativeNavigationInput {
 public:
  void prime(const NativeNavigationKeys& keys) {
    held_ = {{keys.left, keys.right, keys.up, keys.down, keys.enter, keys.back}};
    head_ = count_ = 0;
  }
  void update(const NativeNavigationKeys& keys) {
    const std::array<bool, 6> down{{keys.left, keys.right, keys.up, keys.down, keys.enter, keys.back}};
    constexpr std::array<int, 6> actions{{-1, 1, -1, 1, 2, 3}};
    for (size_t i = 0; i < down.size(); ++i) {
      if (down[i] && !held_[i]) {
        if (i < counts_.size()) ++counts_[i];
        // Repeated OK is harmless during recording; it must never crowd out
        // a later Back/direction that is needed to cancel before submission.
        if (count_ == queue_.size() && actions[i] != 2) {
          for (size_t j = 0; j < count_; ++j) {
            if (queue_[(head_ + j) % queue_.size()] != 2) continue;
            for (size_t k = j; k + 1 < count_; ++k)
              queue_[(head_ + k) % queue_.size()] = queue_[(head_ + k + 1) % queue_.size()];
            --count_;
            ++dropped_;
            break;
          }
        }
        if (count_ < queue_.size()) {
          queue_[(head_ + count_) % queue_.size()] = actions[i];
          ++count_;
        } else {
          ++dropped_;
        }
      }
      held_[i] = down[i];
    }
  }

  int pop() {
    if (!count_) return 0;
    const int action = queue_[head_];
    head_ = (head_ + 1) % queue_.size();
    --count_;
    return action;
  }
  bool pending() const { return count_ != 0; }
  bool interruptPending() const {
    for (size_t i = 0; i < count_; ++i)
      if (queue_[(head_ + i) % queue_.size()] != 2) return true;
    return false;
  }
  uint32_t count() const { return counts_[0] + counts_[1] + counts_[2] + counts_[3]; }
  NativeNavigationStats stats() const {
    NativeNavigationStats result;
    result.left = counts_[0]; result.right = counts_[1];
    result.up = counts_[2]; result.down = counts_[3];
    result.ok = counts_[4]; result.back = counts_[5];
    result.dropped = dropped_; result.queued = count_;
    return result;
  }

 private:
  std::array<bool, 6> held_{};
  std::array<uint32_t, 6> counts_{};
  std::array<int, 8> queue_{};
  size_t head_ = 0, count_ = 0;
  uint32_t dropped_ = 0;
};
