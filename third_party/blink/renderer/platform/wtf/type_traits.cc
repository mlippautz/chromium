// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/wtf/type_traits.h"

#include "third_party/blink/renderer/platform/wtf/thread_specific.h"

namespace WTF {
namespace internal {

namespace {

ThreadSpecific<ScopedBanGarbageCollectedAlloc*>&
GetBanGarbageCollectedAllocTLS() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      ThreadSpecific<ScopedBanGarbageCollectedAlloc*>,
      ban_garbage_collected_alloc_tls, ());
  return ban_garbage_collected_alloc_tls;
}

}  // namespace

#if DCHECK_IS_ON()
ScopedBanGarbageCollectedAlloc::ScopedBanGarbageCollectedAlloc() {
  auto& ban_garbage_collected_alloc_tls = GetBanGarbageCollectedAllocTLS();
  DCHECK(!ban_garbage_collected_alloc_tls.IsSet());
  *ban_garbage_collected_alloc_tls = this;
}

ScopedBanGarbageCollectedAlloc::~ScopedBanGarbageCollectedAlloc() {
  auto& ban_garbage_collected_alloc_tls = GetBanGarbageCollectedAllocTLS();
  DCHECK_EQ(*ban_garbage_collected_alloc_tls, this);
  *ban_garbage_collected_alloc_tls = nullptr;
}
#endif  // DCHECK_IS_ON()

}  // namespace internal

bool IsGarbageCollectedAllocAllowed() {
  return internal::GetBanGarbageCollectedAllocTLS().IsSet();
}

}  // namespace WTF
