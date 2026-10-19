#pragma once
#include "OfficialStudioVoice.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace official_recording {

constexpr std::size_t kReserveBytes = 64 * 1024;
constexpr std::size_t kAppendBytes = 1024;
constexpr std::size_t kMaxRecordingBytes = 30 * 16000 * 2;
constexpr std::size_t kPathBytes = 64;
constexpr char kDirectoryPath[] = "/EKKO/TMP";
constexpr char kMarkerPath[] = "/EKKO/TMP/OWNER";
constexpr char kMarkerContent[] = "EKKO-CARDPUTER-SD-TEMP-v1\n";
// Strict basename validation is shared by Store, backend and host tests.
bool isScratchPath(const char* path);
bool isMarkerNewPath(const char* path);
bool isMarkerBackupPath(const char* path);
bool isMarkerRenamePair(const char* from, const char* to);

enum class Error : uint8_t {
  Ok, Busy, InvalidState, InvalidInput, MountFailed, MetadataReadFailed,
  OwnershipConflict, MarkerMissing, MarkerInvalid, MarkerReadFailed,
  MarkerWriteFailed, InsufficientSpace, OpenFailed, WriteFailed, FlushFailed,
  CloseFailed, SizeMismatch, ReadFailed, RemoveFailed, OutOfMemory,
  DirectoryFailed, RandomFailed
};
using Result = Error;
enum class Node : uint8_t { Missing, Regular, Other, Error };
enum class State : uint8_t { Idle, Prepared, Recording, Finished, Failed };
enum class RenameResult : uint8_t { Ok, Exists, Failed };

// Read-only ownership-check progress. No marker bytes or path are exposed.
// `checked` gates node/statBytes; later flags distinguish a rejected stat,
// failed opened-file length, incomplete read, and an exact-content mismatch.
struct MarkerDiagnostics {
  bool checked;
  Node node;
  size_t statBytes;
  bool openedLengthChecked;
  bool openedLengthMatches;
  bool contentChecked;
  bool contentMatches;
};
struct RecoveryDiagnostics {
  bool emptyChecked;
  bool emptyConfirmed;
  bool emptyBackupPreserved;
  bool newMarkerPublished;
};

class File {
 public:
  virtual ~File() = default;
  virtual size_t read(uint8_t* out, size_t capacity) = 0;
  virtual size_t write(const uint8_t* bytes, size_t count) = 0;
  virtual bool flush() = 0;  // Flush pending user-space buffers; report errors.
  // Must commit filesystem caches and release the handle even when reporting
  // an error. Idempotent. A take/marker is never accepted before checked close
  // and independent reopen.
  virtual bool close() = 0;
  virtual bool length(size_t& bytes) = 0;
  virtual bool failed() const = 0;
};

// One application owner serializes filesystem mutation. Owned File objects
// remain valid independently of Backend/Store calls; their read/close is used
// by one worker only. Exclusive create must never truncate an existing file.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool mount() = 0;  // Never format on failure.
  virtual bool prepareDirectory() = 0;  // mkdir only; reject non-directories.
  virtual bool randomBytes(uint8_t* bytes, size_t count) = 0;
  virtual Node inspect(const char* path, size_t& bytes) = 0;
  virtual bool space(size_t& total, size_t& used) = 0;
  virtual std::unique_ptr<File> openRead(const char* path) = 0;
  virtual std::unique_ptr<File> openWrite(const char* path, bool exclusive) = 0;
  virtual bool remove(const char* path) = 0;
  // Never replace a destination. Only OWNER -> random BAK and random NEW ->
  // OWNER are permitted; callers must close all file handles first. FAT rename
  // is not a guarantee of durability/atomicity across arbitrary power loss.
  virtual RenameResult renameExclusive(const char* from, const char* to) = 0;
};

// Methods, accessors and destruction belong to one application/main task.
// Only the returned Reader's read/destructor runs on a worker; its lease is
// atomic, and it never calls back into Store or accesses UI/global Strings.
class Store {
 public:
  explicit Store(Backend& backend) : backend_(backend) {}
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // requestedBytes=0 reports/uses the available bound; a positive request
  // must fit in full. Never silently substitute a shorter recording duration.
  // Bounded to 30 seconds even if the SD card has many gigabytes free.
  Result prepare(size_t requestedBytes = 0);
  Result begin();
  Result append(const uint8_t* bytes, size_t count);  // PCM16 mono, even, <=1 KiB.
  Result finish();
  std::unique_ptr<official_voice::Reader> openReader(Result& result);
  // Aborts a writer if any, deletes only a scratch created by this Store,
  // and preserves the marker. Never removes anything while a reader is alive.
  Result discard();

  size_t capacityBytes() const { return capacity_; }
  size_t recordedBytes() const { return recorded_; }
  bool readerActive() const;
  State state() const { return state_; }
  Result result() const { return result_; }
  const char* scratchPath() const { return scratchPath_; }
  MarkerDiagnostics markerDiagnostics() const { return markerDiagnostics_; }
  RecoveryDiagnostics recoveryDiagnostics() const { return recoveryDiagnostics_; }

 private:
  struct Lease;
  class OwnedReader;
  Backend& backend_;
  std::unique_ptr<File> writer_;
  Lease* lease_ = nullptr;
  size_t capacity_ = 0, recorded_ = 0, requested_ = 0;
  State state_ = State::Idle;
  Result result_ = Error::Ok;
  bool mounted_ = false, owned_ = false, scratchOwned_ = false;
  char scratchPath_[kPathBytes]{};
  MarkerDiagnostics markerDiagnostics_{false, Node::Error, 0, false, false, false, false};
  RecoveryDiagnostics recoveryDiagnostics_{false, false, false, false};

  Result set(Result result) { result_ = result; return result; }
  Result fail(Result result);
  Result validateMarker();
  Result checkMarker(const char* path, MarkerDiagnostics* diagnostics);
  Result preserveEmptyMarker();
  Result publishMarker();
  bool available(size_t& free);
};

const char* errorName(Error);

// Last backend I/O failure only. Values never contain a path, file content,
// network identifier or credential; sequence lets callers spot a new failure.
enum class Stage : uint8_t {
  None, CardStatus, SpiInitialize, Unmount, Mount, DirectoryStat,
  DirectoryCreate, MetadataStat, DriveLookup, FreeSpace,
  FreeSpaceInvariant, OpenRead, OpenWrite, FileStat, Read, Write,
  Flush, Sync, Close, Remove, Path, Random, Rename
};
struct Diagnostics {
  Stage stage;
  int espCode;
  int fatCode;
  int errnoCode;
  uint32_t sequence;
};
const char* stageName(Stage);

#ifdef ARDUINO
class SdBackend final : public Backend {
 public:
  bool mount() override;
  bool prepareDirectory() override;
  bool randomBytes(uint8_t* bytes, size_t count) override;
  Node inspect(const char* path, size_t& bytes) override;
  bool space(size_t& total, size_t& used) override;
  std::unique_ptr<File> openRead(const char* path) override;
  std::unique_ptr<File> openWrite(const char* path, bool exclusive) override;
  bool remove(const char* path) override;
  RenameResult renameExclusive(const char* from, const char* to) override;
  // Safe snapshot while a voice worker is reading/closing a file. Successful
  // operations do not erase a failure before the UI can report it.
  Diagnostics diagnostics() const;
};
#endif

}  // namespace official_recording
