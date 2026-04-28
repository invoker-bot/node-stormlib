#include <napi.h>
#include <StormLib.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

#if defined(_WIN32) && defined(UNICODE)
using NativePath = std::wstring;
#else
using NativePath = std::string;
#endif

class ArchiveHandle {
 public:
  explicit ArchiveHandle(HANDLE handle) : handle_(handle) {}
  ArchiveHandle(const ArchiveHandle&) = delete;
  ArchiveHandle& operator=(const ArchiveHandle&) = delete;
  ArchiveHandle(ArchiveHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  ArchiveHandle& operator=(ArchiveHandle&& other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr) {
        SFileCloseArchive(handle_);
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }
  ~ArchiveHandle() {
    if (handle_ != nullptr) {
      SFileCloseArchive(handle_);
    }
  }

  HANDLE get() const {
    return handle_;
  }

 private:
  HANDLE handle_;
};

class FileHandle {
 public:
  explicit FileHandle(HANDLE handle) : handle_(handle) {}
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;
  ~FileHandle() {
    if (handle_ != nullptr) {
      SFileCloseFile(handle_);
    }
  }

  HANDLE get() const {
    return handle_;
  }

 private:
  HANDLE handle_;
};

class FindHandle {
 public:
  explicit FindHandle(HANDLE handle) : handle_(handle) {}
  FindHandle(const FindHandle&) = delete;
  FindHandle& operator=(const FindHandle&) = delete;
  ~FindHandle() {
    if (handle_ != nullptr) {
      SFileFindClose(handle_);
    }
  }

 private:
  HANDLE handle_;
};

std::string StormErrorCode(DWORD errorCode) {
  std::ostringstream code;
  code << "STORM_" << errorCode;
  return code.str();
}

std::string StormErrorMessage(const std::string& action, DWORD errorCode) {
  std::ostringstream message;
  message << action << " failed with StormLib error " << errorCode;
  return message.str();
}

void ThrowStormError(Napi::Env env, const std::string& action) {
  DWORD errorCode = SErrGetLastError();
  Napi::Error error = Napi::Error::New(env, StormErrorMessage(action, errorCode));
  error.Value().Set("code", StormErrorCode(errorCode));
  error.Value().Set("stormCode", Napi::Number::New(env, errorCode));
  error.ThrowAsJavaScriptException();
}

void ThrowCodedRangeError(Napi::Env env, const std::string& message) {
  Napi::RangeError error = Napi::RangeError::New(env, message);
  error.Value().Set("code", "ERR_NODE_STORMLIB_LIMIT");
  error.ThrowAsJavaScriptException();
}

bool ReadStringArgument(
    const Napi::CallbackInfo& info,
    size_t index,
    const char* name,
    std::string* value) {
  Napi::Env env = info.Env();
  if (info.Length() <= index || !info[index].IsString()) {
    Napi::TypeError::New(env, std::string(name) + " must be a string")
        .ThrowAsJavaScriptException();
    return false;
  }

  *value = info[index].As<Napi::String>().Utf8Value();
  return true;
}

bool ReadSafeIntegerArgument(
    const Napi::CallbackInfo& info,
    size_t index,
    const char* name,
    uint64_t* value) {
  Napi::Env env = info.Env();
  if (info.Length() <= index || !info[index].IsNumber()) {
    Napi::TypeError::New(env, std::string(name) + " must be a non-negative safe integer")
        .ThrowAsJavaScriptException();
    return false;
  }

  double number = info[index].As<Napi::Number>().DoubleValue();
  constexpr double kMaxSafeInteger = 9007199254740991.0;
  if (!std::isfinite(number) ||
      number < 0 ||
      number > kMaxSafeInteger ||
      std::floor(number) != number) {
    Napi::RangeError::New(env, std::string(name) + " must be a non-negative safe integer")
        .ThrowAsJavaScriptException();
    return false;
  }

  *value = static_cast<uint64_t>(number);
  return true;
}

bool ReadOptionalOptionsArgument(
    const Napi::CallbackInfo& info,
    size_t index,
    const char* name,
    Napi::Object* value) {
  Napi::Env env = info.Env();
  if (info.Length() <= index || info[index].IsUndefined() || info[index].IsNull()) {
    *value = Napi::Object::New(env);
    return true;
  }

  if (!info[index].IsObject() || info[index].IsArray()) {
    Napi::TypeError::New(env, std::string(name) + " must be an object")
        .ThrowAsJavaScriptException();
    return false;
  }

  *value = info[index].As<Napi::Object>();
  return true;
}

bool ReadSafeIntegerOption(
    Napi::Env env,
    const Napi::Object& options,
    const char* name,
    bool* hasValue,
    uint64_t* value) {
  *hasValue = false;
  *value = 0;

  Napi::Value option = options.Get(name);
  if (option.IsUndefined() || option.IsNull()) {
    return true;
  }

  if (!option.IsNumber()) {
    Napi::TypeError::New(env, std::string(name) + " must be a non-negative safe integer")
        .ThrowAsJavaScriptException();
    return false;
  }

  double number = option.As<Napi::Number>().DoubleValue();
  constexpr double kMaxSafeInteger = 9007199254740991.0;
  if (!std::isfinite(number) ||
      number < 0 ||
      number > kMaxSafeInteger ||
      std::floor(number) != number) {
    Napi::RangeError::New(env, std::string(name) + " must be a non-negative safe integer")
        .ThrowAsJavaScriptException();
    return false;
  }

  *hasValue = true;
  *value = static_cast<uint64_t>(number);
  return true;
}

bool ReadBooleanOption(
    Napi::Env env,
    const Napi::Object& options,
    const char* name,
    bool defaultValue,
    bool* value) {
  Napi::Value option = options.Get(name);
  if (option.IsUndefined() || option.IsNull()) {
    *value = defaultValue;
    return true;
  }

  if (!option.IsBoolean()) {
    Napi::TypeError::New(env, std::string(name) + " must be a boolean")
        .ThrowAsJavaScriptException();
    return false;
  }

  *value = option.As<Napi::Boolean>().Value();
  return true;
}

bool ReadDwordOption(
    Napi::Env env,
    const Napi::Object& options,
    const char* name,
    uint64_t defaultValue,
    uint64_t minValue,
    uint64_t maxValue,
    DWORD* value) {
  bool hasValue = false;
  uint64_t parsedValue = 0;
  if (!ReadSafeIntegerOption(env, options, name, &hasValue, &parsedValue)) {
    return false;
  }

  if (!hasValue) {
    parsedValue = defaultValue;
  }

  if (parsedValue < minValue || parsedValue > maxValue) {
    std::ostringstream message;
    message << name << " must be between " << minValue << " and " << maxValue;
    Napi::RangeError::New(env, message.str()).ThrowAsJavaScriptException();
    return false;
  }

  *value = static_cast<DWORD>(parsedValue);
  return true;
}

bool ReadCreateArchiveFlags(
    Napi::Env env,
    const Napi::Object& options,
    DWORD* createFlags) {
  DWORD version = 1;
  if (!ReadDwordOption(env, options, "version", 1, 1, 4, &version)) {
    return false;
  }

  switch (version) {
    case 1:
      *createFlags = MPQ_CREATE_ARCHIVE_V1;
      return true;
    case 2:
      *createFlags = MPQ_CREATE_ARCHIVE_V2;
      return true;
    case 3:
      *createFlags = MPQ_CREATE_ARCHIVE_V3;
      return true;
    case 4:
      *createFlags = MPQ_CREATE_ARCHIVE_V4;
      return true;
    default:
      Napi::RangeError::New(env, "version must be between 1 and 4")
          .ThrowAsJavaScriptException();
      return false;
  }
}

bool ReadCompressionOption(
    Napi::Env env,
    const Napi::Object& options,
    DWORD* fileFlags,
    DWORD* compression) {
  *fileFlags = 0;
  *compression = 0;

  Napi::Value option = options.Get("compression");
  if (option.IsUndefined() || option.IsNull()) {
    *fileFlags = MPQ_FILE_COMPRESS;
    *compression = MPQ_COMPRESSION_ZLIB;
    return true;
  }

  if (option.IsBoolean()) {
    if (option.As<Napi::Boolean>().Value()) {
      *fileFlags = MPQ_FILE_COMPRESS;
      *compression = MPQ_COMPRESSION_ZLIB;
    }
    return true;
  }

  if (option.IsNumber()) {
    bool hasValue = false;
    uint64_t numericCompression = 0;
    if (!ReadSafeIntegerOption(env, options, "compression", &hasValue, &numericCompression)) {
      return false;
    }

    if (numericCompression > std::numeric_limits<DWORD>::max()) {
      Napi::RangeError::New(env, "compression must fit in a 32-bit integer")
          .ThrowAsJavaScriptException();
      return false;
    }

    if (numericCompression != 0) {
      *fileFlags = MPQ_FILE_COMPRESS;
      *compression = static_cast<DWORD>(numericCompression);
    }
    return true;
  }

  if (!option.IsString()) {
    Napi::TypeError::New(env, "compression must be a string, number, or boolean")
        .ThrowAsJavaScriptException();
    return false;
  }

  std::string compressionName = option.As<Napi::String>().Utf8Value();
  if (compressionName == "none") {
    return true;
  }
  if (compressionName == "implode") {
    *fileFlags = MPQ_FILE_IMPLODE;
    return true;
  }

  *fileFlags = MPQ_FILE_COMPRESS;
  if (compressionName == "zlib") {
    *compression = MPQ_COMPRESSION_ZLIB;
  } else if (compressionName == "pkware") {
    *compression = MPQ_COMPRESSION_PKWARE;
  } else if (compressionName == "bzip2") {
    *compression = MPQ_COMPRESSION_BZIP2;
  } else if (compressionName == "sparse") {
    *compression = MPQ_COMPRESSION_SPARSE;
  } else if (compressionName == "lzma") {
    *compression = MPQ_COMPRESSION_LZMA;
  } else {
    Napi::RangeError::New(env, "compression must be one of none, zlib, pkware, bzip2, sparse, lzma, or implode")
        .ThrowAsJavaScriptException();
    return false;
  }

  return true;
}

bool ReadListFilesArguments(
    const Napi::CallbackInfo& info,
    std::string* archivePath,
    std::string* mask,
    Napi::Object* options) {
  Napi::Env env = info.Env();
  if (!ReadStringArgument(info, 0, "archivePath", archivePath)) {
    return false;
  }

  *mask = "*";
  size_t optionsIndex = 2;
  if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
    if (info[1].IsString()) {
      *mask = info[1].As<Napi::String>().Utf8Value();
    } else if (info[1].IsObject() && !info[1].IsArray()) {
      optionsIndex = 1;
    } else {
      Napi::TypeError::New(env, "mask must be a string")
          .ThrowAsJavaScriptException();
      return false;
    }
  }

  return ReadOptionalOptionsArgument(info, optionsIndex, "options", options);
}

bool Utf8ToNativePath(Napi::Env env, const std::string& path, NativePath* value) {
#if defined(_WIN32) && defined(UNICODE)
  if (path.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    Napi::RangeError::New(env, "path is too long").ThrowAsJavaScriptException();
    return false;
  }

  if (path.empty()) {
    value->clear();
    return true;
  }

  int wideLength = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      path.data(),
      static_cast<int>(path.size()),
      nullptr,
      0);
  if (wideLength == 0) {
    Napi::TypeError::New(env, "path must be valid UTF-8")
        .ThrowAsJavaScriptException();
    return false;
  }

  value->resize(static_cast<size_t>(wideLength));
  int converted = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      path.data(),
      static_cast<int>(path.size()),
      &(*value)[0],
      wideLength);
  if (converted == 0) {
    Napi::TypeError::New(env, "path must be valid UTF-8")
        .ThrowAsJavaScriptException();
    return false;
  }

  return true;
#else
  *value = path;
  return true;
#endif
}

ArchiveHandle OpenArchiveOrThrow(Napi::Env env, const std::string& archivePath) {
  NativePath nativeArchivePath;
  if (!Utf8ToNativePath(env, archivePath, &nativeArchivePath)) {
    return ArchiveHandle(nullptr);
  }

  HANDLE archive = nullptr;
  if (!SFileOpenArchive(nativeArchivePath.c_str(), 0, 0, &archive)) {
    ThrowStormError(env, "SFileOpenArchive");
    return ArchiveHandle(nullptr);
  }

  return ArchiveHandle(archive);
}

bool GetArchiveDword(HANDLE archive, SFileInfoClass infoClass, DWORD* value) {
  return SFileGetFileInfo(archive, infoClass, value, sizeof(*value), nullptr);
}

bool GetFileDword(HANDLE file, SFileInfoClass infoClass, DWORD* value) {
  return SFileGetFileInfo(file, infoClass, value, sizeof(*value), nullptr);
}

bool GetArchiveUInt64(HANDLE archive, SFileInfoClass infoClass, ULONGLONG* value) {
  return SFileGetFileInfo(archive, infoClass, value, sizeof(*value), nullptr);
}

Napi::Object FindDataToObject(Napi::Env env, const SFILE_FIND_DATA& data) {
  Napi::Object entry = Napi::Object::New(env);
  entry.Set("name", data.cFileName);
  entry.Set("plainName", data.szPlainName == nullptr ? "" : data.szPlainName);
  entry.Set("size", Napi::Number::New(env, data.dwFileSize));
  entry.Set("compressedSize", Napi::Number::New(env, data.dwCompSize));
  entry.Set("flags", Napi::Number::New(env, data.dwFileFlags));
  entry.Set("locale", Napi::Number::New(env, data.lcLocale));
  entry.Set("hashIndex", Napi::Number::New(env, data.dwHashIndex));
  entry.Set("blockIndex", Napi::Number::New(env, data.dwBlockIndex));
  return entry;
}

}  // namespace

class NodeStormAddon : public Napi::Addon<NodeStormAddon> {
 public:
  NodeStormAddon(Napi::Env env, Napi::Object exports) {
    DefineAddon(exports, {
      InstanceMethod("getArchiveInfo", &NodeStormAddon::GetArchiveInfo),
      InstanceMethod("listFiles", &NodeStormAddon::ListFiles),
      InstanceMethod("hasFile", &NodeStormAddon::HasFile),
      InstanceMethod("getFileInfo", &NodeStormAddon::GetFileInfo),
      InstanceMethod("readFile", &NodeStormAddon::ReadFile),
      InstanceMethod("readFileChunk", &NodeStormAddon::ReadFileChunk),
      InstanceMethod("extractFile", &NodeStormAddon::ExtractFile),
      InstanceMethod("createArchive", &NodeStormAddon::CreateArchive),
      InstanceMethod("addFile", &NodeStormAddon::AddFile),
      InstanceMethod("writeFile", &NodeStormAddon::WriteFile),
      InstanceMethod("compactArchive", &NodeStormAddon::CompactArchive),
    });
  }

 private:
  Napi::Value GetArchiveInfo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    DWORD fileCount = 0;
    DWORD maxFileCount = 0;
    DWORD sectorSize = 0;
    ULONGLONG archiveSize = 0;

    Napi::Object result = Napi::Object::New(env);
    result.Set("path", archivePath);
    if (GetArchiveDword(archive.get(), SFileMpqNumberOfFiles, &fileCount)) {
      result.Set("fileCount", Napi::Number::New(env, fileCount));
    }
    if (GetArchiveDword(archive.get(), SFileMpqMaxFileCount, &maxFileCount)) {
      result.Set("maxFileCount", Napi::Number::New(env, maxFileCount));
    }
    if (GetArchiveDword(archive.get(), SFileMpqSectorSize, &sectorSize)) {
      result.Set("sectorSize", Napi::Number::New(env, sectorSize));
    }
    if (GetArchiveUInt64(archive.get(), SFileMpqArchiveSize64, &archiveSize)) {
      result.Set("archiveSize", Napi::Number::New(env, static_cast<double>(archiveSize)));
    }

    return result;
  }

  Napi::Value ListFiles(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string mask;
    Napi::Object options;
    if (!ReadListFilesArguments(info, &archivePath, &mask, &options)) {
      return env.Null();
    }

    bool hasMaxEntries = false;
    uint64_t maxEntries = 0;
    if (!ReadSafeIntegerOption(env, options, "maxEntries", &hasMaxEntries, &maxEntries)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    if (hasMaxEntries && maxEntries == 0) {
      return Napi::Array::New(env);
    }

    SFILE_FIND_DATA data = {};
    HANDLE search = SFileFindFirstFile(archive.get(), mask.c_str(), &data, nullptr);
    if (search == nullptr) {
      DWORD errorCode = SErrGetLastError();
      if (errorCode == ERROR_NO_MORE_FILES || errorCode == ERROR_FILE_NOT_FOUND) {
        return Napi::Array::New(env);
      }

      ThrowStormError(env, "SFileFindFirstFile");
      return env.Null();
    }

    FindHandle searchHandle(search);
    Napi::Array files = Napi::Array::New(env);
    uint32_t index = 0;
    bool exhausted = false;
    do {
      files.Set(index++, FindDataToObject(env, data));
      if (hasMaxEntries && index >= maxEntries) {
        return files;
      }
      if (index == std::numeric_limits<uint32_t>::max()) {
        ThrowCodedRangeError(env, "too many archive entries to return");
        return env.Null();
      }
    } while (SFileFindNextFile(search, &data));
    exhausted = true;

    DWORD errorCode = SErrGetLastError();
    if (exhausted && errorCode != ERROR_NO_MORE_FILES) {
      ThrowStormError(env, "SFileFindNextFile");
      return env.Null();
    }

    return files;
  }

  Napi::Value HasFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string fileName;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "fileName", &fileName)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    HANDLE file = nullptr;
    if (!SFileOpenFileEx(archive.get(), fileName.c_str(), SFILE_OPEN_FROM_MPQ, &file)) {
      DWORD errorCode = SErrGetLastError();
      if (errorCode == ERROR_FILE_NOT_FOUND) {
        return Napi::Boolean::New(env, false);
      }

      ThrowStormError(env, "SFileOpenFileEx");
      return env.Null();
    }

    FileHandle fileHandle(file);
    return Napi::Boolean::New(env, true);
  }

  Napi::Value GetFileInfo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string fileName;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "fileName", &fileName)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    HANDLE file = nullptr;
    if (!SFileOpenFileEx(archive.get(), fileName.c_str(), SFILE_OPEN_FROM_MPQ, &file)) {
      ThrowStormError(env, "SFileOpenFileEx");
      return env.Null();
    }
    FileHandle fileHandle(file);

    DWORD size = 0;
    if (!GetFileDword(fileHandle.get(), SFileInfoFileSize, &size)) {
      ThrowStormError(env, "SFileGetFileInfo");
      return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("name", fileName);
    result.Set("size", Napi::Number::New(env, size));

    DWORD value = 0;
    if (GetFileDword(fileHandle.get(), SFileInfoCompressedSize, &value)) {
      result.Set("compressedSize", Napi::Number::New(env, value));
    }
    if (GetFileDword(fileHandle.get(), SFileInfoFlags, &value)) {
      result.Set("flags", Napi::Number::New(env, value));
    }
    if (GetFileDword(fileHandle.get(), SFileInfoLocale, &value)) {
      result.Set("locale", Napi::Number::New(env, value));
    }
    if (GetFileDword(fileHandle.get(), SFileInfoHashIndex, &value)) {
      result.Set("hashIndex", Napi::Number::New(env, value));
    }
    if (GetFileDword(fileHandle.get(), SFileInfoFileIndex, &value)) {
      result.Set("blockIndex", Napi::Number::New(env, value));
    }
    if (GetFileDword(fileHandle.get(), SFileInfoCRC32, &value)) {
      result.Set("crc32", Napi::Number::New(env, value));
    }

    return result;
  }

  Napi::Value ReadFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string fileName;
    Napi::Object options;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "fileName", &fileName) ||
        !ReadOptionalOptionsArgument(info, 2, "options", &options)) {
      return env.Null();
    }

    bool hasMaxBytes = false;
    uint64_t maxBytes = 0;
    if (!ReadSafeIntegerOption(env, options, "maxBytes", &hasMaxBytes, &maxBytes)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    HANDLE file = nullptr;
    if (!SFileOpenFileEx(archive.get(), fileName.c_str(), SFILE_OPEN_FROM_MPQ, &file)) {
      ThrowStormError(env, "SFileOpenFileEx");
      return env.Null();
    }
    FileHandle fileHandle(file);

    DWORD highSize = 0;
    DWORD lowSize = SFileGetFileSize(fileHandle.get(), &highSize);
    if (lowSize == SFILE_INVALID_SIZE && SErrGetLastError() != ERROR_SUCCESS) {
      ThrowStormError(env, "SFileGetFileSize");
      return env.Null();
    }

    uint64_t fileSize = (static_cast<uint64_t>(highSize) << 32) | lowSize;
    if (hasMaxBytes && fileSize > maxBytes) {
      ThrowCodedRangeError(env, "file exceeds maxBytes limit");
      return env.Null();
    }

    if (highSize != 0 || lowSize > static_cast<DWORD>(std::numeric_limits<int32_t>::max())) {
      ThrowCodedRangeError(env, "file is too large to read into a Node.js Buffer");
      return env.Null();
    }

    std::vector<char> buffer(lowSize);
    DWORD bytesRead = 0;
    if (lowSize > 0 &&
        !SFileReadFile(fileHandle.get(), buffer.data(), lowSize, &bytesRead, nullptr)) {
      ThrowStormError(env, "SFileReadFile");
      return env.Null();
    }

    if (bytesRead != lowSize) {
      Napi::Error error = Napi::Error::New(env, "SFileReadFile returned fewer bytes than expected");
      error.Value().Set("code", "ERR_NODE_STORMLIB_SHORT_READ");
      error.ThrowAsJavaScriptException();
      return env.Null();
    }

    return Napi::Buffer<char>::Copy(env, buffer.data(), buffer.size());
  }

  Napi::Value ReadFileChunk(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string fileName;
    uint64_t offset = 0;
    uint64_t length = 0;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "fileName", &fileName) ||
        !ReadSafeIntegerArgument(info, 2, "offset", &offset) ||
        !ReadSafeIntegerArgument(info, 3, "length", &length)) {
      return env.Null();
    }

    if (length > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      ThrowCodedRangeError(env, "chunk length is too large to read into a Node.js Buffer");
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    HANDLE file = nullptr;
    if (!SFileOpenFileEx(archive.get(), fileName.c_str(), SFILE_OPEN_FROM_MPQ, &file)) {
      ThrowStormError(env, "SFileOpenFileEx");
      return env.Null();
    }
    FileHandle fileHandle(file);

    DWORD highSize = 0;
    DWORD lowSize = SFileGetFileSize(fileHandle.get(), &highSize);
    if (lowSize == SFILE_INVALID_SIZE && SErrGetLastError() != ERROR_SUCCESS) {
      ThrowStormError(env, "SFileGetFileSize");
      return env.Null();
    }

    uint64_t fileSize = (static_cast<uint64_t>(highSize) << 32) | lowSize;
    if (offset >= fileSize || length == 0) {
      return Napi::Buffer<char>::New(env, 0);
    }

    uint64_t bytesToRead64 = std::min(length, fileSize - offset);
    if (bytesToRead64 > static_cast<uint64_t>(std::numeric_limits<DWORD>::max())) {
      ThrowCodedRangeError(env, "chunk length is too large to read from StormLib");
      return env.Null();
    }

    LONG highOffset = static_cast<LONG>(offset >> 32);
    DWORD lowOffset = SFileSetFilePointer(
        fileHandle.get(),
        static_cast<LONG>(offset & 0xFFFFFFFF),
        &highOffset,
        FILE_BEGIN);
    if (lowOffset == SFILE_INVALID_POS && SErrGetLastError() != ERROR_SUCCESS) {
      ThrowStormError(env, "SFileSetFilePointer");
      return env.Null();
    }

    DWORD bytesToRead = static_cast<DWORD>(bytesToRead64);
    std::vector<char> buffer(bytesToRead);
    DWORD bytesRead = 0;
    if (bytesToRead > 0 &&
        !SFileReadFile(fileHandle.get(), buffer.data(), bytesToRead, &bytesRead, nullptr)) {
      ThrowStormError(env, "SFileReadFile");
      return env.Null();
    }

    return Napi::Buffer<char>::Copy(env, buffer.data(), bytesRead);
  }

  Napi::Value ExtractFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string fileName;
    std::string outputPath;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "fileName", &fileName) ||
        !ReadStringArgument(info, 2, "outputPath", &outputPath)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    NativePath nativeOutputPath;
    if (!Utf8ToNativePath(env, outputPath, &nativeOutputPath)) {
      return env.Null();
    }

    if (!SFileExtractFile(
            archive.get(),
            fileName.c_str(),
            nativeOutputPath.c_str(),
            SFILE_OPEN_FROM_MPQ)) {
      ThrowStormError(env, "SFileExtractFile");
      return env.Null();
    }

    return Napi::Boolean::New(env, true);
  }

  Napi::Value CreateArchive(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    Napi::Object options;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadOptionalOptionsArgument(info, 1, "options", &options)) {
      return env.Null();
    }

    DWORD maxFileCount = 0;
    DWORD createFlags = 0;
    if (!ReadDwordOption(
            env,
            options,
            "maxFileCount",
            HASH_TABLE_SIZE_DEFAULT,
            HASH_TABLE_SIZE_MIN,
            HASH_TABLE_SIZE_MAX,
            &maxFileCount) ||
        !ReadCreateArchiveFlags(env, options, &createFlags)) {
      return env.Null();
    }

    NativePath nativeArchivePath;
    if (!Utf8ToNativePath(env, archivePath, &nativeArchivePath)) {
      return env.Null();
    }

    HANDLE archive = nullptr;
    if (!SFileCreateArchive(nativeArchivePath.c_str(), createFlags, maxFileCount, &archive)) {
      ThrowStormError(env, "SFileCreateArchive");
      return env.Null();
    }

    ArchiveHandle archiveHandle(archive);
    return Napi::Boolean::New(env, true);
  }

  Napi::Value AddFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string sourcePath;
    std::string archivedName;
    Napi::Object options;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "sourcePath", &sourcePath) ||
        !ReadStringArgument(info, 2, "archivedName", &archivedName) ||
        !ReadOptionalOptionsArgument(info, 3, "options", &options)) {
      return env.Null();
    }

    DWORD fileFlags = 0;
    DWORD compression = 0;
    bool replaceExisting = true;
    if (!ReadCompressionOption(env, options, &fileFlags, &compression) ||
        !ReadBooleanOption(env, options, "replaceExisting", true, &replaceExisting)) {
      return env.Null();
    }

    if (replaceExisting) {
      fileFlags |= MPQ_FILE_REPLACEEXISTING;
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    NativePath nativeSourcePath;
    if (!Utf8ToNativePath(env, sourcePath, &nativeSourcePath)) {
      return env.Null();
    }

    if (!SFileAddFileEx(
            archive.get(),
            nativeSourcePath.c_str(),
            archivedName.c_str(),
            fileFlags,
            compression,
            MPQ_COMPRESSION_NEXT_SAME)) {
      ThrowStormError(env, "SFileAddFileEx");
      return env.Null();
    }

    return Napi::Boolean::New(env, true);
  }

  Napi::Value WriteFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    std::string archivedName;
    Napi::Object options;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadStringArgument(info, 1, "archivedName", &archivedName)) {
      return env.Null();
    }

    if (info.Length() <= 2 || !info[2].IsBuffer()) {
      Napi::TypeError::New(env, "data must be a Buffer")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (!ReadOptionalOptionsArgument(info, 3, "options", &options)) {
      return env.Null();
    }

    Napi::Buffer<uint8_t> data = info[2].As<Napi::Buffer<uint8_t>>();
    if (data.Length() > std::numeric_limits<DWORD>::max()) {
      ThrowCodedRangeError(env, "data is too large to write into an MPQ file");
      return env.Null();
    }

    DWORD fileFlags = 0;
    DWORD compression = 0;
    bool replaceExisting = true;
    if (!ReadCompressionOption(env, options, &fileFlags, &compression) ||
        !ReadBooleanOption(env, options, "replaceExisting", true, &replaceExisting)) {
      return env.Null();
    }

    if (replaceExisting) {
      fileFlags |= MPQ_FILE_REPLACEEXISTING;
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    HANDLE file = nullptr;
    if (!SFileCreateFile(
            archive.get(),
            archivedName.c_str(),
            0,
            static_cast<DWORD>(data.Length()),
            SFileGetLocale(),
            fileFlags,
            &file)) {
      ThrowStormError(env, "SFileCreateFile");
      return env.Null();
    }

    if (!SFileWriteFile(file, data.Data(), static_cast<DWORD>(data.Length()), compression)) {
      ThrowStormError(env, "SFileWriteFile");
      SFileCloseFile(file);
      return env.Null();
    }

    if (!SFileFinishFile(file)) {
      ThrowStormError(env, "SFileFinishFile");
      return env.Null();
    }

    return Napi::Boolean::New(env, true);
  }

  Napi::Value CompactArchive(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string archivePath;
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    if (!SFileCompactArchive(archive.get(), nullptr, false)) {
      ThrowStormError(env, "SFileCompactArchive");
      return env.Null();
    }

    return Napi::Boolean::New(env, true);
  }

};

NODE_API_ADDON(NodeStormAddon)
