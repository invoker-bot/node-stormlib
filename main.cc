#include <napi.h>
#include <StormLib.h>

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
  DWORD errorCode = GetLastError();
  Napi::Error error = Napi::Error::New(env, StormErrorMessage(action, errorCode));
  error.Value().Set("code", StormErrorCode(errorCode));
  error.Value().Set("stormCode", Napi::Number::New(env, errorCode));
  error.ThrowAsJavaScriptException();
}

void ThrowCodedRangeError(Napi::Env env, const std::string& message) {
  Napi::RangeError error = Napi::RangeError::New(env, message);
  error.Value().Set("code", "ERR_NODE_STORM_LIMIT");
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
      InstanceMethod("readFile", &NodeStormAddon::ReadFile),
      InstanceMethod("extractFile", &NodeStormAddon::ExtractFile),
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
      DWORD errorCode = GetLastError();
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

    DWORD errorCode = GetLastError();
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
      DWORD errorCode = GetLastError();
      if (errorCode == ERROR_FILE_NOT_FOUND) {
        return Napi::Boolean::New(env, false);
      }

      ThrowStormError(env, "SFileOpenFileEx");
      return env.Null();
    }

    FileHandle fileHandle(file);
    return Napi::Boolean::New(env, true);
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
    if (lowSize == SFILE_INVALID_SIZE && GetLastError() != ERROR_SUCCESS) {
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
      error.Value().Set("code", "ERR_NODE_STORM_SHORT_READ");
      error.ThrowAsJavaScriptException();
      return env.Null();
    }

    return Napi::Buffer<char>::Copy(env, buffer.data(), buffer.size());
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

};

NODE_API_ADDON(NodeStormAddon)
