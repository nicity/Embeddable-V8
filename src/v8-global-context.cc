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

#include "v8.h"
#include "allocation.h"
#include "api.h"
#include "bootstrapper.h"
#include "compilation-cache.h"
#include "compiler.h"
#include "debug.h"
#include "disassembler.h"
#include "execution.h"
#include "global-handles.h"
#include "jump-target.h"
#include "mark-compact.h"
#include "objects.h"
#include "regexp-stack.h"
#include "scanner.h"
#include "scopeinfo.h"
#include "serialize.h"
#include "stub-cache.h"
#include "top.h"
#include "v8threads.h"

namespace v8 {

internal::Thread::LocalStorageKey context_key =
  internal::Thread::CreateThreadLocalKey();

internal::V8Context* default_context;
internal::V8Context v8context;
V8ContextBinder default_context_binder(&v8context, true);
bool using_one_v8_instance = true;

AllowSeveralV8InstancesInProcess::AllowSeveralV8InstancesInProcess() {
  using_one_v8_instance = false;
}

AllowSeveralV8InstancesInProcess::~AllowSeveralV8InstancesInProcess() {
}

void BindContext(internal::V8Context* context,
  bool bind_default = using_one_v8_instance) {
  if (bind_default) {
    default_context = context;
  } else {
    ASSERT(v8_context() != context);
    internal::Thread::SetThreadLocal(context_key, context);
  }
}

V8ContextProvider::V8ContextProvider()
  :v8context_(new internal::V8Context()) {
}

V8ContextProvider::~V8ContextProvider() { delete v8context_; }

V8ContextBinder::V8ContextBinder(internal::V8Context* v8context,
  bool bind_default)
  :v8context_(v8context), bound_default_(bind_default) {
  BindContext(v8context_, bind_default);
}

V8ContextBinder::~V8ContextBinder() {
  BindContext(NULL, bound_default_);
}

internal::V8Context::V8Context()
  :thread_manager_data_(*new ThreadManagerData()),
  heap_data_(*new HeapData()),
  v8_data_(*new V8Data()),
  transcendental_cache_data_(*new TranscendentalCacheData()),
  descriptor_lookup_cache_data_(*new DescriptorLookupCacheData()),
  keyed_lookup_cache_data_(*new KeyedLookupCacheData()),
  stack_guard_data_(*new StackGuardData()),
  top_data_(*new TopData()),
  reg_exp_stack_data_(*new RegExpStackData()),
  serializer_data_(*new SerializerData()),
  context_slot_cache_data_(*new ContextSlotCacheData()),
  handle_scope_implementer_(*new HandleScopeImplementer()),
  handle_scope_data_(*new ImplementationUtilities::HandleScopeData()),
  stub_cache_data_(*new StubCacheData()),
  compilation_cache_data_(*new CompilationCacheData()),
  global_handles_data_(*new GlobalHandlesData()),
  memory_allocator_data_(*new MemoryAllocatorData()),
  code_range_data_(*new CodeRangeData()),
  mark_compact_collector_data_(*new MarkCompactCollectorData()),
  relocatable_data_(*new RelocatableData()),
  code_generator_data_(*new CodeGeneratorData()),
  bootstrapper_data_(*new BootstrapperData()),
  compiler_data_(*new CompilerData()),
  scanner_data_(*new ScannerData()),
  storage_data_(*new StorageData()),
  builtins_data_(*new BuiltinsData()),
  api_data_(*new ApiData()),
  objects_data_(*new ObjectsData()),
  stats_table_data_(*new StatsTableData()),
  runtime_data_(NULL),
  assembler_data_(NULL),
  counters_(*new Counters()),
  logger_data_(*new LoggerData()),
  log_data_(*new LogData()),
#ifdef ENABLE_DEBUGGER_SUPPORT
  debugger_data_(*new DebuggerData()),
  debug_data_(*new DebugData()),
#endif
  zone_data_(*new ZoneData()) {
  handle_scope_data_.Initialize();
  // explicitly bind during construction
  V8Context* const prev_v8_context = v8_context();
  BindContext(this);

  StackGuard::PostConstruct();
  Top::PostConstruct();
  Runtime::PostConstruct();
  Assembler::PostConstruct();

  #ifdef ENABLE_DISASSEMBLER
  Disassembler::PostConstruct();
  #endif

  BindContext(prev_v8_context);
}

internal::V8Context::~V8Context() {
  // explicitly bind during destruction
  BindContext(this);

  internal::ThreadManager::FreeThreadResources();
  Top::PreDestroy();
  StackGuard::PreDestroy();

  Runtime::PreDestroy();
  Assembler::PreDestroy();
  #ifdef ENABLE_DISASSEMBLER
  Disassembler::PreDestroy();
  #endif

  delete &thread_manager_data_;
  delete &v8_data_;
  delete &heap_data_;
  delete &transcendental_cache_data_;
  delete &descriptor_lookup_cache_data_;
  delete &keyed_lookup_cache_data_;
  delete &zone_data_;
  delete &top_data_;
  delete &stack_guard_data_;
  delete &reg_exp_stack_data_;
  delete &serializer_data_;
  delete &context_slot_cache_data_;
  delete &handle_scope_implementer_;
  delete &handle_scope_data_;
  delete &stub_cache_data_;
  delete &compilation_cache_data_;
  delete &global_handles_data_;
  delete &memory_allocator_data_;
  delete &code_range_data_;
  delete &mark_compact_collector_data_;
  delete &relocatable_data_;
  delete &code_generator_data_;
  delete &bootstrapper_data_;
  delete &scanner_data_;
  delete &compiler_data_;
  delete &storage_data_;
  delete &stats_table_data_;
  delete &api_data_;
  delete &objects_data_;
  delete &builtins_data_;
  delete &counters_;
  delete &logger_data_;
  delete &log_data_;

#ifdef ENABLE_DEBUGGER_SUPPORT
  delete &debugger_data_;
  delete &debug_data_;
#endif
  BindContext(NULL);
}
}

