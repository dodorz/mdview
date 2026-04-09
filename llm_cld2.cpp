#include "llm_cld2.h"

#include <string.h>

#include "cld2/public/compact_lang_det.h"
#include "cld2/internal/lang_script.h"

BOOL
LlmCld2_DetectLanguage(const char* utf8Text, char* languageCode, size_t languageCodeCount, BOOL* isReliable)
{
  CLD2::CLDHints hints = {NULL, NULL, 0, CLD2::UNKNOWN_LANGUAGE};
  CLD2::Language language3[3] = {CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE};
  int percent3[3] = {0, 0, 0};
  double normalizedScore3[3] = {0.0, 0.0, 0.0};
  int textBytes = 0;
  int validPrefixBytes = 0;
  bool reliable = false;
  CLD2::Language detected;
  const char* detectedCode;

  if (languageCode != NULL && languageCodeCount > 0) {
    languageCode[0] = '\0';
  }
  if (isReliable != NULL) {
    *isReliable = FALSE;
  }
  if (utf8Text == NULL || languageCode == NULL || languageCodeCount == 0) {
    return FALSE;
  }

  detected = CLD2::ExtDetectLanguageSummaryCheckUTF8(
      utf8Text,
      (int)strlen(utf8Text),
      true,
      &hints,
      CLD2::kCLDFlagBestEffort,
      language3,
      percent3,
      normalizedScore3,
      NULL,
      &textBytes,
      &reliable,
      &validPrefixBytes);

  detectedCode = CLD2::LanguageCode(detected);
  if (detectedCode == NULL || *detectedCode == '\0') {
    return FALSE;
  }

  strncpy_s(languageCode, languageCodeCount, detectedCode, _TRUNCATE);
  if (isReliable != NULL) {
    *isReliable = reliable ? TRUE : FALSE;
  }
  return TRUE;
}
