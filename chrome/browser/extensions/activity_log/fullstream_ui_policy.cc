// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/activity_log/activity_database.h"
#include "chrome/browser/extensions/activity_log/api_actions.h"
#include "chrome/browser/extensions/activity_log/blocked_actions.h"
#include "chrome/browser/extensions/activity_log/dom_actions.h"
#include "chrome/browser/extensions/activity_log/fullstream_ui_policy.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/dom_action_types.h"
#include "chrome/common/extensions/extension.h"
#include "sql/error_delegate_util.h"
#include "url/gurl.h"

using base::Callback;
using base::FilePath;
using base::Time;
using base::Unretained;
using content::BrowserThread;

namespace {

// Key strings for passing parameters to the ProcessAction member function.
const char kKeyReason[] =         "fsuip.reason";
const char kKeyDomainAction[] =   "fsuip.domact";
const char kKeyURLTitle[] =       "fsuip.urltitle";
const char kKeyDetailsString[] =  "fsuip.details";

// Obsolete database tables: these should be dropped from the database if
// found.
const char* kObsoleteTables[] = {"activitylog_apis", "activitylog_blocked",
                                 "activitylog_urls"};

}  // namespace

namespace extensions {

const char* FullStreamUIPolicy::kTableName = "activitylog_full";
const char* FullStreamUIPolicy::kTableContentFields[] = {
  "extension_id", "time", "action_type", "api_name", "args", "page_url",
  "arg_url", "other"
};
const char* FullStreamUIPolicy::kTableFieldTypes[] = {
  "LONGVARCHAR NOT NULL", "INTEGER", "INTEGER", "LONGVARCHAR", "LONGVARCHAR",
  "LONGVARCHAR", "LONGVARCHAR", "LONGVARCHAR"
};
const int FullStreamUIPolicy::kTableFieldCount = arraysize(kTableContentFields);

FullStreamUIPolicy::FullStreamUIPolicy(Profile* profile)
    : ActivityLogPolicy(profile) {
  db_ = new ActivityDatabase(this);
  FilePath database_name = profile_base_path_.Append(
      chrome::kExtensionActivityLogFilename);
  ScheduleAndForget(db_, &ActivityDatabase::Init, database_name);
}

bool FullStreamUIPolicy::OnDatabaseInit(sql::Connection* db) {
  // Drop old database tables.
  for (size_t i = 0; i < arraysize(kObsoleteTables); i++) {
    const char* table_name = kObsoleteTables[i];
    if (db->DoesTableExist(table_name)) {
      std::string drop_statement =
          base::StringPrintf("DROP TABLE %s", table_name);
      if (!db->Execute(drop_statement.c_str())) {
        return false;
      }
    }
  }

  // Create the unified activity log entry table.
  return ActivityDatabase::InitializeTable(db,
                                           kTableName,
                                           kTableContentFields,
                                           kTableFieldTypes,
                                           arraysize(kTableContentFields));
}

void FullStreamUIPolicy::OnDatabaseClose() {
  delete this;
}

void FullStreamUIPolicy::Close() {
  // The policy object should have never been created if there's no DB thread.
  DCHECK(BrowserThread::IsMessageLoopValid(BrowserThread::DB));
  ScheduleAndForget(db_, &ActivityDatabase::Close);
}

// Get data as a set of key-value pairs.  The keys are policy-specific.
void FullStreamUIPolicy::ReadData(
    const std::string& extension_id,
    const int day,
    const Callback
        <void(scoped_ptr<std::vector<scoped_refptr<Action> > >)>& callback)
    const {
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::DB,
      FROM_HERE,
      base::Bind(&ActivityDatabase::GetActions, Unretained(db_),
                 extension_id, day),
      callback);
}

std::string FullStreamUIPolicy::GetKey(ActivityLogPolicy::KeyType key_ty) const
{
  switch (key_ty) {
    case PARAM_KEY_REASON:
      return std::string(kKeyReason);
    case PARAM_KEY_DOM_ACTION:
      return std::string(kKeyDomainAction);
    case PARAM_KEY_URL_TITLE:
      return std::string(kKeyURLTitle);
    case PARAM_KEY_DETAILS_STRING:
      return std::string(kKeyDetailsString);
    default:
      return std::string();
  }
}

scoped_ptr<base::ListValue> FullStreamUIPolicy::ProcessArguments(
    ActionType action_type,
    const std::string& name,
    const base::ListValue* args) const {
  if (args)
    return make_scoped_ptr(args->DeepCopy());
  else
    return scoped_ptr<base::ListValue>();
}

std::string FullStreamUIPolicy::JoinArguments(
    ActionType action_type,
    const std::string& name,
    const base::ListValue* args) const {
  std::string processed_args;
  if (args) {
    base::ListValue::const_iterator it = args->begin();
    // TODO(felt,dbabic) Think about replacing the loop with a single
    // call to SerializeAndOmitBinaryValues.
    for (; it != args->end(); ++it) {
      std::string arg;
      JSONStringValueSerializer serializer(&arg);
      if (serializer.SerializeAndOmitBinaryValues(**it)) {
        if (it != args->begin()) {
          processed_args.append(", ");
        }
        processed_args.append(arg);
      }
    }
  }
  return processed_args;
}

void FullStreamUIPolicy::ProcessWebRequestModifications(
    DictionaryValue& details,
    std::string& details_string) const {
  JSONStringValueSerializer serializer(&details_string);
  serializer.Serialize(details);
}

void FullStreamUIPolicy::ProcessAction(
    ActionType action_type,
    const std::string& extension_id,
    const std::string& name,
    const GURL& url_param,
    const base::ListValue* args_in,
    const DictionaryValue* details) {
  scoped_ptr<base::ListValue> args =
      ProcessArguments(action_type, name, args_in);
  std::string concatenated_args = JoinArguments(action_type, name, args.get());
  const Time now = Time::Now();
  scoped_refptr<Action> action;
  std::string extra;
  if (details) {
    details->GetString(GetKey(PARAM_KEY_EXTRA), &extra);
  }

  switch (action_type) {
    case ACTION_API: {
      action = new APIAction(
          extension_id,
          now,
          APIAction::CALL,
          name,
          concatenated_args,
          *args,
          extra);
      break;
    }
    case ACTION_EVENT: {
      action = new APIAction(
          extension_id,
          now,
          APIAction::EVENT_CALLBACK,
          name,
          concatenated_args,
          *args,
          extra);
      break;
    }
    case ACTION_BLOCKED: {
      int reason = 0;
      if (details) {
        details->GetInteger(GetKey(PARAM_KEY_REASON), &reason);
      }

      action = new BlockedAction(
          extension_id,
          now,
          name,
          concatenated_args,
          static_cast<BlockedAction::Reason>(reason),
          extra);
      break;
    }
    case ACTION_DOM: {
      string16 value;
      DomActionType::Type action_type = DomActionType::MODIFIED;

      if (details) {
        int action_id = 0;
        details->GetInteger(GetKey(PARAM_KEY_DOM_ACTION), &action_id);
        action_type = static_cast<DomActionType::Type>(action_id);
        details->GetString(GetKey(PARAM_KEY_URL_TITLE), &value);
      }

      action = new DOMAction(
          extension_id,
          now,
          action_type,
          url_param,
          value,
          name,
          concatenated_args,
          extra);
      break;
    }
    case ACTION_WEB_REQUEST: {
      std::string details_string;
      if (details) {
        scoped_ptr<DictionaryValue> copy_of_details(details->DeepCopy());
        ProcessWebRequestModifications(*copy_of_details.get(), details_string);
      }

      action = new DOMAction(
          extension_id,
          now,
          DomActionType::WEBREQUEST,
          url_param,
          string16(),
          name,
          details_string,
          extra);
      break;
    }
    default:
      NOTREACHED();
  }

  ScheduleAndForget(db_, &ActivityDatabase::RecordAction, action);
}

}  // namespace extensions
