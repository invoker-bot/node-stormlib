#include <napi.h>
#include <StormLib.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

std::string StormErrorMessage(const std::string& action) {
  std::ostringstream message;
  message << action << " failed with StormLib error " << GetLastError();
  return message.str();
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

bool ReadOptionalStringArgument(
    const Napi::CallbackInfo& info,
    size_t index,
    const char* name,
    const char* defaultValue,
    std::string* value) {
  Napi::Env env = info.Env();
  if (info.Length() <= index || info[index].IsUndefined() || info[index].IsNull()) {
    *value = defaultValue;
    return true;
  }

  if (!info[index].IsString()) {
    Napi::TypeError::New(env, std::string(name) + " must be a string")
        .ThrowAsJavaScriptException();
    return false;
  }

  *value = info[index].As<Napi::String>().Utf8Value();
  return true;
}

ArchiveHandle OpenArchiveOrThrow(Napi::Env env, const std::string& archivePath) {
  HANDLE archive = nullptr;
  if (!SFileOpenArchive(archivePath.c_str(), 0, 0, &archive)) {
    Napi::Error::New(env, StormErrorMessage("SFileOpenArchive"))
        .ThrowAsJavaScriptException();
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
    if (!ReadStringArgument(info, 0, "archivePath", &archivePath) ||
        !ReadOptionalStringArgument(info, 1, "mask", "*", &mask)) {
      return env.Null();
    }

    ArchiveHandle archive = OpenArchiveOrThrow(env, archivePath);
    if (env.IsExceptionPending()) {
      return env.Null();
    }

    SFILE_FIND_DATA data = {};
    HANDLE search = SFileFindFirstFile(archive.get(), mask.c_str(), &data, nullptr);
    if (search == nullptr) {
      DWORD errorCode = GetLastError();
      if (errorCode == ERROR_NO_MORE_FILES || errorCode == ERROR_FILE_NOT_FOUND) {
        return Napi::Array::New(env);
      }

      Napi::Error::New(env, StormErrorMessage("SFileFindFirstFile"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    FindHandle searchHandle(search);
    Napi::Array files = Napi::Array::New(env);
    uint32_t index = 0;
    do {
      files.Set(index++, FindDataToObject(env, data));
    } while (SFileFindNextFile(search, &data));

    DWORD errorCode = GetLastError();
    if (errorCode != ERROR_NO_MORE_FILES) {
      Napi::Error::New(env, StormErrorMessage("SFileFindNextFile"))
          .ThrowAsJavaScriptException();
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

      Napi::Error::New(env, StormErrorMessage("SFileOpenFileEx"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    FileHandle fileHandle(file);
    return Napi::Boolean::New(env, true);
  }

  Napi::Value ReadFile(const Napi::CallbackInfo& info) {
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
      Napi::Error::New(env, StormErrorMessage("SFileOpenFileEx"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }
    FileHandle fileHandle(file);

    DWORD highSize = 0;
    DWORD lowSize = SFileGetFileSize(fileHandle.get(), &highSize);
    if (lowSize == SFILE_INVALID_SIZE && GetLastError() != ERROR_SUCCESS) {
      Napi::Error::New(env, StormErrorMessage("SFileGetFileSize"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (highSize != 0 || lowSize > static_cast<DWORD>(std::numeric_limits<int32_t>::max())) {
      Napi::RangeError::New(env, "file is too large to read into a Node.js Buffer")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::vector<char> buffer(lowSize);
    DWORD bytesRead = 0;
    if (lowSize > 0 &&
        !SFileReadFile(fileHandle.get(), buffer.data(), lowSize, &bytesRead, nullptr)) {
      Napi::Error::New(env, StormErrorMessage("SFileReadFile"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    if (bytesRead != lowSize) {
      Napi::Error::New(env, "SFileReadFile returned fewer bytes than expected")
          .ThrowAsJavaScriptException();
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

    if (!SFileExtractFile(
            archive.get(),
            fileName.c_str(),
            outputPath.c_str(),
            SFILE_OPEN_FROM_MPQ)) {
      Napi::Error::New(env, StormErrorMessage("SFileExtractFile"))
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    return Napi::Boolean::New(env, true);
  }

};

NODE_API_ADDON(NodeStormAddon)
