// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Features shared by parsing and pre-parsing scanners.

#ifndef V8_PARSING_SCANNER_H_
#define V8_PARSING_SCANNER_H_

#include "src/allocation.h"
#include "src/base/logging.h"
#include "src/char-predicates.h"
#include "src/collector.h"
#include "src/globals.h"
#include "src/hashmap.h"
#include "src/list.h"
#include "src/messages.h"
#include "src/parsing/token.h"
#include "src/unicode.h"
#include "src/unicode-decoder.h"

namespace v8 {
namespace internal {


class AstRawString;
class AstValueFactory;
class ParserRecorder;
class UnicodeCache;


// ---------------------------------------------------------------------
// Buffered stream of UTF-16 code units, using an internal UTF-16 buffer.
// A code unit is a 16 bit value representing either a 16 bit code point
// or one part of a surrogate pair that make a single 21 bit code point.

class Utf16CharacterStream {
 public:
  Utf16CharacterStream() : pos_(0) { }
  virtual ~Utf16CharacterStream() { }

  // Returns and advances past the next UTF-16 code unit in the input
  // stream. If there are no more code units, it returns a negative
  // value.
  inline uc32 Advance() {
    if (buffer_cursor_ < buffer_end_ || ReadBlock()) {
      pos_++;
      return static_cast<uc32>(*(buffer_cursor_++));
    }
    // Note: currently the following increment is necessary to avoid a
    // parser problem! The scanner treats the final kEndOfInput as
    // a code unit with a position, and does math relative to that
    // position.
    pos_++;

    return kEndOfInput;
  }

  // Return the current position in the code unit stream.
  // Starts at zero.
  inline size_t pos() const { return pos_; }

  // Skips forward past the next code_unit_count UTF-16 code units
  // in the input, or until the end of input if that comes sooner.
  // Returns the number of code units actually skipped. If less
  // than code_unit_count,
  inline size_t SeekForward(size_t code_unit_count) {
    size_t buffered_chars = buffer_end_ - buffer_cursor_;
    if (code_unit_count <= buffered_chars) {
      buffer_cursor_ += code_unit_count;
      pos_ += code_unit_count;
      return code_unit_count;
    }
    return SlowSeekForward(code_unit_count);
  }

  // Pushes back the most recently read UTF-16 code unit (or negative
  // value if at end of input), i.e., the value returned by the most recent
  // call to Advance.
  // Must not be used right after calling SeekForward.
  virtual void PushBack(int32_t code_unit) = 0;

  virtual bool SetBookmark();
  virtual void ResetToBookmark();

 protected:
  static const uc32 kEndOfInput = -1;

  // Ensures that the buffer_cursor_ points to the code_unit at
  // position pos_ of the input, if possible. If the position
  // is at or after the end of the input, return false. If there
  // are more code_units available, return true.
  virtual bool ReadBlock() = 0;
  virtual size_t SlowSeekForward(size_t code_unit_count) = 0;

  const uint16_t* buffer_cursor_;
  const uint16_t* buffer_end_;
  size_t pos_;
};


// ---------------------------------------------------------------------
// DuplicateFinder discovers duplicate symbols.

class DuplicateFinder {
 public:
  explicit DuplicateFinder(UnicodeCache* constants)
      : unicode_constants_(constants),
        backing_store_(16),
        map_(&Match) { }

  int AddOneByteSymbol(Vector<const uint8_t> key, int value);
  int AddTwoByteSymbol(Vector<const uint16_t> key, int value);
  // Add a a number literal by converting it (if necessary)
  // to the string that ToString(ToNumber(literal)) would generate.
  // and then adding that string with AddOneByteSymbol.
  // This string is the actual value used as key in an object literal,
  // and the one that must be different from the other keys.
  int AddNumber(Vector<const uint8_t> key, int value);

 private:
  int AddSymbol(Vector<const uint8_t> key, bool is_one_byte, int value);
  // Backs up the key and its length in the backing store.
  // The backup is stored with a base 127 encoding of the
  // length (plus a bit saying whether the string is one byte),
  // followed by the bytes of the key.
  uint8_t* BackupKey(Vector<const uint8_t> key, bool is_one_byte);

  // Compare two encoded keys (both pointing into the backing store)
  // for having the same base-127 encoded lengths and representation.
  // and then having the same 'length' bytes following.
  static bool Match(void* first, void* second);
  // Creates a hash from a sequence of bytes.
  static uint32_t Hash(Vector<const uint8_t> key, bool is_one_byte);
  // Checks whether a string containing a JS number is its canonical
  // form.
  static bool IsNumberCanonical(Vector<const uint8_t> key);

  // Size of buffer. Sufficient for using it to call DoubleToCString in
  // from conversions.h.
  static const int kBufferSize = 100;

  UnicodeCache* unicode_constants_;
  // Backing store used to store strings used as hashmap keys.
  SequenceCollector<unsigned char> backing_store_;
  HashMap map_;
  // Buffer used for string->number->canonical string conversions.
  char number_buffer_[kBufferSize];
};


// ----------------------------------------------------------------------------
// LiteralBuffer -  Collector of chars of literals.

class LiteralBuffer {
 public:
  LiteralBuffer() : is_one_byte_(true), position_(0), backing_store_() { }

  ~LiteralBuffer() { backing_store_.Dispose(); }

  INLINE(void AddChar(uint32_t code_unit)) {
    if (position_ >= backing_store_.length()) ExpandBuffer();
    if (is_one_byte_) {
      if (code_unit <= unibrow::Latin1::kMaxChar) {
        backing_store_[position_] = static_cast<byte>(code_unit);
        position_ += kOneByteSize;
        return;
      }
      ConvertToTwoByte();
    }
    if (code_unit <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
      *reinterpret_cast<uint16_t*>(&backing_store_[position_]) = code_unit;
      position_ += kUC16Size;
    } else {
      *reinterpret_cast<uint16_t*>(&backing_store_[position_]) =
          unibrow::Utf16::LeadSurrogate(code_unit);
      position_ += kUC16Size;
      if (position_ >= backing_store_.length()) ExpandBuffer();
      *reinterpret_cast<uint16_t*>(&backing_store_[position_]) =
          unibrow::Utf16::TrailSurrogate(code_unit);
      position_ += kUC16Size;
    }
  }

  bool is_one_byte() const { return is_one_byte_; }

  bool is_contextual_keyword(Vector<const char> keyword) const {
    return is_one_byte() && keyword.length() == position_ &&
        (memcmp(keyword.start(), backing_store_.start(), position_) == 0);
  }

  Vector<const uint16_t> two_byte_literal() const {
    DCHECK(!is_one_byte_);
    DCHECK((position_ & 0x1) == 0);
    return Vector<const uint16_t>(
        reinterpret_cast<const uint16_t*>(backing_store_.start()),
        position_ >> 1);
  }

  Vector<const uint8_t> one_byte_literal() const {
    DCHECK(is_one_byte_);
    return Vector<const uint8_t>(
        reinterpret_cast<const uint8_t*>(backing_store_.start()),
        position_);
  }

  int length() const {
    return is_one_byte_ ? position_ : (position_ >> 1);
  }

  void ReduceLength(int delta) {
    position_ -= delta * (is_one_byte_ ? kOneByteSize : kUC16Size);
  }

  void Reset() {
    position_ = 0;
    is_one_byte_ = true;
  }

  Handle<String> Internalize(Isolate* isolate) const;

  void CopyFrom(const LiteralBuffer* other) {
    if (other == nullptr) {
      Reset();
    } else {
      is_one_byte_ = other->is_one_byte_;
      position_ = other->position_;
      if (position_ < backing_store_.length()) {
        std::copy(other->backing_store_.begin(),
                  other->backing_store_.begin() + position_,
                  backing_store_.begin());
      } else {
        backing_store_.Dispose();
        backing_store_ = other->backing_store_.Clone();
      }
    }
  }

 private:
  static const int kInitialCapacity = 16;
  static const int kGrowthFactory = 4;
  static const int kMinConversionSlack = 256;
  static const int kMaxGrowth = 1 * MB;
  inline int NewCapacity(int min_capacity) {
    int capacity = Max(min_capacity, backing_store_.length());
    int new_capacity = Min(capacity * kGrowthFactory, capacity + kMaxGrowth);
    return new_capacity;
  }

  void ExpandBuffer() {
    Vector<byte> new_store = Vector<byte>::New(NewCapacity(kInitialCapacity));
    MemCopy(new_store.start(), backing_store_.start(), position_);
    backing_store_.Dispose();
    backing_store_ = new_store;
  }

  void ConvertToTwoByte() {
    DCHECK(is_one_byte_);
    Vector<byte> new_store;
    int new_content_size = position_ * kUC16Size;
    if (new_content_size >= backing_store_.length()) {
      // Ensure room for all currently read code units as UC16 as well
      // as the code unit about to be stored.
      new_store = Vector<byte>::New(NewCapacity(new_content_size));
    } else {
      new_store = backing_store_;
    }
    uint8_t* src = backing_store_.start();
    uint16_t* dst = reinterpret_cast<uint16_t*>(new_store.start());
    for (int i = position_ - 1; i >= 0; i--) {
      dst[i] = src[i];
    }
    if (new_store.start() != backing_store_.start()) {
      backing_store_.Dispose();
      backing_store_ = new_store;
    }
    position_ = new_content_size;
    is_one_byte_ = false;
  }

  bool is_one_byte_;
  int position_;
  Vector<byte> backing_store_;

  DISALLOW_COPY_AND_ASSIGN(LiteralBuffer);
};


// ----------------------------------------------------------------------------
// JavaScript Scanner.

class Scanner {
 public:
  // Scoped helper for literal recording. Automatically drops the literal
  // if aborting the scanning before it's complete.
  class LiteralScope {
   public:
    explicit LiteralScope(Scanner* self) : scanner_(self), complete_(false) {
      scanner_->StartLiteral();
    }
     ~LiteralScope() {
       if (!complete_) scanner_->DropLiteral();
     }
    void Complete() {
      complete_ = true;
    }

   private:
    Scanner* scanner_;
    bool complete_;
  };

  // Scoped helper for a re-settable bookmark.
  class BookmarkScope {
   public:
    explicit BookmarkScope(Scanner* scanner) : scanner_(scanner) {
      DCHECK_NOT_NULL(scanner_);
    }
    ~BookmarkScope() { scanner_->DropBookmark(); }

    bool Set() { return scanner_->SetBookmark(); }
    void Reset() { scanner_->ResetToBookmark(); }
    bool HasBeenSet() { return scanner_->BookmarkHasBeenSet(); }
    bool HasBeenReset() { return scanner_->BookmarkHasBeenReset(); }

   private:
    Scanner* scanner_;

    DISALLOW_COPY_AND_ASSIGN(BookmarkScope);
  };

  // Representation of an interval of source positions.
  struct Location {
    Location(int b, int e) : beg_pos(b), end_pos(e) { }
    Location() : beg_pos(0), end_pos(0) { }

    bool IsValid() const {
      return beg_pos >= 0 && end_pos >= beg_pos;
    }

    static Location invalid() { return Location(-1, -1); }

    int beg_pos;
    int end_pos;
  };

  // -1 is outside of the range of any real source code.
  static const int kNoOctalLocation = -1;

  explicit Scanner(UnicodeCache* scanner_contants);

  void Initialize(Utf16CharacterStream* source);

  // Returns the next token and advances input.
  Token::Value Next();
  // Returns the token following peek()
  Token::Value PeekAhead();
  // Returns the current token again.
  Token::Value current_token() { return current_.token; }
  // Returns the location information for the current token
  // (the token last returned by Next()).
  Location location() const { return current_.location; }

  bool has_error() const { return scanner_error_ != MessageTemplate::kNone; }
  MessageTemplate::Template error() const { return scanner_error_; }
  Location error_location() const { return scanner_error_location_; }

  // Similar functions for the upcoming token.

  // One token look-ahead (past the token returned by Next()).
  Token::Value peek() const { return next_.token; }

  Location peek_location() const { return next_.location; }

  bool literal_contains_escapes() const {
    return LiteralContainsEscapes(current_);
  }
  bool next_literal_contains_escapes() const {
    return LiteralContainsEscapes(next_);
  }
  bool is_literal_contextual_keyword(Vector<const char> keyword) {
    DCHECK_NOT_NULL(current_.literal_chars);
    return current_.literal_chars->is_contextual_keyword(keyword);
  }
  bool is_next_contextual_keyword(Vector<const char> keyword) {
    DCHECK_NOT_NULL(next_.literal_chars);
    return next_.literal_chars->is_contextual_keyword(keyword);
  }

  const AstRawString* CurrentSymbol(AstValueFactory* ast_value_factory);
  const AstRawString* NextSymbol(AstValueFactory* ast_value_factory);
  const AstRawString* CurrentRawSymbol(AstValueFactory* ast_value_factory);

  double DoubleValue();
  bool ContainsDot();
  bool LiteralMatches(const char* data, int length, bool allow_escapes = true) {
    if (is_literal_one_byte() &&
        literal_length() == length &&
        (allow_escapes || !literal_contains_escapes())) {
      const char* token =
          reinterpret_cast<const char*>(literal_one_byte_string().start());
      return !strncmp(token, data, length);
    }
    return false;
  }
  inline bool UnescapedLiteralMatches(const char* data, int length) {
    return LiteralMatches(data, length, false);
  }

  void IsGetOrSet(bool* is_get, bool* is_set) {
    if (is_literal_one_byte() &&
        literal_length() == 3 &&
        !literal_contains_escapes()) {
      const char* token =
          reinterpret_cast<const char*>(literal_one_byte_string().start());
      *is_get = strncmp(token, "get", 3) == 0;
      *is_set = !*is_get && strncmp(token, "set", 3) == 0;
    }
  }

  int FindSymbol(DuplicateFinder* finder, int value);

  UnicodeCache* unicode_cache() { return unicode_cache_; }

  // Returns the location of the last seen octal literal.
  Location octal_position() const { return octal_pos_; }
  void clear_octal_position() { octal_pos_ = Location::invalid(); }

  // Returns the value of the last smi that was scanned.
  int smi_value() const { return current_.smi_value_; }

  // Seek forward to the given position.  This operation does not
  // work in general, for instance when there are pushed back
  // characters, but works for seeking forward until simple delimiter
  // tokens, which is what it is used for.
  void SeekForward(int pos);

  // Returns true if there was a line terminator before the peek'ed token,
  // possibly inside a multi-line comment.
  bool HasAnyLineTerminatorBeforeNext() const {
    return has_line_terminator_before_next_ ||
           has_multiline_comment_before_next_;
  }

  // Scans the input as a regular expression pattern, previous
  // character(s) must be /(=). Returns true if a pattern is scanned.
  bool ScanRegExpPattern(bool seen_equal);
  // Scans the input as regular expression flags. Returns the flags on success.
  Maybe<RegExp::Flags> ScanRegExpFlags();

  // Scans the input as a template literal
  Token::Value ScanTemplateStart();
  Token::Value ScanTemplateContinuation();

  const LiteralBuffer* source_url() const { return &source_url_; }
  const LiteralBuffer* source_mapping_url() const {
    return &source_mapping_url_;
  }

  bool IdentifierIsFutureStrictReserved(const AstRawString* string) const;

  bool FoundHtmlComment() const { return found_html_comment_; }

#define DECLARE_ACCESSORS(name)                                \
  inline bool allow_##name() const { return allow_##name##_; } \
  inline void set_allow_##name(bool allow) { allow_##name##_ = allow; }
  DECLARE_ACCESSORS(harmony_exponentiation_operator)
#undef ACCESSOR

 private:
  // The current and look-ahead token.
  struct TokenDesc {
    Token::Value token;
    Location location;
    LiteralBuffer* literal_chars;
    LiteralBuffer* raw_literal_chars;
    int smi_value_;
  };

  static const int kCharacterLookaheadBufferSize = 1;

  // Scans octal escape sequence. Also accepts "\0" decimal escape sequence.
  template <bool capture_raw>
  uc32 ScanOctalEscape(uc32 c, int length);

  // Call this after setting source_ to the input.
  void Init() {
    // Set c0_ (one character ahead)
    STATIC_ASSERT(kCharacterLookaheadBufferSize == 1);
    Advance();
    // Initialize current_ to not refer to a literal.
    current_.literal_chars = NULL;
    current_.raw_literal_chars = NULL;
    next_next_.token = Token::UNINITIALIZED;
    found_html_comment_ = false;
    scanner_error_ = MessageTemplate::kNone;
  }

  // Support BookmarkScope functionality.
  bool SetBookmark();
  void ResetToBookmark();
  bool BookmarkHasBeenSet();
  bool BookmarkHasBeenReset();
  void DropBookmark();
  static void CopyTokenDesc(TokenDesc* to, TokenDesc* from);

  void ReportScannerError(const Location& location,
                          MessageTemplate::Template error) {
    if (has_error()) return;
    scanner_error_ = error;
    scanner_error_location_ = location;
  }

  void ReportScannerError(int pos, MessageTemplate::Template error) {
    if (has_error()) return;
    scanner_error_ = error;
    scanner_error_location_ = Location(pos, pos + 1);
  }

  // Literal buffer support
  inline void StartLiteral() {
    LiteralBuffer* free_buffer =
        (current_.literal_chars == &literal_buffer0_)
            ? &literal_buffer1_
            : (current_.literal_chars == &literal_buffer1_) ? &literal_buffer2_
                                                            : &literal_buffer0_;
    free_buffer->Reset();
    next_.literal_chars = free_buffer;
  }

  inline void StartRawLiteral() {
    LiteralBuffer* free_buffer =
        (current_.raw_literal_chars == &raw_literal_buffer0_)
            ? &raw_literal_buffer1_
            : (current_.raw_literal_chars == &raw_literal_buffer1_)
                  ? &raw_literal_buffer2_
                  : &raw_literal_buffer0_;
    free_buffer->Reset();
    next_.raw_literal_chars = free_buffer;
  }

  INLINE(void AddLiteralChar(uc32 c)) {
    DCHECK_NOT_NULL(next_.literal_chars);
    next_.literal_chars->AddChar(c);
  }

  INLINE(void AddRawLiteralChar(uc32 c)) {
    DCHECK_NOT_NULL(next_.raw_literal_chars);
    next_.raw_literal_chars->AddChar(c);
  }

  INLINE(void ReduceRawLiteralLength(int delta)) {
    DCHECK_NOT_NULL(next_.raw_literal_chars);
    next_.raw_literal_chars->ReduceLength(delta);
  }

  // Stops scanning of a literal and drop the collected characters,
  // e.g., due to an encountered error.
  inline void DropLiteral() {
    next_.literal_chars = NULL;
    next_.raw_literal_chars = NULL;
  }

  inline void AddLiteralCharAdvance() {
    AddLiteralChar(c0_);
    Advance();
  }

  // Low-level scanning support.
  template <bool capture_raw = false, bool check_surrogate = true>
  void Advance() {
    if (capture_raw) {
      AddRawLiteralChar(c0_);
    }
    c0_ = source_->Advance();
    if (check_surrogate) HandleLeadSurrogate();
  }

  void HandleLeadSurrogate() {
    if (unibrow::Utf16::IsLeadSurrogate(c0_)) {
      uc32 c1 = source_->Advance();
      if (!unibrow::Utf16::IsTrailSurrogate(c1)) {
        source_->PushBack(c1);
      } else {
        c0_ = unibrow::Utf16::CombineSurrogatePair(c0_, c1);
      }
    }
  }

  void PushBack(uc32 ch) {
    if (ch > static_cast<uc32>(unibrow::Utf16::kMaxNonSurrogateCharCode)) {
      source_->PushBack(unibrow::Utf16::TrailSurrogate(c0_));
      source_->PushBack(unibrow::Utf16::LeadSurrogate(c0_));
    } else {
      source_->PushBack(c0_);
    }
    c0_ = ch;
  }

  inline Token::Value Select(Token::Value tok) {
    Advance();
    return tok;
  }

  inline Token::Value Select(uc32 next, Token::Value then, Token::Value else_) {
    Advance();
    if (c0_ == next) {
      Advance();
      return then;
    } else {
      return else_;
    }
  }

  // Returns the literal string, if any, for the current token (the
  // token last returned by Next()). The string is 0-terminated.
  // Literal strings are collected for identifiers, strings, numbers as well
  // as for template literals. For template literals we also collect the raw
  // form.
  // These functions only give the correct result if the literal was scanned
  // when a LiteralScope object is alive.
  Vector<const uint8_t> literal_one_byte_string() {
    DCHECK_NOT_NULL(current_.literal_chars);
    return current_.literal_chars->one_byte_literal();
  }
  Vector<const uint16_t> literal_two_byte_string() {
    DCHECK_NOT_NULL(current_.literal_chars);
    return current_.literal_chars->two_byte_literal();
  }
  bool is_literal_one_byte() {
    DCHECK_NOT_NULL(current_.literal_chars);
    return current_.literal_chars->is_one_byte();
  }
  int literal_length() const {
    DCHECK_NOT_NULL(current_.literal_chars);
    return current_.literal_chars->length();
  }
  // Returns the literal string for the next token (the token that
  // would be returned if Next() were called).
  Vector<const uint8_t> next_literal_one_byte_string() {
    DCHECK_NOT_NULL(next_.literal_chars);
    return next_.literal_chars->one_byte_literal();
  }
  Vector<const uint16_t> next_literal_two_byte_string() {
    DCHECK_NOT_NULL(next_.literal_chars);
    return next_.literal_chars->two_byte_literal();
  }
  bool is_next_literal_one_byte() {
    DCHECK_NOT_NULL(next_.literal_chars);
    return next_.literal_chars->is_one_byte();
  }
  Vector<const uint8_t> raw_literal_one_byte_string() {
    DCHECK_NOT_NULL(current_.raw_literal_chars);
    return current_.raw_literal_chars->one_byte_literal();
  }
  Vector<const uint16_t> raw_literal_two_byte_string() {
    DCHECK_NOT_NULL(current_.raw_literal_chars);
    return current_.raw_literal_chars->two_byte_literal();
  }
  bool is_raw_literal_one_byte() {
    DCHECK_NOT_NULL(current_.raw_literal_chars);
    return current_.raw_literal_chars->is_one_byte();
  }

  template <bool capture_raw, bool unicode = false>
  uc32 ScanHexNumber(int expected_length);
  // Scan a number of any length but not bigger than max_value. For example, the
  // number can be 000000001, so it's very long in characters but its value is
  // small.
  template <bool capture_raw>
  uc32 ScanUnlimitedLengthHexNumber(int max_value, int beg_pos);

  // Scans a single JavaScript token.
  void Scan();

  bool SkipWhiteSpace();
  Token::Value SkipSingleLineComment();
  Token::Value SkipSourceURLComment();
  void TryToParseSourceURLComment();
  Token::Value SkipMultiLineComment();
  // Scans a possible HTML comment -- begins with '<!'.
  Token::Value ScanHtmlComment();

  void ScanDecimalDigits();
  Token::Value ScanNumber(bool seen_period);
  Token::Value ScanIdentifierOrKeyword();
  Token::Value ScanIdentifierSuffix(LiteralScope* literal, bool escaped);

  Token::Value ScanString();

  // Scans an escape-sequence which is part of a string and adds the
  // decoded character to the current literal. Returns true if a pattern
  // is scanned.
  template <bool capture_raw, bool in_template_literal>
  bool ScanEscape();

  // Decodes a Unicode escape-sequence which is part of an identifier.
  // If the escape sequence cannot be decoded the result is kBadChar.
  uc32 ScanIdentifierUnicodeEscape();
  // Helper for the above functions.
  template <bool capture_raw>
  uc32 ScanUnicodeEscape();

  Token::Value ScanTemplateSpan();

  // Return the current source position.
  int source_pos() {
    return static_cast<int>(source_->pos()) - kCharacterLookaheadBufferSize;
  }

  static bool LiteralContainsEscapes(const TokenDesc& token) {
    Location location = token.location;
    int source_length = (location.end_pos - location.beg_pos);
    if (token.token == Token::STRING) {
      // Subtract delimiters.
      source_length -= 2;
    }
    return token.literal_chars->length() != source_length;
  }

  UnicodeCache* unicode_cache_;

  // Buffers collecting literal strings, numbers, etc.
  LiteralBuffer literal_buffer0_;
  LiteralBuffer literal_buffer1_;
  LiteralBuffer literal_buffer2_;

  // Values parsed from magic comments.
  LiteralBuffer source_url_;
  LiteralBuffer source_mapping_url_;

  // Buffer to store raw string values
  LiteralBuffer raw_literal_buffer0_;
  LiteralBuffer raw_literal_buffer1_;
  LiteralBuffer raw_literal_buffer2_;

  TokenDesc current_;    // desc for current token (as returned by Next())
  TokenDesc next_;       // desc for next token (one token look-ahead)
  TokenDesc next_next_;  // desc for the token after next (after PeakAhead())

  // Variables for Scanner::BookmarkScope and the *Bookmark implementation.
  // These variables contain the scanner state when a bookmark is set.
  //
  // We will use bookmark_c0_ as a 'control' variable, where:
  // - bookmark_c0_ >= 0: A bookmark has been set and this contains c0_.
  // - bookmark_c0_ == -1: No bookmark has been set.
  // - bookmark_c0_ == -2: The bookmark has been applied (ResetToBookmark).
  //
  // Which state is being bookmarked? The parser state is distributed over
  // several variables, roughly like this:
  //   ...    1234        +       5678 ..... [character stream]
  //       [current_] [next_] c0_ |      [scanner state]
  // So when the scanner is logically at the beginning of an expression
  // like "1234 + 4567", then:
  // - current_ contains "1234"
  // - next_ contains "+"
  // - c0_ contains ' ' (the space between "+" and "5678",
  // - the source_ character stream points to the beginning of "5678".
  // To be able to restore this state, we will keep copies of current_, next_,
  // and c0_; we'll ask the stream to bookmark itself, and we'll copy the
  // contents of current_'s and next_'s literal buffers to bookmark_*_literal_.
  static const uc32 kNoBookmark = -1;
  static const uc32 kBookmarkWasApplied = -2;
  uc32 bookmark_c0_;
  TokenDesc bookmark_current_;
  TokenDesc bookmark_next_;
  LiteralBuffer bookmark_current_literal_;
  LiteralBuffer bookmark_current_raw_literal_;
  LiteralBuffer bookmark_next_literal_;
  LiteralBuffer bookmark_next_raw_literal_;

  // Input stream. Must be initialized to an Utf16CharacterStream.
  Utf16CharacterStream* source_;


  // Start position of the octal literal last scanned.
  Location octal_pos_;

  // One Unicode character look-ahead; c0_ < 0 at the end of the input.
  uc32 c0_;

  // Whether there is a line terminator whitespace character after
  // the current token, and  before the next. Does not count newlines
  // inside multiline comments.
  bool has_line_terminator_before_next_;
  // Whether there is a multi-line comment that contains a
  // line-terminator after the current token, and before the next.
  bool has_multiline_comment_before_next_;

  // Whether this scanner encountered an HTML comment.
  bool found_html_comment_;

  bool allow_harmony_exponentiation_operator_;

  MessageTemplate::Template scanner_error_;
  Location scanner_error_location_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_SCANNER_H_
