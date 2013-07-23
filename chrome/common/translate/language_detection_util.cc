// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/translate/language_detection_util.h"

#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/translate/translate_common_metrics.h"
#include "chrome/common/translate/translate_util.h"

#if defined(ENABLE_LANGUAGE_DETECTION)
#include "third_party/cld/encodings/compact_lang_det/compact_lang_det.h"
#include "third_party/cld/encodings/compact_lang_det/win/cld_unicodetext.h"
#endif

namespace {

// Similar language code list. Some languages are very similar and difficult
// for CLD to distinguish.
struct SimilarLanguageCode {
  const char* const code;
  int group;
};

const SimilarLanguageCode kSimilarLanguageCodes[] = {
  {"bs", 1},
  {"hr", 1},
  {"hi", 2},
  {"ne", 2},
};

// Checks |kSimilarLanguageCodes| and returns group code.
int GetSimilarLanguageGroupCode(const std::string& language) {
  for (size_t i = 0; i < arraysize(kSimilarLanguageCodes); ++i) {
    if (language.find(kSimilarLanguageCodes[i].code) != 0)
      continue;
    return kSimilarLanguageCodes[i].group;
  }
  return 0;
}

// Well-known languages which often have wrong server configuration of
// Content-Language: en.
// TODO(toyoshim): Remove these static tables and caller functions to
// chrome/common/translate, and implement them as std::set<>.
const char* kWellKnownCodesOnWrongConfiguration[] = {
  "es", "pt", "ja", "ru", "de", "zh-CN", "zh-TW", "ar", "id", "fr", "it", "th"
};

// Applies a series of language code modification in proper order.
void ApplyLanguageCodeCorrection(std::string* code) {
  // Correct well-known format errors.
  LanguageDetectionUtil::CorrectLanguageCodeTypo(code);

  if (!LanguageDetectionUtil::IsValidLanguageCode(*code)) {
    *code = std::string();
    return;
  }

  TranslateUtil::ToTranslateLanguageSynonym(code);
}

#if defined(ENABLE_LANGUAGE_DETECTION)
// Returns the ISO 639 language code of the specified |text|, or 'unknown' if it
// failed.
// |is_cld_reliable| will be set as true if CLD says the detection is reliable.
std::string DetermineTextLanguage(const base::string16& text,
                                  bool* is_cld_reliable) {
  std::string language = chrome::kUnknownLanguageCode;
  int num_languages = 0;
  int text_bytes = 0;
  bool is_reliable = false;
  Language cld_language =
      DetectLanguageOfUnicodeText(NULL, text.c_str(), true, &is_reliable,
                                  &num_languages, NULL, &text_bytes);
  if (is_cld_reliable != NULL)
    *is_cld_reliable = is_reliable;

  // We don't trust the result if the CLD reports that the detection is not
  // reliable, or if the actual text used to detect the language was less than
  // 100 bytes (short texts can often lead to wrong results).
  // TODO(toyoshim): CLD provides |is_reliable| flag. But, it just says that
  // the determined language code is correct with 50% confidence. Chrome should
  // handle the real confidence value to judge.
  if (is_reliable && text_bytes >= 100 && cld_language != NUM_LANGUAGES &&
      cld_language != UNKNOWN_LANGUAGE && cld_language != TG_UNKNOWN_LANGUAGE) {
    // We should not use LanguageCode_ISO_639_1 because it does not cover all
    // the languages CLD can detect. As a result, it'll return the invalid
    // language code for tradtional Chinese among others.
    // |LanguageCodeWithDialect| will go through ISO 639-1, ISO-639-2 and
    // 'other' tables to do the 'right' thing. In addition, it'll return zh-CN
    // for Simplified Chinese.
    language = LanguageCodeWithDialects(cld_language);
  }
  VLOG(9) << "Detected lang_id: " << language << ", from Text:\n" << text
          << "\n*************************************\n";
  return language;
}
#endif  // defined(ENABLE_LANGUAGE_DETECTION)

// Checks if CLD can complement a sub code when the page language doesn't know
// the sub code.
bool CanCLDComplementSubCode(
    const std::string& page_language, const std::string& cld_language) {
  // Translate server cannot treat general Chinese. If Content-Language and
  // CLD agree that the language is Chinese and Content-Language doesn't know
  // which dialect is used, CLD language has priority.
  // TODO(hajimehoshi): How about the other dialects like zh-MO?
  return page_language == "zh" && StartsWithASCII(cld_language, "zh-", false);
}

}  // namespace

namespace LanguageDetectionUtil {

std::string DeterminePageLanguage(const std::string& code,
                                  const std::string& html_lang,
                                  const base::string16& contents,
                                  std::string* cld_language_p,
                                  bool* is_cld_reliable_p) {
#if defined(ENABLE_LANGUAGE_DETECTION)
  base::TimeTicks begin_time = base::TimeTicks::Now();
  bool is_cld_reliable;
  std::string cld_language = DetermineTextLanguage(contents, &is_cld_reliable);
  TranslateCommonMetrics::ReportLanguageDetectionTime(begin_time,
                                                      base::TimeTicks::Now());

  if (cld_language_p != NULL)
    *cld_language_p = cld_language;
  if (is_cld_reliable_p != NULL)
    *is_cld_reliable_p = is_cld_reliable;
  TranslateUtil::ToTranslateLanguageSynonym(&cld_language);
#endif  // defined(ENABLE_LANGUAGE_DETECTION)

  // Check if html lang attribute is valid.
  std::string modified_html_lang;
  if (!html_lang.empty()) {
    modified_html_lang = html_lang;
    ApplyLanguageCodeCorrection(&modified_html_lang);
    TranslateCommonMetrics::ReportHtmlLang(html_lang, modified_html_lang);
    VLOG(9) << "html lang based language code: " << modified_html_lang;
  }

  // Check if Content-Language is valid.
  std::string modified_code;
  if (!code.empty()) {
    modified_code = code;
    ApplyLanguageCodeCorrection(&modified_code);
    TranslateCommonMetrics::ReportContentLanguage(code, modified_code);
  }

  // Adopt |modified_html_lang| if it is valid. Otherwise, adopt
  // |modified_code|.
  std::string language = modified_html_lang.empty() ? modified_code :
                                                      modified_html_lang;

#if defined(ENABLE_LANGUAGE_DETECTION)
  // If |language| is empty, just use CLD result even though it might be
  // chrome::kUnknownLanguageCode.
  if (language.empty()) {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_CLD_ONLY);
    return cld_language;
  }

  if (cld_language == chrome::kUnknownLanguageCode) {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_UNKNOWN);
    return language;
  } else if (CanCLDComplementSubCode(language, cld_language)) {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_CLD_COMPLEMENT_SUB_CODE);
    return cld_language;
  } else if (IsSameOrSimilarLanguages(language, cld_language)) {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_CLD_AGREE);
    return language;
  } else if (MaybeServerWrongConfiguration(language, cld_language)) {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_TRUST_CLD);
    return cld_language;
  } else {
    TranslateCommonMetrics::ReportLanguageVerification(
        TranslateCommonMetrics::LANGUAGE_VERIFICATION_CLD_DISAGREE);
    // Content-Language value might be wrong because CLD says that this page
    // is written in another language with confidence.
    // In this case, Chrome doesn't rely on any of the language codes, and
    // gives up suggesting a translation.
    return std::string(chrome::kUnknownLanguageCode);
  }
#else  // defined(ENABLE_LANGUAGE_DETECTION)
  TranslateCommonMetrics::ReportLanguageVerification(
      TranslateCommonMetrics::LANGUAGE_VERIFICATION_CLD_DISABLED);
#endif  // defined(ENABLE_LANGUAGE_DETECTION)

  return language;
}

void CorrectLanguageCodeTypo(std::string* code) {
  DCHECK(code);

  size_t coma_index = code->find(',');
  if (coma_index != std::string::npos) {
    // There are more than 1 language specified, just keep the first one.
    *code = code->substr(0, coma_index);
  }
  TrimWhitespaceASCII(*code, TRIM_ALL, code);

  // An underscore instead of a dash is a frequent mistake.
  size_t underscore_index = code->find('_');
  if (underscore_index != std::string::npos)
    (*code)[underscore_index] = '-';

  // Change everything up to a dash to lower-case and everything after to upper.
  size_t dash_index = code->find('-');
  if (dash_index != std::string::npos) {
    *code = StringToLowerASCII(code->substr(0, dash_index)) +
        StringToUpperASCII(code->substr(dash_index));
  } else {
    *code = StringToLowerASCII(*code);
  }
}

bool IsValidLanguageCode(const std::string& code) {
  // Roughly check if the language code follows /[a-zA-Z]{2,3}(-[a-zA-Z]{2})?/.
  // TODO(hajimehoshi): How about es-419, which is used as an Accept language?
  std::vector<std::string> chunks;
  base::SplitString(code, '-', &chunks);

  if (chunks.size() < 1 || 2 < chunks.size())
    return false;

  const std::string& main_code = chunks[0];

  if (main_code.size() < 1 || 3 < main_code.size())
    return false;

  for (std::string::const_iterator it = main_code.begin();
       it != main_code.end(); ++it) {
    if (!IsAsciiAlpha(*it))
      return false;
  }

  if (chunks.size() == 1)
    return true;

  const std::string& sub_code = chunks[1];

  if (sub_code.size() != 2)
    return false;

  for (std::string::const_iterator it = sub_code.begin();
       it != sub_code.end(); ++it) {
    if (!IsAsciiAlpha(*it))
      return false;
  }

  return true;
}

bool IsSameOrSimilarLanguages(const std::string& page_language,
                              const std::string& cld_language) {
  // Language code part of |page_language| is matched to one of |cld_language|.
  // Country code is ignored here.
  if (page_language.size() >= 2 &&
      cld_language.find(page_language.c_str(), 0, 2) == 0) {
    // Languages are matched strictly. Reports false to metrics, but returns
    // true.
    TranslateCommonMetrics::ReportSimilarLanguageMatch(false);
    return true;
  }

  // Check if |page_language| and |cld_language| are in the similar language
  // list and belong to the same language group.
  int page_code = GetSimilarLanguageGroupCode(page_language);
  bool match = page_code != 0 &&
               page_code == GetSimilarLanguageGroupCode(cld_language);

  TranslateCommonMetrics::ReportSimilarLanguageMatch(match);
  return match;
}

bool MaybeServerWrongConfiguration(const std::string& page_language,
                                   const std::string& cld_language) {
  // If |page_language| is not "en-*", respect it and just return false here.
  if (!StartsWithASCII(page_language, "en", false))
    return false;

  // A server provides a language meta information representing "en-*". But it
  // might be just a default value due to missing user configuration.
  // Let's trust |cld_language| if the determined language is not difficult to
  // distinguish from English, and the language is one of well-known languages
  // which often provide "en-*" meta information mistakenly.
  for (size_t i = 0; i < arraysize(kWellKnownCodesOnWrongConfiguration); ++i) {
    if (cld_language == kWellKnownCodesOnWrongConfiguration[i])
      return true;
  }
  return false;
}

std::string GetCLDVersion() {
#if defined(ENABLE_LANGUAGE_DETECTION)
  return CompactLangDet::DetectLanguageVersion();
#else
  return ""
#endif
}

}  // namespace LanguageDetectionUtil
