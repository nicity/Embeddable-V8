// Copyright 2006-2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_LOG_INL_H_
#define V8_LOG_INL_H_

#include "log.h"

namespace v8 {
namespace internal {

//
// VMState class implementation.  A simple stack of VM states held by the
// logger and partially threaded through the call stack.  States are pushed by
// VMState construction and popped by destruction.
//
#ifdef ENABLE_LOGGING_AND_PROFILING
inline const char* StateToString(StateTag state) {
  switch (state) {
    case JS:
      return "JS";
    case GC:
      return "GC";
    case COMPILER:
      return "COMPILER";
    case OTHER:
      return "OTHER";
    default:
      UNREACHABLE();
      return NULL;
  }
}

VMState::VMState(StateTag state) : disabled_(true), external_callback_(NULL) {
  if (!Logger::is_logging()) {
    return;
  }

  disabled_ = false;
#if !defined(ENABLE_HEAP_PROTECTION)
  // When not protecting the heap, there is no difference between
  // EXTERNAL and OTHER.  As an optimization in that case, we will not
  // perform EXTERNAL->OTHER transitions through the API.  We thus
  // compress the two states into one.
  if (state == EXTERNAL) state = OTHER;
#endif
  state_ = state;
  LoggerData& data = v8_context()->logger_data_;
  previous_ = data.current_state_;
  data.current_state_ = this;

  if (FLAG_log_state_changes) {
    LOG(UncheckedStringEvent("Entering", StateToString(state_)));
    if (previous_ != NULL) {
      LOG(UncheckedStringEvent("From", StateToString(previous_->state_)));
    }
  }

#ifdef ENABLE_HEAP_PROTECTION
  if (FLAG_protect_heap && previous_ != NULL) {
    if (state_ == EXTERNAL) {
      // We are leaving V8.
      ASSERT(previous_->state_ != EXTERNAL);
      Heap::Protect();
    } else if (previous_->state_ == EXTERNAL) {
      // We are entering V8.
      Heap::Unprotect();
    }
  }
#endif
}


VMState::~VMState() {
  if (disabled_) return;
  v8_context()->logger_data_.current_state_ = previous_;

  if (FLAG_log_state_changes) {
    LOG(UncheckedStringEvent("Leaving", StateToString(state_)));
    if (previous_ != NULL) {
      LOG(UncheckedStringEvent("To", StateToString(previous_->state_)));
    }
  }

#ifdef ENABLE_HEAP_PROTECTION
  if (FLAG_protect_heap && previous_ != NULL) {
    if (state_ == EXTERNAL) {
      // We are reentering V8.
      ASSERT(previous_->state_ != EXTERNAL);
      Heap::Unprotect();
    } else if (previous_->state_ == EXTERNAL) {
      // We are leaving V8.
      Heap::Protect();
    }
  }
#endif
}
#endif


} }  // namespace v8::internal

#endif  // V8_LOG_INL_H_
