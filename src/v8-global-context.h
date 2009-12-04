// Copyright 2009 the V8 project authors. All rights reserved.
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

#ifndef V8_GLOBAL_CONTEXT_H_
#define V8_GLOBAL_CONTEXT_H_
#include "apiutils.h"
#include "utils.h"
#include "platform.h"

namespace disasm {
  class DisassemblerData;
}

namespace v8 {

namespace internal {
class HeapData;
class ThreadManagerData;
class V8Data;
class TranscendentalCacheData;
class DescriptorLookupCacheData;
class KeyedLookupCacheData;
class ZoneData;
class TopData;
class StackGuardData;
class RegExpStackData;
class SerializerData;
class ContextSlotCacheData;
class HandleScopeImplementer;
class StubCacheData;
class CompilationCacheData;
class GlobalHandlesData;
class MemoryAllocatorData;
class CodeRangeData;
class MarkCompactCollectorData;
class RelocatableData;
class CodeGeneratorData;
class BootstrapperData;
class ScannerData;
class CompilerData;
class StorageData;
class StatsTableData;
class RuntimeData;
class AssemblerData;
class ApiData;
class ObjectsData;
class BuiltinsData;
class Counters;
class LoggerData;
class LogData;
#ifdef ENABLE_DEBUGGER_SUPPORT
class DebugData;
class DebuggerData;
#endif


class V8Context {
 public:
  ThreadManagerData& thread_manager_data_;
  V8Data& v8_data_;
  HeapData& heap_data_;
  TranscendentalCacheData& transcendental_cache_data_;
  DescriptorLookupCacheData& descriptor_lookup_cache_data_;
  KeyedLookupCacheData& keyed_lookup_cache_data_;
  ZoneData& zone_data_;
  TopData& top_data_;
  StackGuardData& stack_guard_data_;
  RegExpStackData& reg_exp_stack_data_;
  SerializerData& serializer_data_;
  ContextSlotCacheData& context_slot_cache_data_;
  HandleScopeImplementer& handle_scope_implementer_;
  ImplementationUtilities::HandleScopeData& handle_scope_data_;
  StubCacheData& stub_cache_data_;
  CompilationCacheData& compilation_cache_data_;
  GlobalHandlesData& global_handles_data_;
  MemoryAllocatorData& memory_allocator_data_;
  CodeRangeData& code_range_data_;
  MarkCompactCollectorData& mark_compact_collector_data_;
  RelocatableData& relocatable_data_;
  CodeGeneratorData& code_generator_data_;
  BootstrapperData& bootstrapper_data_;
  CompilerData& compiler_data_;
  ScannerData& scanner_data_;
  StorageData& storage_data_;
  StatsTableData& stats_table_data_;
  RuntimeData* runtime_data_;
  AssemblerData* assembler_data_;
  ApiData& api_data_;
  ObjectsData& objects_data_;
  BuiltinsData& builtins_data_;
  Counters& counters_;
  LoggerData& logger_data_;
  LogData& log_data_;

  // #ifdef ENABLE_DISASSEMBLER
  disasm::DisassemblerData* disassembler_data_;
  // #endif

  #ifdef ENABLE_DEBUGGER_SUPPORT
  DebugData& debug_data_;
  DebuggerData& debugger_data_;
  #endif


  V8Context();
  ~V8Context();
 private:
  DISALLOW_COPY_AND_ASSIGN(V8Context);
};
}

extern internal::V8Context* default_context;
extern bool using_one_v8_instance;
extern internal::Thread::LocalStorageKey context_key;

inline internal::V8Context* v8_context() {
  if (using_one_v8_instance) return default_context;
  internal::V8Context* v8context = reinterpret_cast<internal::V8Context*>(
    internal::Thread::GetThreadLocal(context_key));
  if (v8context == NULL) v8context = default_context;
  return v8context;
}

template<class LockType>
class V8ResourceLocker {
  LockType* lock_;
  bool locked_;
 public:
  explicit V8ResourceLocker(LockType* lock)
    :locked_(false), lock_(lock) {
    if (!using_one_v8_instance) {
      lock_->Lock();
      locked_ = true;
    }
  }

  ~V8ResourceLocker() {
    if (locked_) lock_->Unlock();
  }
};

class MutexLockAdapter {
  internal::Mutex* mutex_;
 public:
  explicit MutexLockAdapter(internal::Mutex *mutex):mutex_(mutex) {}

  void Lock() {
    mutex_->Lock();
  }

  void Unlock() {
    mutex_->Unlock();
  }
};

typedef V8ResourceLocker<MutexLockAdapter> V8SharedStateLocker;
}
#endif  // V8_GLOBAL_CONTEXT_H_

