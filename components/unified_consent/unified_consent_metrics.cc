// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/unified_consent/unified_consent_metrics.h"

#include "base/metrics/histogram_macros.h"

namespace {

// Histogram name.
const char kConsentBumpActionMetricName[] = "UnifiedConsent.ConsentBump.Action";

}  // namespace

namespace unified_consent {

namespace metrics {

void RecordConsentBumpMetric(UnifiedConsentBumpAction action) {
  UMA_HISTOGRAM_ENUMERATION(
      kConsentBumpActionMetricName, action,
      UnifiedConsentBumpAction::kUnifiedConsentBumpActionMoreOptionsMax);
}

}  // namespace metrics

}  // namespace unified_consent
