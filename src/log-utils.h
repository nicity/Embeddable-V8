// Copyright 2006-2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_LOG_UTILS_H_
#define V8_LOG_UTILS_H_

namespace v8 {
namespace internal {

#ifdef ENABLE_LOGGING_AND_PROFILING

// A memory buffer that increments its size as you write in it.  Size
// is incremented with 'block_size' steps, never exceeding 'max_size'.
// During growth, memory contents are never copied.  At the end of the
// buffer an amount of memory specified in 'seal_size' is reserved.
// When writing position reaches max_size - seal_size, buffer auto-seals
// itself with 'seal' and allows no further writes. Data pointed by
// 'seal' must be available during entire LogDynamicBuffer lifetime.
//
// An instance of this class is created dynamically by Log.
class LogDynamicBuffer {
 public:
  LogDynamicBuffer(
      int block_size, int max_size, const char* seal, int seal_size);

  ~LogDynamicBuffer();

  // Reads contents of the buffer starting from 'from_pos'.  Upon
  // return, 'dest_buf' is filled with the data. Actual amount of data
  // filled is returned, it is <= 'buf_size'.
  int Read(int from_pos, char* dest_buf, int buf_size);

  // Writes 'data' to the buffer, making it larger if necessary.  If
  // data is too big to fit in the buffer, it doesn't get written at
  // all. In that case, buffer auto-seals itself and stops to accept
  // any incoming writes. Returns amount of data written (it is either
  // 'data_size', or 0, if 'data' is too big).
  int Write(const char* data, int data_size);

 private:
  void AllocateBlock(int index) {
    blocks_[index] = NewArray<char>(block_size_);
  }

  int BlockIndex(int pos) const { return pos / block_size_; }

  int BlocksCount() const { return BlockIndex(max_size_) + 1; }

  int PosInBlock(int pos) const { return pos % block_size_; }

  int Seal();

  int WriteInternal(const char* data, int data_size);

  const int block_size_;
  const int max_size_;
  const char* seal_;
  const int seal_size_;
  ScopedVector<char*> blocks_;
  int write_pos_;
  int block_index_;
  int block_write_pos_;
  bool is_sealed_;
};

class LogData {
  // Whether logging is stopped (e.g. due to insufficient resources).
  bool is_stopped_;

  // When logging is active, either output_handle_ or output_buffer_ is used
  // to store a pointer to log destination. If logging was opened via OpenStdout
  // or OpenFile, then output_handle_ is used. If logging was opened
  // via OpenMemoryBuffer, then output_buffer_ is used.
  // mutex_ should be acquired before using output_handle_ or output_buffer_.
  FILE* output_handle_;

  LogDynamicBuffer* output_buffer_;

  // mutex_ is a Mutex used for enforcing exclusive
  // access to the formatting buffer and the log file or log memory buffer.
  Mutex* mutex_;

  // Buffer used for formatting log messages. This is a singleton buffer and
  // mutex_ should be acquired before using it.
  char* message_buffer_;

  typedef int (*WritePtr)(const char* msg, int length);
  // Write functions assume that mutex_ is acquired by the caller.
  WritePtr Write;

  // A handler that is called when Log::Write fails.
  typedef void (*WriteFailureHandler)();

  WriteFailureHandler write_failure_handler;

  void set_write_failure_handler(WriteFailureHandler handler) {
    write_failure_handler = handler;
  }

  friend class V8Context;
  friend class Log;
  friend class Logger;
  friend class LogMessageBuilder;

  LogData();
  DISALLOW_COPY_AND_ASSIGN(LogData);
};

// Functions and data for performing output of log messages.
class Log : public AllStatic {
 public:
  // Opens stdout for logging.
  static void OpenStdout();

  // Opens file for logging.
  static void OpenFile(const char* name);

  // Opens memory buffer for logging.
  static void OpenMemoryBuffer();

  // Disables logging, but preserves acquired resources.
  static void stop() {
    v8_context()->log_data_.is_stopped_ = true;
  }

  // Frees all resources acquired in Open... functions.
  static void Close();

  // See description in include/v8.h.
  static int GetLogLines(int from_pos, char* dest_buf, int max_size);

  // Returns whether logging is enabled.
  static bool IsEnabled() {
    LogData& log_data = v8_context()->log_data_;
    return !log_data.is_stopped_ &&
      (log_data.output_handle_ != NULL || log_data.output_buffer_ != NULL);
  }

  // Size of buffer used for formatting log messages.
  static const int kMessageBufferSize = 2048;

 private:
  // Initialization function called from Open... functions.
  static void Init();

  // Implementation of writing to a log file.
  static int WriteToFile(const char* msg, int length) {
    LogData& log_data = v8_context()->log_data_;
    ASSERT(log_data.output_handle_ != NULL);
    size_t rv = fwrite(msg, 1, length, log_data.output_handle_);
    ASSERT(static_cast<size_t>(length) == rv);
    USE(rv);
    return length;
  }

  // Implementation of writing to a memory buffer.
  static int WriteToMemory(const char* msg, int length) {
    LogData& log_data = v8_context()->log_data_;
    ASSERT(log_data.output_buffer_ != NULL);
    return log_data.output_buffer_->Write(msg, length);
  }

// Size of dynamic buffer block (and dynamic buffer initial size).
  static const int kDynamicBufferBlockSize = 65536;

  // Maximum size of dynamic buffer.
  static const int kMaxDynamicBufferSize = 50 * 1024 * 1024;

  // Message to "seal" dynamic buffer with.
  static const char kDynamicBufferSeal[];

  friend class LogMessageBuilder;
  friend class LogRecordCompressor;
};


// An utility class for performing backward reference compression
// of string ends. It operates using a window of previous strings.
class LogRecordCompressor {
 public:
  // 'window_size' is the size of backward lookup window.
  explicit LogRecordCompressor(int window_size)
      : buffer_(window_size + kNoCompressionWindowSize),
        kMaxBackwardReferenceSize(
            GetBackwardReferenceSize(window_size, Log::kMessageBufferSize)),
        curr_(-1), prev_(-1) {
  }

  ~LogRecordCompressor();

  // Fills vector with a compressed version of the previous record.
  // Returns false if there is no previous record.
  bool RetrievePreviousCompressed(Vector<char>* prev_record);

  // Stores a record if it differs from a previous one (or there's no previous).
  // Returns true, if the record has been stored.
  bool Store(const Vector<const char>& record);

 private:
  // The minimum size of a buffer: a place needed for the current and
  // the previous record. Since there is no place for precedessors of a previous
  // record, it can't be compressed at all.
  static const int kNoCompressionWindowSize = 2;

  // Formatting strings for back references.
  static const char* kLineBackwardReferenceFormat;
  static const char* kBackwardReferenceFormat;

  static int GetBackwardReferenceSize(int distance, int pos);

  static void PrintBackwardReference(Vector<char> dest, int distance, int pos);

  ScopedVector< Vector<const char> > buffer_;
  const int kMaxBackwardReferenceSize;
  int curr_;
  int prev_;
};


// Utility class for formatting log messages. It fills the message into the
// static buffer in Log.
class LogMessageBuilder BASE_EMBEDDED {
 public:
  // Create a message builder starting from position 0. This acquires the mutex
  // in the log as well.
  explicit LogMessageBuilder();
  ~LogMessageBuilder() { }

  // Append string data to the log message.
  void Append(const char* format, ...);

  // Append string data to the log message.
  void AppendVA(const char* format, va_list args);

  // Append a character to the log message.
  void Append(const char c);

  // Append a heap string.
  void Append(String* str);

  // Appends an address, compressing it if needed by offsetting
  // from Logger::last_address_.
  void AppendAddress(Address addr);

  // Appends an address, compressing it if needed.
  void AppendAddress(Address addr, Address bias);

  void AppendDetailed(String* str, bool show_impl_info);

  // Append a portion of a string.
  void AppendStringPart(const char* str, int len);

  // Stores log message into compressor, returns true if the message
  // was stored (i.e. doesn't repeat the previous one).
  bool StoreInCompressor(LogRecordCompressor* compressor);

  // Sets log message to a previous version of compressed message.
  // Returns false, if there is no previous message.
  bool RetrieveCompressedPrevious(LogRecordCompressor* compressor) {
    return RetrieveCompressedPrevious(compressor, "");
  }

  // Does the same at the version without arguments, and sets a prefix.
  bool RetrieveCompressedPrevious(LogRecordCompressor* compressor,
                                  const char* prefix);

  // Write the log message to the log file currently opened.
  void WriteToLogFile();

  // Write a null-terminated string to to the log file currently opened.
  void WriteCStringToLogFile(const char* str);

 private:
  LogData& log_data_;
  ScopedLock sl;
  int pos_;
};

#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal

#endif  // V8_LOG_UTILS_H_
