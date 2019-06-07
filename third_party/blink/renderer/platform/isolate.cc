// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/isolate.h"

#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/thread_specific.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

namespace {

ThreadSpecific<Isolate*>& GetIsolateCache() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(ThreadSpecific<Isolate*>, isolate_cache, ());
  return isolate_cache;
}

}  // namespace

// static
Isolate* Isolate::g_current_main_thread_isolate_ = nullptr;

std::atomic<size_t> Isolate::global_count_ = {0};
typename Isolate::CreateFunc Isolate::create_funcs_[Isolate::kMaxGlobals];
constexpr size_t Isolate::kMaxGlobals;

// static
Isolate* Isolate::Current() {
  if (WTF::IsMainThread()) {
    DCHECK(g_current_main_thread_isolate_);
    return g_current_main_thread_isolate_;
  }
  DCHECK(*GetIsolateCache());
  return *GetIsolateCache();
}

// static
void Isolate::SetCurrentFromMainThread(Isolate* isolate) {
  DCHECK(WTF::IsMainThread());
  g_current_main_thread_isolate_ = isolate;
}

// static
void Isolate::SetCurrentFromWorker(Isolate* isolate) {
  DCHECK(!WTF::IsMainThread());
  DCHECK(!*GetIsolateCache());
  *GetIsolateCache() = isolate;
}

// static
size_t Isolate::RegisterGlobal(CreateFunc create_func) {
  const size_t index = global_count_.fetch_add(1, std::memory_order_relaxed);
  CHECK_LT(index, kMaxGlobals);
  create_funcs_[index] = create_func;
  return index;
}

// static
bool Isolate::IsMainThread() {
  return WTF::IsMainThread();
}

void* Isolate::CreateGlobal(size_t index) {
  globals_initialized_[index] = true;
  globals_[index] = create_funcs_[index]();
  return globals_[index];
}

Isolate::Isolate(Isolate* parent) : parent_(parent) {}

Isolate* Isolate::ParentIsolate() const {
  return (parent_) ? parent_ : const_cast<Isolate*>(this);
}

ScopedSetMainThreadIsolate::ScopedSetMainThreadIsolate(Isolate* isolate) {
  DCHECK(isolate);
  DCHECK(WTF::IsMainThread());
  DCHECK(!Isolate::MainThreadCurrent());
  Isolate::SetCurrentFromMainThread(isolate);
}

ScopedSetMainThreadIsolate::~ScopedSetMainThreadIsolate() {
  DCHECK(WTF::IsMainThread());
  DCHECK(!Isolate::MainThreadCurrent());
  Isolate::SetCurrentFromMainThread(nullptr);
}

}  // namespace blink
