// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/advanced_protection_status_manager.h"

#include "chrome/browser/safe_browsing/advanced_protection_status_manager_factory.h"
#include "chrome/browser/signin/account_tracker_service_factory.h"
#include "chrome/browser/signin/fake_profile_oauth2_token_service_builder.h"
#include "chrome/browser/signin/fake_signin_manager_builder.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safe_browsing_prefs.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/fake_profile_oauth2_token_service.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace safe_browsing {
namespace {

static const char* kIdTokenAdvancedProtectionEnabled =
    "dummy-header."
    "eyAic2VydmljZXMiOiBbInRpYSJdIH0="  // payload: { "services": ["tia"] }
    ".dummy-signature";
static const char* kIdTokenAdvancedProtectionDisabled =
    "dummy-header."
    "eyAic2VydmljZXMiOiBbXSB9"  // payload: { "services": [] }
    ".dummy-signature";

class AdvancedProtectionStatusManagerTest : public testing::Test {
 public:
  AdvancedProtectionStatusManagerTest() {
    TestingProfile::Builder builder;
    builder.AddTestingFactory(SigninManagerFactory::GetInstance(),
                              BuildFakeSigninManagerBase);
    builder.AddTestingFactory(ProfileOAuth2TokenServiceFactory::GetInstance(),
                              BuildFakeProfileOAuth2TokenService);
    testing_profile_.reset(builder.Build().release());
    fake_signin_manager_ = static_cast<FakeSigninManagerForTesting*>(
        SigninManagerFactory::GetForProfile(testing_profile_.get()));
    account_tracker_service_ =
        AccountTrackerServiceFactory::GetForProfile(testing_profile_.get());
  }

  ~AdvancedProtectionStatusManagerTest() override {}

#if !defined(OS_CHROMEOS)
  std::string SignIn(const std::string& gaia_id,
                     const std::string& email,
                     bool is_under_advanced_protection) {
    AccountInfo account_info;
    account_info.gaia = gaia_id;
    account_info.email = email;
    account_info.is_under_advanced_protection = is_under_advanced_protection;
    std::string account_id =
        account_tracker_service_->SeedAccountInfo(account_info);
    fake_signin_manager_->SignIn(gaia_id, email, "password");
    GetTokenService()->UpdateCredentials(account_id, "refresh_token");
    return account_id;
  }
#endif

  FakeProfileOAuth2TokenService* GetTokenService() {
    ProfileOAuth2TokenService* service =
        ProfileOAuth2TokenServiceFactory::GetForProfile(testing_profile_.get());
    return static_cast<FakeProfileOAuth2TokenService*>(service);
  }

  bool IsRequestActive() {
    return !GetTokenService()->GetPendingRequests().empty();
  }

  void MakeOAuthTokenFetchSucceed(const std::string& account_id,
                                  bool is_under_advanced_protection) {
    ASSERT_TRUE(IsRequestActive());
    GetTokenService()->IssueAllTokensForAccount(
        account_id,
        OAuth2AccessTokenConsumer::TokenResponse(
            "access_token", base::Time::Now() + base::TimeDelta::FromHours(1),
            is_under_advanced_protection ? kIdTokenAdvancedProtectionEnabled
                                         : kIdTokenAdvancedProtectionDisabled));
  }

  void MakeOAuthTokenFetchFail(const std::string& account_id,
                               bool is_transient_error) {
    ASSERT_TRUE(IsRequestActive());
    GetTokenService()->IssueErrorForAllPendingRequestsForAccount(
        account_id,
        GoogleServiceAuthError(
            is_transient_error
                ? GoogleServiceAuthError::CONNECTION_FAILED
                : GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));
  }

 protected:
  content::TestBrowserThreadBundle thread_bundle;
  std::unique_ptr<TestingProfile> testing_profile_;
  FakeSigninManagerForTesting* fake_signin_manager_;
  AccountTrackerService* account_tracker_service_;
};

}  // namespace

#if !defined(OS_CHROMEOS)
TEST_F(AdvancedProtectionStatusManagerTest, NotSignedInOnStartUp) {
  ASSERT_FALSE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  ASSERT_FALSE(aps_manager.IsUserSignedIn());

  // If user's not signed-in. No refresh is required.
  EXPECT_FALSE(aps_manager.is_under_advanced_protection());
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());
  EXPECT_FALSE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest,
       SignedInLongTimeAgoRefreshFailTransientError) {
  ASSERT_FALSE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));

  // Simulates the situation where user signed in long time ago, thus
  // has no advanced protection status.
  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ false);
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(aps_manager.IsUserSignedIn());

  // An OAuth2 access token request should be sent.
  ASSERT_TRUE(IsRequestActive());
  // Simulates receiving access token, and this user is not under advanced
  // protection.
  MakeOAuthTokenFetchFail(account_id, /* is_transient_error = */ true);

  EXPECT_FALSE(aps_manager.is_under_advanced_protection());

  // A retry should be scheduled.
  EXPECT_TRUE(aps_manager.IsRefreshScheduled());
  EXPECT_FALSE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest,
       SignedInLongTimeAgoRefreshFailNonTransientError) {
  // Simulates the situation where user signed in long time ago, thus
  // has no advanced protection status.
  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ false);
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(aps_manager.IsUserSignedIn());

  // An OAuth2 access token request should be sent.
  ASSERT_TRUE(IsRequestActive());
  // Simulates receiving access token, and this user is not under advanced
  // protection.
  MakeOAuthTokenFetchFail(account_id, /* is_transient_error = */ false);

  EXPECT_FALSE(aps_manager.is_under_advanced_protection());

  // No retry should be scheduled.
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest, SignedInLongTimeAgoNotUnderAP) {
  ASSERT_FALSE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));

  // Simulates the situation where user signed in long time ago, thus
  // has no advanced protection status.
  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ false);
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  ASSERT_TRUE(aps_manager.IsUserSignedIn());
  base::RunLoop().RunUntilIdle();
  // An OAuth2 access token request should be sent.
  ASSERT_TRUE(IsRequestActive());
  // Simulates receiving access token, and this user is not under advanced
  // protection.
  MakeOAuthTokenFetchSucceed(account_id,
                             /* is_under_advanced_protection = */ false);

  EXPECT_FALSE(aps_manager.is_under_advanced_protection());
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());
  EXPECT_TRUE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest, SignedInLongTimeAgoUnderAP) {
  // Simulates the situation where user signed in long time ago, thus
  // has no advanced protection status yet.
  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ false);
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  base::RunLoop().RunUntilIdle();
  // Simulates receiving access token, and this user is not under advanced
  // protection.
  MakeOAuthTokenFetchSucceed(account_id,
                             /* is_under_advanced_protection = */ true);

  EXPECT_TRUE(aps_manager.is_under_advanced_protection());
  EXPECT_TRUE(aps_manager.IsRefreshScheduled());
  EXPECT_TRUE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest, AlreadySignedInAndUnderAP) {
  testing_profile_->GetPrefs()->SetInt64(
      prefs::kAdvancedProtectionLastRefreshInUs,
      base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());

  // Simulates the situation where user signed in long time ago, thus
  // has no advanced protection status.
  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ true);
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  ASSERT_TRUE(aps_manager.IsUserSignedIn());
  ASSERT_TRUE(aps_manager.is_under_advanced_protection());

  // Since user is already under advanced protection, no need to refresh.
  EXPECT_FALSE(IsRequestActive());
  // A refresh is scheduled in the future.
  EXPECT_TRUE(aps_manager.IsRefreshScheduled());
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest, SignInAndSignOutEvent) {
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  ASSERT_FALSE(aps_manager.is_under_advanced_protection());
  ASSERT_FALSE(aps_manager.IsUserSignedIn());

  SignIn("gaia_id", "email", /* is_under_advanced_protection = */ true);
  EXPECT_TRUE(aps_manager.is_under_advanced_protection());
  EXPECT_TRUE(aps_manager.IsRefreshScheduled());

  fake_signin_manager_->ForceSignOut();
  EXPECT_FALSE(aps_manager.is_under_advanced_protection());
  EXPECT_TRUE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());
  aps_manager.UnsubscribeFromSigninEvents();
}

TEST_F(AdvancedProtectionStatusManagerTest, AccountRemoval) {
  AdvancedProtectionStatusManager aps_manager(
      testing_profile_.get(), base::TimeDelta() /*no min delay*/);
  ASSERT_FALSE(aps_manager.is_under_advanced_protection());
  ASSERT_FALSE(aps_manager.IsUserSignedIn());

  std::string account_id =
      SignIn("gaia_id", "email", /* is_under_advanced_protection = */ false);
  EXPECT_FALSE(aps_manager.is_under_advanced_protection());
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());

  // Simulates account update.
  account_tracker_service_->SetIsAdvancedProtectionAccount(
      account_id, /* is_under_advanced_protection= */ true);
  EXPECT_TRUE(aps_manager.is_under_advanced_protection());
  EXPECT_TRUE(aps_manager.IsRefreshScheduled());

  account_tracker_service_->RemoveAccount(account_id);
  EXPECT_FALSE(aps_manager.is_under_advanced_protection());
  EXPECT_TRUE(testing_profile_->GetPrefs()->HasPrefPath(
      prefs::kAdvancedProtectionLastRefreshInUs));
  EXPECT_FALSE(aps_manager.IsRefreshScheduled());
  aps_manager.UnsubscribeFromSigninEvents();
}
#endif
}  // namespace safe_browsing
