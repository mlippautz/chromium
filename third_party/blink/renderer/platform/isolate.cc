// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/isolate.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/thread_specific.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

namespace {

Isolate* g_current_main_thread_isolate = nullptr;
WTF::ThreadSpecific<Isolate*>* g_worker_thread_isolate = nullptr;

}  // namespace

std::atomic<size_t> Isolate::global_count_ = {0};
typename Isolate::CreateFunc Isolate::create_funcs_[Isolate::kMaxGlobals];
typename Isolate::DestroyFunc Isolate::destroy_funcs_[Isolate::kMaxGlobals];

Isolate* Isolate::Current() {
  if (WTF::IsMainThread()) {
    return g_current_main_thread_isolate;
  }
  DCHECK(g_worker_thread_isolate);
  return **g_worker_thread_isolate;
}

void Isolate::SetCurrentFromMainThread(Isolate* isolate) {
  DCHECK(WTF::IsMainThread());
  g_current_main_thread_isolate = isolate;
}

void Isolate::SetCurrentFromWorker(Isolate* isolate) {
  DCHECK(!WTF::IsMainThread());
  if (!g_worker_thread_isolate) {
    g_worker_thread_isolate = new ThreadSpecific<Isolate*>();
  }
  **g_worker_thread_isolate = isolate;
}

size_t Isolate::RegisterGlobal(CreateFunc create_func,
                               DestroyFunc destroy_func) {
  const size_t index = global_count_.fetch_add(1);
  CHECK_LT(index, kMaxGlobals);
  create_funcs_[index] = create_func;
  destroy_funcs_[index] = destroy_func;
  return index;
}

void* Isolate::CreateGlobal(size_t index) {
  globals_initialized_[index] = true;
  globals_[index] = create_funcs_[index]();
  return globals_[index];
}

}  // namespace blink

