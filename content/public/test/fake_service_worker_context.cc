// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/fake_service_worker_context.h"
#include "content/public/browser/service_worker_context_observer.h"

#include "base/callback.h"
#include "base/logging.h"

namespace content {

FakeServiceWorkerContext::FakeServiceWorkerContext() {}
FakeServiceWorkerContext::~FakeServiceWorkerContext() {}

void FakeServiceWorkerContext::AddObserver(
    ServiceWorkerContextObserver* observer) {
  observers_.AddObserver(observer);
}
void FakeServiceWorkerContext::RemoveObserver(
    ServiceWorkerContextObserver* observer) {
  observers_.RemoveObserver(observer);
}
void FakeServiceWorkerContext::RegisterServiceWorker(
    const GURL& script_url,
    const blink::mojom::ServiceWorkerRegistrationOptions& options,
    ResultCallback callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::UnregisterServiceWorker(
    const GURL& pattern,
    ResultCallback callback) {
  NOTREACHED();
}
bool FakeServiceWorkerContext::StartingExternalRequest(
    int64_t service_worker_version_id,
    const std::string& request_uuid) {
  NOTREACHED();
  return false;
}
bool FakeServiceWorkerContext::FinishedExternalRequest(
    int64_t service_worker_version_id,
    const std::string& request_uuid) {
  NOTREACHED();
  return false;
}
void FakeServiceWorkerContext::CountExternalRequestsForTest(
    const GURL& url,
    CountExternalRequestsCallback callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::GetAllOriginsInfo(
    GetUsageInfoCallback callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::DeleteForOrigin(const GURL& origin,
                                               ResultCallback callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::CheckHasServiceWorker(
    const GURL& url,
    const GURL& other_url,
    CheckHasServiceWorkerCallback callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::ClearAllServiceWorkersForTest(
    base::OnceClosure) {
  NOTREACHED();
}
void FakeServiceWorkerContext::StartWorkerForPattern(
    const GURL& pattern,
    ServiceWorkerContext::StartWorkerCallback info_callback,
    base::OnceClosure failure_callback) {
  NOTREACHED();
}
void FakeServiceWorkerContext::StartServiceWorkerForNavigationHint(
    const GURL& document_url,
    StartServiceWorkerForNavigationHintCallback callback) {
  start_service_worker_for_navigation_hint_called_ = true;
}

void FakeServiceWorkerContext::StartServiceWorkerAndDispatchLongRunningMessage(
    const GURL& pattern,
    blink::TransferableMessage message,
    ResultCallback result_callback) {
  start_service_worker_and_dispatch_long_running_message_calls_.push_back(
      std::make_tuple(pattern, std::move(message), std::move(result_callback)));
}

void FakeServiceWorkerContext::StopAllServiceWorkersForOrigin(
    const GURL& origin) {
  NOTREACHED();
}
void FakeServiceWorkerContext::StopAllServiceWorkers(base::OnceClosure) {
  NOTREACHED();
}

void FakeServiceWorkerContext::NotifyObserversOnVersionActivated(
    int64_t version_id,
    const GURL& scope) {
  for (auto& observer : observers_)
    observer.OnVersionActivated(version_id, scope);
}

void FakeServiceWorkerContext::NotifyObserversOnVersionRedundant(
    int64_t version_id,
    const GURL& scope) {
  for (auto& observer : observers_)
    observer.OnVersionRedundant(version_id, scope);
}

void FakeServiceWorkerContext::NotifyObserversOnNoControllees(
    int64_t version_id,
    const GURL& scope) {
  for (auto& observer : observers_)
    observer.OnNoControllees(version_id, scope);
}

}  // namespace content
