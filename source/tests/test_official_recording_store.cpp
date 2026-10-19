#include "OfficialRecordingStore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace official_recording;

class FakeBackend final : public Backend {
 public:
  struct Entry { Node node = Node::Missing; std::vector<uint8_t> data; };
  std::map<std::string, Entry> files;
  size_t total = 32 * 1024 * 1024, external = 0, writeOpens = 0, removes = 0, liveFiles = 0;
  bool present = true, directoryOk = true, randomOk = true, spaceOk = true;
  bool markerMetaError = false, scratchMetaError = false, markerOpenError = false, scratchOpenError = false;
  bool markerReadError = false, scratchReadError = false, markerWriteShort = false, scratchWriteShort = false;
  bool markerFlushError = false, scratchFlushError = false, closeError = false;
  bool markerLengthError = false, markerLengthMismatch = false;
  bool markerStatZero = false, markerLengthZero = false;
  bool corruptMarkerOnFlush = false, truncateScratchOnClose = false, removeError = false;
  bool detachOnFault = false, ownerAppearsOnPublish = false, corruptAfterPublish = false;
  bool backupMutatesThenFails = false, publishMutatesThenFails = false;
  bool published = false, newReadbackVerified = false;
  unsigned randomCalls = 0, collisions = 0, backupCollisions = 0, newCollisions = 0, renames = 0;
  std::string fault;
  unsigned faultSkip = 0, randomFailAfter = ~0u;
  std::vector<std::string> events;
  std::vector<std::string> opened, removed;
  static bool readable(const char* path) {
    return path && (!std::strcmp(path, kMarkerPath) || isScratchPath(path) || isMarkerNewPath(path) || isMarkerBackupPath(path));
  }
  static const char* role(const char* path) {
    return isScratchPath(path) ? "scratch" : isMarkerNewPath(path) ? "new" : isMarkerBackupPath(path) ? "backup" : "owner";
  }
  bool hit(const std::string& event) {
    const std::string name = published && event.compare(0, 6, "owner.") == 0 ? "final." + event.substr(6) : event;
    events.push_back(name);
    if (fault != name) return false;
    if (faultSkip) { --faultSkip; return false; }
    if (detachOnFault) present = false;
    return true;
  }
  Entry& marker() { return files[kMarkerPath]; }
  void validMarker() { marker().node = Node::Regular; marker().data.assign(kMarkerContent, kMarkerContent + sizeof(kMarkerContent) - 1); }
  bool mount() override { return present; }
  bool prepareDirectory() override { return present && directoryOk; }
  bool randomBytes(uint8_t* bytes, size_t count) override {
    if (!randomOk || randomCalls >= randomFailAfter) return false;
    ++randomCalls;
    std::fill(bytes, bytes + count, uint8_t(randomCalls));
    if (collisions) {
      --collisions;
      char name[kPathBytes]; char nonce[33]{};
      for (size_t n = 0; n < 16; ++n) std::snprintf(nonce + n * 2, 3, "%02x", unsigned(uint8_t(randomCalls)));
      std::snprintf(name, sizeof(name), "/EKKO/TMP/REC-%s.PCM", nonce);
      auto& entry = files[name]; entry.node = Node::Regular; entry.data.assign(10, 77);
    }
    if (newCollisions) {
      --newCollisions;
      char name[kPathBytes], nonce[33]{};
      for (size_t n = 0; n < 16; ++n) std::snprintf(nonce + n * 2, 3, "%02x", unsigned(uint8_t(randomCalls)));
      std::snprintf(name, sizeof(name), "/EKKO/TMP/OWNER-%s.NEW", nonce);
      auto& entry = files[name]; entry.node = Node::Regular; entry.data.assign(7, 81);
    }
    return true;
  }
  Node inspect(const char* path, size_t& bytes) override {
    assert(readable(path));
    const bool marker = !std::strcmp(path, kMarkerPath);
    if (!present || hit(std::string(role(path)) + ".stat") || (marker ? markerMetaError : isScratchPath(path) && scratchMetaError)) return Node::Error;
    auto found = files.find(path);
    bytes = found == files.end() || (marker && markerStatZero) ? 0 : found->second.data.size();
    return found == files.end() ? Node::Missing : found->second.node;
  }
  bool space(size_t& all, size_t& used) override {
    all = total; used = external;
    for (const auto& entry : files) used += entry.second.data.size();
    return present && spaceOk;
  }
  class Handle final : public File {
   public:
    Handle(FakeBackend& fs, Entry& entry, const char* path, bool writing)
        : fs_(fs), entry_(entry), path_(path), marker_(!isScratchPath(path)), writing_(writing) { ++fs_.liveFiles; }
    ~Handle() override { close(); }
    size_t read(uint8_t* out, size_t capacity) override {
      assert(!closed_ && !writing_);
      if (!fs_.present || fs_.hit(std::string(role(path_.c_str())) + ".read") ||
          (marker_ ? fs_.markerReadError : fs_.scratchReadError)) { failed_ = true; return 0; }
      const size_t count = std::min(capacity, entry_.data.size() - offset_);
      if (count) std::memcpy(out, entry_.data.data() + offset_, count);
      offset_ += count; if (!count) eof_ = true; return count;
    }
    size_t write(const uint8_t* bytes, size_t count) override {
      assert(!closed_ && writing_ && count <= kAppendBytes);
      assert(isScratchPath(path_.c_str()) || isMarkerNewPath(path_.c_str()));
      if (!fs_.present || fs_.hit(std::string(role(path_.c_str())) + ".write")) { failed_ = true; return 0; }
      const bool shortWrite = marker_ ? fs_.markerWriteShort : fs_.scratchWriteShort;
      const size_t written = shortWrite ? count / 2 : count;
      entry_.data.insert(entry_.data.end(), bytes, bytes + written);
      if (written != count) failed_ = true;
      return written;
    }
    bool flush() override {
      assert(!closed_);
      if (!fs_.present || fs_.hit(std::string(role(path_.c_str())) + ".flush") ||
          (marker_ ? fs_.markerFlushError : fs_.scratchFlushError)) failed_ = true;
      if (marker_ && fs_.corruptMarkerOnFlush && !entry_.data.empty()) entry_.data[0] ^= 1;
      return !failed_;
    }
    bool close() override {
      if (!closed_) {
        --fs_.liveFiles; closed_ = true;
        if (!marker_ && writing_ && fs_.truncateScratchOnClose && !entry_.data.empty()) entry_.data.pop_back();
        if (fs_.closeError || !fs_.present ||
            fs_.hit(std::string(role(path_.c_str())) + (writing_ ? ".write_close" : ".read_close"))) failed_ = true;
        if (!writing_ && isMarkerNewPath(path_.c_str()) && !failed_ && eof_ &&
            entry_.data == std::vector<uint8_t>(kMarkerContent, kMarkerContent + sizeof(kMarkerContent) - 1)) fs_.newReadbackVerified = true;
      }
      return !failed_;
    }
    bool length(size_t& bytes) override {
      assert(!closed_);
      bytes = entry_.data.size() + (marker_ && fs_.markerLengthMismatch ? 1 : 0);
      if (marker_ && fs_.markerLengthZero) bytes = 0;
      return fs_.present && !fs_.hit(std::string(role(path_.c_str())) + ".length") && !(marker_ && fs_.markerLengthError);
    }
    bool failed() const override { return failed_; }
   private:
    FakeBackend& fs_; Entry& entry_; std::string path_;
    bool marker_, writing_, closed_ = false, failed_ = false, eof_ = false; size_t offset_ = 0;
  };
  std::unique_ptr<File> openRead(const char* path) override {
    assert(readable(path));
    const bool marker = !isScratchPath(path);
    auto found = files.find(path);
    if (!present || hit(std::string(role(path)) + ".read_open") || found == files.end() ||
        found->second.node != Node::Regular || (marker ? markerOpenError : scratchOpenError)) return {};
    return std::unique_ptr<File>(new Handle(*this, found->second, path, false));
  }
  std::unique_ptr<File> openWrite(const char* path, bool exclusive) override {
    assert(exclusive && (isScratchPath(path) || isMarkerNewPath(path)));
    const bool marker = isMarkerNewPath(path);
    if (hit(std::string(role(path)) + ".write_open")) return {};
    auto& entry = files[path];
    if (!present || entry.node != Node::Missing || (marker ? markerOpenError : scratchOpenError)) return {};
    ++writeOpens; opened.push_back(path); entry.node = Node::Regular; entry.data.clear();
    if (marker) newReadbackVerified = false;
    return std::unique_ptr<File>(new Handle(*this, entry, path, true));
  }
  bool remove(const char* path) override {
    assert(isScratchPath(path)); ++removes;
    if (!present || removeError) return false;
    removed.push_back(path); files.erase(path); return true;
  }
  RenameResult renameExclusive(const char* from, const char* to) override {
    const bool backup = !std::strcmp(from, kMarkerPath) && isMarkerBackupPath(to);
    const bool commit = isMarkerNewPath(from) && !std::strcmp(to, kMarkerPath);
    assert(isMarkerRenamePair(from, to) && (backup || commit) && liveFiles == 0);
    ++renames;
    if (!present || hit(backup ? "backup.rename" : "publish.rename")) return RenameResult::Failed;
    if (backup && backupCollisions) {
      --backupCollisions; auto& collision = files[to]; collision.node = Node::Regular; collision.data.assign(5, 53);
    }
    if (commit && ownerAppearsOnPublish) { marker().node = Node::Regular; marker().data.assign(9, 65); }
    const auto source = files.find(from);
    if (source == files.end() || source->second.node != Node::Regular ||
        source->second.data.size() != (backup ? 0 : sizeof(kMarkerContent) - 1)) return RenameResult::Failed;
    if (files.count(to)) return RenameResult::Exists;
    if (commit) assert(newReadbackVerified);
    files.emplace(to, source->second); files.erase(source);
    if (commit) {
      published = true;
      if (corruptAfterPublish) marker().data[0] ^= 1;
    }
    if ((backup && backupMutatesThenFails) || (commit && publishMutatesThenFails)) return RenameResult::Failed;
    return RenameResult::Ok;
  }
};

static std::array<uint8_t, kAppendBytes> pcm() {
  std::array<uint8_t, kAppendBytes> data{};
  for (size_t n = 0; n < data.size(); ++n) data[n] = uint8_t(n * 13);
  return data;
}
static void happyPathAndBusyReader() {
  FakeBackend fs;
  auto& foreign = fs.files["/RECORD/last.wav"]; foreign.node = Node::Regular; foreign.data.assign(5000, 42);
  auto& unrelated = fs.files["/EKKO/TMP/my-notes.txt"]; unrelated.node = Node::Regular; unrelated.data.assign(42, 19);
  const auto original = foreign.data, notes = unrelated.data; Store store(fs); const auto bytes = pcm();
  assert(store.begin() == Error::InvalidState);
  assert(store.prepare(2048) == Error::Ok && fs.marker().node == Node::Regular && !*store.scratchPath());
  assert(store.begin() == Error::Ok && store.capacityBytes() == 2048);
  const std::string path = store.scratchPath(); assert(isScratchPath(path.c_str()));
  assert(store.append(bytes.data(), bytes.size()) == Error::Ok && store.append(bytes.data(), bytes.size()) == Error::Ok);
  assert(store.finish() == Error::Ok && store.recordedBytes() == 2048 && fs.liveFiles == 0);
  Error error; auto reader = store.openReader(error);
  assert(error == Error::Ok && reader && store.readerActive());
  const auto opens = fs.writeOpens, removals = fs.removes;
  assert(store.prepare(1024) == Error::Busy && store.begin() == Error::Busy && store.discard() == Error::Busy);
  assert(store.append(bytes.data(), bytes.size()) == Error::Busy && store.finish() == Error::Busy);
  assert(!store.openReader(error) && error == Error::Busy && fs.writeOpens == opens && fs.removes == removals);
  std::array<uint8_t, 37> buffer{}; size_t read = 0;
  while (const size_t count = reader->read(buffer.data(), buffer.size())) {
    for (size_t i = 0; i < count; ++i) assert(buffer[i] == bytes[(read + i) % bytes.size()]);
    read += count;
  }
  assert(read == 2048 && store.readerActive());
  std::thread worker([&]() { reader.reset(); }); worker.join();
  assert(!store.readerActive() && fs.liveFiles == 0);
  assert(store.discard() == Error::Ok && fs.files.count(path) == 0 && fs.marker().node == Node::Regular);
  assert(fs.files["/RECORD/last.wav"].data == original && fs.files["/EKKO/TMP/my-notes.txt"].data == notes);
  assert(store.discard() == Error::Ok && fs.removes == 1);
  assert(store.prepare(1024) == Error::Ok && store.begin() == Error::Ok && path != store.scratchPath());
  assert(store.discard() == Error::Ok);
}
static void ownershipAndDirectoryFailures() {
  for (int mode = 0; mode < 5; ++mode) {
    FakeBackend fs;
    if (mode == 0) fs.present = false;
    if (mode == 1) fs.directoryOk = false;
    if (mode == 2) { fs.marker().node = Node::Regular; fs.marker().data.assign(12, 0); }
    if (mode == 3) fs.marker().node = Node::Other;
    if (mode == 4) fs.markerMetaError = true;
    Store store(fs); assert(store.prepare() != Error::Ok && fs.writeOpens == 0 && fs.removes == 0);
    assert(store.begin() == Error::InvalidState);
  }
  FakeBackend fs; fs.validMarker(); Store store(fs);
  assert(store.prepare(1024) == Error::Ok);
  fs.marker().data[0] ^= 1;
  assert(store.begin() == Error::MarkerInvalid && fs.removes == 0);
  assert(store.discard() == Error::MarkerInvalid);
}
static void markerCommitFailures() {
  for (int mode = 0; mode < 6; ++mode) {
    FakeBackend fs;
    if (mode == 0) fs.markerWriteShort = true;
    if (mode == 1) fs.markerFlushError = true;
    if (mode == 2) fs.closeError = true;
    if (mode == 3) fs.corruptMarkerOnFlush = true;
    if (mode == 4) fs.markerReadError = true;
    if (mode == 5) fs.markerOpenError = true;
    Store store(fs); assert(store.prepare(1024) != Error::Ok);
    assert(fs.randomCalls == 1 && fs.removes == 0 && fs.liveFiles == 0);
    assert(fs.files.count(kMarkerPath) == 0); // A failed staged commit never half-writes OWNER.
  }
}
static void incompleteTakesAndRemoval() {
  const auto bytes = pcm();
  for (int mode = 0; mode < 11; ++mode) {
    FakeBackend fs; Store store(fs); assert(store.prepare(2048) == Error::Ok && store.begin() == Error::Ok);
    const std::string path = store.scratchPath();
    assert(store.append(bytes.data(), bytes.size()) == Error::Ok);
    if (mode == 0) { fs.scratchWriteShort = true; assert(store.append(bytes.data(), bytes.size()) == Error::WriteFailed); }
    if (mode == 1) { fs.scratchFlushError = true; assert(store.finish() == Error::FlushFailed); }
    if (mode == 2) { fs.closeError = true; assert(store.finish() == Error::CloseFailed); }
    if (mode == 3) { fs.truncateScratchOnClose = true; assert(store.finish() == Error::SizeMismatch); }
    if (mode == 4) assert(store.append(bytes.data(), 3) == Error::InvalidInput);
    if (mode == 5) assert(store.append(bytes.data(), kAppendBytes + 2) == Error::InvalidInput);
    if (mode == 6) { fs.spaceOk = false; assert(store.append(bytes.data(), bytes.size()) == Error::MetadataReadFailed); }
    if (mode == 7) { fs.scratchOpenError = true; assert(store.finish() == Error::ReadFailed); }
    if (mode == 8) { fs.scratchMetaError = true; assert(store.finish() == Error::MetadataReadFailed); }
    if (mode == 9) { fs.present = false; assert(store.append(bytes.data(), bytes.size()) == Error::MetadataReadFailed); }
    if (mode == 10) { fs.present = false; assert(store.finish() == Error::FlushFailed); }
    Error error; assert(!store.openReader(error) && error == Error::InvalidState);
    assert(fs.liveFiles == 0);
    fs.closeError = false; fs.spaceOk = true; fs.scratchFlushError = false; fs.present = true;
    fs.scratchOpenError = fs.scratchMetaError = false;
    assert(store.discard() == Error::Ok && fs.files.count(path) == 0);
  }
}
static void boundsSpaceAndCollisions() {
  const auto bytes = pcm(); FakeBackend fs; Store store(fs);
  assert(store.prepare() == Error::Ok && store.capacityBytes() == kMaxRecordingBytes);
  assert(store.prepare(kMaxRecordingBytes + 2) == Error::InsufficientSpace);
  assert(store.prepare(1024) == Error::Ok && store.begin() == Error::Ok);
  assert(store.append(bytes.data(), bytes.size()) == Error::Ok && store.append(bytes.data(), 2) == Error::InsufficientSpace);
  assert(store.finish() == Error::InvalidState && store.discard() == Error::Ok);
  assert(store.prepare(2048) == Error::Ok && store.begin() == Error::Ok);
  fs.external = fs.total - fs.marker().data.size() - kReserveBytes - 100;
  assert(store.append(bytes.data(), bytes.size()) == Error::InsufficientSpace && store.recordedBytes() == 0);
  assert(store.discard() == Error::Ok);
  fs.external = 0; assert(store.prepare(1) == Error::InvalidInput);
  fs.total = kReserveBytes; assert(store.prepare() == Error::InsufficientSpace);
  FakeBackend collision; collision.validMarker(); collision.collisions = 2; Store unique(collision);
  assert(unique.prepare(1024) == Error::Ok && unique.begin() == Error::Ok && collision.randomCalls == 3);
  assert(unique.discard() == Error::Ok && collision.files.size() == 3); // marker + two untouched collisions.
  for (const auto& entry : collision.files) if (isScratchPath(entry.first.c_str())) assert(entry.second.data == std::vector<uint8_t>(10, 77));
  FakeBackend exhausted; exhausted.validMarker(); exhausted.collisions = 8; Store unable(exhausted);
  assert(unable.prepare() == Error::Ok && unable.begin() == Error::OpenFailed && exhausted.writeOpens == 0);
  FakeBackend random; random.validMarker(); random.randomOk = false; Store noRandom(random);
  assert(noRandom.prepare() == Error::Ok && noRandom.begin() == Error::RandomFailed);
}
static void readerLifetimeAndNoSweep() {
  FakeBackend fs; const auto bytes = pcm();
  std::unique_ptr<official_voice::Reader> reader; std::string oldPath;
  {
    Store store(fs); assert(store.prepare(1024) == Error::Ok && store.begin() == Error::Ok);
    oldPath = store.scratchPath();
    assert(store.append(bytes.data(), bytes.size()) == Error::Ok && store.finish() == Error::Ok);
    fs.scratchOpenError = true; Error error;
    assert(!store.openReader(error) && error == Error::ReadFailed && !store.readerActive());
    fs.scratchOpenError = false; reader = store.openReader(error); assert(reader && error == Error::Ok);
    fs.present = false; uint8_t out[4]{}; assert(reader->read(out, sizeof(out)) == 0);
    reader.reset(); assert(!store.readerActive());
    fs.present = true; reader = store.openReader(error); assert(reader);
  }
  std::array<uint8_t, 1024> out{}; assert(reader->read(out.data(), out.size()) == out.size() && out == bytes);
  reader.reset(); assert(fs.liveFiles == 0 && fs.removes == 0);
  Store reboot(fs); assert(reboot.prepare(1024) == Error::Ok && reboot.begin() == Error::Ok);
  assert(oldPath != reboot.scratchPath() && fs.files[oldPath].data.size() == 1024);
  assert(reboot.append(bytes.data(), bytes.size()) == Error::Ok && reboot.finish() == Error::Ok);
  fs.removeError = true; assert(reboot.discard() == Error::RemoveFailed);
  fs.removeError = false; assert(reboot.discard() == Error::Ok && fs.files[oldPath].data.size() == 1024);
  assert(reboot.prepare() == Error::Ok && reboot.begin() == Error::Ok);
  assert(reboot.append(bytes.data(), bytes.size()) == Error::Ok && reboot.finish() == Error::Ok);
  fs.files.erase(kMarkerPath); assert(reboot.prepare() == Error::MarkerMissing && fs.files.count(kMarkerPath) == 0);
}
static void safePaths() {
  assert(isScratchPath("/EKKO/TMP/REC-0123456789abcdef0123456789abcdef.PCM"));
  const char* invalid[] = {nullptr, "", "/EKKO/TMP/OWNER", "/RECORD/last.wav", "/EKKO/TMP/REC-1.PCM", "/EKKO/TMP/REC-0123456789abcdef0123456789abcdef.PCM/../x", "/EKKO/TMP/REC-0123456789abcdef0123456789abcdeg.PCM"};
  for (const char* path : invalid) assert(!isScratchPath(path));
  const char* pending = "/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdef.NEW";
  const char* backup = "/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdef.BAK";
  assert(isMarkerNewPath(pending) && isMarkerBackupPath(backup));
  assert(isMarkerRenamePair(kMarkerPath, backup) && isMarkerRenamePair(pending, kMarkerPath));
  const char* sources[] = {nullptr, kMarkerPath, pending, backup, "/RECORD/last.wav", "/EKKO/TMP/OWNER-1.NEW"};
  const char* destinations[] = {nullptr, kMarkerPath, pending, backup, "/RECORD/last.wav", "/EKKO/TMP/OWNER-1.BAK"};
  for (const char* from : sources) {
    for (const char* to : destinations) {
      const bool expected = from && to && ((!std::strcmp(from, kMarkerPath) && !std::strcmp(to, backup)) ||
                            (!std::strcmp(from, pending) && !std::strcmp(to, kMarkerPath)));
      assert(isMarkerRenamePair(from, to) == expected);
    }
  }
  const char* badMarkers[] = {"/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdef.NEW/../OWNER",
                            "/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdeg.NEW",
                            "/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdef.new",
                            "/EKKO/TMP/OWNER-0123456789abcdef0123456789abcdef.BAK/OWNER"};
  for (const char* path : badMarkers) assert(!isMarkerNewPath(path) && !isMarkerBackupPath(path));
}
static void markerDiagnostics() {
  static_assert(std::is_pod<MarkerDiagnostics>::value, "marker diagnostics must remain a fixed POD");
  const size_t expected = sizeof(kMarkerContent) - 1;
  assert(expected == 26);
  for (const size_t length : {size_t(1), expected - 1, expected + 1}) {
    FakeBackend fs; fs.marker().node = Node::Regular; fs.marker().data.assign(length, 0);
    const auto original = fs.marker().data;
    Store store(fs); assert(!store.markerDiagnostics().checked);
    assert(store.prepare() == Error::MarkerInvalid);
    const auto d = store.markerDiagnostics();
    assert(d.checked && d.node == Node::Regular && d.statBytes == length);
    assert(!d.openedLengthChecked && !d.openedLengthMatches && !d.contentChecked && !d.contentMatches);
    assert(fs.marker().data == original && fs.writeOpens == 0 && fs.removes == 0);
  }
  {
    FakeBackend fs; fs.marker().node = Node::Other;
    Store store(fs); assert(store.prepare() == Error::MarkerInvalid);
    const auto d = store.markerDiagnostics();
    assert(d.checked && d.node == Node::Other && !d.openedLengthChecked && !d.contentChecked);
    assert(fs.writeOpens == 0 && fs.removes == 0);
  }
  for (int mode = 0; mode < 6; ++mode) {
    FakeBackend fs; fs.validMarker();
    if (mode == 0) fs.marker().data[0] ^= 1;
    if (mode == 1) fs.markerReadError = true;
    if (mode == 2) fs.markerLengthError = true;
    if (mode == 3) fs.markerLengthMismatch = true;
    if (mode == 4) fs.markerOpenError = true;
    const auto original = fs.marker().data;
    Store store(fs);
    const Error expectedError = mode == 0 ? Error::MarkerInvalid : mode == 5 ? Error::Ok : Error::MarkerReadFailed;
    assert(store.prepare() == expectedError);
    const auto d = store.markerDiagnostics();
    assert(d.checked && d.node == Node::Regular && d.statBytes == expected);
    assert(d.openedLengthChecked == (mode != 4));
    assert(d.openedLengthMatches == (mode != 2 && mode != 3 && mode != 4));
    assert(d.contentChecked == (mode == 0 || mode == 5));
    assert(d.contentMatches == (mode == 5));
    assert(fs.marker().data == original && fs.writeOpens == 0 && fs.removes == 0 && fs.liveFiles == 0);
    // A fresh prepare that fails before validation must not retain old success.
    fs.present = false; assert(store.prepare() == Error::MountFailed);
    const auto reset = store.markerDiagnostics();
    assert(!reset.checked && reset.statBytes == 0 && !reset.openedLengthChecked && !reset.contentChecked);
  }
  {
    FakeBackend fs; fs.validMarker(); Store store(fs);
    assert(store.prepare() == Error::Ok && store.markerDiagnostics().contentMatches);
    fs.marker().data.clear();
    assert(store.begin() == Error::MarkerInvalid);
    const auto d = store.markerDiagnostics();
    assert(d.checked && d.statBytes == 0 && !d.openedLengthChecked && !d.contentChecked);
    assert(fs.writeOpens == 0 && fs.removes == 0);
  }
}
static void emptyMarkerRecovery() {
  static_assert(std::is_pod<RecoveryDiagnostics>::value, "recovery diagnostics must remain a fixed POD");
  FakeBackend fs; fs.marker().node = Node::Regular;
  auto& foreign = fs.files["/EKKO/TMP/do-not-touch.txt"];
  foreign.node = Node::Regular; foreign.data = {17, 29, 43};
  const auto original = foreign.data;
  Store store(fs);
  assert(store.prepare(1024) == Error::Ok);
  assert(fs.marker().data == std::vector<uint8_t>(kMarkerContent, kMarkerContent + sizeof(kMarkerContent) - 1));
  size_t backups = 0;
  for (const auto& entry : fs.files) {
    if (entry.first.size() > 4 && entry.first.substr(entry.first.size() - 4) == ".BAK") {
      ++backups; assert(entry.second.node == Node::Regular && entry.second.data.empty());
    }
  }
  assert(backups == 1 && fs.removes == 0 && fs.files["/EKKO/TMP/do-not-touch.txt"].data == original);
  assert(std::find(fs.opened.begin(), fs.opened.end(), kMarkerPath) == fs.opened.end());
  const auto recovery = store.recoveryDiagnostics();
  assert(recovery.emptyChecked && recovery.emptyConfirmed && recovery.emptyBackupPreserved && recovery.newMarkerPublished);
  assert(store.markerDiagnostics().contentMatches && fs.newReadbackVerified && fs.liveFiles == 0);
  // Ordinary validation must not wipe the completed recovery progress.
  assert(store.begin() == Error::Ok && store.recoveryDiagnostics().emptyBackupPreserved);
  assert(store.discard() == Error::Ok && store.recoveryDiagnostics().newMarkerPublished);
  assert(store.prepare(1024) == Error::Ok);
  const auto reset = store.recoveryDiagnostics();
  assert(!reset.emptyChecked && !reset.emptyConfirmed && !reset.emptyBackupPreserved && !reset.newMarkerPublished);
  assert(fs.files.size() == 3); // Permanent backup, OWNER and the untouched file.
}
static size_t countRole(const FakeBackend& fs, bool backup) {
  size_t count = 0;
  for (const auto& entry : fs.files) if (backup ? isMarkerBackupPath(entry.first.c_str()) : isMarkerNewPath(entry.first.c_str())) ++count;
  return count;
}
static void assertNoOwnerWrites(const FakeBackend& fs) {
  for (const auto& path : fs.opened) assert(isMarkerNewPath(path.c_str()) || isScratchPath(path.c_str()));
}
static void emptyRequiresIndependentRead() {
  for (int mode = 0; mode < 8; ++mode) {
    FakeBackend fs; fs.marker().node = Node::Regular;
    if (mode == 0) { fs.marker().data.assign(26, 91); fs.markerStatZero = true; }
    if (mode == 1) { fs.marker().data.assign(1, 91); fs.markerStatZero = fs.markerLengthZero = true; }
    if (mode == 2) fs.fault = "owner.read_open";
    if (mode == 3) fs.fault = "owner.length";
    if (mode == 4) fs.fault = "owner.read";
    if (mode == 5) fs.fault = "owner.read_close";
    if (mode == 6) fs.markerLengthMismatch = true;
    if (mode == 7) { fs.fault = "owner.read"; fs.detachOnFault = true; }
    const auto original = fs.marker().data;
    Store store(fs); assert(store.prepare() != Error::Ok);
    const auto d = store.recoveryDiagnostics();
    assert(d.emptyChecked && !d.emptyConfirmed && !d.emptyBackupPreserved && !d.newMarkerPublished);
    assert(fs.marker().data == original && fs.writeOpens == 0 && fs.renames == 0 && fs.removes == 0 && fs.liveFiles == 0);
  }
  // Any non-empty unknown marker, including an otherwise plausible prefix,
  // remains exactly where it was; recovery is never attempted.
  for (size_t length = 1; length <= 28; ++length) {
    FakeBackend fs; fs.marker().node = Node::Regular;
    fs.marker().data.assign(length, 'E'); const auto original = fs.marker().data;
    Store store(fs); assert(store.prepare() == Error::MarkerInvalid);
    assert(!store.recoveryDiagnostics().emptyChecked && fs.marker().data == original);
    assert(fs.writeOpens == 0 && fs.renames == 0 && fs.removes == 0);
  }
}
static void recoveryFaultMatrix() {
  const char* faults[] = {"backup.rename", "new.write_open", "new.write", "new.flush", "new.write_close",
                         "new.stat", "new.read_open", "new.length", "new.read", "new.read_close", "publish.rename",
                         "final.stat", "final.read_open", "final.length", "final.read", "final.read_close"};
  for (const char* fault : faults) for (const bool detached : {false, true}) {
    FakeBackend fs; fs.marker().node = Node::Regular; fs.fault = fault; fs.detachOnFault = detached;
    if (fs.fault == "new.stat") fs.faultSkip = 1; // Initial missing-name probe succeeds; independent reopen then fails.
    auto& foreign = fs.files["/RECORD/leave.wav"]; foreign.node = Node::Regular; foreign.data.assign(44, 13);
    const auto original = foreign.data;
    Store store(fs); assert(store.prepare() != Error::Ok);
    const auto d = store.recoveryDiagnostics();
    assert(d.emptyChecked && d.emptyConfirmed);
    const bool backupFailed = fs.fault == "backup.rename";
    const bool finalFailed = fs.fault.compare(0, 6, "final.") == 0;
    assert(d.emptyBackupPreserved == !backupFailed && d.newMarkerPublished == finalFailed);
    assert(countRole(fs, true) == (backupFailed ? 0 : 1));
    if (backupFailed) assert(fs.files.at(kMarkerPath).data.empty());
    else if (!finalFailed) assert(fs.files.count(kMarkerPath) == 0);
    else assert(fs.files.at(kMarkerPath).data.size() == sizeof(kMarkerContent) - 1);
    assert(fs.files.at("/RECORD/leave.wav").data == original && fs.removes == 0 && fs.liveFiles == 0);
    assertNoOwnerWrites(fs);
  }
  for (int mode = 0; mode < 4; ++mode) {
    FakeBackend fs; fs.marker().node = Node::Regular;
    if (mode == 0) fs.markerWriteShort = true;
    if (mode == 1) fs.corruptMarkerOnFlush = true;
    if (mode == 2) fs.corruptAfterPublish = true;
    if (mode == 3) fs.total = kReserveBytes;
    Store store(fs); assert(store.prepare() != Error::Ok);
    assert(countRole(fs, true) == 1 && fs.removes == 0 && fs.liveFiles == 0);
    assert(store.recoveryDiagnostics().emptyBackupPreserved);
    assert(store.recoveryDiagnostics().newMarkerPublished == (mode == 2));
    if (mode != 2) assert(fs.files.count(kMarkerPath) == 0);
    assertNoOwnerWrites(fs);
  }
}
static void markerCollisionsAndRestarts() {
  for (const unsigned collisions : {2u, 8u}) {
    FakeBackend fs; fs.marker().node = Node::Regular; fs.backupCollisions = collisions;
    Store store(fs);
    assert((store.prepare() == Error::Ok) == (collisions < 8));
    assert(countRole(fs, true) == collisions + (collisions < 8 ? 1 : 0));
    for (const auto& entry : fs.files) if (isMarkerBackupPath(entry.first.c_str()) && !entry.second.data.empty()) {
      assert(entry.second.data == std::vector<uint8_t>(5, 53));
    }
    assert(fs.removes == 0 && fs.renames <= 8 + 1);
  }
  for (const unsigned collisions : {2u, 8u}) {
    FakeBackend fs; fs.newCollisions = collisions; Store store(fs);
    assert((store.prepare() == Error::Ok) == (collisions < 8));
    assert(countRole(fs, false) == collisions);
    for (const auto& entry : fs.files) if (isMarkerNewPath(entry.first.c_str())) assert(entry.second.data == std::vector<uint8_t>(7, 81));
    assert(fs.randomCalls <= 8 && fs.removes == 0);
    assertNoOwnerWrites(fs);
  }
  {
    FakeBackend fs; fs.marker().node = Node::Regular; fs.ownerAppearsOnPublish = true; Store store(fs);
    assert(store.prepare() == Error::OwnershipConflict);
    assert(fs.files.at(kMarkerPath).data == std::vector<uint8_t>(9, 65));
    assert(countRole(fs, true) == 1 && countRole(fs, false) == 1 && fs.removes == 0);
  }
  for (const unsigned after : {0u, 1u}) {
    FakeBackend fs; fs.marker().node = Node::Regular; fs.randomFailAfter = after; Store store(fs);
    assert(store.prepare() == Error::RandomFailed);
    assert(countRole(fs, true) == after && fs.removes == 0);
    assert(store.recoveryDiagnostics().emptyBackupPreserved == (after == 1));
  }
  // Errors can occur after a filesystem mutation; never infer rollback or
  // remove retained names merely because rename returned an error.
  for (int mode = 0; mode < 3; ++mode) {
    FakeBackend fs; fs.marker().node = Node::Regular;
    if (mode == 0) fs.backupMutatesThenFails = true;
    if (mode == 1) fs.publishMutatesThenFails = true;
    if (mode == 2) fs.fault = "new.flush";
    { Store store(fs); assert(store.prepare() != Error::Ok); }
    const auto before = fs.files;
    fs.backupMutatesThenFails = fs.publishMutatesThenFails = false; fs.fault.clear();
    Store reboot(fs); assert(reboot.prepare() == Error::Ok);
    for (const auto& entry : before) if (entry.first != kMarkerPath) {
      const auto found = fs.files.find(entry.first);
      assert(found != fs.files.end() && found->second.node == entry.second.node && found->second.data == entry.second.data);
    }
    assert(countRole(fs, true) == 1 && fs.removes == 0);
  }
}
static void recoveryNeverMutatesAnActiveTake() {
  FakeBackend fs; fs.validMarker(); const auto data = pcm(); Store store(fs);
  assert(store.prepare() == Error::Ok && store.begin() == Error::Ok);
  assert(store.append(data.data(), data.size()) == Error::Ok && store.finish() == Error::Ok);
  Error result; auto reader = store.openReader(result); assert(reader && result == Error::Ok);
  fs.marker().data.clear(); const auto renames = fs.renames; const auto writes = fs.writeOpens;
  assert(store.prepare() == Error::Busy && !store.recoveryDiagnostics().emptyChecked);
  assert(fs.renames == renames && fs.writeOpens == writes);
  reader.reset();
  // Even after the reader closes, do not re-establish ownership over a card
  // whose marker disappeared while this Store still owns a take.
  assert(store.prepare() == Error::MarkerInvalid && !store.recoveryDiagnostics().emptyChecked);
  assert(fs.renames == renames && fs.writeOpens == writes && fs.removes == 0);
}
int main() {
  emptyMarkerRecovery();
  happyPathAndBusyReader(); ownershipAndDirectoryFailures(); markerCommitFailures();
  incompleteTakesAndRemoval(); boundsSpaceAndCollisions(); readerLifetimeAndNoSweep(); safePaths(); markerDiagnostics();
  emptyRequiresIndependentRead(); recoveryFaultMatrix(); markerCollisionsAndRestarts(); recoveryNeverMutatesAnActiveTake();
  std::printf("SD recording store passed: staged NEW commit, retained empty BAK, no overwrite/sweep, fault matrix, bounded PCM, reader lifetime; state=%zu bytes\n", sizeof(Store));
}
