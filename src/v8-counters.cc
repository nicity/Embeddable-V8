// Copyright 2007-2008 the V8 project authors. All rights reserved.
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

#include "v8.h"

#include "v8-counters.h"

namespace v8 {
namespace internal {
static StatsCounter state_counters[] = {
#define COUNTER_NAME(name) \
  { "c:V8.State" #name, NULL, false },
  STATE_TAG_LIST(COUNTER_NAME)
#undef COUNTER_NAME
};

enum {
  #define COUNTER_ID(name) k##name,
  STATE_TAG_LIST(COUNTER_ID)
#undef COUNTER_ID
  state_counters_count
};

Counters::Counters()
  : state_counters(new StatsCounter[state_counters_count]) {
  #define HT(name, caption) \
    HistogramTimer _##name = { #caption, NULL, false, 0, 0 }; \
    name = _##name;                                           \

    HISTOGRAM_TIMER_LIST(HT)
  #undef HT

  #define SC(name, caption) \
    StatsCounter _##name = { "c:" #caption, NULL, false }; \
    name = _##name;

    STATS_COUNTER_LIST_1(SC)
    STATS_COUNTER_LIST_2(SC)
  #undef SC

  for (int i = 0; i < state_counters_count; ++i) {
    state_counters[i] = v8::internal::state_counters[i];
  }
}

} }  // namespace v8::internal
