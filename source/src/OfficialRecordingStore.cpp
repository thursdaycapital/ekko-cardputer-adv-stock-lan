#include "OfficialRecordingStore.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <limits>
#include <new>
#ifdef ARDUINO
#include "ExclusiveRecordingFile.h"
#include <driver/spi_master.h>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <diskio_sdmmc.h>
#include <ff.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace official_recording {

namespace {
bool noncePath(const char* path, const char* prefix, const char* extension) {
  const size_t prefixBytes = std::strlen(prefix);
  if (!path || std::strlen(path) != prefixBytes + 32 + std::strlen(extension) ||
      std::strncmp(path, prefix, prefixBytes)) return false;
  const char* suffix = path + prefixBytes;
  for (size_t n = 0; n < 32; ++n) {
    const char c = suffix[n];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return !std::strcmp(suffix + 32, extension);
}
bool randomPath(Backend& backend, const char* role, const char* extension, char (&path)[kPathBytes]) {
  uint8_t nonce[16];
  if (!backend.randomBytes(nonce, sizeof(nonce))) return false;
  constexpr char hex[] = "0123456789abcdef";
  char encoded[33]{};
  for (size_t n = 0; n < sizeof(nonce); ++n) {
    encoded[n * 2] = hex[nonce[n] >> 4]; encoded[n * 2 + 1] = hex[nonce[n] & 15];
  }
  const int bytes = std::snprintf(path, sizeof(path), "%s/%s%s%s", kDirectoryPath, role, encoded, extension);
  return bytes > 0 && size_t(bytes) < sizeof(path);
}
}  // namespace
bool isScratchPath(const char* path) { return noncePath(path, "/EKKO/TMP/REC-", ".PCM"); }
bool isMarkerNewPath(const char* path) { return noncePath(path, "/EKKO/TMP/OWNER-", ".NEW"); }
bool isMarkerBackupPath(const char* path) { return noncePath(path, "/EKKO/TMP/OWNER-", ".BAK"); }
bool isMarkerRenamePair(const char* from, const char* to) {
  return (from && !std::strcmp(from, kMarkerPath) && isMarkerBackupPath(to)) ||
         (isMarkerNewPath(from) && to && !std::strcmp(to, kMarkerPath));
}

struct Store::Lease {
  std::atomic<unsigned> references{1}, readers{0};
  void release() { if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }
};

class Store::OwnedReader final : public official_voice::Reader {
 public:
  OwnedReader(std::unique_ptr<File> file, size_t bytes, Lease* lease)
      : file_(std::move(file)), remaining_(bytes), lease_(lease) {
    lease_->references.fetch_add(1, std::memory_order_relaxed);
    lease_->readers.fetch_add(1, std::memory_order_release);
  }
  ~OwnedReader() override {
    file_->close(); file_.reset();  // Close BEFORE allowing main to remove/reuse.
    lease_->readers.fetch_sub(1, std::memory_order_release);
    lease_->release();
  }
  size_t read(uint8_t* out, size_t capacity) override {
    if (!out || !capacity || !remaining_ || failed_) return 0;
    const size_t wanted = std::min(capacity, remaining_);
    const size_t count = file_->read(out, wanted);
    if (!count || count > wanted || file_->failed()) { failed_ = true; return 0; }
    remaining_ -= count;
    return count;
  }
 private:
  std::unique_ptr<File> file_;
  size_t remaining_;
  Lease* lease_;
  bool failed_ = false;
};

Store::~Store() {
  if (writer_) writer_->close();
  if (lease_) lease_->release();
}

bool Store::readerActive() const {
  return lease_ && lease_->readers.load(std::memory_order_acquire) != 0;
}

Result Store::fail(Result result) {
  if (writer_) { writer_->close(); writer_.reset(); }
  state_ = State::Failed;
  return set(result);
}

bool Store::available(size_t& free) {
  size_t total = 0, used = 0;
  if (!backend_.space(total, used) || !total || used > total) return false;
  free = total - used;
  return true;
}

Result Store::checkMarker(const char* path, MarkerDiagnostics* diagnostics) {
  MarkerDiagnostics unused{};
  auto& diagnostic = diagnostics ? *diagnostics : unused;
  diagnostic = {false, Node::Error, 0, false, false, false, false};
  size_t bytes = 0;
  const Node node = backend_.inspect(path, bytes);
  diagnostic.checked = true;
  diagnostic.node = node;
  diagnostic.statBytes = bytes;
  if (node == Node::Error) return Error::MarkerReadFailed;
  if (node == Node::Missing) return Error::MarkerMissing;
  if (node != Node::Regular || bytes != sizeof(kMarkerContent) - 1) return Error::MarkerInvalid;
  auto file = backend_.openRead(path);
  if (!file) return Error::MarkerReadFailed;
  size_t openedSize = 0;
  diagnostic.openedLengthChecked = true;
  diagnostic.openedLengthMatches = file->length(openedSize) && openedSize == bytes;
  if (!diagnostic.openedLengthMatches) { file->close(); return Error::MarkerReadFailed; }
  uint8_t content[sizeof(kMarkerContent)]{};
  size_t used = 0;
  while (used < bytes) {
    const size_t count = file->read(content + used, bytes - used);
    if (!count || count > bytes - used || file->failed()) { file->close(); return Error::MarkerReadFailed; }
    used += count;
  }
  uint8_t extra = 0;
  const size_t trailing = file->read(&extra, 1);
  const bool readOk = !file->failed();
  const bool closed = file->close();
  if (!readOk || !closed) return Error::MarkerReadFailed;
  diagnostic.contentChecked = true;
  diagnostic.contentMatches = !trailing && !std::memcmp(content, kMarkerContent, bytes);
  if (!diagnostic.contentMatches) return Error::MarkerInvalid;
  return Error::Ok;
}

Result Store::validateMarker() {
  owned_ = false;
  const Result result = checkMarker(kMarkerPath, &markerDiagnostics_);
  owned_ = result == Error::Ok;
  return result;
}

Result Store::preserveEmptyMarker() {
  recoveryDiagnostics_.emptyChecked = true;
  auto file = backend_.openRead(kMarkerPath);
  if (!file) return Error::MarkerReadFailed;
  size_t bytes = 0;
  const bool lengthOk = file->length(bytes);
  uint8_t byte = 0;
  const size_t count = lengthOk && bytes == 0 ? file->read(&byte, 1) : 0;
  const bool readOk = !file->failed();
  const bool closed = file->close(); file.reset();
  if (!lengthOk || !readOk || !closed) return Error::MarkerReadFailed;
  if (bytes != 0 || count != 0) return Error::MarkerInvalid;
  recoveryDiagnostics_.emptyConfirmed = true;
  char backup[kPathBytes]{};
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    if (!randomPath(backend_, "OWNER-", ".BAK", backup)) return Error::RandomFailed;
    const RenameResult moved = backend_.renameExclusive(kMarkerPath, backup);
    if (moved == RenameResult::Exists) continue;
    if (moved != RenameResult::Ok) return Error::MarkerWriteFailed;
    // An empty file does not prove provenance. Retain its backup permanently;
    // neither failure cleanup nor a later prepare scans or removes backups.
    recoveryDiagnostics_.emptyBackupPreserved = true;
    return Error::Ok;
  }
  return Error::MarkerWriteFailed;
}

Result Store::publishMarker() {
  size_t free = 0;
  if (!available(free)) return Error::MetadataReadFailed;
  if (free <= kReserveBytes + sizeof(kMarkerContent)) return Error::InsufficientSpace;
  char pending[kPathBytes]{};
  std::unique_ptr<File> file;
  for (unsigned attempt = 0; attempt < 8 && !file; ++attempt) {
    if (!randomPath(backend_, "OWNER-", ".NEW", pending)) return Error::RandomFailed;
    size_t bytes = 0;
    const Node node = backend_.inspect(pending, bytes);
    if (node == Node::Error) return Error::MetadataReadFailed;
    if (node != Node::Missing) continue;
    file = backend_.openWrite(pending, true);
    if (!file) {
      const Node raced = backend_.inspect(pending, bytes);
      if (raced == Node::Missing || raced == Node::Error) return Error::MarkerWriteFailed;
    }
  }
  if (!file) return Error::MarkerWriteFailed;
  const size_t written = file->write(reinterpret_cast<const uint8_t*>(kMarkerContent), sizeof(kMarkerContent) - 1);
  const bool flushed = written == sizeof(kMarkerContent) - 1 && !file->failed() && file->flush();
  const bool closed = file->close(); file.reset();
  if (!flushed || !closed) return Error::MarkerWriteFailed;
  const Result verified = checkMarker(pending, nullptr);
  if (verified != Error::Ok) return verified;
  const RenameResult published = backend_.renameExclusive(pending, kMarkerPath);
  if (published == RenameResult::Exists) return Error::OwnershipConflict;
  if (published != RenameResult::Ok) return Error::MarkerWriteFailed;
  recoveryDiagnostics_.newMarkerPublished = true;
  // Verify the final name independently. FAT rename is not a power-loss
  // transaction; no NEW/BAK sweep or unsafe rollback is attempted on errors.
  return validateMarker();
}

Result Store::prepare(size_t requestedBytes) {
  markerDiagnostics_ = {false, Node::Error, 0, false, false, false, false};
  recoveryDiagnostics_ = {false, false, false, false};
  if (readerActive() || state_ == State::Recording) return set(Error::Busy);
  if (requestedBytes && (requestedBytes & 1)) return set(Error::InvalidInput);
  if (writer_) { writer_->close(); writer_.reset(); }
  state_ = State::Idle; mounted_ = owned_ = false; capacity_ = recorded_ = requested_ = 0;
  if (!backend_.mount()) return fail(Error::MountFailed);
  mounted_ = true;
  if (!backend_.prepareDirectory()) return fail(Error::DirectoryFailed);
  Result ownership = validateMarker();
  if (ownership == Error::MarkerMissing) {
    // A previous card/take may have disappeared. Never recreate its marker
    // and then treat an unrelated path on a replacement card as ours.
    if (scratchOwned_) return fail(Error::MarkerMissing);
    ownership = publishMarker();
  } else if (ownership == Error::MarkerInvalid && markerDiagnostics_.node == Node::Regular &&
             markerDiagnostics_.statBytes == 0 && !scratchOwned_) {
    ownership = preserveEmptyMarker();
    if (ownership == Error::Ok) ownership = publishMarker();
  }
  if (ownership != Error::Ok) return fail(ownership);
  if (scratchOwned_) {
    const Result removed = discard();
    if (removed != Error::Ok) return fail(removed);
  }
  size_t free = 0;
  if (!available(free)) return fail(Error::MetadataReadFailed);
  capacity_ = free > kReserveBytes ? std::min(kMaxRecordingBytes, (free - kReserveBytes) & ~size_t(1)) : 0;
  requested_ = requestedBytes;
  if (!capacity_ || requestedBytes > capacity_) return fail(Error::InsufficientSpace);
  state_ = State::Prepared;
  return set(Error::Ok);
}

Result Store::begin() {
  if (readerActive()) return set(Error::Busy);
  if (!mounted_ || state_ != State::Prepared || !owned_) return set(Error::InvalidState);
  const Result ownership = validateMarker();
  if (ownership != Error::Ok) return fail(ownership);
  // Every take gets an exclusive 128-bit random name. No reuse, truncation,
  // directory scan or recovery of files from another Store/previous boot.
  for (unsigned attempt = 0; attempt < 8 && !writer_; ++attempt) {
    if (!randomPath(backend_, "REC-", ".PCM", scratchPath_)) return fail(Error::RandomFailed);
    size_t bytes = 0;
    const Node node = backend_.inspect(scratchPath_, bytes);
    if (node == Node::Error) return fail(Error::MetadataReadFailed);
    if (node != Node::Missing) continue;
    writer_ = backend_.openWrite(scratchPath_, true);
    if (!writer_) {
      // Retry an O_EXCL race, but do not spin on full/removed/broken cards.
      const Node raced = backend_.inspect(scratchPath_, bytes);
      if (raced == Node::Missing || raced == Node::Error) return fail(Error::OpenFailed);
    }
  }
  if (!writer_) { scratchPath_[0] = '\0'; return fail(Error::OpenFailed); }
  scratchOwned_ = true;
  size_t empty = 0, free = 0;
  if (!writer_->length(empty) || empty != 0) return fail(Error::SizeMismatch);
  if (!available(free)) return fail(Error::MetadataReadFailed);
  const size_t actual = free > kReserveBytes ? (free - kReserveBytes) & ~size_t(1) : 0;
  capacity_ = std::min(capacity_, actual);
  if (!capacity_ || requested_ > capacity_) return fail(Error::InsufficientSpace);
  if (requested_) capacity_ = requested_;
  recorded_ = 0; state_ = State::Recording;
  return set(Error::Ok);
}

Result Store::append(const uint8_t* bytes, size_t count) {
  if (readerActive()) return set(Error::Busy);
  if (state_ != State::Recording || !writer_) return set(Error::InvalidState);
  if (!bytes || !count || (count & 1) || count > kAppendBytes) return fail(Error::InvalidInput);
  if (recorded_ > capacity_ || count > capacity_ - recorded_) return fail(Error::InsufficientSpace);
  size_t free = 0;
  if (!available(free)) return fail(Error::MetadataReadFailed);
  if (free < kReserveBytes || count > free - kReserveBytes) return fail(Error::InsufficientSpace);
  const size_t written = writer_->write(bytes, count);
  if (written != count || writer_->failed()) return fail(Error::WriteFailed);
  recorded_ += count;
  if (!available(free)) return fail(Error::MetadataReadFailed);
  if (free < kReserveBytes) return fail(Error::InsufficientSpace);
  return set(Error::Ok);
}

Result Store::finish() {
  if (readerActive()) return set(Error::Busy);
  if (state_ != State::Recording || !writer_) return set(Error::InvalidState);
  if (!recorded_ || (recorded_ & 1)) return fail(Error::SizeMismatch);
  if (!writer_->flush() || writer_->failed()) return fail(Error::FlushFailed);
  size_t bytes = 0;
  const bool closed = writer_->close(); writer_.reset();
  if (!closed) return fail(Error::CloseFailed);
  size_t free = 0;
  if (!available(free)) return fail(Error::MetadataReadFailed);
  if (free < kReserveBytes) return fail(Error::InsufficientSpace);
  const Node node = backend_.inspect(scratchPath_, bytes);
  if (node == Node::Error) return fail(Error::MetadataReadFailed);
  if (node != Node::Regular || bytes != recorded_ || !bytes || (bytes & 1)) return fail(Error::SizeMismatch);
  // Independent descriptor after flush/close: a prefix is not a finished take.
  auto file = backend_.openRead(scratchPath_);
  if (!file) return fail(Error::ReadFailed);
  const bool readbackSize = file->length(bytes);
  const bool readbackClosed = file->close();
  if (!readbackSize || !readbackClosed) return fail(Error::ReadFailed);
  if (bytes != recorded_) return fail(Error::SizeMismatch);
  state_ = State::Finished;
  return set(Error::Ok);
}

std::unique_ptr<official_voice::Reader> Store::openReader(Result& result) {
  auto rejected = [&](Result error) -> std::unique_ptr<official_voice::Reader> { result = set(error); return {}; };
  if (readerActive()) return rejected(Error::Busy);
  if (state_ != State::Finished || !recorded_ || (recorded_ & 1)) return rejected(Error::InvalidState);
  const Result ownership = validateMarker();
  if (ownership != Error::Ok) return rejected(ownership);
  size_t bytes = 0;
  const Node node = backend_.inspect(scratchPath_, bytes);
  if (node == Node::Error) return rejected(Error::MetadataReadFailed);
  if (node != Node::Regular || bytes != recorded_) return rejected(Error::SizeMismatch);
  auto file = backend_.openRead(scratchPath_);
  if (!file) return rejected(Error::ReadFailed);
  if (!file->length(bytes) || bytes != recorded_) { file->close(); return rejected(Error::SizeMismatch); }
  if (!lease_) lease_ = new(std::nothrow) Lease;
  if (!lease_) { file->close(); return rejected(Error::OutOfMemory); }
  auto* reader = new(std::nothrow) OwnedReader(std::move(file), recorded_, lease_);
  if (!reader) { if (file) file->close(); return rejected(Error::OutOfMemory); }
  result = set(Error::Ok);
  return std::unique_ptr<official_voice::Reader>(reader);
}

Result Store::discard() {
  if (readerActive()) return set(Error::Busy);
  if (!mounted_) return set(Error::InvalidState);
  bool closed = true;
  if (writer_) { closed = writer_->close(); writer_.reset(); }
  const Result ownership = validateMarker();
  if (ownership != Error::Ok) return fail(ownership);
  if (scratchOwned_) {
    if (!isScratchPath(scratchPath_)) return fail(Error::OwnershipConflict);
    size_t bytes = 0;
    const Node node = backend_.inspect(scratchPath_, bytes);
    if (node == Node::Error) return fail(Error::MetadataReadFailed);
    if (node != Node::Missing && node != Node::Regular) return fail(Error::OwnershipConflict);
    if (node == Node::Regular && !backend_.remove(scratchPath_)) return fail(Error::RemoveFailed);
    scratchOwned_ = false; scratchPath_[0] = '\0';
  }
  capacity_ = recorded_ = requested_ = 0; state_ = State::Idle;
  return set(closed ? Error::Ok : Error::CloseFailed);
}

const char* errorName(Error error) {
  switch (error) {
    case Error::Ok: return "ok";
    case Error::Busy: return "busy";
    case Error::InvalidState: return "invalid_state";
    case Error::InvalidInput: return "invalid_input";
    case Error::MountFailed: return "mount_failed";
    case Error::MetadataReadFailed: return "metadata_read_failed";
    case Error::OwnershipConflict: return "ownership_conflict";
    case Error::MarkerMissing: return "marker_missing";
    case Error::MarkerInvalid: return "marker_invalid";
    case Error::MarkerReadFailed: return "marker_read_failed";
    case Error::MarkerWriteFailed: return "marker_write_failed";
    case Error::InsufficientSpace: return "insufficient_space";
    case Error::OpenFailed: return "open_failed";
    case Error::WriteFailed: return "write_failed";
    case Error::FlushFailed: return "flush_failed";
    case Error::CloseFailed: return "close_failed";
    case Error::SizeMismatch: return "size_mismatch";
    case Error::ReadFailed: return "read_failed";
    case Error::RemoveFailed: return "remove_failed";
    case Error::OutOfMemory: return "out_of_memory";
    case Error::DirectoryFailed: return "directory_failed";
    case Error::RandomFailed: return "random_failed";
  }
  return "unknown";
}

const char* stageName(Stage stage) {
  switch (stage) {
    case Stage::None: return "none";
    case Stage::CardStatus: return "card_status";
    case Stage::SpiInitialize: return "spi_initialize";
    case Stage::Unmount: return "unmount";
    case Stage::Mount: return "mount";
    case Stage::DirectoryStat: return "directory_stat";
    case Stage::DirectoryCreate: return "directory_create";
    case Stage::MetadataStat: return "metadata_stat";
    case Stage::DriveLookup: return "drive_lookup";
    case Stage::FreeSpace: return "free_space";
    case Stage::FreeSpaceInvariant: return "free_space_invariant";
    case Stage::OpenRead: return "open_read";
    case Stage::OpenWrite: return "open_write";
    case Stage::FileStat: return "file_stat";
    case Stage::Read: return "read";
    case Stage::Write: return "write";
    case Stage::Flush: return "flush";
    case Stage::Sync: return "sync";
    case Stage::Close: return "close";
    case Stage::Remove: return "remove";
    case Stage::Path: return "path";
    case Stage::Random: return "random";
    case Stage::Rename: return "rename";
  }
  return "unknown";
}

#ifdef ARDUINO
namespace {
constexpr char kMountPoint[] = "/sdcard";
sdmmc_card_t* sdCard = nullptr;
bool spiInitialized = false;
std::atomic<unsigned> liveFiles{0};
portMUX_TYPE diagnosticMux = portMUX_INITIALIZER_UNLOCKED;
Diagnostics lastDiagnostic{Stage::None, 0, 0, 0, 0};

void ioFailure(Stage stage, int espCode = 0, int fatCode = 0, int errnoCode = 0) {
  // No filesystem work or logging while holding this short snapshot lock.
  portENTER_CRITICAL(&diagnosticMux);
  lastDiagnostic.stage = stage;
  lastDiagnostic.espCode = espCode;
  lastDiagnostic.fatCode = fatCode;
  lastDiagnostic.errnoCode = errnoCode;
  if (++lastDiagnostic.sequence == 0) ++lastDiagnostic.sequence;
  portEXIT_CRITICAL(&diagnosticMux);
}

bool cardReady() {
  const esp_err_t status = sdCard ? sdmmc_get_status(sdCard) : ESP_ERR_INVALID_STATE;
  if (status != ESP_OK) ioFailure(Stage::CardStatus, status);
  return status == ESP_OK;
}
bool cardDrive(BYTE& drive) {
  drive = ff_diskio_get_pdrv_card(sdCard);
  if (drive >= FF_VOLUMES || drive > 9
#if FF_MULTI_PARTITION
      || VolToPart[drive].pd != drive
#endif
     ) {
    ioFailure(Stage::DriveLookup, 0, FR_INVALID_DRIVE); return false;
  }
  return true;
}
bool allowed(const char* path) {
  return path && (!std::strcmp(path, kMarkerPath) || isScratchPath(path) ||
                  isMarkerNewPath(path) || isMarkerBackupPath(path));
}
bool mountedPath(const char* path, char (&out)[80]) {
  if (!allowed(path)) { ioFailure(Stage::Path, ESP_ERR_INVALID_ARG); return false; }
  const int count = std::snprintf(out, sizeof(out), "%s%s", kMountPoint, path);
  if (count <= 0 || size_t(count) >= sizeof(out)) { ioFailure(Stage::Path, ESP_ERR_INVALID_SIZE); return false; }
  return true;
}
bool directory(const char* path) {
  struct stat info{};
  if (::stat(path, &info) == 0) {
    if (S_ISDIR(info.st_mode)) return true;
    ioFailure(Stage::DirectoryStat, 0, 0, ENOTDIR); return false;
  }
  if (errno != ENOENT) { ioFailure(Stage::DirectoryStat, 0, 0, errno); return false; }
  if (::mkdir(path, 0700) == 0) return true;
  const int createError = errno;
  if (createError == EEXIST && ::stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return true;
  ioFailure(Stage::DirectoryCreate, 0, 0, createError); return false;
}
class PosixFile final : public File {
 public:
  explicit PosixFile(std::FILE* file) : file_(file) {
    std::setvbuf(file_, nullptr, _IONBF, 0);
    liveFiles.fetch_add(1, std::memory_order_release);
  }
  ~PosixFile() override { close(); }
  size_t read(uint8_t* out, size_t count) override {
    if (!file_ || (!out && count) || !cardReady()) { failed_ = true; return 0; }
    errno = 0;
    const size_t got = std::fread(out, 1, count, file_);
    if (std::ferror(file_)) { failed_ = true; ioFailure(Stage::Read, 0, 0, errno); }
    return got;
  }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (!file_ || (!bytes && count) || !cardReady()) { failed_ = true; return 0; }
    errno = 0;
    const size_t put = std::fwrite(bytes, 1, count, file_);
    if (put != count || std::ferror(file_)) { failed_ = true; ioFailure(Stage::Write, 0, 0, errno); }
    return put;
  }
  bool flush() override {
    if (!file_ || !cardReady()) failed_ = true;
    if (!failed_ && std::fflush(file_)) { failed_ = true; ioFailure(Stage::Flush, 0, 0, errno); }
    // FAT VFS provides fsync. Unlike the former SPIFFS implementation, an
    // unsupported or failed sync is an error, never accepted as a saved take.
    if (!failed_ && ::fsync(::fileno(file_))) { failed_ = true; ioFailure(Stage::Sync, 0, 0, errno); }
    return !failed_;
  }
  bool close() override {
    if (file_) {
      const int result = std::fclose(file_); file_ = nullptr;
      if (result) { failed_ = true; ioFailure(Stage::Close, 0, 0, errno); }
      liveFiles.fetch_sub(1, std::memory_order_release);
    }
    return !failed_;
  }
  bool length(size_t& bytes) override {
    struct stat info{};
    if (!file_ || !cardReady()) { failed_ = true; return false; }
    if (::fstat(::fileno(file_), &info)) {
      failed_ = true; ioFailure(Stage::FileStat, 0, 0, errno); return false;
    }
    if (info.st_size < 0 || uint64_t(info.st_size) > std::numeric_limits<size_t>::max()) {
      failed_ = true; ioFailure(Stage::FileStat, ESP_ERR_INVALID_SIZE); return false;
    }
    bytes = size_t(info.st_size); return true;
  }
  bool failed() const override { return failed_; }
 private:
  std::FILE* file_;
  bool failed_ = false;
};
std::unique_ptr<File> own(std::FILE* file, Stage stage) {
  if (!file) return {};
  auto* owned = new(std::nothrow) PosixFile(file);
  if (!owned) { std::fclose(file); ioFailure(stage, ESP_ERR_NO_MEM); }
  return std::unique_ptr<File>(owned);
}
}  // namespace

Diagnostics SdBackend::diagnostics() const {
  portENTER_CRITICAL(&diagnosticMux);
  const Diagnostics snapshot = lastDiagnostic;
  portEXIT_CRITICAL(&diagnosticMux);
  return snapshot;
}

bool SdBackend::mount() {
  if (sdCard) {
    if (cardReady()) return true;
    // Never unmount handles used by an STT worker. A failed take is closed
    // before the next prepare can retry mounting a reinserted card.
    if (liveFiles.load(std::memory_order_acquire)) return false;
    sdmmc_card_t* old = sdCard; sdCard = nullptr;
    const esp_err_t status = esp_vfs_fat_sdcard_unmount(kMountPoint, old);
    if (status != ESP_OK) { ioFailure(Stage::Unmount, status); return false; }
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = 4000;
  if (!spiInitialized) {
    spi_bus_config_t bus{};
    // Same Cardputer ADV wiring as factory v1.7.0. LCD uses the other bus.
    bus.mosi_io_num = 14; bus.miso_io_num = 39; bus.sclk_io_num = 40;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1; bus.max_transfer_sz = 4096;
    const esp_err_t status = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus, SDSPI_DEFAULT_DMA);
    if (status != ESP_OK) { ioFailure(Stage::SpiInitialize, status); return false; }
    spiInitialized = true;
  }
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = GPIO_NUM_12;
  slot.host_id = static_cast<spi_host_device_t>(host.slot);
  esp_vfs_fat_sdmmc_mount_config_t config{};
  config.format_if_mount_failed = false; config.max_files = 5; config.allocation_unit_size = 16 * 1024;
  const esp_err_t status = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &config, &sdCard);
  if (status != ESP_OK) { ioFailure(Stage::Mount, status); return false; }
  return cardReady();
}
bool SdBackend::prepareDirectory() {
  return cardReady() && directory("/sdcard/EKKO") && directory("/sdcard/EKKO/TMP");
}
bool SdBackend::randomBytes(uint8_t* bytes, size_t count) {
  if (!bytes || !count) { ioFailure(Stage::Random, ESP_ERR_INVALID_ARG); return false; }
  esp_fill_random(bytes, count); return true;
}
Node SdBackend::inspect(const char* path, size_t& bytes) {
  bytes = 0; char full[80]{};
  if (!mountedPath(path, full) || !cardReady()) return Node::Error;
  struct stat info{};
  if (::stat(full, &info)) {
    if (errno == ENOENT) return Node::Missing;
    ioFailure(Stage::MetadataStat, 0, 0, errno); return Node::Error;
  }
  if (!S_ISREG(info.st_mode)) return Node::Other;
  if (info.st_size < 0 || uint64_t(info.st_size) > std::numeric_limits<size_t>::max()) {
    ioFailure(Stage::MetadataStat, ESP_ERR_INVALID_SIZE); return Node::Error;
  }
  bytes = size_t(info.st_size); return Node::Regular;
}
bool SdBackend::space(size_t& total, size_t& used) {
  total = used = 0;
  if (!cardReady()) return false;
  BYTE drive;
  if (!cardDrive(drive)) return false;
  const char path[] = {char('0' + drive), ':', '\0'};
  DWORD clusters = 0; FATFS* fs = nullptr;
  const FRESULT result = f_getfree(path, &clusters, &fs);
  if (result != FR_OK) { ioFailure(Stage::FreeSpace, 0, result); return false; }
  if (!fs || fs->pdrv != drive || fs->n_fatent < 2) {
    ioFailure(Stage::FreeSpaceInvariant, ESP_ERR_INVALID_RESPONSE); return false;
  }
#if FF_MAX_SS != FF_MIN_SS
  const uint64_t clusterBytes = uint64_t(fs->csize) * fs->ssize;
#else
  const uint64_t clusterBytes = uint64_t(fs->csize) * FF_MAX_SS;
#endif
  const uint64_t all = uint64_t(fs->n_fatent - 2) * clusterBytes;
  const uint64_t free = uint64_t(clusters) * clusterBytes;
  if (!all || free > all) { ioFailure(Stage::FreeSpaceInvariant, ESP_ERR_INVALID_RESPONSE); return false; }
  // On the 32-bit MCU, preserve available capacity instead of overflowing
  // cards larger than 4 GiB. Store additionally caps every take at 30 s.
  total = size_t(std::min<uint64_t>(all, std::numeric_limits<size_t>::max()));
  used = total - size_t(std::min<uint64_t>(free, total));
  return true;
}
std::unique_ptr<File> SdBackend::openRead(const char* path) {
  char full[80]{}; if (!mountedPath(path, full) || !cardReady()) return {};
  std::FILE* file = std::fopen(full, "rb");
  if (!file) { ioFailure(Stage::OpenRead, 0, 0, errno); return {}; }
  return own(file, Stage::OpenRead);
}
std::unique_ptr<File> SdBackend::openWrite(const char* path, bool exclusive) {
  char full[80]{};
  // Every writable file is exclusive. Never expose truncate, format,
  // recursive deletion or arbitrary paths to the recording application.
  if (!exclusive || (!isScratchPath(path) && !isMarkerNewPath(path)) ||
      !mountedPath(path, full) || !cardReady()) return {};
  std::FILE* file = detail::openExclusiveRecordingFile(full);
  if (!file) { ioFailure(Stage::OpenWrite, 0, 0, errno); return {}; }
  return own(file, Stage::OpenWrite);
}
bool SdBackend::remove(const char* path) {
  char full[80]{};
  if (!isScratchPath(path) || !mountedPath(path, full) || !cardReady()) return false;
  if (::unlink(full)) { ioFailure(Stage::Remove, 0, 0, errno); return false; }
  return true;
}
RenameResult SdBackend::renameExclusive(const char* from, const char* to) {
  const bool backup = from && !std::strcmp(from, kMarkerPath) && isMarkerBackupPath(to);
  if (!isMarkerRenamePair(from, to)) { ioFailure(Stage::Path, ESP_ERR_INVALID_ARG); return RenameResult::Failed; }
  if (liveFiles.load(std::memory_order_acquire)) {
    ioFailure(Stage::Rename, ESP_ERR_INVALID_STATE); return RenameResult::Failed;
  }
  if (!cardReady()) return RenameResult::Failed;
  BYTE drive;
  if (!cardDrive(drive)) return RenameResult::Failed;
  // Use FatFs logical paths, not VFS /sdcard paths. Both ends are constructed
  // on this same drive; FatFs itself ignores the new path's drive prefix.
  char oldName[kPathBytes + 4], newName[kPathBytes + 4];
  const int oldBytes = std::snprintf(oldName, sizeof(oldName), "%u:%s", unsigned(drive), from);
  const int newBytes = std::snprintf(newName, sizeof(newName), "%u:%s", unsigned(drive), to);
  if (oldBytes <= 0 || size_t(oldBytes) >= sizeof(oldName) || newBytes <= 0 || size_t(newBytes) >= sizeof(newName)) {
    ioFailure(Stage::Path, ESP_ERR_INVALID_SIZE); return RenameResult::Failed;
  }
  FILINFO source{};
  FRESULT result = f_stat(oldName, &source);
  if (result == FR_OK && ((source.fattrib & AM_DIR) || source.fsize != (backup ? 0 : sizeof(kMarkerContent) - 1))) {
    ioFailure(Stage::Rename, ESP_ERR_INVALID_SIZE); return RenameResult::Failed;
  }
  if (result == FR_OK) result = f_rename(oldName, newName);
  if (result == FR_OK) return RenameResult::Ok;
  ioFailure(Stage::Rename, 0, result);
  return result == FR_EXIST ? RenameResult::Exists : RenameResult::Failed;
}
#endif

}  // namespace official_recording
