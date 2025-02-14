// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/auth/arc_auth_service.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/arc/arc_optin_uma.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/chromeos/arc/auth/arc_background_auth_code_fetcher.h"
#include "chrome/browser/chromeos/arc/auth/arc_robot_auth_code_fetcher.h"
#include "chrome/browser/chromeos/arc/policy/arc_policy_util.h"
#include "chrome/browser/chromeos/login/demo_mode/demo_session.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_tracker_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/signin_ui_util.h"
#include "chrome/browser/ui/app_list/arc/arc_data_removal_dialog.h"
#include "chromeos/account_manager/account_manager_factory.h"
#include "chromeos/chromeos_switches.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_browser_context_keyed_service_factory_base.h"
#include "components/arc/arc_features.h"
#include "components/arc/arc_prefs.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/arc_supervision_transition.h"
#include "components/arc/arc_util.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace arc {

namespace {

// Singleton factory for ArcAuthService.
class ArcAuthServiceFactory
    : public internal::ArcBrowserContextKeyedServiceFactoryBase<
          ArcAuthService,
          ArcAuthServiceFactory> {
 public:
  // Factory name used by ArcBrowserContextKeyedServiceFactoryBase.
  static constexpr const char* kName = "ArcAuthServiceFactory";

  static ArcAuthServiceFactory* GetInstance() {
    return base::Singleton<ArcAuthServiceFactory>::get();
  }

 private:
  friend struct base::DefaultSingletonTraits<ArcAuthServiceFactory>;

  ArcAuthServiceFactory() = default;
  ~ArcAuthServiceFactory() override = default;
};

// Convers mojom::ArcSignInStatus into ProvisiningResult.
ProvisioningResult ConvertArcSignInStatusToProvisioningResult(
    mojom::ArcSignInStatus reason) {
  using ArcSignInStatus = mojom::ArcSignInStatus;

#define MAP_PROVISIONING_RESULT(name) \
  case ArcSignInStatus::name:         \
    return ProvisioningResult::name

  switch (reason) {
    MAP_PROVISIONING_RESULT(UNKNOWN_ERROR);
    MAP_PROVISIONING_RESULT(MOJO_VERSION_MISMATCH);
    MAP_PROVISIONING_RESULT(MOJO_CALL_TIMEOUT);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_FAILED);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_TIMEOUT);
    MAP_PROVISIONING_RESULT(DEVICE_CHECK_IN_INTERNAL_ERROR);
    MAP_PROVISIONING_RESULT(GMS_NETWORK_ERROR);
    MAP_PROVISIONING_RESULT(GMS_SERVICE_UNAVAILABLE);
    MAP_PROVISIONING_RESULT(GMS_BAD_AUTHENTICATION);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_FAILED);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_TIMEOUT);
    MAP_PROVISIONING_RESULT(GMS_SIGN_IN_INTERNAL_ERROR);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_FAILED);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_TIMEOUT);
    MAP_PROVISIONING_RESULT(CLOUD_PROVISION_FLOW_INTERNAL_ERROR);
    MAP_PROVISIONING_RESULT(NO_NETWORK_CONNECTION);
    MAP_PROVISIONING_RESULT(CHROME_SERVER_COMMUNICATION_ERROR);
    MAP_PROVISIONING_RESULT(ARC_DISABLED);
    MAP_PROVISIONING_RESULT(SUCCESS);
    MAP_PROVISIONING_RESULT(SUCCESS_ALREADY_PROVISIONED);
  }
#undef MAP_PROVISIONING_RESULT

  NOTREACHED() << "unknown reason: " << static_cast<int>(reason);
  return ProvisioningResult::UNKNOWN_ERROR;
}

mojom::ChromeAccountType GetAccountType(const Profile* profile) {
  if (profile->IsChild())
    return mojom::ChromeAccountType::CHILD_ACCOUNT;

  chromeos::DemoSession* demo_session = chromeos::DemoSession::Get();
  if (demo_session && demo_session->started()) {
    // Internally, demo mode is implemented as a public session, and should
    // generally follow normal robot account provisioning flow. Offline enrolled
    // demo mode is an exception, as it is expected to work purely offline, with
    // a (fake) robot account not known to auth service - this means that it has
    // to go through different, offline provisioning flow.
    DCHECK(IsRobotOrOfflineDemoAccountMode());
    return demo_session->offline_enrolled()
               ? mojom::ChromeAccountType::OFFLINE_DEMO_ACCOUNT
               : mojom::ChromeAccountType::ROBOT_ACCOUNT;
  }

  return IsRobotOrOfflineDemoAccountMode()
             ? mojom::ChromeAccountType::ROBOT_ACCOUNT
             : mojom::ChromeAccountType::USER_ACCOUNT;
}

mojom::AccountInfoPtr CreateAccountInfo(bool is_enforced,
                                        const std::string& auth_info,
                                        const std::string& account_name,
                                        mojom::ChromeAccountType account_type,
                                        bool is_managed,
                                        bool is_secondary_account) {
  mojom::AccountInfoPtr account_info = mojom::AccountInfo::New();
  account_info->account_name = account_name;
  if (account_type == mojom::ChromeAccountType::ACTIVE_DIRECTORY_ACCOUNT) {
    account_info->enrollment_token = auth_info;
  } else {
    if (!is_enforced)
      account_info->auth_code = base::nullopt;
    else
      account_info->auth_code = auth_info;
  }
  account_info->account_type = account_type;
  account_info->is_managed = is_managed;
  account_info->is_secondary_account = is_secondary_account;
  return account_info;
}

}  // namespace

// static
const char ArcAuthService::kArcServiceName[] = "arc::ArcAuthService";

// static
ArcAuthService* ArcAuthService::GetForBrowserContext(
    content::BrowserContext* context) {
  return ArcAuthServiceFactory::GetForBrowserContext(context);
}

ArcAuthService::ArcAuthService(content::BrowserContext* browser_context,
                               ArcBridgeService* arc_bridge_service)
    : profile_(Profile::FromBrowserContext(browser_context)),
      account_tracker_service_(
          AccountTrackerServiceFactory::GetInstance()->GetForProfile(profile_)),
      arc_bridge_service_(arc_bridge_service),
      account_mapper_util_(account_tracker_service_),
      url_loader_factory_(
          content::BrowserContext::GetDefaultStoragePartition(profile_)
              ->GetURLLoaderFactoryForBrowserProcess()),
      weak_ptr_factory_(this) {
  arc_bridge_service_->auth()->SetHost(this);
  arc_bridge_service_->auth()->AddObserver(this);

  if (chromeos::switches::IsAccountManagerEnabled()) {
    // TODO(sinhak): This will need to be independent of Profile, when
    // Multi-Profile on Chrome OS is launched.
    chromeos::AccountManagerFactory* factory =
        g_browser_process->platform_part()->GetAccountManagerFactory();
    account_manager_ = factory->GetAccountManager(profile_->GetPath().value());
    account_manager_->AddObserver(this);
  }
}

ArcAuthService::~ArcAuthService() {
  if (chromeos::switches::IsAccountManagerEnabled()) {
    account_manager_->RemoveObserver(this);
  }
  arc_bridge_service_->auth()->RemoveObserver(this);
  arc_bridge_service_->auth()->SetHost(nullptr);
}

void ArcAuthService::OnConnectionReady() {
  if (chromeos::switches::IsAccountManagerEnabled()) {
    // The Mojo |OnSecondaryAccountUpserted| API is guaranteed to be called at
    // least once for every account at startup. We need to get the list of
    // accounts from |AccountManager|, just to be safe, since we may have missed
    // the initial |AccountManager::Observer| notifications. We can safely call
    // the Mojo |OnSecondaryAccountUpserted| API twice for every account since
    // it is guaranteed to be idempotent.
    account_manager_->GetAccounts(base::BindOnce(
        &ArcAuthService::GetAccountsCallback, weak_ptr_factory_.GetWeakPtr()));
  }
}

void ArcAuthService::OnConnectionClosed() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  pending_token_requests_.clear();
}

void ArcAuthService::OnAuthorizationComplete(mojom::ArcSignInStatus status,
                                             bool initial_signin) {
  if (!initial_signin) {
    // Note, UMA for initial signin is updated from ArcSessionManager.
    DCHECK_NE(mojom::ArcSignInStatus::SUCCESS_ALREADY_PROVISIONED, status);
    UpdateReauthorizationResultUMA(
        ConvertArcSignInStatusToProvisioningResult(status), profile_);
    return;
  }

  ArcSessionManager::Get()->OnProvisioningFinished(
      ConvertArcSignInStatusToProvisioningResult(status));
}

void ArcAuthService::OnSignInCompleteDeprecated() {
  OnAuthorizationComplete(mojom::ArcSignInStatus::SUCCESS,
                          true /* initial_signin */);
}

void ArcAuthService::OnSignInFailedDeprecated(mojom::ArcSignInStatus reason) {
  DCHECK_NE(mojom::ArcSignInStatus::SUCCESS, reason);
  OnAuthorizationComplete(reason, true /* initial_signin */);
}

void ArcAuthService::ReportMetrics(mojom::MetricsType metrics_type,
                                   int32_t value) {
  switch (metrics_type) {
    case mojom::MetricsType::NETWORK_WAITING_TIME_MILLISECONDS:
      UpdateAuthTiming("ArcAuth.NetworkWaitTime",
                       base::TimeDelta::FromMilliseconds(value));
      break;
    case mojom::MetricsType::CHECKIN_ATTEMPTS:
      UpdateAuthCheckinAttempts(value);
      break;
    case mojom::MetricsType::CHECKIN_TIME_MILLISECONDS:
      UpdateAuthTiming("ArcAuth.CheckinTime",
                       base::TimeDelta::FromMilliseconds(value));
      break;
    case mojom::MetricsType::SIGNIN_TIME_MILLISECONDS:
      UpdateAuthTiming("ArcAuth.SignInTime",
                       base::TimeDelta::FromMilliseconds(value));
      break;
    case mojom::MetricsType::ACCOUNT_CHECK_MILLISECONDS:
      UpdateAuthTiming("ArcAuth.AccountCheckTime",
                       base::TimeDelta::FromMilliseconds(value));
      break;
  }
}

void ArcAuthService::ReportAccountCheckStatus(
    mojom::AccountCheckStatus status) {
  UpdateAuthAccountCheckStatus(status);
}

void ArcAuthService::ReportSupervisionChangeStatus(
    mojom::SupervisionChangeStatus status) {
  UpdateSupervisionTransitionResultUMA(status);
  switch (status) {
    case mojom::SupervisionChangeStatus::CLOUD_DPC_DISABLED:
    case mojom::SupervisionChangeStatus::CLOUD_DPC_ALREADY_DISABLED:
    case mojom::SupervisionChangeStatus::CLOUD_DPC_ENABLED:
    case mojom::SupervisionChangeStatus::CLOUD_DPC_ALREADY_ENABLED:
      profile_->GetPrefs()->SetInteger(
          prefs::kArcSupervisionTransition,
          static_cast<int>(ArcSupervisionTransition::NO_TRANSITION));
      // TODO(brunokim): notify potential observers.
      break;
    case mojom::SupervisionChangeStatus::CLOUD_DPC_DISABLING_FAILED:
    case mojom::SupervisionChangeStatus::CLOUD_DPC_ENABLING_FAILED:
      LOG(ERROR) << "Child transition failed: " << status;
      ShowDataRemovalConfirmationDialog(
          profile_, base::BindOnce(&ArcAuthService::OnDataRemovalAccepted,
                                   weak_ptr_factory_.GetWeakPtr()));
      break;
    case mojom::SupervisionChangeStatus::INVALID_SUPERVISION_STATE:
      NOTREACHED() << "Invalid status of child transition: " << status;
  }
}

void ArcAuthService::OnAccountInfoReady(mojom::AccountInfoPtr account_info,
                                        mojom::ArcSignInStatus status) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* instance = ARC_GET_INSTANCE_FOR_METHOD(arc_bridge_service_->auth(),
                                               OnAccountInfoReady);
  if (!instance) {
    LOG(ERROR) << "Auth instance is not available.";
    return;
  }
  instance->OnAccountInfoReady(std::move(account_info), status);
}

void ArcAuthService::RequestAccountInfo(
    bool initial_signin,
    const base::Optional<std::string>& account_name) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // Check if |account_name| points to a Secondary Account.
  if (account_name.has_value()) {
    DCHECK(!account_name.value().empty());

    const std::string& gaia_id =
        account_tracker_service_->FindAccountInfoByEmail(account_name.value())
            .gaia;
    DCHECK(!gaia_id.empty());

    if (!IsDeviceAccount(chromeos::AccountManager::AccountKey{
            gaia_id,
            chromeos::account_manager::AccountType::ACCOUNT_TYPE_GAIA})) {
      FetchSecondaryAccountInfo(account_name.value());
      return;
    }
  }

  FetchDeviceAccountInfo(initial_signin);
}

void ArcAuthService::FetchDeviceAccountInfo(bool initial_signin) {
  const mojom::ChromeAccountType account_type = GetAccountType(profile_);

  if (IsArcOptInVerificationDisabled()) {
    OnAccountInfoReady(
        CreateAccountInfo(false /* is_enforced */,
                          std::string() /* auth_info */,
                          std::string() /* auth_name */, account_type,
                          policy_util::IsAccountManaged(profile_),
                          false /* is_secondary_account */),
        mojom::ArcSignInStatus::SUCCESS);
    return;
  }

  if (IsActiveDirectoryUserForProfile(profile_)) {
    // For Active Directory enrolled devices, we get an enrollment token for a
    // managed Google Play account from DMServer.
    auto enrollment_token_fetcher =
        std::make_unique<ArcActiveDirectoryEnrollmentTokenFetcher>(
            ArcSessionManager::Get()->support_host());
    enrollment_token_fetcher->Fetch(base::BindOnce(
        &ArcAuthService::OnActiveDirectoryEnrollmentTokenFetched,
        weak_ptr_factory_.GetWeakPtr(), enrollment_token_fetcher.get()));
    pending_token_requests_.emplace_back(std::move(enrollment_token_fetcher));
    return;
  }

  if (account_type == mojom::ChromeAccountType::OFFLINE_DEMO_ACCOUNT) {
    // Skip account auth code fetch for offline enrolled demo mode.
    OnAccountInfoReady(
        CreateAccountInfo(true /* is_enforced */, std::string() /* auth_info */,
                          std::string() /* auth_name */, account_type,
                          true /* is_managed */,
                          false /* is_secondary_account */),
        mojom::ArcSignInStatus::SUCCESS);
    return;
  }

  // For non-AD enrolled devices an auth code is fetched.
  std::unique_ptr<ArcAuthCodeFetcher> auth_code_fetcher;
  if (account_type == mojom::ChromeAccountType::ROBOT_ACCOUNT) {
    // For robot accounts, which are used in kiosk and public session mode
    // (which includes online demo sessions), use Robot auth code fetching.
    auth_code_fetcher = std::make_unique<ArcRobotAuthCodeFetcher>();
  } else {
    // Optionally retrieve auth code in silent mode.
    const SigninManagerBase* const signin_manager =
        SigninManagerFactory::GetForProfile(profile_);
    auth_code_fetcher = CreateArcBackgroundAuthCodeFetcher(
        signin_manager->GetAuthenticatedAccountId(), initial_signin);
  }
  auth_code_fetcher->Fetch(
      base::Bind(&ArcAuthService::OnDeviceAccountAuthCodeFetched,
                 weak_ptr_factory_.GetWeakPtr(), auth_code_fetcher.get()));
  pending_token_requests_.emplace_back(std::move(auth_code_fetcher));
}

void ArcAuthService::OnTokenUpserted(
    const chromeos::AccountManager::AccountKey& account_key) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // We send only Secondary Account update notifications to ARC++. ARC++ has a
  // polling mechanism for the Device Account (See the Mojo APIs
  // |RequestAccountInfo| and |OnAccountInfoReady|).
  if (IsDeviceAccount(account_key)) {
    return;
  }

  auto* instance = ARC_GET_INSTANCE_FOR_METHOD(arc_bridge_service_->auth(),
                                               OnSecondaryAccountUpserted);
  if (!instance) {
    LOG(ERROR) << "Auth instance is not available.";
    return;
  }

  const std::string& account_name =
      account_mapper_util_.AccountKeyToGaiaAccountInfo(account_key).email;
  DCHECK(!account_name.empty());
  instance->OnSecondaryAccountUpserted(account_name);
}

void ArcAuthService::OnAccountRemoved(
    const chromeos::AccountManager::AccountKey& account_key) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!IsDeviceAccount(account_key));

  auto* instance = ARC_GET_INSTANCE_FOR_METHOD(arc_bridge_service_->auth(),
                                               OnSecondaryAccountRemoved);
  if (!instance) {
    LOG(ERROR) << "Auth instance is not available.";
    return;
  }

  const std::string& account_name =
      account_mapper_util_.AccountKeyToGaiaAccountInfo(account_key).email;
  DCHECK(!account_name.empty());
  instance->OnSecondaryAccountRemoved(account_name);
}

void ArcAuthService::OnActiveDirectoryEnrollmentTokenFetched(
    ArcActiveDirectoryEnrollmentTokenFetcher* fetcher,
    ArcActiveDirectoryEnrollmentTokenFetcher::Status status,
    const std::string& enrollment_token,
    const std::string& user_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DeletePendingTokenRequest(fetcher);
  fetcher = nullptr;

  switch (status) {
    case ArcActiveDirectoryEnrollmentTokenFetcher::Status::SUCCESS: {
      // Save user_id to the user profile.
      profile_->GetPrefs()->SetString(prefs::kArcActiveDirectoryPlayUserId,
                                      user_id);

      // Send enrollment token to ARC.
      OnAccountInfoReady(
          CreateAccountInfo(true /* is_enforced */, enrollment_token,
                            std::string() /* account_name */,
                            mojom::ChromeAccountType::ACTIVE_DIRECTORY_ACCOUNT,
                            true /* is_managed */,
                            false /* is_secondary_account */),
          mojom::ArcSignInStatus::SUCCESS);
      break;
    }
    case ArcActiveDirectoryEnrollmentTokenFetcher::Status::FAILURE: {
      // Send error to ARC.
      OnAccountInfoReady(
          nullptr, mojom::ArcSignInStatus::CHROME_SERVER_COMMUNICATION_ERROR);
      break;
    }
    case ArcActiveDirectoryEnrollmentTokenFetcher::Status::ARC_DISABLED: {
      // Send error to ARC.
      OnAccountInfoReady(nullptr, mojom::ArcSignInStatus::ARC_DISABLED);
      break;
    }
  }
}

void ArcAuthService::OnDeviceAccountAuthCodeFetched(
    ArcAuthCodeFetcher* fetcher,
    bool success,
    const std::string& auth_code) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DeletePendingTokenRequest(fetcher);
  fetcher = nullptr;

  if (success) {
    const SigninManagerBase* const signin_manager =
        SigninManagerFactory::GetForProfile(profile_);
    const std::string& full_account_id = base::UTF16ToUTF8(
        signin_ui_util::GetAuthenticatedUsername(signin_manager));
    OnAccountInfoReady(
        CreateAccountInfo(!IsArcOptInVerificationDisabled(), auth_code,
                          full_account_id, GetAccountType(profile_),
                          policy_util::IsAccountManaged(profile_),
                          false /* is_secondary_account */),
        mojom::ArcSignInStatus::SUCCESS);
  } else if (chromeos::DemoSession::Get() &&
             chromeos::DemoSession::Get()->started()) {
    // For demo sessions, if auth code fetch failed (e.g. because the device is
    // offline), fall back to accountless offline demo mode provisioning.
    OnAccountInfoReady(
        CreateAccountInfo(true /* is_enforced */, std::string() /* auth_info */,
                          std::string() /* auth_name */,
                          mojom::ChromeAccountType::OFFLINE_DEMO_ACCOUNT,
                          true /* is_managed */,
                          false /* is_secondary_account */),
        mojom::ArcSignInStatus::SUCCESS);
  } else {
    // Send error to ARC.
    OnAccountInfoReady(
        nullptr, mojom::ArcSignInStatus::CHROME_SERVER_COMMUNICATION_ERROR);
  }
}

void ArcAuthService::FetchSecondaryAccountInfo(
    const std::string& account_name) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const std::string& account_id =
      account_tracker_service_->FindAccountInfoByEmail(account_name).account_id;
  DCHECK(!account_id.empty());

  std::unique_ptr<ArcBackgroundAuthCodeFetcher> fetcher =
      CreateArcBackgroundAuthCodeFetcher(account_id,
                                         false /* initial_signin */);
  fetcher->Fetch(base::BindRepeating(
      &ArcAuthService::OnSecondaryAccountAuthCodeFetched,
      weak_ptr_factory_.GetWeakPtr(), account_name, fetcher.get()));
  pending_token_requests_.emplace_back(std::move(fetcher));
}

void ArcAuthService::OnSecondaryAccountAuthCodeFetched(
    const std::string& account_name,
    ArcBackgroundAuthCodeFetcher* fetcher,
    bool success,
    const std::string& auth_code) {
  DeletePendingTokenRequest(fetcher);
  fetcher = nullptr;

  if (success) {
    OnAccountInfoReady(
        CreateAccountInfo(true /* is_enforced */, auth_code, account_name,
                          mojom::ChromeAccountType::USER_ACCOUNT,
                          false /* is_managed */,
                          true /* is_secondary_account */),
        mojom::ArcSignInStatus::SUCCESS);
  } else {
    OnAccountInfoReady(
        nullptr, mojom::ArcSignInStatus::CHROME_SERVER_COMMUNICATION_ERROR);
  }
}

void ArcAuthService::DeletePendingTokenRequest(ArcFetcherBase* fetcher) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  for (auto it = pending_token_requests_.begin();
       it != pending_token_requests_.end(); ++it) {
    if (it->get() == fetcher) {
      pending_token_requests_.erase(it);
      return;
    }
  }

  // We should not have received a call to delete a |fetcher| that was not in
  // |pending_token_requests_|.
  NOTREACHED();
}

void ArcAuthService::SetURLLoaderFactoryForTesting(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  url_loader_factory_ = std::move(url_loader_factory);
}

void ArcAuthService::OnDataRemovalAccepted(bool accepted) {
  if (!accepted)
    return;
  if (!IsArcPlayStoreEnabledForProfile(profile_))
    return;
  VLOG(1)
      << "Request for data removal on child transition failure is confirmed";
  ArcSessionManager::Get()->RequestArcDataRemoval();
  ArcSessionManager::Get()->StopAndEnableArc();
}

void ArcAuthService::GetAccountsCallback(
    std::vector<chromeos::AccountManager::AccountKey> accounts) {
  for (const auto& account_key : accounts) {
    OnTokenUpserted(account_key);
  }
}

bool ArcAuthService::IsDeviceAccount(
    const chromeos::AccountManager::AccountKey& account_key) const {
  const AccountId& device_account_id = chromeos::ProfileHelper::Get()
                                           ->GetUserByProfile(profile_)
                                           ->GetAccountId();

  return account_mapper_util_.IsEqual(account_key, device_account_id);
}

std::unique_ptr<ArcBackgroundAuthCodeFetcher>
ArcAuthService::CreateArcBackgroundAuthCodeFetcher(
    const std::string& account_id,
    bool initial_signin) {
  auto fetcher = std::make_unique<ArcBackgroundAuthCodeFetcher>(
      url_loader_factory_, profile_, account_id, initial_signin);
  if (skip_merge_session_for_testing_)
    fetcher->SkipMergeSessionForTesting();

  return fetcher;
}

void ArcAuthService::SkipMergeSessionForTesting() {
  skip_merge_session_for_testing_ = true;
}

}  // namespace arc
