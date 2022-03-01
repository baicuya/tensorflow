/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_PJRT_PJRT_EVENT_H_
#define TENSORFLOW_COMPILER_XLA_PJRT_PJRT_EVENT_H_

#include <functional>
#include <utility>

#include "absl/types/span.h"
#include "tfrt/host_context/async_value.h"  // from @tf_runtime
#include "tfrt/host_context/async_value_ref.h"  // from @tf_runtime
#include "tfrt/host_context/host_context.h"  // from @tf_runtime
#include "tfrt/support/ref_count.h"  // from @tf_runtime

namespace xla {

struct PjRtEventContext {
 public:
  struct ProfilingKeys {
    uint64_t traceme_context_id = -1;
  };
  using OnBlockStartFn = std::function<ProfilingKeys()>;
  using OnBlockEndFn = std::function<void(ProfilingKeys)>;

  static PjRtEventContext Create();

 private:
  template <class T>
  friend class PjRtEvent;

  explicit PjRtEventContext(std::unique_ptr<tfrt::HostContext> ctx)
      : host_ctx(std::move(ctx)) {}
  std::unique_ptr<tfrt::HostContext> host_ctx;
};

template <class T>
class PjRtEvent {
 public:
  struct Event {
   public:
    explicit Event() = default;
    Event(Event&& other) = default;
    Event(const Event& other) : avr(other.avr.CopyRef()) {}
    Event& operator=(const Event& other) {
      avr = other.avr.CopyRef();
      return *this;
    }
    bool operator!() { return !avr; }

    void Set(T value) { avr.emplace(std::move(value)); }

   private:
    friend class PjRtEvent<T>;
    explicit Event(tfrt::AsyncValueRef<T> ref) : avr(std::move(ref)) {}
    tfrt::AsyncValueRef<T> avr;
  };

  static Event CreateUnSetEvent() {
    return Event(tfrt::MakeUnconstructedAsyncValueRef<T>());
  }

  explicit PjRtEvent(T t)
      : host_ctx_(nullptr),
        event_(tfrt::MakeAvailableAsyncValueRef<T>(t)),
        on_block_start_([]() { return PjRtEventContext::ProfilingKeys(); }),
        on_block_end_([](PjRtEventContext::ProfilingKeys) {}) {}

  explicit PjRtEvent(
      tfrt::HostContext* host_ctx, tfrt::AsyncValueRef<T> event,
      PjRtEventContext::OnBlockStartFn on_block_start =
          []() { return PjRtEventContext::ProfilingKeys(); },
      PjRtEventContext::OnBlockEndFn on_block_end =
          [](PjRtEventContext::ProfilingKeys) {})
      : host_ctx_(host_ctx),
        event_(std::move(event)),
        on_block_start_(std::move(on_block_start)),
        on_block_end_(std::move(on_block_end)) {}

  explicit PjRtEvent(
      const PjRtEventContext& ctx, Event event,
      PjRtEventContext::OnBlockStartFn on_block_start =
          []() { return PjRtEventContext::ProfilingKeys(); },
      PjRtEventContext::OnBlockEndFn on_block_end =
          [](PjRtEventContext::ProfilingKeys) {})
      : host_ctx_(ctx.host_ctx.get()),
        event_(std::move(event.avr)),
        on_block_start_(std::move(on_block_start)),
        on_block_end_(std::move(on_block_end)) {}

  T BlockHostUntilReady() {
    if (!event_.IsAvailable()) {
      host_ctx_->Await({event_.CopyRCRef()});
    }
    DCHECK(event_.IsConcrete());
    return *event_;
  }

  void OnReady(std::function<void(T)> callback) {
    event_.AndThen(
        [event = event_.CopyRef(), callback = std::move(callback)]() {
          DCHECK(event.IsConcrete());
          callback(*event);
        });
  }

  static tfrt::AsyncValueRef<T> MakeUnconstructedAVR() {
    return tfrt::MakeUnconstructedAsyncValueRef<T>();
  }

 private:
  tfrt::HostContext* host_ctx_;  // not owned
  tfrt::AsyncValueRef<T> event_;
  PjRtEventContext::OnBlockStartFn on_block_start_;
  PjRtEventContext::OnBlockEndFn on_block_end_;
};

}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_PJRT_PJRT_EVENT_H_
