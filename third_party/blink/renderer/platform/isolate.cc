// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/isolate.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

namespace {

Isolate* g_current_main_thread_isolate = nullptr;
WTF::ThreadSpecific<Isolate*>* g_worker_thread_isolate = nullptr;

}  // namespace

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

}  // namespace blink

