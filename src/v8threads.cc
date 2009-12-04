// Copyright 2008 the V8 project authors. All rights reserved.
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

#include "api.h"
#include "bootstrapper.h"
#include "debug.h"
#include "execution.h"
#include "v8threads.h"
#include "regexp-stack.h"

namespace v8 {

// Constructor for the Locker object.  Once the Locker is constructed the
// current thread will be guaranteed to have the big V8 lock.
Locker::Locker() : has_lock_(false), top_level_(true) {
  internal::V8Context* const v8context = v8_context();

  // Record that the Locker has been used at least once.
  v8context->v8_data_.active_ = true;
  // Get the big lock if necessary.
  if (!internal::ThreadManager::IsLockedByCurrentThread()) {
    internal::ThreadManager::Lock();
    has_lock_ = true;
    // Make sure that V8 is initialized.  Archiving of threads interferes
    // with deserialization by adding additional root pointers, so we must
    // initialize here, before anyone can call ~Locker() or Unlocker().
    if (!internal::V8::IsRunning()) {
      V8::Initialize();
    }
    // This may be a locker within an unlocker in which case we have to
    // get the saved state for this thread and restore it.
    if (internal::ThreadManager::RestoreThread()) {
      top_level_ = false;
    } else {
      internal::ExecutionAccess access;
      internal::StackGuard::ClearThread(access);
      internal::StackGuard::InitThread(access);
    }
  }
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());

  // Make sure this thread is assigned a thread id.
  internal::ThreadManager::AssignId();
}

bool Locker::IsActive() {
  return v8_context()->v8_data_.active_;
}

bool Locker::IsLocked() {
  return internal::ThreadManager::IsLockedByCurrentThread();
}


Locker::~Locker() {
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
  if (has_lock_) {
    if (top_level_) {
      internal::ThreadManager::FreeThreadResources();
    } else {
      internal::ThreadManager::ArchiveThread();
    }
    internal::ThreadManager::Unlock();
  }
}


Unlocker::Unlocker() {
  ASSERT(internal::ThreadManager::IsLockedByCurrentThread());
  internal::ThreadManager::ArchiveThread();
  internal::ThreadManager::Unlock();
}


Unlocker::~Unlocker() {
  ASSERT(!internal::ThreadManager::IsLockedByCurrentThread());
  internal::ThreadManager::Lock();
  internal::ThreadManager::RestoreThread();
}


void Locker::StartPreemption(int every_n_ms) {
  v8::internal::ContextSwitcher::StartPreemption(every_n_ms);
}


void Locker::StopPreemption() {
  v8::internal::ContextSwitcher::StopPreemption();
}


namespace internal {


bool ThreadManager::RestoreThread() {
  ThreadManagerData& thread_manager_data = v8_context()->thread_manager_data_;
  // First check whether the current thread has been 'lazily archived', ie
  // not archived at all.  If that is the case we put the state storage we
  // had prepared back in the free list, since we didn't need it after all.
  if (thread_manager_data.lazily_archived_thread_.IsSelf()) {
    thread_manager_data.lazily_archived_thread_.Initialize(
      ThreadHandle::INVALID);
    ASSERT(Thread::GetThreadLocal(thread_manager_data.thread_state_key_) ==
           thread_manager_data.lazily_archived_thread_state_);
    thread_manager_data.lazily_archived_thread_state_->set_id(kInvalidId);
    thread_manager_data.lazily_archived_thread_state_->LinkInto(
      ThreadState::FREE_LIST);

    thread_manager_data.lazily_archived_thread_state_ = NULL;
    Thread::SetThreadLocal(thread_manager_data.thread_state_key_, NULL);
    return true;
  }

  // Make sure that the preemption thread cannot modify the thread state while
  // it is being archived or restored.
  ExecutionAccess access;

  // If there is another thread that was lazily archived then we have to really
  // archive it now.
  if (thread_manager_data.lazily_archived_thread_.IsValid()) {
    EagerlyArchiveThread();
  }
  ThreadState* state = reinterpret_cast<ThreadState*>(
    Thread::GetThreadLocal(thread_manager_data.thread_state_key_));
  if (state == NULL) {
    // This is a new thread.
    StackGuard::InitThread(access);
    return false;
  }
  char* from = state->data();
  from = HandleScopeImplementer::RestoreThread(from);
  from = Top::RestoreThread(from);
  from = Relocatable::RestoreState(from);
#ifdef ENABLE_DEBUGGER_SUPPORT
  from = Debug::RestoreDebug(from);
#endif
  from = StackGuard::RestoreStackGuard(from);
  from = RegExpStack::RestoreStack(from);
  from = Bootstrapper::RestoreState(from);
  Thread::SetThreadLocal(thread_manager_data.thread_state_key_, NULL);
  if (state->terminate_on_restore()) {
    StackGuard::TerminateExecution();
    state->set_terminate_on_restore(false);
  }
  state->set_id(kInvalidId);
  state->Unlink();
  state->LinkInto(ThreadState::FREE_LIST);
  return true;
}


void ThreadManager::Lock() {
  v8_context()->thread_manager_data_.mutex_->Lock();
  v8_context()->thread_manager_data_.mutex_owner_.Initialize(
    ThreadHandle::SELF);
  ASSERT(IsLockedByCurrentThread());
}


void ThreadManager::Unlock() {
  v8_context()->thread_manager_data_.mutex_owner_.Initialize(
    ThreadHandle::INVALID);
  v8_context()->thread_manager_data_.mutex_->Unlock();
}


static int ArchiveSpacePerThread() {
  return HandleScopeImplementer::ArchiveSpacePerThread() +
                            Top::ArchiveSpacePerThread() +
#ifdef ENABLE_DEBUGGER_SUPPORT
                          Debug::ArchiveSpacePerThread() +
#endif
                     StackGuard::ArchiveSpacePerThread() +
                    RegExpStack::ArchiveSpacePerThread() +
                   Bootstrapper::ArchiveSpacePerThread() +
                    Relocatable::ArchiveSpacePerThread();
}




ThreadState::ThreadState() : id_(ThreadManager::kInvalidId),
                             terminate_on_restore_(false),
                             next_(this), previous_(this) {
}


void ThreadState::AllocateSpace() {
  data_ = NewArray<char>(ArchiveSpacePerThread());
}


void ThreadState::Unlink() {
  next_->previous_ = previous_;
  previous_->next_ = next_;
}


void ThreadState::LinkInto(List list) {
  ThreadState* flying_anchor = list == FREE_LIST ?
    v8_context()->thread_manager_data_.free_anchor_
    : v8_context()->thread_manager_data_.in_use_anchor_;
  next_ = flying_anchor->next_;
  previous_ = flying_anchor;
  flying_anchor->next_ = this;
  next_->previous_ = this;
}


ThreadState* ThreadState::GetFree() {
  ThreadState* gotten = v8_context()->thread_manager_data_.free_anchor_->next_;
  if (gotten == v8_context()->thread_manager_data_.free_anchor_) {
    ThreadState* new_thread_state = new ThreadState();
    new_thread_state->AllocateSpace();
    return new_thread_state;
  }
  return gotten;
}


// Gets the first in the list of archived threads.
ThreadState* ThreadState::FirstInUse() {
  return v8_context()->thread_manager_data_.in_use_anchor_->Next();
}


ThreadState* ThreadState::Next() {
  if (next_ == v8_context()->thread_manager_data_.in_use_anchor_) return NULL;
  return next_;
}


ThreadManagerData::ThreadManagerData()
// Thread ids must start with 1, because in TLS having thread id 0 can't
// be distinguished from not having a thread id at all (since NULL is
// defined as 0.)
  :last_id_(0),
  mutex_(OS::CreateMutex()),
  mutex_owner_(ThreadHandle::INVALID),
  lazily_archived_thread_(ThreadHandle::INVALID),
  lazily_archived_thread_state_(NULL),
  free_anchor_(new ThreadState()),
  in_use_anchor_(new ThreadState()),
  singleton_(NULL),
  thread_state_key_(Thread::CreateThreadLocalKey()),
  thread_id_key_(Thread::CreateThreadLocalKey()) {
}

void ThreadManager::ArchiveThread() {
  ThreadManagerData& thread_manager_data = v8_context()->thread_manager_data_;
  ASSERT(!thread_manager_data.lazily_archived_thread_.IsValid());
  ASSERT(!IsArchived());
  ThreadState* state = ThreadState::GetFree();
  state->Unlink();
  Thread::SetThreadLocal(thread_manager_data .thread_state_key_,
    reinterpret_cast<void*>(state));

  thread_manager_data.lazily_archived_thread_.Initialize(ThreadHandle::SELF);
  thread_manager_data.lazily_archived_thread_state_ = state;
  ASSERT(state->id() == kInvalidId);
  state->set_id(CurrentId());
  ASSERT(state->id() != kInvalidId);
}


void ThreadManager::EagerlyArchiveThread() {
  ThreadManagerData& thread_manager_data = v8_context()->thread_manager_data_;
  ThreadState* state = thread_manager_data.lazily_archived_thread_state_;
  state->LinkInto(ThreadState::IN_USE_LIST);
  char* to = state->data();
  // Ensure that data containing GC roots are archived first, and handle them
  // in ThreadManager::Iterate(ObjectVisitor*).
  to = HandleScopeImplementer::ArchiveThread(to);
  to = Top::ArchiveThread(to);
  to = Relocatable::ArchiveState(to);
#ifdef ENABLE_DEBUGGER_SUPPORT
  to = Debug::ArchiveDebug(to);
#endif
  to = StackGuard::ArchiveStackGuard(to);
  to = RegExpStack::ArchiveStack(to);
  to = Bootstrapper::ArchiveState(to);
  thread_manager_data.lazily_archived_thread_.Initialize(ThreadHandle::INVALID);
  thread_manager_data.lazily_archived_thread_state_ = NULL;
}


void ThreadManager::FreeThreadResources() {
  HandleScopeImplementer::FreeThreadResources();
  Top::FreeThreadResources();
#ifdef ENABLE_DEBUGGER_SUPPORT
  Debug::FreeThreadResources();
#endif
  StackGuard::FreeThreadResources();
  RegExpStack::FreeThreadResources();
  Bootstrapper::FreeThreadResources();
}


bool ThreadManager::IsArchived() {
  return Thread::HasThreadLocal(
    v8_context()->thread_manager_data_.thread_state_key_);
}


void ThreadManager::Iterate(ObjectVisitor* v) {
  // Expecting no threads during serialization/deserialization
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data = HandleScopeImplementer::Iterate(v, data);
    data = Top::Iterate(v, data);
    data = Relocatable::Iterate(v, data);
  }
}


void ThreadManager::MarkCompactPrologue(bool is_compacting) {
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    Top::MarkCompactPrologue(is_compacting, data);
  }
}


void ThreadManager::MarkCompactEpilogue(bool is_compacting) {
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    Top::MarkCompactEpilogue(is_compacting, data);
  }
}


int ThreadManager::CurrentId() {
  return Thread::GetThreadLocalInt(
    v8_context()->thread_manager_data_.thread_id_key_);
}


void ThreadManager::AssignId() {
  if (!HasId()) {
    ASSERT(Locker::IsLocked());
    ThreadManagerData& thread_manager_data = v8_context()->thread_manager_data_;
    int thread_id = ++thread_manager_data.last_id_;
    ASSERT(thread_id > 0);  // see the comment near last_id_ definition.
    Thread::SetThreadLocalInt(thread_manager_data.thread_id_key_, thread_id);
    Top::set_thread_id(thread_id);
  }
}


bool ThreadManager::HasId() {
  return Thread::HasThreadLocal(
    v8_context()->thread_manager_data_.thread_id_key_);
}


void ThreadManager::TerminateExecution(int thread_id) {
  for (ThreadState* state = ThreadState::FirstInUse();
       state != NULL;
       state = state->Next()) {
    if (thread_id == state->id()) {
      state->set_terminate_on_restore(true);
    }
  }
}

ContextSwitcher::ContextSwitcher(int every_n_ms)
  : keep_going_(true),
    sleep_ms_(every_n_ms) {
}


// Set the scheduling interval of V8 threads. This function starts the
// ContextSwitcher thread if needed.
void ContextSwitcher::StartPreemption(int every_n_ms) {
  ASSERT(Locker::IsLocked());
  ContextSwitcher* & singleton = v8_context()->thread_manager_data_.singleton_;
  if (singleton == NULL) {
    // If the ContextSwitcher thread is not running at the moment start it now.
    singleton = new ContextSwitcher(every_n_ms);
    singleton->Start();
  } else {
    // ContextSwitcher thread is already running, so we just change the
    // scheduling interval.
    singleton->sleep_ms_ = every_n_ms;
  }
}


// Disable preemption of V8 threads. If multiple threads want to use V8 they
// must cooperatively schedule amongst them from this point on.
void ContextSwitcher::StopPreemption() {
  ASSERT(Locker::IsLocked());
  ContextSwitcher* & singleton = v8_context()->thread_manager_data_.singleton_;
  if (singleton != NULL) {
    // The ContextSwitcher thread is running. We need to stop it and release
    // its resources.
    singleton->keep_going_ = false;
    singleton->Join();  // Wait for the ContextSwitcher thread to exit.
    // Thread has exited, now we can delete it.
    delete(singleton);
    singleton = NULL;
  }
}


// Main loop of the ContextSwitcher thread: Preempt the currently running V8
// thread at regular intervals.
void ContextSwitcher::Run() {
  while (keep_going_) {
    OS::Sleep(sleep_ms_);
    StackGuard::Preempt();
  }
}


// Acknowledge the preemption by the receiving thread.
void ContextSwitcher::PreemptionReceived() {
  ASSERT(Locker::IsLocked());
  // There is currently no accounting being done for this. But could be in the
  // future, which is why we leave this in.
}


}  // namespace internal
}  // namespace v8
