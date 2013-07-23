// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/user_chooser_screen_handler.h"

#include "base/bind.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_info_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image_util.h"
#include "ui/webui/web_ui_util.h"

namespace {
// User dictionary keys.
const char kKeyUsername[] = "username";
const char kKeyDisplayName[]= "displayName";
const char kKeyEmailAddress[] = "emailAddress";
const char kKeyPublicAccount[] = "publicAccount";
const char kKeyLocallyManagedUser[] = "locallyManagedUser";
const char kKeySignedIn[] = "signedIn";
const char kKeyCanRemove[] = "canRemove";
const char kKeyIsOwner[] = "isOwner";
const char kKeyIsDesktop[] = "isDesktopUser";
const char kKeyAvatarUrl[] = "userImage";
const char kKeyNeedsSignin[] = "needsSignin";
const char kGAIAPictureFileNameKey[] = "gaia_picture_file_name";

// Max number of users to show.
const size_t kMaxUsers = 18;

// Type of the login screen UI that is currently presented to user.
const char kSourceGaiaSignin[] = "gaia-signin";
const char kSourceAccountPicker[] = "account-picker";

// JS API callback names.
const char kJsApiUserChooserInitialize[] = "userChooserInitialize";
const char kJsApiUserChooserAddUser[] = "addUser";
const char kJsApiUserChooserLaunchGuest[] = "launchGuest";
const char kJsApiUserChooserLaunchUser[] = "launchUser";
const char kJsApiUserChooserRemoveUser[] = "removeUser";

void HandleAndDoNothing(const base::ListValue* args) {
}

} // namespace

UserChooserScreenHandler::UserChooserScreenHandler() {
}

UserChooserScreenHandler::~UserChooserScreenHandler() {
}

void UserChooserScreenHandler::HandleInitialize(const base::ListValue* args) {
  SendUserList();
  web_ui()->CallJavascriptFunction("cr.ui.Oobe.showUserChooserScreen");
}

void UserChooserScreenHandler::HandleAddUser(const base::ListValue* args) {
  // TODO(noms): Should redirect to a sign in page.
  chrome::ShowSingletonTab(chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents()),
      GURL("chrome://settings/createProfile"));
}

void UserChooserScreenHandler::HandleRemoveUser(const base::ListValue* args) {
  // TODO(noms): Should delete the user.
  chrome::ShowSingletonTab(chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents()),
      GURL("chrome://settings/search#Users"));
}

void UserChooserScreenHandler::HandleLaunchGuest(const base::ListValue* args) {
  // TODO(noms): Once guest mode is ready, should launch a guest browser.
  chrome::NewIncognitoWindow(chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents()));
}

void UserChooserScreenHandler::HandleLaunchUser(const base::ListValue* args) {
  string16 emailAddress;
  string16 displayName;

  if (!args->GetString(0, &emailAddress) ||
      !args->GetString(1, &displayName)) {
    NOTREACHED();
    return;
  }

  ProfileInfoCache& info_cache =
      g_browser_process->profile_manager()->GetProfileInfoCache();
  chrome::HostDesktopType desktop_type = chrome::GetActiveDesktop();

  for (size_t i = 0; i < info_cache.GetNumberOfProfiles(); ++i) {
    if (info_cache.GetUserNameOfProfileAtIndex(i) == emailAddress &&
        info_cache.GetNameOfProfileAtIndex(i) == displayName) {
      base::FilePath path = info_cache.GetPathOfProfileAtIndex(i);
      profiles::SwitchToProfile(path, desktop_type, true);
      break;
    }
  }
}

void UserChooserScreenHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(kJsApiUserChooserInitialize,
      base::Bind(&UserChooserScreenHandler::HandleInitialize,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kJsApiUserChooserAddUser,
      base::Bind(&UserChooserScreenHandler::HandleAddUser,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kJsApiUserChooserLaunchGuest,
      base::Bind(&UserChooserScreenHandler::HandleLaunchGuest,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kJsApiUserChooserLaunchUser,
      base::Bind(&UserChooserScreenHandler::HandleLaunchUser,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kJsApiUserChooserRemoveUser,
      base::Bind(&UserChooserScreenHandler::HandleRemoveUser,
                 base::Unretained(this)));

  const content::WebUI::MessageCallback& kDoNothingCallback =
      base::Bind(&HandleAndDoNothing);

  // Unused callbacks from screen_account_picker.js
  web_ui()->RegisterMessageCallback("accountPickerReady", kDoNothingCallback);
  web_ui()->RegisterMessageCallback("loginUIStateChanged", kDoNothingCallback);
  web_ui()->RegisterMessageCallback("hideCaptivePortal", kDoNothingCallback);
  // Unused callbacks from display_manager.js
  web_ui()->RegisterMessageCallback("showAddUser", kDoNothingCallback);
  web_ui()->RegisterMessageCallback("loadWallpaper", kDoNothingCallback);
  web_ui()->RegisterMessageCallback("updateCurrentScreen", kDoNothingCallback);
  web_ui()->RegisterMessageCallback("loginVisible", kDoNothingCallback);
  // Unused callbacks from user_pod_row.js
  web_ui()->RegisterMessageCallback("userImagesLoaded", kDoNothingCallback);
}

void UserChooserScreenHandler::GetLocalizedValues(
    base::DictionaryValue* localized_strings) {
  // For Control Bar.
  localized_strings->SetString("signedIn",
      l10n_util::GetStringUTF16(IDS_SCREEN_LOCK_ACTIVE_USER));
  localized_strings->SetString("signinButton",
      l10n_util::GetStringUTF16(IDS_LOGIN_BUTTON));
  localized_strings->SetString("addUser",
      l10n_util::GetStringUTF16(IDS_ADD_USER_BUTTON));
  localized_strings->SetString("cancel", l10n_util::GetStringUTF16(IDS_CANCEL));
  localized_strings->SetString("browseAsGuest",
      l10n_util::GetStringUTF16(IDS_GO_INCOGNITO_BUTTON));
  localized_strings->SetString("signOutUser",
      l10n_util::GetStringUTF16(IDS_SCREEN_LOCK_SIGN_OUT));

  // For AccountPickerScreen.
  localized_strings->SetString("screenType", "login-add-user");
  localized_strings->SetString("highlightStrength", "normal");
  localized_strings->SetString("title", "User Chooser");
  localized_strings->SetString("passwordHint",
      l10n_util::GetStringUTF16(IDS_LOGIN_POD_EMPTY_PASSWORD_TEXT));
  localized_strings->SetString("podMenuButtonAccessibleName",
      l10n_util::GetStringUTF16(IDS_LOGIN_POD_MENU_BUTTON_ACCESSIBLE_NAME));
  localized_strings->SetString("podMenuRemoveItemAccessibleName",
      l10n_util::GetStringUTF16(
          IDS_LOGIN_POD_MENU_REMOVE_ITEM_ACCESSIBLE_NAME));
  localized_strings->SetString("removeUser",
      l10n_util::GetStringUTF16(IDS_LOGIN_POD_REMOVE_USER));
  localized_strings->SetString("passwordFieldAccessibleName",
      l10n_util::GetStringUTF16(IDS_LOGIN_POD_PASSWORD_FIELD_ACCESSIBLE_NAME));
  localized_strings->SetString("bootIntoWallpaper", "off");

 }

void UserChooserScreenHandler::SendUserList() {
  ListValue users_list;
  base::FilePath current_profile_path =
      web_ui()->GetWebContents()->GetBrowserContext()->GetPath();
  const ProfileInfoCache& info_cache =
      g_browser_process->profile_manager()->GetProfileInfoCache();

  for (size_t i = 0; i < info_cache.GetNumberOfProfiles(); ++i) {
    DictionaryValue* profile_value = new DictionaryValue();

    base::FilePath profile_path = info_cache.GetPathOfProfileAtIndex(i);
    bool is_active_user = (profile_path == current_profile_path);
    bool needs_signin = info_cache.ProfileIsSigninRequiredAtIndex(i);

    profile_value->SetString(
        kKeyUsername, info_cache.GetUserNameOfProfileAtIndex(i));
    profile_value->SetString(
        kKeyEmailAddress, info_cache.GetUserNameOfProfileAtIndex(i));
    profile_value->SetString(
        kKeyDisplayName, info_cache.GetNameOfProfileAtIndex(i));
    profile_value->SetBoolean(kKeyPublicAccount, false);
    profile_value->SetBoolean(kKeyLocallyManagedUser, false);
    profile_value->SetBoolean(kKeySignedIn, is_active_user);
    profile_value->SetBoolean(kKeyNeedsSignin, needs_signin);
    profile_value->SetBoolean(kKeyIsOwner, false);
    profile_value->SetBoolean(kKeyCanRemove, true);
    profile_value->SetBoolean(kKeyIsDesktop, true);

    bool is_gaia_picture =
        info_cache.IsUsingGAIAPictureOfProfileAtIndex(i) &&
        info_cache.GetGAIAPictureOfProfileAtIndex(i);

    gfx::Image icon = profiles::GetSizedAvatarIconWithBorder(
        info_cache.GetAvatarIconOfProfileAtIndex(i), is_gaia_picture, 160, 160);
    profile_value->SetString(kKeyAvatarUrl,
        webui::GetBitmapDataUrl(icon.AsBitmap()));

    if (is_active_user)
      users_list.Insert(0, profile_value);
    else
      users_list.Append(profile_value);
  }

  web_ui()->CallJavascriptFunction("login.AccountPickerScreen.loadUsers",
    users_list, base::FundamentalValue(false), base::FundamentalValue(true));
}
