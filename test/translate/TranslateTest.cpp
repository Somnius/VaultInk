#include <Translate.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace translate;

namespace {

// --- Answers -----------------------------------------------------------------------------------------------------
// Where each one comes from is said next to it. "Captured" means the body api.mymemory.translated.net sent on
// 2026-11-13 for a one-word test query, unchanged. "Hand-written" means typed here after the documented fields;
// no server sent it.

// Captured: GET /get?q=book&langpair=en|el (HTTP 200). The translation it gave for "book" really is "book":
// translation-memory quality.
const char* const MM_BOOK_JSON =
    R"JSON({"responseData":{"translatedText":"book","match":1},"quotaFinished":false,"mtLangSupported":null,"responseDetails":"","responseStatus":200,"responderId":null,"exception_code":null,"matches":[{"id":"522648473","segment":"book","translation":"book","source":"en-GB","target":"el-GR","quality":74,"reference":null,"usage-count":2,"subject":"","created-by":"MateCat","last-updated-by":"MateCat","create-date":"2025-02-22 10:20:19","last-update-date":"2025-02-22 10:20:19","match":1,"penalty":0},{"id":"516568176","segment":"book","translation":"&#1603;&#1578;&#1575;&#1576;","source":"en-US","target":"el-GR","quality":"74","reference":null,"usage-count":3,"subject":"All","created-by":"MateCat","last-updated-by":"MateCat","create-date":"2024-11-01 07:38:11","last-update-date":"2024-11-01 07:38:11","match":0.99,"penalty":0},{"id":"548285682","segment":"Book","translation":"Κράτηση","source":"en-GB","target":"el-GR","quality":"74","reference":null,"usage-count":4,"subject":"All","created-by":"MateCat","last-updated-by":"MateCat","create-date":"2026-07-17 22:40:18","last-update-date":"2026-07-17 22:40:18","match":0.98,"penalty":0}]})JSON";

// Captured: GET /get?q=book&langpair=en|zz (HTTP 200, the error is in the body, the status a string).
const char* const MM_BAD_TARGET_JSON =
    R"JSON({"responseData":{"translatedText":"'ZZ' IS AN INVALID TARGET LANGUAGE . EXAMPLE: LANGPAIR=EN|IT USING 2 LETTER ISO OR RFC3066 LIKE ZH-CN. ALMOST ALL LANGUAGES SUPPORTED BUT SOME MAY HAVE NO CONTENT"},"quotaFinished":null,"mtLangSupported":null,"responseDetails":"'ZZ' IS AN INVALID TARGET LANGUAGE . EXAMPLE: LANGPAIR=EN|IT USING 2 LETTER ISO OR RFC3066 LIKE ZH-CN. ALMOST ALL LANGUAGES SUPPORTED BUT SOME MAY HAVE NO CONTENT","responseStatus":"403","responderId":null,"exception_code":null,"matches":""})JSON";

// Captured: GET /get?q=book&langpair=en|en (HTTP 200).
const char* const MM_SAME_LANGUAGE_JSON =
    R"JSON({"responseData":{"translatedText":"PLEASE SELECT TWO DISTINCT LANGUAGES"},"quotaFinished":null,"mtLangSupported":null,"responseDetails":"PLEASE SELECT TWO DISTINCT LANGUAGES","responseStatus":"403","responderId":null,"exception_code":null,"matches":""})JSON";

// Hand-written, in the shape of the captured ones: a Greek translation as \u escapes with an HTML entity in it,
// and a match whose own "translatedText"-like fields must not be taken.
const char* const MM_GREEK_JSON =
    R"JSON({"responseData":{"translatedText":"βιβλίο &amp; τετράδιο","match":0.97},"quotaFinished":false,"mtLangSupported":null,"responseDetails":"","responseStatus":200,"responderId":"12","exception_code":null,"matches":[{"id":"1","segment":"book & notebook","translation":"not this one","translatedText":"nor this one","responseData":{"translatedText":"nor this"},"quality":"74","match":0.97}]})JSON";

// Hand-written: the day's allowance used up. The wording and the fields (quotaFinished true, status 429, the
// warning in translatedText) are as remembered, NOT captured: nobody here has run into the limit. The reader
// takes any one of the three signs.
const char* const MM_QUOTA_JSON =
    R"JSON({"responseData":{"translatedText":"MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS FOR TODAY. NEXT AVAILABLE IN 10 HOURS 05 MINUTES 31 SECONDS VISIT HTTPS://MYMEMORY.TRANSLATED.NET/DOC/USAGELIMITS.PHP TO TRANSLATE MORE"},"quotaFinished":true,"mtLangSupported":null,"responseDetails":"MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS FOR TODAY. NEXT AVAILABLE IN 10 HOURS 05 MINUTES 31 SECONDS VISIT HTTPS://MYMEMORY.TRANSLATED.NET/DOC/USAGELIMITS.PHP TO TRANSLATE MORE","responseStatus":429,"responderId":null,"exception_code":null,"matches":""})JSON";

// Hand-written after the examples of https://docs.libretranslate.com/guides/api_usage/ (read 2026-11-13).
const char* const LT_SIMPLE_JSON = R"JSON({
    "translatedText": "¡Hola!"
})JSON";
const char* const LT_DETECTED_JSON = R"JSON({
    "detectedLanguage": {
        "confidence": 90.0,
        "language": "fr",
        "translatedText": "not this one"
    },
    "translatedText": "Hello!"
})JSON";
const char* const LT_ALTERNATIVES_JSON = R"JSON({
    "alternatives": [
        "Salve",
        "Pronto"
    ],
    "translatedText": "Ciao"
})JSON";
// Hand-written: the documented error shape is {"error": "<message>"}; the messages themselves are made up.
const char* const LT_ERROR_SLOW_JSON = R"JSON({"error":"Slowdown: 20 per 1 minute"})JSON";
const char* const LT_ERROR_LANGUAGE_JSON = R"JSON({"error":"xx is not supported"})JSON";
const char* const LT_ERROR_KEY_JSON = R"JSON({"error":"Please contact the server operator to get an API key"})JSON";
const char* const LT_ERROR_OTHER_JSON = R"JSON({"error":"Invalid request: missing q parameter"})JSON";

Result myMemory(const char* json, const int status = 200, const size_t chunk = 0) {
  AnswerParser parser;
  const size_t len = strlen(json);
  if (chunk == 0) {
    parser.feed(json, len);
  } else {
    for (size_t at = 0; at < len; at += chunk) parser.feed(json + at, std::min(chunk, len - at));
  }
  return parser.finishMyMemory(status);
}

Result libre(const char* json, const int status = 200) {
  AnswerParser parser;
  parser.feed(json, strlen(json));
  return parser.finishLibreTranslate(status);
}

// --- Languages ---------------------------------------------------------------------------------------------------

TEST(TranslatePair, FromTheBookIntoTheMenuLanguage) {
  const Pair p = languagePair("en-US", "EL");
  EXPECT_EQ(p.source, "en");
  EXPECT_EQ(p.target, "el");
  EXPECT_TRUE(p.ok());
  EXPECT_EQ(languagePair("fr", "de").source, "fr");
  EXPECT_EQ(languagePair("fr", "de").target, "de");
  EXPECT_EQ(languagePair("ell", "EN").source, "el");
  EXPECT_EQ(languagePair("ger", "EN").source, "de");
  EXPECT_EQ(languagePair(" el_GR ", "EN").source, "el");
}

TEST(TranslatePair, ABookInTheMenuLanguageGoesIntoEnglish) {
  const Pair p = languagePair("el-GR", "EL");
  EXPECT_EQ(p.source, "el");
  EXPECT_EQ(p.target, "en");
}

TEST(TranslatePair, AnEnglishBookUnderEnglishMenusHasNoPair) {
  const Pair p = languagePair("en", "EN");
  EXPECT_FALSE(p.ok());
  EXPECT_EQ(p.target, "");
}

TEST(TranslatePair, AnUnknownBookLanguageIsLeftEmpty) {
  for (const char* book : {"", "und", "mul", "zxx", "english", "e", "12", "ceb"}) {
    const Pair p = languagePair(book, "EL");
    EXPECT_EQ(p.source, "") << book;
    EXPECT_EQ(p.target, "el") << book;
    EXPECT_TRUE(p.ok());
  }
}

TEST(TranslatePair, AnUnknownMenuLanguageCountsAsEnglish) {
  EXPECT_EQ(languagePair("el", "").target, "en");
  EXPECT_EQ(languagePair("el", "??").target, "en");
  EXPECT_FALSE(languagePair("en", "").ok());
}

// --- Requests ----------------------------------------------------------------------------------------------------

TEST(TranslateQuery, IsTrimmedAndItsSpacesCollapsed) {
  EXPECT_EQ(cleanQuery("  the   quick\n brown\tfox \r\n"), "the quick brown fox");
  EXPECT_EQ(cleanQuery("hy\xC2\xADphen"), "hyphen");
  EXPECT_EQ(cleanQuery("a\xC2\xA0"
                       "b"),
            "a b");
  EXPECT_EQ(cleanQuery("a\x01"
                       "b\x7F"),
            "ab");
  EXPECT_EQ(cleanQuery("«Καλημέρα, κόσμε!»"), "«Καλημέρα, κόσμε!»");
  EXPECT_EQ(cleanQuery(""), "");
  EXPECT_EQ(cleanQuery(" \n\t "), "");
}

TEST(TranslateQuery, RefusesMoreThanFiveHundredBytes) {
  EXPECT_EQ(cleanQuery(std::string(500, 'a')).size(), 500u);
  EXPECT_EQ(cleanQuery(std::string(501, 'a')), "");
  // 250 Greek letters are 500 bytes; 251 are too many.
  std::string greek;
  for (int i = 0; i < 250; ++i) greek += "α";
  EXPECT_EQ(cleanQuery(greek).size(), 500u);
  EXPECT_EQ(cleanQuery(greek + "α"), "");
  // Spaces that are dropped do not count.
  EXPECT_EQ(cleanQuery("  " + std::string(500, 'a') + "  ").size(), 500u);
}

TEST(TranslateQuery, PercentEncodingOfGreek) {
  EXPECT_EQ(percentEncode("book"), "book");
  EXPECT_EQ(percentEncode("a b&c=d?e/f|g"), "a%20b%26c%3Dd%3Fe%2Ff%7Cg");
  EXPECT_EQ(percentEncode("βιβλίο"), "%CE%B2%CE%B9%CE%B2%CE%BB%CE%AF%CE%BF");
  EXPECT_EQ(percentEncode("-._~"), "-._~");
  EXPECT_EQ(percentEncode("100%"), "100%25");
}

TEST(TranslateMyMemory, UrlForAWordAndAGreekPhrase) {
  EXPECT_EQ(myMemoryUrl("book", languagePair("en", "EL")),
            "https://api.mymemory.translated.net/get?q=book&langpair=en%7Cel");
  EXPECT_EQ(myMemoryUrl(" καλό  βιβλίο ", languagePair("el", "EN")),
            "https://api.mymemory.translated.net/get?q=%CE%BA%CE%B1%CE%BB%CF%8C%20%CE%B2%CE%B9%CE%B2%CE%BB%CE%AF%CE%BF"
            "&langpair=el%7Cen");
  // Nothing the selection holds can add a parameter.
  EXPECT_EQ(myMemoryUrl("a&de=x@y.z#frag", languagePair("en", "EL")),
            "https://api.mymemory.translated.net/get?q=a%26de%3Dx%40y.z%23frag&langpair=en%7Cel");
}

TEST(TranslateMyMemory, NoUrlWithoutBothLanguagesOrAQuery) {
  EXPECT_EQ(myMemoryUrl("book", languagePair("", "EL")), "");    // MyMemory needs the source language
  EXPECT_EQ(myMemoryUrl("book", languagePair("en", "EN")), "");  // nothing to translate into
  EXPECT_EQ(myMemoryUrl("   ", languagePair("en", "EL")), "");
  EXPECT_EQ(myMemoryUrl(std::string(501, 'a'), languagePair("en", "EL")), "");
}

TEST(TranslateLibre, UrlFromWhatTheUserTyped) {
  EXPECT_EQ(libreTranslateUrl("https://lt.example.org"), "https://lt.example.org/translate");
  EXPECT_EQ(libreTranslateUrl("https://lt.example.org/"), "https://lt.example.org/translate");
  EXPECT_EQ(libreTranslateUrl(" https://lt.example.org/translate/ "), "https://lt.example.org/translate");
  EXPECT_EQ(libreTranslateUrl("http://192.168.1.20:5000"), "http://192.168.1.20:5000/translate");
  EXPECT_EQ(libreTranslateUrl("HTTPS://lt.example.org/api"), "HTTPS://lt.example.org/api/translate");
  // A host that happens to be called "translate" still gets the path.
  EXPECT_EQ(libreTranslateUrl("https://translate"), "https://translate/translate");
}

TEST(TranslateLibre, NoUrlForWhatIsNotAnAddress) {
  for (const char* bad : {"", "https://", "http://", "lt.example.org", "ftp://lt.example.org", "https:/x",
                          "https://lt.example.org/?q=1", "https://lt.example.org/#x", "https://lt example.org"}) {
    EXPECT_EQ(libreTranslateUrl(bad), "") << bad;
  }
}

TEST(TranslateLibre, BodyWithAndWithoutAKey) {
  EXPECT_EQ(libreTranslateBody("Hello!", languagePair("en", "ES"), ""),
            R"({"q":"Hello!","source":"en","target":"es","format":"text"})");
  EXPECT_EQ(libreTranslateBody("Hello!", languagePair("en", "ES"), "abc-123"),
            R"({"q":"Hello!","source":"en","target":"es","format":"text","api_key":"abc-123"})");
  // An unknown book language: the server detects it.
  EXPECT_EQ(libreTranslateBody("Bonjour", languagePair("", "EN"), ""),
            R"({"q":"Bonjour","source":"auto","target":"en","format":"text"})");
}

TEST(TranslateLibre, BodyEscapesTheSelection) {
  EXPECT_EQ(libreTranslateBody("say \"hi\" \\ now", languagePair("en", "EL"), ""),
            R"({"q":"say \"hi\" \\ now","source":"en","target":"el","format":"text"})");
  EXPECT_EQ(libreTranslateBody("καλό βιβλίο", languagePair("el", "EN"), "k\"ey"),
            R"({"q":"καλό βιβλίο","source":"el","target":"en","format":"text","api_key":"k\"ey"})");
  EXPECT_EQ(libreTranslateBody("", languagePair("en", "EL"), ""), "");
  EXPECT_EQ(libreTranslateBody("book", languagePair("en", "EN"), ""), "");
}

// --- MyMemory answers --------------------------------------------------------------------------------------------

TEST(TranslateMyMemory, ReadsACapturedAnswer) {
  const Result r = myMemory(MM_BOOK_JSON);
  EXPECT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.text, "book");
  EXPECT_FALSE(r.truncated);
}

TEST(TranslateMyMemory, ReadsGreekEscapesAndEntitiesAndSkipsTheMatches) {
  const Result r = myMemory(MM_GREEK_JSON);
  EXPECT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.text, "βιβλίο & τετράδιο");
}

TEST(TranslateMyMemory, TheSameAnswerInAnyChunkSize) {
  for (const size_t chunk : {1u, 2u, 3u, 7u, 64u}) {
    EXPECT_EQ(myMemory(MM_GREEK_JSON, 200, chunk).text, "βιβλίο & τετράδιο") << chunk;
    EXPECT_EQ(myMemory(MM_BOOK_JSON, 200, chunk).text, "book") << chunk;
    EXPECT_EQ(myMemory(MM_BAD_TARGET_JSON, 200, chunk).status, Status::BadLanguage) << chunk;
  }
}

TEST(TranslateMyMemory, AnErrorInsideA200IsNotATranslation) {
  Result r = myMemory(MM_BAD_TARGET_JSON);
  EXPECT_EQ(r.status, Status::BadLanguage);
  EXPECT_EQ(r.text, "");
  EXPECT_NE(r.detail.find("INVALID TARGET LANGUAGE"), std::string::npos);
  r = myMemory(MM_SAME_LANGUAGE_JSON);
  EXPECT_EQ(r.status, Status::BadLanguage);
  EXPECT_EQ(r.text, "");
  EXPECT_EQ(r.detail, "PLEASE SELECT TWO DISTINCT LANGUAGES");
}

TEST(TranslateMyMemory, QuotaByAnyOfItsSigns) {
  Result r = myMemory(MM_QUOTA_JSON);
  EXPECT_EQ(r.status, Status::Quota);
  EXPECT_EQ(r.text, "");
  EXPECT_NE(r.detail.find("MYMEMORY WARNING"), std::string::npos);
  // HTTP 429 with any body, or none.
  EXPECT_EQ(myMemory("", 429).status, Status::Quota);
  EXPECT_EQ(myMemory("<html>Too Many Requests</html>", 429).status, Status::Quota);
  EXPECT_EQ(myMemory(MM_BOOK_JSON, 429).status, Status::Quota);
  // Each sign alone, inside a 200.
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"x"},"quotaFinished":true,"responseStatus":200})").status,
            Status::Quota);
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"x"},"responseStatus":429})").status, Status::Quota);
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"x"},"responseStatus":"429"})").status, Status::Quota);
  EXPECT_EQ(
      myMemory(R"({"responseData":{"translatedText":"MYMEMORY WARNING: YOU USED ALL"},"responseStatus":200})").status,
      Status::Quota);
  // A translation that only mentions the words is a translation.
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"a MYMEMORY WARNING sign"},"responseStatus":200})").status,
            Status::Ok);
}

TEST(TranslateMyMemory, OtherStatusesAreNamed) {
  EXPECT_EQ(myMemory("", 500).status, Status::ServerError);
  EXPECT_EQ(myMemory("<html>Bad Gateway</html>", 502).status, Status::ServerError);
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"x"},"responseStatus":"500"})").status, Status::ServerError);
  EXPECT_EQ(myMemory("", 403).status, Status::Refused);
  EXPECT_EQ(
      myMemory(
          R"({"responseData":{"translatedText":"NO ACCESS"},"responseDetails":"NO ACCESS","responseStatus":"403"})")
          .status,
      Status::Refused);
  EXPECT_EQ(myMemory("", 404).status, Status::Invalid);
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"x"},"responseStatus":"abc"})").status, Status::Invalid);
}

TEST(TranslateMyMemory, WhatIsNotAnAnswerIsInvalid) {
  EXPECT_EQ(myMemory("").status, Status::Invalid);
  EXPECT_EQ(myMemory("<html>hello</html>").status, Status::Invalid);
  EXPECT_EQ(myMemory("[]").status, Status::Invalid);
  EXPECT_EQ(myMemory(R"(["translatedText","x"])").status, Status::Invalid);
  EXPECT_EQ(myMemory(R"({"responseStatus":200})").status, Status::Invalid);
  // translatedText somewhere else than responseData or the top level is not it.
  EXPECT_EQ(myMemory(R"({"other":{"translatedText":"x"},"responseStatus":200})").status, Status::Invalid);
  EXPECT_EQ(myMemory(R"({"responseData":[{"translatedText":"x"}],"responseStatus":200})").status, Status::Invalid);
  // A key called like a value.
  EXPECT_EQ(myMemory(R"({"a":"responseData","b":{"translatedText":"x"},"responseStatus":200})").status,
            Status::Invalid);
}

TEST(TranslateMyMemory, AnEmptyTranslationIsNoTranslation) {
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":""},"responseStatus":200})").status, Status::NoTranslation);
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":"  \n "},"responseStatus":200})").status,
            Status::NoTranslation);
  // null is not a text at all.
  EXPECT_EQ(myMemory(R"({"responseData":{"translatedText":null},"responseStatus":200})").status, Status::Invalid);
}

TEST(TranslateMyMemory, TheFirstTranslatedTextWins) {
  const Result r = myMemory(
      R"({"responseData":{"translatedText":"one","translatedText":"two"},"translatedText":"three","responseStatus":200})");
  EXPECT_EQ(r.text, "one");
}

TEST(TranslateAnswer, EntitiesAndEscapes) {
  const Result r = myMemory(
      R"({"responseData":{"translatedText":"it&#39;s &quot;A&amp;B&quot; &lt;b&gt; &#x3b1;&#945; &nbsp; &#0; &#xD800; & \/ \\ 🏛\t\u0001x"},"responseStatus":200})");
  EXPECT_EQ(r.status, Status::Ok);
  EXPECT_EQ(r.text, "it's \"A&B\" <b> αα &nbsp; &#0; &#xD800; & / \\ 🏛  x");
}

TEST(TranslateAnswer, ALongTranslationIsCutOnAWholeCharacter) {
  std::string json = R"({"responseData":{"translatedText":")";
  for (int i = 0; i < 1500; ++i) json += "α";  // 3000 bytes, over MAX_TEXT_BYTES
  json += R"("},"responseStatus":200})";
  const Result r = myMemory(json.c_str());
  EXPECT_EQ(r.status, Status::Ok);
  EXPECT_TRUE(r.truncated);
  EXPECT_LE(r.text.size(), MAX_TEXT_BYTES + 4);
  const std::string tail = " \xE2\x80\xA6";
  ASSERT_GT(r.text.size(), tail.size());
  EXPECT_EQ(r.text.substr(r.text.size() - tail.size()), tail);
  const std::string body = r.text.substr(0, r.text.size() - tail.size());
  EXPECT_EQ(body.size() % 2, 0u);  // whole two-byte letters only
  EXPECT_EQ(body.substr(body.size() - 2), "α");
}

TEST(TranslateAnswer, StopsAtEightKilobytes) {
  // A long list of matches before the fields that matter: nothing after the limit is read.
  std::string json = R"({"matches":[)";
  while (json.size() < MAX_BODY_BYTES + 100) json += R"({"segment":"filler filler filler filler"},)";
  json += R"({}],"responseData":{"translatedText":"late"},"responseStatus":200})";
  AnswerParser parser;
  EXPECT_FALSE(parser.feed(json.data(), json.size()));
  EXPECT_FALSE(parser.feed("x", 1));
  EXPECT_EQ(parser.finishMyMemory(200).status, Status::Invalid);

  AnswerParser small;
  EXPECT_TRUE(small.feed(MM_BOOK_JSON, strlen(MM_BOOK_JSON)));
}

TEST(TranslateAnswer, AnAnswerCutInsideTheTranslationShowsWhatCame) {
  const std::string json = R"({"responseStatus":200,"responseData":{"translatedText":"καλό βιβλ)";
  // Cut in the middle of the two bytes of a letter.
  AnswerParser parser;
  parser.feed(json.data(), json.size() - 1);
  const Result r = parser.finishMyMemory(200);
  EXPECT_EQ(r.status, Status::Ok);
  EXPECT_TRUE(r.truncated);
  EXPECT_EQ(r.text, "καλό βιβ \xE2\x80\xA6");
}

// --- LibreTranslate answers --------------------------------------------------------------------------------------

TEST(TranslateLibre, ReadsTheDocumentedAnswers) {
  EXPECT_EQ(libre(LT_SIMPLE_JSON).status, Status::Ok);
  EXPECT_EQ(libre(LT_SIMPLE_JSON).text, "¡Hola!");
  EXPECT_EQ(libre(LT_DETECTED_JSON).text, "Hello!");
  EXPECT_EQ(libre(LT_ALTERNATIVES_JSON).text, "Ciao");
}

TEST(TranslateLibre, ErrorStatusesAreMapped) {
  Result r = libre(LT_ERROR_SLOW_JSON, 429);
  EXPECT_EQ(r.status, Status::Quota);
  EXPECT_EQ(r.detail, "Slowdown: 20 per 1 minute");
  EXPECT_EQ(r.text, "");
  EXPECT_EQ(libre(LT_ERROR_KEY_JSON, 403).status, Status::Refused);
  EXPECT_EQ(libre(LT_ERROR_OTHER_JSON, 500).status, Status::ServerError);
  EXPECT_EQ(libre("", 503).status, Status::ServerError);
  EXPECT_EQ(libre(LT_ERROR_LANGUAGE_JSON, 400).status, Status::BadLanguage);
  EXPECT_EQ(libre(LT_ERROR_KEY_JSON, 400).status, Status::Refused);
  EXPECT_EQ(libre(LT_ERROR_OTHER_JSON, 400).status, Status::Invalid);
  EXPECT_EQ(libre(LT_ERROR_OTHER_JSON, 404).status, Status::Invalid);
  // A translation next to an error status is not shown.
  EXPECT_EQ(libre(LT_SIMPLE_JSON, 429).text, "");
  EXPECT_EQ(libre(LT_SIMPLE_JSON, 500).text, "");
}

TEST(TranslateLibre, A200WithoutATranslationIsInvalid) {
  EXPECT_EQ(libre(R"({"error":"odd"})").status, Status::Invalid);
  EXPECT_EQ(libre("<html>a sign-in page</html>").status, Status::Invalid);
  EXPECT_EQ(libre(R"({"translatedText":["a","b"]})").status, Status::Invalid);  // the answer to a list of texts
  EXPECT_EQ(libre(R"({"translatedText":""})").status, Status::NoTranslation);
}

}  // namespace
