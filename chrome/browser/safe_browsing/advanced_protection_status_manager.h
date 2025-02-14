// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_ADVANCED_PROTECTION_STATUS_MANAGER_H_
#define CHROME_BROWSER_SAFE_BROWSING_ADVANCED_PROTECTION_STATUS_MANAGER_H_

#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "google_apis/gaia/oauth2_token_service.h"

class Profile;

namespace safe_browsing {

// Responsible for keeping track of advanced protection status of the sign-in
// profile.
// Note that for profile that is not signed-in or is in incognito mode, we
// consider it NOT under advanced protection.
class AdvancedProtectionStatusManager : public KeyedService,
                                        public AccountTrackerService::Observer,
                                        public SigninManagerBase::Observer {
 public:
  class AdvancedProtectionTokenConsumer : public OAuth2TokenService::Consumer {
   public:
    AdvancedProtectionTokenConsumer(const std::string& account_id,
                                    AdvancedProtectionStatusManager* manager);

    ~AdvancedProtectionTokenConsumer() override;

    // OAuth2TokenService::Consumer implementation.
    void OnGetTokenSuccess(const OAuth2TokenService::Request* request,
                           const OAuth2AccessTokenConsumer::TokenResponse&
                               token_response) override;
    void OnGetTokenFailure(const OAuth2TokenService::Request* request,
                           const GoogleServiceAuthError& error) override;

   private:
    AdvancedProtectionStatusManager* manager_;

    DISALLOW_COPY_AND_ASSIGN(AdvancedProtectionTokenConsumer);
  };

  explicit AdvancedProtectionStatusManager(Profile* profile);
  ~AdvancedProtectionStatusManager() override;

  // If |kAdvancedProtectionStatusFeature| is enabled.
  static bool IsEnabled();

  // If the primary account of |profile| is under advanced protection.
  static bool IsUnderAdvancedProtection(Profile* profile);

  bool is_under_advanced_protection() const {
    return is_under_advanced_protection_;
  }

  Profile* profile() const { return profile_; }

  // KeyedService:
  void Shutdown() override;

  bool IsRefreshScheduled();

  // If primary user of this profile is signed in.
  bool IsUserSignedIn();

 private:
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           NotSignedInOnStartUp);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           SignedInLongTimeAgoRefreshFailTransientError);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           SignedInLongTimeAgoRefreshFailNonTransientError);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           SignedInLongTimeAgoNotUnderAP);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           SignedInLongTimeAgoUnderAP);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           AlreadySignedInAndUnderAP);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest,
                           SignInAndSignOutEvent);
  FRIEND_TEST_ALL_PREFIXES(AdvancedProtectionStatusManagerTest, AccountRemoval);

  void Initialize();

  // Called after |Initialize()|. May trigger advanced protection status
  // refresh.
  void MaybeRefreshOnStartUp();

  // Subscribes to sign-in events.
  void SubscribeToSigninEvents();

  // Subscribes from sign-in events.
  void UnsubscribeFromSigninEvents();

  // AccountTrackerService::Observer implementations.
  void OnAccountUpdated(const AccountInfo& info) override;
  void OnAccountRemoved(const AccountInfo& info) override;

  // SigninManagerBase::Observer implementations.
  void GoogleSigninSucceeded(const AccountInfo& account_info) override;
  void GoogleSignedOut(const AccountInfo& account_info) override;

  void OnAdvancedProtectionEnabled();

  void OnAdvancedProtectionDisabled();

  // Requests Gaia refresh token to obtain advanced protection status.
  void RefreshAdvancedProtectionStatus();

  // Starts a timer to schedule next refresh.
  void ScheduleNextRefresh();

  // Cancels any status refresh in the future.
  void CancelFutureRefresh();

  // Sets |last_refresh_| to now and persists it.
  void UpdateLastRefreshTime();

  bool IsPrimaryAccount(const AccountInfo& account_info);

  // Decodes |id_token| to get advanced protection status.
  void OnGetIDToken(const std::string& account_id, const std::string& id_token);

  void OnTokenRefreshDone(const OAuth2TokenService::Request* request,
                          const GoogleServiceAuthError& error);

  // Only called in tests.
  void SetMinimumRefreshDelay(const base::TimeDelta& delay);

  // Only called in tests to set a customized minimum delay.
  AdvancedProtectionStatusManager(Profile* profile,
                                  const base::TimeDelta& min_delay);

  Profile* const profile_;

  SigninManagerBase* signin_manager_;
  AccountTrackerService* account_tracker_service_;

  // Is the profile account under advanced protection.
  bool is_under_advanced_protection_;

  // Account ID of the primary account of |profile_|. If this profile is not
  // signed in, this field will be empty.
  std::string primary_account_id_;

  std::unique_ptr<AdvancedProtectionTokenConsumer> token_consumer_;

  std::unique_ptr<OAuth2TokenService::Request> access_token_request_;

  base::OneShotTimer timer_;
  base::Time last_refreshed_;
  base::TimeDelta minimum_delay_;

  DISALLOW_COPY_AND_ASSIGN(AdvancedProtectionStatusManager);
};

}  // namespace safe_browsing
#endif  // CHROME_BROWSER_SAFE_BROWSING_ADVANCED_PROTECTION_STATUS_MANAGER_H_
