#pragma once
#include "AgentSelection.h"
#include <string>

// A page is not a running interaction. Idle chat must not block VAD or invent
// a task, and late interaction events must not reopen a page after Back.
class NativeConversationView {
 public:
  void requestOpen(const NativeAgentSelection& selection, const char* profile,
                   bool available) {
    target_ = selection.browse;
    profile_ = profile ? profile : "";
    conversation_ = false;
    waiting_ = true;
    synchronize(selection, profile, available, false);
  }
  void back() { conversation_ = waiting_ = false; }
  bool synchronize(const NativeAgentSelection& selection, const char* profile,
                   bool available, bool hasError) {
    const bool wasConversation = conversation_, wasWaiting = waiting_;
    if (conversation_ || waiting_) {
      if (selection.browse != target_ || profile_ != (profile ? profile : "") ||
          (conversation_ && selection.confirmed && selection.active != target_)) {
        back();
      } else if (hasError) {
        waiting_ = false;
      } else if (waiting_ && available && selection.ready() &&
                 selection.active == target_ && !profile_.empty()) {
        conversation_ = true;
        waiting_ = false;
      }
    }
    return wasConversation != conversation_ || wasWaiting != waiting_;
  }
  bool conversation() const { return conversation_; }
  bool waiting() const { return waiting_; }

 private:
  bool conversation_ = false, waiting_ = false;
  size_t target_ = 0;
  std::string profile_;
};
