#include <Arduino.h>
#include <ArduinoJson.h>
#include "CardputerHardware.h"
#include "OfficialStudioProtocol.h"
#include "OfficialStudioConnection.h"
#include "UiLocalization.h"
#include "FusionWebFont.h"
#include "OfficialStudioModels.h"
#include "OfficialStudioIncoming.h"
#include "OfficialStudioSelection.h"
#include "OfficialStudioSelectionNotice.h"
#include "OfficialStudioHistory.h"
#include "OfficialVoiceJob.h"
#include "RamRecordingStore.h"
#include "MicCaptureQueue.h"
#include "OfficialSpeechPlan.h"
#include "OfficialRunGuard.h"
#include "OfficialRunFailureNotice.h"
#include "OfficialSessionStore.h"
#include "WebSocketFrameReader.h"
#include "ChatBrowser.h"
#include "ChatStatus.h"
#include "FactoryFirmware.h"
#include "SharedWifiConfig.h"
#include "RemoteFallback.h"
#include "WifiRecovery.h"
#include "McuReauth.h"
#include "DeviceWebTarget.h"
#include "McuIdentity.h"
#include "NativeTlsTrust.h"
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>
#include <vector>
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "nvs.h"

namespace {
#ifndef HERMES_MCU_FIRMWARE_VERSION
#define HERMES_MCU_FIRMWARE_VERSION "cardputer-adv-v9.0.0-dev.10-ram-lan"
#endif
#ifndef HERMES_MCU_FIRMWARE_MANIFEST_PATH
#define HERMES_MCU_FIRMWARE_MANIFEST_PATH "/api/studio/mcu/firmware/cardputer-adv/manifest"
#endif
#ifndef HERMES_ES8311_DAC_VOLUME
#define HERMES_ES8311_DAC_VOLUME 0xBF
#endif
#ifndef HERMES_DEFAULT_OUTPUT_VOLUME_PERCENT
#define HERMES_DEFAULT_OUTPUT_VOLUME_PERCENT 80
#endif
#ifndef HERMES_MAX_OUTPUT_VOLUME_PERCENT
#define HERMES_MAX_OUTPUT_VOLUME_PERCENT 100
#endif

constexpr char kApName[] = "Ekko Studio-WIFI";
constexpr char kMcuFirmwareVersion[] = HERMES_MCU_FIRMWARE_VERSION;
constexpr char kMcuFirmwareManifestPath[] = HERMES_MCU_FIRMWARE_MANIFEST_PATH;
constexpr uint32_t kConnectTimeoutMs = 18000;
constexpr uint32_t kMcuOtaFirstCheckMs = 30000;
constexpr uint32_t kMcuOtaIntervalMs = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kMcuOtaRetryMs = 5UL * 60UL * 1000UL;
constexpr bool kForceSetupAp = false;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
constexpr char kMissingSttPromptPcmUrl[] =
    "/api/studio/mcu/audio/missing-stt-24k.s16le.pcm";
constexpr char kNoDevicePromptPcmUrl[] =
    "/api/studio/mcu/audio/no-device-24k.s16le.pcm";
constexpr char kTokenInvalidPromptPcmUrl[] =
    "/api/studio/mcu/audio/token-invalid-24k.s16le.pcm";
constexpr float kBatteryDividerRatio = 2.0f;
constexpr uint8_t kDefaultOledAddr = 0x3C;
constexpr uint8_t kAltOledAddr = 0x3D;
constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;
constexpr size_t kOledBufferBytes = (kOledWidth * kOledHeight) / 8;
constexpr int kMcuSubtitleMaxWidth = 124;
constexpr uint8_t kMcuSubtitleLinesPerPage = 3;
constexpr uint32_t kOledI2cClockHz = 400000;
constexpr uint32_t kCodecI2cClockHz = 100000;
constexpr uint32_t kOledRefreshIntervalMs = 160;
constexpr uint32_t kOledSuccessReturnDelayMs = 2500;
constexpr uint32_t kOledErrorReturnDelayMs = 6000;
constexpr uint32_t kProvisionRestartDelayMs = 2500;
constexpr uint32_t kProvisionRedirectDelayMs = 6500;
constexpr int kMaxScannedNetworks = 20;
constexpr int kMaxWifiHistoryNetworks = 5;
constexpr int kMaxWifiCandidates = kMaxWifiHistoryNetworks + 1;
constexpr uint16_t kLanDiscoveryLocalPort = 48630;
constexpr uint16_t kHermesDiscoveryPort = 48640;
constexpr uint32_t kLanDiscoveryTimeoutMs = 1200;
constexpr uint32_t kLanDiscoveryStaleMs = 30000;
constexpr int kMaxLanDevices = 8;
constexpr int kMaxManualDevices = 8;
constexpr uint32_t kMcuLoginTimeoutMs = 8000;
constexpr uint32_t kMcuRemoteTlsHandshakeTimeoutSec = 15;
constexpr uint32_t kMcuRemoteHttpTimeoutMs = 20000;
constexpr uint32_t kMcuRemoteAgentAckTimeoutMs = 20000;
constexpr uint32_t kMcuRemoteLoginVerifyTimeoutMs = 60000;
constexpr uint32_t kMcuSocketReconnectMs = 3000;
constexpr uint32_t kMcuSocketReconnectMaxMs = 30000;
const char kRemoteDeviceLookupUrl[] = "https://api.ekkostudio.xyz";
constexpr int kMaxProfiles = 8;
constexpr int kMaxMcuAudioQueue = 4;
constexpr uint32_t kMcuInteractionIdleDelayMs = 3500;
constexpr uint32_t kMcuFailureIdleDelayMs = 20000;
constexpr uint8_t kDefaultIdlePowerSaveMinutes = 3;
constexpr uint8_t kMaxIdlePowerSaveMinutes = 60;
constexpr uint32_t kMcuAudioDefaultDurationMs = 1800;
constexpr uint32_t kMcuAudioMaxDurationMs = 9000;
constexpr uint32_t kMcuAudioHttpTimeoutMs = 30000;
constexpr uint32_t kMcuAudioPrebufferMs = 240;
constexpr uint32_t kMcuAudioPrebufferTimeoutMs = 2500;
constexpr size_t kMcuAudioPrebufferMaxBytes = 16 * 1024;
constexpr uint32_t kMcuAudioDrainMinMs = 180;
constexpr uint32_t kMcuAudioDrainMaxMs = 650;
constexpr uint32_t kMcuAudioTailSilenceMs = 120;
constexpr size_t kMcuAdpcmHeaderBytes = 20;
constexpr size_t kMcuAdpcmReadChunkBytes = 256;
constexpr size_t kMcuAdpcmOutputFrames = 256;
constexpr uint32_t kBootDebounceMs = 80;
constexpr uint32_t kBootInputArmDelayMs = 2500;
constexpr uint32_t kBootLongPressMs = 360;
constexpr uint32_t kBootDoubleClickMs = 320;
constexpr uint32_t kVoiceStreamReleaseDebounceMs = 240;
constexpr uint32_t kWifiDisconnectGraceMs = 8000;
constexpr uint32_t kBatteryReadIntervalMs = 5000;
constexpr bool kAutoOtaEnabled = false;
constexpr uint32_t kVoiceRecordMs = 4000;
constexpr uint32_t kVoiceRecordMinMs = 300;
constexpr uint32_t kVoiceRecordHardTimeoutMs = 125000;
constexpr uint32_t kVoiceVadRmsStart = 190;
constexpr uint32_t kVoiceVadPeakStart = 480;
constexpr uint32_t kVoiceVadActiveThreshold = 260;
constexpr uint32_t kVoiceVadMinActiveSamples = 16;
constexpr uint32_t kListeningSilenceStopMs = 1000;
constexpr uint32_t kListeningMaxRecordMs = 30000;
constexpr uint32_t kListeningRetriggerDelayMs = 1200;
constexpr uint32_t kListeningAfterCompletionDelayMs = 1000;
// Keep enough free heap for the remote TLS/WebSocket transport while listening.
constexpr size_t kListeningPreRollFrames = 4096;
constexpr uint32_t kListeningVadRmsStart = 420;
constexpr uint32_t kListeningVadPeakStart = 1200;
constexpr uint32_t kListeningVadActiveThreshold = 520;
constexpr uint32_t kListeningVadMinActiveSamples = 24;
constexpr uint32_t kListeningVadMinCandidateMs = 450;
constexpr uint8_t kListeningVadMinSpeechWindows = 36;
constexpr uint8_t kListeningVadMaxQuietWindows = 63;
constexpr int kVoiceInputGainPermille = 1800;
constexpr int kAudioSampleRate = 24000;
constexpr int kVoiceInputSampleRate = 16000;
constexpr int kMcuAudioDefaultSampleRate = 24000;
// Keep the reusable ADPCM chunk small enough to avoid fragmented-heap failures.
constexpr size_t kVoiceStreamChunkFrames = 1024;
constexpr size_t kVoiceStreamAdpcmMaxBytes = kMcuAdpcmHeaderBytes + ((kVoiceStreamChunkFrames + 1) / 2);
constexpr size_t kVoiceRecordMaxFrames = (kVoiceInputSampleRate * kVoiceRecordMs) / 1000UL;
constexpr size_t kVoiceRecordBufferBytes = 44 + kVoiceRecordMaxFrames * sizeof(int16_t);
constexpr uint8_t kDefaultOutputVolumePercent = HERMES_DEFAULT_OUTPUT_VOLUME_PERCENT;
constexpr uint8_t kMaxOutputVolumePercent = HERMES_MAX_OUTPUT_VOLUME_PERCENT;
constexpr int32_t kVoiceBoostKnee = 18000;
constexpr int32_t kVoiceBoostCeiling = 28000;
constexpr uint8_t kEs8311DacVolume = HERMES_ES8311_DAC_VOLUME;

Preferences prefs;
const char* factoryWifiSource = "none";
const char* connectedWifiSource = "none";
bool sharedWifiSaveFailed = false;
esp_err_t factoryBootError = ESP_OK;
class DeviceWebServer:public WebServer {
 public:
  using WebServer::WebServer;
  // Inspection accepts no HTTP bytes. Stop idle listening before invoking
  // the base class's synchronous header parser, including for slow clients.
  bool hasPendingManagement() {return bool(_currentClient) || _server.hasClient();}
};
DeviceWebServer server(80);
WiFiUDP lanUdp;
WiFiClient mcuWsPlainClient;
WiFiClientSecure mcuWsSecureClient;
WiFiClient *mcuWsClient = &mcuWsPlainClient;
bool wifiReady = false;
bool setupApMode = false;
bool oledFound = false;
bool oledReady = false;
bool oledDirty = true;
bool lanUdpReady = false;
bool wsReady = false;
bool mcuSocketConnected = false;
bool mcuSocketNamespaceReady = false;
bool mcuStatusNamespaceReady = false;
uint32_t statusReconnectAtMs=0,statusReconnectDelayMs=3000;
official_connection::Namespaces officialNamespaces;
bool restartPending = false;
bool audioBusy = false;
bool paEnabled = false;
bool i2sReady = false;
bool es8311Found = false;
bool es8311Ready = false;
bool bootWasPressed = false;
bool bootLongPressHandled = false;
bool bootClickPending = false;
bool bootSecondClickStarted = false;
bool bootInputArmed = false;
bool idlePowerSaveActive = false;
bool listeningModeEnabled = false;
NativeAgentSelection agentSelection;
NativeConversationView conversationView;
NativeChatModel chatModel;
String chatNotice, chatHistoryRequest, chatPendingSend, chatSentAgent, chatSentProfile, chatSentBinding;
String officialAccount, officialBinding, officialSession, officialRun, officialLiveId;
class OfficialSessionNvs final : public official_sessions::Storage {
 public:
  official_sessions::ReadStatus read(const char* key,char* out,size_t capacity,size_t& bytes) override {
    using official_sessions::ReadStatus;bytes=0;nvs_handle_t handle=0;
    const auto opened=nvs_open("ekkoChats",NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND) return ReadStatus::Missing;
    if(opened!=ESP_OK) return ReadStatus::Error;
    size_t count=0;auto status=nvs_get_str(handle,key,nullptr,&count);
    if(status==ESP_ERR_NVS_NOT_FOUND) {nvs_close(handle);return ReadStatus::Missing;}
    if(status!=ESP_OK || !count || count>capacity) {nvs_close(handle);return ReadStatus::Error;}
    status=nvs_get_str(handle,key,out,&count);nvs_close(handle);
    if(status!=ESP_OK || !count || out[count-1]!=0) return ReadStatus::Error;
    bytes=count-1;return ReadStatus::Found;
  }
  bool writeCommit(const char* key,const char* value,size_t bytes) override {
    if(!value || bytes!=official_sessions::kRecordBytes || value[bytes]) return false;
    nvs_handle_t handle=0;if(nvs_open("ekkoChats",NVS_READWRITE,&handle)!=ESP_OK) return false;
    auto status=nvs_set_str(handle,key,value);if(status==ESP_OK) status=nvs_commit(handle);
    nvs_close(handle);return status==ESP_OK;
  }
  bool random(uint8_t* out,size_t bytes) override {if(!out||!bytes)return false;esp_fill_random(out,bytes);return true;}
};
OfficialSessionNvs officialSessionNvs;
official_sessions::Store officialSessionStore(officialSessionNvs);
official_studio_models::Selection officialModel;
official_studio::Mode officialMode = official_studio::Mode::Scoped;
official_studio::NativeReadiness officialNativeReadiness;
official_run_guard::Guard officialRunGuard;
official_selection::Job officialSelectionJob;
official_history::Job officialHistoryJob;
String officialHistoryMessageId, officialHistoryRequestId;
uint32_t officialHistoryRequestPage=0, officialHistoryRequestRevision=0, officialHistoryRevision=0;
int8_t officialHistoryRequestDirection=1;
uint32_t officialSelectionNotBeforeMs = 0;
uint8_t officialSelectionRetries = 0;
official_incoming::Window officialLiveText;
bool officialSelectionQueued = false, officialHistoryQueued = false;
bool officialSendRunObserved = false, officialHistorySkippingTools = false;
bool officialSendWasDraft = true;
official_voice_job::Job officialVoiceJob;
bool officialVoiceOutputPlaying=false;
ram_recording::Error officialRecordingPrepareResult=ram_recording::Error::Ok;
ram_recording::Error officialRecordingBeginResult=ram_recording::Error::Ok;
ram_recording::Store officialRecordingStore;
constexpr uint32_t kOfficialVoiceHeapCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
uint32_t officialRecordingHeapStart=0,officialRecordingHeapMinimum=0,officialRecordingLargestMinimum=0;
uint32_t officialRecordingWorkerFree=0,officialRecordingWorkerLargest=0;
uint8_t officialRecordingAdmissionError=0;
size_t officialRecordingLastPcmBytes=0,officialRecordingLastEncodedBytes=0;
void sampleOfficialRecordingMemory() {
  const uint32_t free=heap_caps_get_free_size(kOfficialVoiceHeapCaps),largest=heap_caps_get_largest_free_block(kOfficialVoiceHeapCaps);
  if(!officialRecordingHeapStart) officialRecordingHeapStart=free;
  if(!officialRecordingHeapMinimum || free<officialRecordingHeapMinimum) officialRecordingHeapMinimum=free;
  if(!officialRecordingLargestMinimum || largest<officialRecordingLargestMinimum) officialRecordingLargestMinimum=largest;
}
bool officialVoiceCleanupPending=false, officialVoiceDrainPending=false;
bool officialRecordingRequested=false;
uint32_t officialVoiceRate=0,officialVoiceTailFrames=0,officialVoiceDrainAtMs=0,officialVoiceTailAtMs=0;
uint32_t officialVoiceEpoch=0;
official_voice_job::Output officialVoiceCompletion;
int16_t officialStereoPcm[official_voice_job::kPcmSamples*2]{};
official_speech::Plan officialSpeechPlan;
official_speech::Request officialSpeechHistoryRequest;
bool officialHistoryForSpeech=false,officialSpeechNeedsFreshTotal=false,officialVoiceCancelled=false;
uint32_t officialSpeechBaseline=0,officialSpeechTotal=0,officialSpeechCompletedAtMs=0;


bool officialResumeQueued = false, officialSocketResetQueued = false;
bool officialSessionExists = false, officialResumePending = false;
uint32_t officialResumeAtMs = 0;
int8_t officialHistoryDirection = 1;
String officialExpectedMessage;
uint32_t officialSentHistoryBaseline = 0, officialHistoryNotBeforeMs = 0;
uint32_t officialExpectedMessageAtMs = 0;
uint32_t officialEpoch = 0, officialSelectionVersion = 0, officialHistoryOffset = 0;
uint32_t officialHistoryTotal = 0;
struct OfficialMessagePosition { String id; uint32_t offset = 0; };
OfficialMessagePosition officialMessagePositions[kNativeChatMessageCount];
String chatInputNotice;
uint32_t chatInputMethodEvents = 0, chatInputMethodCommits = 0, chatLanguageToggles = 0;
String chatFocusMessage;
String chatLiveRequest;
bool chatFocusAtEnd = false;
bool chatHistoryMode = false;
uint32_t chatScrollUpCount = 0, chatScrollDownCount = 0, chatHistoryToggleCount = 0;
uint32_t chatHistoryOlderCount = 0, chatHistoryNewerCount = 0;
NativeChatBrowseFocus chatPageFocus = NativeChatBrowseFocus::Preserve;
uint32_t chatRequestSequence = 0, chatRequestNonce = 0;
uint32_t chatHistoryAtMs = 0, chatSendAtMs = 0;
char chatSentText[kNativeChatDraftBytes + 1]{};
uint16_t chatSentTextBytes = 0;
uint32_t chatHistoryLoadedVersion = 0, chatUiRevision = 0;
uint32_t chatActivationAttemptVersion = 0;
uint32_t chatAcceptedCount = 0, chatReceivedChunks = 0, chatRejectedFrames = 0;
size_t chatBottomOffset = 0;
bool chatProtocolReady = false, chatSendUnknown = false;
bool chatWasOpen = false, chatHistoryReplace = true;
bool chatRemoteRunActive = false, chatLiveEventsSeen = false;
uint32_t chatRecoveryPollAtMs = 0;
uint32_t chatObservedDroppedKeys = 0;
bool requestChatHistory(const char* messageId = nullptr, uint32_t offset = 0,
                        const char* beforeMessageId = nullptr, const char* afterMessageId = nullptr);
void tickNativeChat();
bool officialStudioDirect();
bool officialChatContextActivated();
void tickOfficialStudio();
bool officialSendDraft();
bool officialSendInput(const char* text,bool fromDraft);
bool officialVoiceNetworkBusy();
void tickOfficialVoice();
void startOfficialRecording();
void cancelOfficialVoice(const char* notice=nullptr);
void armOfficialSpeech();
void applyOfficialSpeechHistory(official_history::Output& output);
bool officialRequestHistory(const char* messageId, uint32_t offset, const char* before, const char* after);
void officialSocketLoop();
void resetOfficialSocketReader();
bool officialEmit(const char* event,const JsonDocument& document);
bool officialControlEvent(const String& event);
String officialUrlEncode(const String& input);
String pendingAgentProfile;
String agentSelectionError;
uint32_t agentSelectionStartedMs = 0;
uint32_t lastOledAtMs = 0;
uint32_t oledStatusReturnAtMs = 0;
uint32_t restartAtMs = 0;
uint32_t lastLanDiscoveryAtMs = 0;
uint32_t lastMcuLoginAtMs = 0;
uint32_t lastMcuSocketConnectAtMs = 0;
uint32_t nextMcuSocketReconnectAtMs = 0;
uint32_t mcuSocketReconnectDelayMs = kMcuSocketReconnectMs;
uint32_t lastBootButtonAtMs = 0;
uint32_t bootPressedAtMs = 0;
uint32_t bootClickPendingAtMs = 0;
uint32_t bootReleaseStartedAtMs = 0;
uint32_t audioInterruptPressStartedAtMs = 0;
uint32_t wifiDisconnectedSinceMs = 0;
uint32_t lastBatteryReadAtMs = 0;
uint32_t lastMcuActivityAtMs = 0;
uint32_t batteryVoltageMv = 0;
uint8_t oledProgress = 0;
uint8_t batteryLevelPercent = 0;
uint8_t oledAddress = kDefaultOledAddr;
String oledTitle = "BOOT";
String oledHint = "starting";
int lastMcuLoginCode = 0;
String lastMcuLoginDetail;
String selectedProfile;
String pendingProfileDeviceKey;
String pendingProfileDeviceUrl;
String profileFormProof;
bool pendingProfileRemoteSource = false;
String activeDeviceKey;
String activeDeviceUrl;
String mcuAuthToken;
String mcuSocketRelayUrl;
String mcuSocketTargetKey;
String mcuRemoteDiscoveryToken;
int remoteDiscoveryHttpCode = 0, remoteDiscoveryTlsError = 0;
uint32_t remoteDiscoveryElapsedMs = 0, remoteDiscoveryHeap = 0, remoteDiscoveryLargestBlock = 0;
size_t remoteDiscoveryResponseBytes = 0;
IPAddress remoteDiscoveryResolvedIp;
bool remoteDiscoveryDnsOk = false;
uint32_t remoteDiscoveryFinishedAtMs = 0;
uint32_t remoteCredentialsRetryAtMs = 0;
native_remote::SocketLiveness mcuSocketLiveness;
native_wifi::SavedNetworkRetry savedNetworkRetry;
native_reauth::RetryPolicy mcuReauthPolicy;
bool mcuReauthAttemptInProgress = false;
bool mcuReauthAudioCleanupPending = false;
String pendingMcuReauthReason;
String pendingMcuReauthMachineId;
String pendingMcuReauthInteractionId;
bool mcuSocketReconnectBlocked = false;
bool pendingMcuReauthAuthInvalid = false;
String mcuInteractionId;
String mcuInteractionStatus = "idle";
String mcuInteractionText;
String mcuToolName;
String mcuToolPreview;
String mcuToolStatus;
String lastAudioDetail = "not started";
uint32_t lastAudioAtMs = 0;
const uint8_t* const oledBuffer = nullptr;
std::vector<String> mcuSubtitleLines;
uint16_t mcuSubtitleLineCount = 0;
uint16_t mcuSubtitleRenderedPage = UINT16_MAX;
int scannedNetworkCount = 0;
String scannedSsids[kMaxScannedNetworks];
int32_t scannedRssi[kMaxScannedNetworks] = {};
bool scannedEncrypted[kMaxScannedNetworks] = {};
String loginProfiles[kMaxProfiles];
int loginProfileCount = 0;
bool mcuInteractionActive = false;
bool mcuAudioPlaying = false;
bool mcuVoiceAfterAudioInterrupt = false;
bool mcuAudioStopOnlyAfterInterrupt = false;
bool mcuSessionClearAfterAudioInterrupt = false;
bool voiceRecordHeardSpeech = false;
uint32_t mcuInteractionUpdatedAtMs = 0;
uint32_t mcuAudioStartedAtMs = 0;
uint32_t mcuAudioDurationMs = 0;
uint32_t mcuAudioPlaybackProgressMs = 0;
uint32_t nextMcuOtaCheckAtMs = kMcuOtaFirstCheckMs;
uint32_t voiceRecordRms = 0;
uint32_t voiceRecordPeak = 0;
uint32_t voiceRecordActiveSamples = 0;
uint8_t outputVolumePercent = kDefaultOutputVolumePercent;
uint8_t idlePowerSaveMinutes = kDefaultIdlePowerSaveMinutes;

struct McuAudioSegment {
  String interactionId;
  String segmentId;
  String text;
  String url;
  String mimeType;
  uint8_t channels = 2;
  uint32_t sampleRate = kMcuAudioDefaultSampleRate;
  uint32_t durationMs = 0;
  bool completionManagedByServer = false;
};

struct VoiceStreamChunk {
  bool done = false;
  uint32_t offset = 0;
  size_t bytes = 0;
  int16_t samples[kVoiceStreamChunkFrames] = {};
  uint8_t adpcm[kVoiceStreamAdpcmMaxBytes] = {};
};

McuAudioSegment mcuAudioQueue[kMaxMcuAudioQueue];
int mcuAudioHead = 0;
int mcuAudioCount = 0;
McuAudioSegment mcuCurrentAudio;
bool audioResumePending = false;
size_t audioResumeAgent = 0;
uint32_t audioResumeVersion = 0;
String audioResumeProfile, audioResumeDeviceKey;

bool requestCardputerAgentSelection(size_t index);
void tickCardputerInput();
void markMcuInteraction(const String &interactionId, const String &status, const String &text);
void triggerBootVoiceTurn();
bool broadcastMcuInterrupt(const String &interactionId, const String &reason);
void broadcastMcuStatus();
void clearMcuSessionByButton();
void disconnectMcuSocketClient();
void connectMcuSocketClient();
void mcuSocketLoop();
void closeMcuSocketTransport(const __FlashStringHelper *reason);
void scheduleMcuSocketReconnect();
void resetMcuSocketReconnect();
void tickMcuSocketReconnect();
void queueMcuReauth(bool authInvalid, const String &reason, const String &machineId = "",
                    const String &interactionId = "", const String &promptUrl = "");
void tickMcuReauth();
void cancelMcuReauth();
bool mcuReauthBlocksSocket();
native_reauth::Target currentMcuReauthTarget();
String userFacingDetail(const String& detail,const char* fallback="请求失败，请在电脑端查看详情");
void setChatNotice(const String& notice);
void noteMcuActivity();
void tickIdlePowerSave();
void resetListeningMonitor();
bool waitForMcuSocketReady(uint32_t timeoutMs);
void clearActiveDeviceState();
bool mcuSocketMatchesActiveTarget();
bool downloadAndApplyMcuFirmware(const String &url, const String &md5, int expectedSize);

enum class McuOtaResult : uint8_t {
  Failed,
  NoUpdate,
  UpdateAvailable,
  Updated,
};

McuOtaResult checkMcuFirmwareUpdate(bool force, bool applyUpdate = true, String *outFirmwareUrl = nullptr, String *outMd5 = nullptr, int *outSize = nullptr);

struct LanDevice {
  String id;
  String name;
  String ip;
  String url;
  String endpointKind;
  String webVersion;
  String agentVersion;
  String profile;
  String relayUrl;
  String displayUrl;
  uint16_t httpPort = 0;
  uint32_t responseMs = 0;
  uint32_t lastSeenMs = 0;
  bool loggedIn = false;
  bool manualSource = false;
  bool remoteSource = false;
  bool remoteLogin = false;
  bool identityVerified = false;
};

LanDevice lanDevices[kMaxLanDevices];
int lanDeviceCount = 0;



enum class OledMode : uint8_t {
  Boot,
  Ready,
  Think,
  Error,
};

OledMode oledMode = OledMode::Boot;

String fitOledText(String value, size_t maxLen) {
  value.trim();
  // Count UTF-8 scalars so Chinese labels never end in a partial character.
  size_t offset = 0, count = 0, prefix = 0;
  while (offset < value.length() && count < maxLen) {
    prefix = offset;
    const uint8_t lead = static_cast<uint8_t>(value[offset]);
    const size_t bytes = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
    offset = min(offset + bytes, value.length());
    ++count;
  }
  if (offset == value.length()) return value;
  return maxLen == 0 ? String() : value.substring(0, prefix) + F("…");
}

uint8_t clampUiValue(uint32_t value, uint8_t ceiling) {
  return value > ceiling ? ceiling : static_cast<uint8_t>(value);
}

uint8_t batteryPercentFromVoltageMv(uint32_t mv) {
  static const struct {
    uint16_t mv;
    uint8_t percent;
  } curve[] = {
      {4200, 100},
      {4100, 90},
      {4000, 80},
      {3920, 70},
      {3850, 60},
      {3790, 50},
      {3730, 40},
      {3670, 30},
      {3610, 20},
      {3500, 10},
      {3300, 0},
  };
  if (mv >= curve[0].mv) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
    if (mv >= curve[i].mv) {
      const uint32_t highMv = curve[i - 1].mv;
      const uint32_t lowMv = curve[i].mv;
      const uint32_t highPercent = curve[i - 1].percent;
      const uint32_t lowPercent = curve[i].percent;
      return static_cast<uint8_t>(lowPercent + ((mv - lowMv) * (highPercent - lowPercent)) / (highMv - lowMv));
    }
  }
  return 0;
}

void updateBatteryReading(bool force = false) {
  uint32_t now = millis();
  if (!force && batteryVoltageMv > 0 && now - lastBatteryReadAtMs < kBatteryReadIntervalMs) return;
  lastBatteryReadAtMs = now;
  int measuredMv = M5Cardputer.Power.getBatteryVoltage();
  batteryVoltageMv = measuredMv > 0 ? static_cast<uint32_t>(measuredMv) : 0;
  int level = M5Cardputer.Power.getBatteryLevel();
  batteryLevelPercent = level < 0 ? batteryPercentFromVoltageMv(batteryVoltageMv) : clampUiValue(level, 100);
}



String hexByte(uint8_t value) {
  char buf[5];
  snprintf(buf, sizeof(buf), "0x%02X", value);
  return String(buf);
}

void setPowerAmp(bool enable) {
  paEnabled = cardputerSpeakerEnable(enable) && enable;
}

bool oledCommand(uint8_t command) {
  if (command != 0xAE && command != 0xAF) return false;
  cardputerDisplayPower(command == 0xAF);
  return true;
}

void leaveIdlePowerSave() {
  if (!idlePowerSaveActive) return;
  idlePowerSaveActive = false;
  WiFi.setSleep(false);
  if (oledReady && !oledCommand(0xAF)) {
    Serial.println(F("Idle power save wake: OLED display-on failed"));
  }
  oledDirty = true;
  Serial.println(F("Idle power save disabled"));
}

void noteMcuActivity() {
  lastMcuActivityAtMs = millis();
  leaveIdlePowerSave();
}

void tickIdlePowerSave() {
  uint32_t now = millis();
  if (lastMcuActivityAtMs == 0) {
    lastMcuActivityAtMs = now;
    return;
  }

  bool busy = mcuInteractionActive || mcuAudioPlaying || mcuAudioCount > 0 || audioBusy;
  bool bootPressed = cardputerTalkPressed();
  bool canPowerSave = wifiReady && WiFi.status() == WL_CONNECTED && !setupApMode &&
                      !restartPending && !busy && !bootPressed;
  if (!canPowerSave) {
    if (idlePowerSaveActive) noteMcuActivity();
    return;
  }
  if (idlePowerSaveMinutes == 0) return;
  uint32_t idleDelayMs = static_cast<uint32_t>(idlePowerSaveMinutes) * 60UL * 1000UL;
  if (idlePowerSaveActive || now - lastMcuActivityAtMs < idleDelayMs) return;

  idlePowerSaveActive = true;
  WiFi.setSleep(true);
  if (paEnabled) setPowerAmp(false);
  if (oledReady && !oledCommand(0xAE)) {
    Serial.println(F("Idle power save: OLED display-off failed"));
  }
  Serial.printf("Idle power save enabled after %u minutes\n", idlePowerSaveMinutes);
}



String interactionStatusLabel() { return String(native_ui::interactionLabel(mcuInteractionStatus.c_str())); }

bool cardputerChatAvailable() {
  return mcuSocketNamespaceReady && mcuAuthToken.length() > 0 &&
      activeDeviceUrl.length() > 0 && selectedProfile.length() > 0;
}

const char* nativeChatStatusLabel() {
  NativeChatStatusState state;
  state.wifiConnected = wifiReady && WiFi.status() == WL_CONNECTED;
  state.loginRequired = mcuSocketReconnectBlocked || mcuReauthPolicy.rejected() ||
      activeDeviceUrl.length() == 0 || selectedProfile.length() == 0 ||
      (!mcuAuthToken.length() && !mcuReauthPolicy.active());
  state.reauthenticating = mcuReauthAttemptInProgress || mcuReauthAudioCleanupPending ||
      mcuReauthPolicy.blocksSocket();
  state.namespaceReady = mcuSocketNamespaceReady;
  state.audioTransportPaused = audioResumePending && audioResumeAgent == agentSelection.active &&
      audioResumeAgent == agentSelection.browse && audioResumeProfile == selectedProfile &&
      audioResumeDeviceKey == activeDeviceKey;
  state.audioPlaying = mcuAudioPlaying || officialVoiceOutputPlaying;
  state.agentConfirmed = agentSelection.confirmed;
  state.agentPending = agentSelection.pending;
  state.protocolReady = chatProtocolReady;
  state.historyPending = chatHistoryRequest.length() != 0;
  state.modelReady = chatModel.ready();
  state.sendPending = chatPendingSend.length() != 0;
  state.remoteRunActive = chatRemoteRunActive;
  state.interactionActive = mcuInteractionActive;
  state.interactionStatus = mcuInteractionStatus.c_str();
  const char* label = nativeChatStatus(state);
  static const char* previousLabel = nullptr;
  if (conversationView.conversation() && (!previousLabel || std::strcmp(previousLabel, label) != 0)) {
    Serial.printf("Chat display status=%s wifi=%d native=%d audio_pause=%d\n", label,
                  state.wifiConnected, state.namespaceReady, state.audioTransportPaused);
    previousLabel = label;
  }
  return label;
}

bool oledFlushPages(uint8_t, uint8_t) {
  conversationView.synchronize(agentSelection, selectedProfile.c_str(),
                               cardputerChatAvailable(), agentSelectionError.length() != 0);
  String address = setupApMode ? String("192.168.4.1") :
      (wifiReady ? WiFi.localIP().toString() + "/device" : String("Wi-Fi connecting"));
  cardputerDraw(oledBuffer, mcuInteractionActive, agentSelection, conversationView, oledTitle,
                oledHint, address, mcuSocketNamespaceReady, agentSelectionError,
                chatModel, nativeChatStatusLabel(), chatNotice, chatBottomOffset,
                chatPendingSend.length() != 0, chatHistoryMode, chatUiRevision);
  return true;
}

bool oledFlush() {
  return oledFlushPages(0, 7);
}

void refreshOled(bool force = false) {
  if(!oledReady || idlePowerSaveActive) return;
  const uint32_t now=millis();
  if(!force && !oledDirty && now-lastOledAtMs<kOledRefreshIntervalMs) return;
  oledDirty=false;lastOledAtMs=now;oledReady=oledFlush();
}

void setOledStatus(OledMode mode, const String &title, const String &hint, uint8_t progress = 0) {
  noteMcuActivity();
  String nextTitle = fitOledText(native_ui::statusLabel(title.c_str()), 10);
  String nextHint = fitOledText(hint.startsWith(F("AUTO ")) ? String(F("自动监听 ")) + hint.substring(5) :
      String(native_ui::statusLabel(hint.c_str())), 20);
  oledMode = mode;
  oledTitle = nextTitle;
  oledHint = nextHint;
  oledProgress = progress > 100 ? 100 : progress;
  oledStatusReturnAtMs = 0;
  bool isHomeStatus = title == F("IP") ||
                      (listeningModeEnabled && title == F("LISTEN") && hint.startsWith(F("AUTO ")));
  if (!isHomeStatus && wifiReady && WiFi.status() == WL_CONNECTED &&
      (mode == OledMode::Ready || mode == OledMode::Error)) {
    uint32_t delayMs = mode == OledMode::Error ? kOledErrorReturnDelayMs : kOledSuccessReturnDelayMs;
    oledStatusReturnAtMs = millis() + delayMs;
  }
  oledDirty = true;
  refreshOled(true);
}

void showLanIpOnOled() {
  if (!wifiReady || WiFi.status() != WL_CONNECTED) return;
  if (listeningModeEnabled) {
    setOledStatus(OledMode::Ready, F("LISTEN"), String(F("AUTO ")) + WiFi.localIP().toString(), 0);
  } else {
    setOledStatus(OledMode::Ready, F("IP"), WiFi.localIP().toString(), 0);
  }
}

void tickOledStatusReturn() {
  if (oledStatusReturnAtMs == 0 || static_cast<int32_t>(millis() - oledStatusReturnAtMs) < 0) return;
  if (!wifiReady || WiFi.status() != WL_CONNECTED) {
    oledStatusReturnAtMs = 0;
    return;
  }
  if (mcuInteractionActive || mcuAudioPlaying || audioBusy) return;
  showLanIpOnOled();
}

void initOledDisplay() {
  // U8g2 is retained only as a RAM glyph/layout renderer; never initializes
  // the C3 OLED I2C bus. Physical LCD and codec were initialized by M5.
  oledFound = oledReady = true;
  oledDirty = true;
}

String escapeHtml(const String &value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&':
        out += F("&amp;");
        break;
      case '<':
        out += F("&lt;");
        break;
      case '>':
        out += F("&gt;");
        break;
      case '"':
        out += F("&quot;");
        break;
      case '\'':
        out += F("&#39;");
        break;
      default:
        out += value[i];
        break;
    }
  }
  return out;
}

void migrateLegacyWifi() {
  Preferences migration;
  if (!migration.begin("net", false)) return;
  bool migrated = migration.getBool("migrated", false);
  String current = migration.getString("ssid", "");
  migration.end();
  if (migrated) return;
  if (current.length() == 0) {
    String ssid, password;
    if (migration.begin("ekko-wifi", true)) {
      ssid = migration.getString("ssid", "");
      password = migration.getString("password", "");
      migration.end();
    }
    if (ssid.length() <= 32 && password.length() <= 64 && ssid.length()) {
      if (!migration.begin("net", false)) return;
      bool copied = migration.putString("ssid", ssid) > 0;
      copied = (migration.putString("pass", password) > 0 || password.length() == 0) && copied;
      if (copied) migration.putBool("migrated", true);
      else { migration.remove("ssid"); migration.remove("pass"); }
      migration.end();
      return;
    }
  }
  if (migration.begin("net", false)) {
    migration.putBool("migrated", true);
    migration.end();
  }
}

String prefString(const char *key) {
  prefs.begin("net", true);
  String value = prefs.getString(key, "");
  prefs.end();
  return value;
}

String wifiHistorySsidKey(int index) {
  return String(F("ssid")) + index;
}

String wifiHistoryPassKey(int index) {
  return String(F("pass")) + index;
}

bool appendWifiCredential(String ssids[], String passes[], int &count, String ssid, const String &pass,
                          int capacity = kMaxWifiHistoryNetworks, bool distinguishPassword = false) {
  if (ssid.length() == 0) return false;
  for (int i = 0; i < count; ++i) {
    if (ssids[i] == ssid && (!distinguishPassword || passes[i] == pass)) return false;
  }
  if (count >= capacity) return false;
  ssids[count] = ssid;
  passes[count] = pass;
  ++count;
  return true;
}

bool readFactoryWifi(SharedWifiConfig::Credentials& credential) {
  credential = SharedWifiConfig::Credentials();
  factoryWifiSource = "none";
  Preferences settings;
  if (!settings.begin(SharedWifiConfig::kNvsNamespace, true)) return false;
  const bool exists = settings.isKey(SharedWifiConfig::kNvsKey);
  const size_t length = settings.getBytesLength(SharedWifiConfig::kNvsKey);
  uint8_t blob[SharedWifiConfig::kBlobSize] = {};
  if (exists) {
    const bool read = length == sizeof(blob) &&
        settings.getBytes(SharedWifiConfig::kNvsKey, blob, sizeof(blob)) == sizeof(blob);
    settings.end();
    const bool valid = read && SharedWifiConfig::decode(blob, sizeof(blob), credential);
    factoryWifiSource = valid ? "factory_shared" : "invalid_shared_record";
    return valid;
  }
  const String ssid = settings.getString("wifi_ssid", "");
  const String password = settings.getString("wifi_password", "");
  settings.end();
  if (!SharedWifiConfig::encode(ssid.c_str(), ssid.length(), password.c_str(), password.length(),
                                blob, sizeof(blob)) ||
      !SharedWifiConfig::decode(blob, sizeof(blob), credential)) return false;
  factoryWifiSource = "factory_legacy";
  return true;
}

bool publishFactoryWifi(const String& ssid, const String& password) {
  uint8_t blob[SharedWifiConfig::kBlobSize] = {};
  if (!SharedWifiConfig::encode(ssid.c_str(), ssid.length(), password.c_str(), password.length(),
                                blob, sizeof(blob))) return false;
  Preferences settings;
  if (!settings.begin(SharedWifiConfig::kNvsNamespace, false)) return false;
  // Preferences::putBytes performs nvs_set_blob and checks nvs_commit.
  const bool saved = settings.putBytes(SharedWifiConfig::kNvsKey, blob, sizeof(blob)) == sizeof(blob);
  settings.end();
  return saved;
}

void loadWifiCredentials(String ssids[], String passes[], int &count) {
  count = 0;
  SharedWifiConfig::Credentials factory;
  if (readFactoryWifi(factory))
    appendWifiCredential(ssids, passes, count, factory.ssid, factory.password, kMaxWifiCandidates, true);
  prefs.begin("net", true);
  appendWifiCredential(ssids, passes, count, prefs.getString("ssid", ""), prefs.getString("pass", ""),
                       kMaxWifiCandidates, true);
  for (int i = 0; i < kMaxWifiHistoryNetworks; ++i) {
    String ssidKey = wifiHistorySsidKey(i);
    String passKey = wifiHistoryPassKey(i);
    appendWifiCredential(ssids, passes, count, prefs.getString(ssidKey.c_str(), ""),
                         prefs.getString(passKey.c_str(), ""), kMaxWifiCandidates, true);
  }
  prefs.end();
}

bool savedWifiPasswordForSsid(const String &ssid, String &pass) {
  String ssids[kMaxWifiCandidates];
  String passes[kMaxWifiCandidates];
  int count = 0;
  loadWifiCredentials(ssids, passes, count);
  for (int i = 0; i < count; ++i) {
    if (ssids[i] == ssid) {
      pass = passes[i];
      return true;
    }
  }
  return false;
}

void rememberWifiCredential(const String &ssid, const String &pass) {
  if (ssid.length() == 0) return;

  String storedSsids[kMaxWifiCandidates];
  String storedPasses[kMaxWifiCandidates];
  int storedCount = 0;
  loadWifiCredentials(storedSsids, storedPasses, storedCount);

  String ssids[kMaxWifiHistoryNetworks];
  String passes[kMaxWifiHistoryNetworks];
  int count = 0;
  appendWifiCredential(ssids, passes, count, ssid, pass);
  for (int i = 0; i < storedCount; ++i) {
    appendWifiCredential(ssids, passes, count, storedSsids[i], storedPasses[i]);
  }

  prefs.begin("net", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  for (int i = 0; i < kMaxWifiHistoryNetworks; ++i) {
    String ssidKey = wifiHistorySsidKey(i);
    String passKey = wifiHistoryPassKey(i);
    if (i < count) {
      prefs.putString(ssidKey.c_str(), ssids[i]);
      prefs.putString(passKey.c_str(), passes[i]);
    } else {
      prefs.remove(ssidKey.c_str());
      prefs.remove(passKey.c_str());
    }
  }
  prefs.end();
}

void forgetAllWifiCredentials() {
  Preferences factory;
  if (factory.begin(SharedWifiConfig::kNvsNamespace, false)) {
    factory.remove(SharedWifiConfig::kNvsKey);
    factory.remove("wifi_ssid");
    factory.remove("wifi_password");
    factory.end();
  }
  prefs.begin("net", false);
  prefs.putBool("migrated", true);
  prefs.remove("ssid");
  prefs.remove("pass");
  for (int i = 0; i < kMaxWifiHistoryNetworks; ++i) {
    String ssidKey = wifiHistorySsidKey(i);
    String passKey = wifiHistoryPassKey(i);
    prefs.remove(ssidKey.c_str());
    prefs.remove(passKey.c_str());
  }
  prefs.end();
}

void loadAudioPreferences() {
  prefs.begin("stockLan", true);
  outputVolumePercent = clampUiValue(
      static_cast<uint32_t>(prefs.getUChar("volume", kDefaultOutputVolumePercent)),
      kMaxOutputVolumePercent);
  idlePowerSaveMinutes = clampUiValue(
      static_cast<uint32_t>(prefs.getUChar("idle_min", kDefaultIdlePowerSaveMinutes)),
      kMaxIdlePowerSaveMinutes);
  listeningModeEnabled = false;
  String savedAgent = prefs.getString("agent_id", prefs.getBool("agent_hermes", false) ? "hermes" : "ekko-agent");
  int savedIndex = nativeAgentIndex(savedAgent.c_str());
  agentSelection.active = agentSelection.browse = savedIndex < 0 ? 0 : static_cast<size_t>(savedIndex);
  prefs.end();
}

String mcuAgentRuntimeValue() { return String(kNativeAgents[agentSelection.active].id); }
String mcuAgentRuntimeLabel() { return String(kNativeAgents[agentSelection.active].name); }

void saveOutputVolume(uint8_t volume) {
  outputVolumePercent = clampUiValue(volume, kMaxOutputVolumePercent);
  prefs.begin("stockLan", false);
  prefs.putUChar("volume", outputVolumePercent);
  prefs.end();
}

void saveIdlePowerSaveMinutes(uint8_t minutes) {
  idlePowerSaveMinutes = clampUiValue(minutes, kMaxIdlePowerSaveMinutes);
  prefs.begin("stockLan", false);
  prefs.putUChar("idle_min", idlePowerSaveMinutes);
  prefs.end();
  noteMcuActivity();
}



String currentIp() {
  return wifiReady ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String deviceUrl(const IPAddress &ip) {
  return String(F("http://")) + ip.toString() + F("/");
}

String deviceId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return String(F("ekko_cardputer_lan_")) + mac;
}

native_identity::BootIdentity<String> mcuBootIdentity;
esp_err_t mcuPreferencesLastError = ESP_OK;

void reportMcuPreferencesFailure(const char* phase, esp_err_t error) {
  mcuPreferencesLastError = error;
  Serial.printf("MCU preferences failure phase=%s error=%s; stored identity and credentials preserved\n",
      phase, esp_err_to_name(error));
  lastAudioDetail = F("设备配置保存或读取失败，请重启后重试");
  setChatNotice(lastAudioDetail);
}

native_identity::ReadResult<String> readMcuPreferenceString(nvs_handle_t handle, const char* key) {
  size_t bytes = 0;
  esp_err_t error = nvs_get_str(handle, key, nullptr, &bytes);
  if (error == ESP_ERR_NVS_NOT_FOUND) return {native_identity::ReadState::Missing, String()};
  if (error != ESP_OK || !bytes) {
    reportMcuPreferencesFailure("read_string_size", error == ESP_OK ? ESP_ERR_INVALID_SIZE : error);
    return {native_identity::ReadState::Failed, String()};
  }
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[bytes]);
  if (!buffer) {
    reportMcuPreferencesFailure("read_string_allocate", ESP_ERR_NO_MEM);
    return {native_identity::ReadState::Failed, String()};
  }
  error = nvs_get_str(handle, key, buffer.get(), &bytes);
  if (error != ESP_OK) {
    reportMcuPreferencesFailure("read_string", error);
    return {native_identity::ReadState::Failed, String()};
  }
  String value(buffer.get());
  if (value.length() + 1 != bytes) {
    reportMcuPreferencesFailure("read_string_copy", ESP_ERR_NO_MEM);
    return {native_identity::ReadState::Failed, String()};
  }
  return {native_identity::ReadState::Present, value};
}

native_identity::ReadResult<String> readStoredMcuDeviceCode() {
  nvs_handle_t handle;
  const esp_err_t error = nvs_open("mcu", NVS_READONLY, &handle);
  if (error == ESP_ERR_NVS_NOT_FOUND) return {native_identity::ReadState::Missing, String()};
  if (error != ESP_OK) {
    reportMcuPreferencesFailure("identity_open_read", error);
    return {native_identity::ReadState::Failed, String()};
  }
  auto stored = readMcuPreferenceString(handle, "device_code");
  nvs_close(handle);
  stored.value.trim(); // Preserve the released reader's whitespace treatment.
  return stored;
}

native_identity::WriteState saveNewMcuDeviceCode(const String& code) {
  nvs_handle_t handle;
  esp_err_t error = nvs_open("mcu", NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    reportMcuPreferencesFailure("identity_open_write", error);
    return native_identity::WriteState::Failed;
  }
  auto existing = readMcuPreferenceString(handle, "device_code");
  existing.value.trim();
  if (existing.state == native_identity::ReadState::Failed) {
    nvs_close(handle);
    return native_identity::WriteState::Failed;
  }
  if (existing.value.length() && existing.value != code) {
    nvs_close(handle);
    reportMcuPreferencesFailure("identity_conflict", ESP_ERR_INVALID_STATE);
    return native_identity::WriteState::Conflict;
  }
  if (!existing.value.length()) error = nvs_set_str(handle, "device_code", code.c_str());
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  if (error != ESP_OK) {
    reportMcuPreferencesFailure("identity_commit", error);
    return native_identity::WriteState::Failed;
  }
  const auto readback = readStoredMcuDeviceCode();
  if (readback.state != native_identity::ReadState::Present || readback.value != code) {
    reportMcuPreferencesFailure("identity_readback", ESP_ERR_INVALID_STATE);
    return native_identity::WriteState::Failed;
  }
  mcuPreferencesLastError = ESP_OK;
  return native_identity::WriteState::Saved;
}

const char* mcuDeviceCodeStateLabel() { return native_identity::stateName(mcuBootIdentity.state()); }

String mcuDeviceCode() {
  const String& resolved = mcuBootIdentity.resolve(readStoredMcuDeviceCode, []() {
    uint8_t entropy[native_identity::kDeviceCodeEntropyBytes];
    esp_fill_random(entropy, sizeof(entropy));
    const auto code = native_identity::newDeviceCode(ESP.getEfuseMac(), entropy);
    if (!code[0]) reportMcuPreferencesFailure("identity_generate", ESP_ERR_INVALID_STATE);
    String generated(code.data());
    if (code[0] && !generated.length()) reportMcuPreferencesFailure("identity_allocate", ESP_ERR_NO_MEM);
    return generated;
  }, saveNewMcuDeviceCode);
  if (!resolved.length() && mcuPreferencesLastError == ESP_OK)
    reportMcuPreferencesFailure("identity_cache", ESP_ERR_NO_MEM);
  return resolved;
}

String mcuSocketStateLabel() {
  if (mcuSocketReconnectBlocked) return F("已被其他设备接管");
  if (mcuSocketNamespaceReady && agentSelection.confirmed)
    return mcuSocketRelayUrl.length() ? F("远程已连接") : F("已连接");
  if (mcuSocketNamespaceReady) return F("等待后台确认");
  if (mcuSocketConnected) return F("握手中");
  if (wsReady) return F("重连中");
  return F("未连接");
}

String escapeJson(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else if (c == '\t') {
      out += F("\\t");
    } else {
      out += c;
    }
  }
  return out;
}

String compactDetail(String value) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.trim();
  if (value.length() > 180) {
    size_t end = 180;
    while (end > 0 && (static_cast<uint8_t>(value[end]) & 0xC0) == 0x80) --end;
    value = value.substring(0, end);
  }
  return value;
}

void rememberMcuLoginResult(int code, const String &detail) {
  lastMcuLoginCode = code;
  lastMcuLoginAtMs = millis();
  lastMcuLoginDetail = compactDetail(detail);
}

void clearLoginProfiles() {
  loginProfileCount = 0;
  for (int i = 0; i < kMaxProfiles; ++i) {
    loginProfiles[i] = "";
  }
}

void parseLoginProfiles(const String &json) {
  clearLoginProfiles();JsonDocument body;
  JsonDocument filter;filter["profiles"]=true;
  if(deserializeJson(body,json,DeserializationOption::Filter(filter)) || !body["profiles"].is<JsonArrayConst>()) return;
  for(JsonVariantConst item:body["profiles"].as<JsonArrayConst>()) {
    if(!item.is<const char*>()) {clearLoginProfiles();return;}
    const JsonString name=item.as<JsonString>();
    if(!name.size() || name.size()>official_selection::kProfileBytes ||
       std::strlen(name.c_str())!=name.size() || !native_chat::validUtf8(name.c_str(),name.size())) {clearLoginProfiles();return;}
    if(loginProfileCount<kMaxProfiles) loginProfiles[loginProfileCount++]=name.c_str();
  }
}

String uptimeText() {
  uint32_t seconds = millis() / 1000UL;
  uint32_t days = seconds / 86400UL;
  seconds %= 86400UL;
  uint8_t hours = seconds / 3600UL;
  seconds %= 3600UL;
  uint8_t minutes = seconds / 60UL;
  seconds %= 60UL;

  String value;
  if (days > 0) {
    value += days;
    value += F("天 ");
  }
  if (hours < 10) value += F("0");
  value += hours;
  value += F(":");
  if (minutes < 10) value += F("0");
  value += minutes;
  value += F(":");
  if (seconds < 10) value += F("0");
  value += seconds;
  return value;
}

void appendInfoRow(String &html, const __FlashStringHelper *label, const String &value) {
  html += F("<div class='info-row'><span>");
  html += label;
  html += F("</span><strong>");
  html += escapeHtml(value);
  html += F("</strong></div>");
}

String jsonStringValue(const String &json, const __FlashStringHelper *key) {
  String pattern = String(F("\"")) + key + F("\"");
  int keyAt = json.indexOf(pattern);
  if (keyAt < 0) return "";
  int colonAt = json.indexOf(':', keyAt + pattern.length());
  if (colonAt < 0) return "";
  int quoteAt = json.indexOf('"', colonAt + 1);
  if (quoteAt < 0) return "";

  String value;
  bool escaped = false;
  for (int i = quoteAt + 1; i < static_cast<int>(json.length()); ++i) {
    char c = json[i];
    if (escaped) {
      value += c;
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') break;
    value += c;
  }
  return value;
}

String jsonObjectValue(const String &json, const __FlashStringHelper *key) {
  String pattern = String(F("\"")) + key + F("\"");
  int keyAt = json.indexOf(pattern);
  if (keyAt < 0) return "";
  int colonAt = json.indexOf(':', keyAt + pattern.length());
  if (colonAt < 0) return "";
  int openAt = json.indexOf('{', colonAt + 1);
  if (openAt < 0) return "";

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (int i = openAt; i < static_cast<int>(json.length()); ++i) {
    char c = json[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = inString;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (c == '{') ++depth;
    if (c == '}') {
      --depth;
      if (depth == 0) return json.substring(openAt, i + 1);
    }
  }
  return "";
}

String jsonArrayValue(const String &json, const __FlashStringHelper *key) {
  String pattern = String(F("\"")) + key + F("\"");
  int keyAt = json.indexOf(pattern);
  if (keyAt < 0) return "";
  int colonAt = json.indexOf(':', keyAt + pattern.length());
  if (colonAt < 0) return "";
  int openAt = json.indexOf('[', colonAt + 1);
  if (openAt < 0) return "";

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (int i = openAt; i < static_cast<int>(json.length()); ++i) {
    char c = json[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = inString;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (c == '[') ++depth;
    if (c == ']') {
      --depth;
      if (depth == 0) return json.substring(openAt, i + 1);
    }
  }
  return "";
}

int jsonIntValue(const String &json, const __FlashStringHelper *key) {
  String pattern = String(F("\"")) + key + F("\"");
  int keyAt = json.indexOf(pattern);
  if (keyAt < 0) return 0;
  int colonAt = json.indexOf(':', keyAt + pattern.length());
  if (colonAt < 0) return 0;
  int startAt = colonAt + 1;
  while (startAt < static_cast<int>(json.length()) && json[startAt] == ' ') ++startAt;
  return json.substring(startAt).toInt();
}

bool jsonBoolValue(const String &json, const __FlashStringHelper *key) {
  String pattern = String(F("\"")) + key + F("\"");
  int keyAt = json.indexOf(pattern);
  if (keyAt < 0) return false;
  int colonAt = json.indexOf(':', keyAt + pattern.length());
  if (colonAt < 0) return false;
  int startAt = colonAt + 1;
  while (startAt < static_cast<int>(json.length()) && json[startAt] == ' ') ++startAt;
  return json.startsWith(F("true"), startAt);
}

void putLe16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void putLe32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void writeWavHeader(uint8_t *wav, uint32_t dataBytes) {
  memcpy(wav, "RIFF", 4);
  putLe32(wav + 4, 36 + dataBytes);
  memcpy(wav + 8, "WAVEfmt ", 8);
  putLe32(wav + 16, 16);
  putLe16(wav + 20, 1);
  putLe16(wav + 22, 1);
  putLe32(wav + 24, kVoiceInputSampleRate);
  putLe32(wav + 28, kVoiceInputSampleRate * 2);
  putLe16(wav + 32, 2);
  putLe16(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  putLe32(wav + 40, dataBytes);
}

bool parseAudioUrl(const String &url, String *scheme, String *host, uint16_t *port, String *path) {
  if (!scheme || !host || !port || !path) return false;
  if (url.startsWith(F("/"))) {
    String baseScheme;
    String baseHost;
    String basePath;
    uint16_t basePort = 0;
    if (!parseAudioUrl(activeDeviceUrl, &baseScheme, &baseHost, &basePort, &basePath)) return false;
    *scheme = baseScheme;
    *host = baseHost;
    *port = basePort;
    *path = url;
    return true;
  }
  int schemeEnd = url.indexOf(F("://"));
  if (schemeEnd <= 0) return false;
  String parsedScheme = url.substring(0, schemeEnd);
  parsedScheme.toLowerCase();
  if (parsedScheme != F("http") && parsedScheme != F("https")) return false;

  String rest = url.substring(schemeEnd + 3);
  int pathStart = rest.indexOf('/');
  String authority = pathStart >= 0 ? rest.substring(0, pathStart) : rest;
  String parsedPath = pathStart >= 0 ? rest.substring(pathStart) : String(F("/"));
  authority.trim();
  parsedPath.trim();
  if (authority.length() == 0) return false;

  uint16_t parsedPort = parsedScheme == F("https") ? 443 : 80;
  String parsedHost = authority;
  int colon = authority.lastIndexOf(':');
  if (colon > 0) {
    int portValue = authority.substring(colon + 1).toInt();
    if (portValue <= 0 || portValue > 65535) return false;
    parsedPort = static_cast<uint16_t>(portValue);
    parsedHost = authority.substring(0, colon);
    parsedHost.trim();
  }
  if (parsedHost.length() == 0) return false;
  if (parsedPath.length() == 0) parsedPath = F("/");
  *scheme = parsedScheme;
  *host = parsedHost;
  *port = parsedPort;
  *path = parsedPath;
  return true;
}

String readHttpLine(WiFiClient &client, uint32_t timeoutMs) {
  String line;
  uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    while (client.available() > 0) {
      char c = static_cast<char>(client.read());
      if (c == '\r') continue;
      if (c == '\n') return line;
      if (line.length() < 240) line += c;
    }
    if (!client.connected()) break;
    delay(5);
    yield();
  }
  return line;
}

bool readHttpResponseHeaders(WiFiClient &client, int *statusCode, int *contentLength, String *contentType) {
  if (!statusCode || !contentLength || !contentType) return false;
  String status = readHttpLine(client, kMcuAudioHttpTimeoutMs);
  status.trim();
  if (!status.startsWith(F("HTTP/"))) {
    lastAudioDetail = String(F("bad HTTP response: ")) + compactDetail(status);
    return false;
  }
  int firstSpace = status.indexOf(' ');
  if (firstSpace < 0) return false;
  *statusCode = status.substring(firstSpace + 1, firstSpace + 4).toInt();
  *contentLength = -1;
  *contentType = "";

  while (true) {
    String line = readHttpLine(client, kMcuAudioHttpTimeoutMs);
    if (line.length() == 0) break;
    int colon = line.indexOf(':');
    if (colon <= 0) continue;
    String name = line.substring(0, colon);
    String value = line.substring(colon + 1);
    name.toLowerCase();
    value.trim();
    if (name == F("content-length")) {
      *contentLength = value.toInt();
    } else if (name == F("content-type")) {
      *contentType = value;
      contentType->toLowerCase();
    }
  }
  return *statusCode > 0;
}

bool setI2sSampleRate(uint32_t sampleRate) {
  return i2sReady && cardputerAudioRate(sampleRate);
}

int16_t shapeOutputSample(int16_t sample) {
  int32_t value = (static_cast<int32_t>(sample) * static_cast<int32_t>(outputVolumePercent) * 15) / 1000;
  int32_t magnitude = value < 0 ? -value : value;
  if (magnitude > kVoiceBoostKnee) {
    const int32_t excess = magnitude - kVoiceBoostKnee;
    const int32_t range = kVoiceBoostCeiling - kVoiceBoostKnee;
    magnitude = kVoiceBoostKnee + (excess * range) / (excess + range);
    value = value < 0 ? -magnitude : magnitude;
  }
  return static_cast<int16_t>(value);
}

int16_t shapeVoiceInputSample(int16_t sample) {
  int32_t value = (static_cast<int32_t>(sample) * kVoiceInputGainPermille) / 1000;
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<int16_t>(value);
}

// Every playback exit, including HTTP failure or a user interruption, joins the
// SDK consumer before the next Agent or recording can acquire the codec.
struct NativeAudioPlaybackScope {
  ~NativeAudioPlaybackScope() { cardputerAudioClear(); }
};

bool drainI2sPlayback(uint32_t writtenBytes, uint8_t channels, uint32_t sampleRate) {
  if (writtenBytes == 0 || channels == 0 || sampleRate == 0) return true;
  uint32_t frameBytes = static_cast<uint32_t>(channels) * sizeof(int16_t);
  uint32_t silenceFrames = (sampleRate * kMcuAudioTailSilenceMs) / 1000UL;
  int16_t silence[128 * 2] = {};
  while (silenceFrames > 0) {
    uint32_t frames = min<uint32_t>(silenceFrames, 128);
    size_t written = 0;
    esp_err_t err = cardputerAudioWrite(silence, frames * frameBytes, &written, pdMS_TO_TICKS(1000));
    if (err != ESP_OK || written != frames * frameBytes) {
      lastAudioDetail = F("audio tail write failed"); return false;
    }
    silenceFrames -= written / frameBytes;
    yield();
  }
  uint32_t totalMs = (writtenBytes / frameBytes) * 1000UL / sampleRate;
  uint32_t drainMs = min<uint32_t>(max<uint32_t>(totalMs / 4, kMcuAudioDrainMinMs), kMcuAudioDrainMaxMs);
  // Source-release callbacks precede the physical DMA tail. First drain the
  // continuous queue, then retain the existing codec/DMA tail allowance.
  if (!cardputerAudioDrain(pdMS_TO_TICKS(2000))) {
    lastAudioDetail = F("audio queue drain timed out"); return false;
  }
  delay(drainMs);
  yield();
  return true;
}

uint16_t sampleMagnitude(int16_t sample) {
  return sample == INT16_MIN ? 32768 : static_cast<uint16_t>(sample < 0 ? -sample : sample);
}

enum class VoiceInputChannel : uint8_t {
  Undecided,
  Left,
  Right,
  Mixed,
};

bool listeningMonitorReady = false;
const char *voiceInputChannelName(VoiceInputChannel channel) {
  switch (channel) {
    case VoiceInputChannel::Left:
      return "left";
    case VoiceInputChannel::Right:
      return "right";
    case VoiceInputChannel::Mixed:
      return "mixed";
    default:
      return "undecided";
  }
}

void updateVoiceInputChannel(VoiceInputChannel &channel, const int16_t *samples, size_t count) {
  if (channel != VoiceInputChannel::Undecided || !samples) return;

  uint16_t leftPeak = 0;
  uint16_t rightPeak = 0;
  uint64_t leftSquares = 0;
  uint64_t rightSquares = 0;
  for (size_t i = 0; i + 1 < count; i += 2) {
    uint16_t leftMag = sampleMagnitude(samples[i]);
    uint16_t rightMag = sampleMagnitude(samples[i + 1]);
    if (leftMag > leftPeak) leftPeak = leftMag;
    if (rightMag > rightPeak) rightPeak = rightMag;
    leftSquares += static_cast<uint64_t>(leftMag) * static_cast<uint64_t>(leftMag);
    rightSquares += static_cast<uint64_t>(rightMag) * static_cast<uint64_t>(rightMag);
  }

  if (leftPeak < kVoiceVadPeakStart && rightPeak < kVoiceVadPeakStart) return;

  constexpr uint64_t kDominanceRatio = 4;
  if (leftPeak >= kVoiceVadPeakStart &&
      (rightPeak < kVoiceVadPeakStart || leftSquares > rightSquares * kDominanceRatio)) {
    channel = VoiceInputChannel::Left;
  } else if (rightPeak >= kVoiceVadPeakStart &&
             (leftPeak < kVoiceVadPeakStart || rightSquares > leftSquares * kDominanceRatio)) {
    channel = VoiceInputChannel::Right;
  } else {
    channel = VoiceInputChannel::Mixed;
  }
}

int16_t voiceInputMonoSample(int16_t left, int16_t right, VoiceInputChannel channel) {
  if (channel == VoiceInputChannel::Left) return shapeVoiceInputSample(left);
  if (channel == VoiceInputChannel::Right) return shapeVoiceInputSample(right);

  int32_t mixed = (static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2;
  return shapeVoiceInputSample(static_cast<int16_t>(mixed));
}

struct OfficialRecordingState {
  bool active=false,automatic=false,heardSpeech=false,stopping=false;
  uint32_t epoch=0,keys=0,navigation=0,startedAt=0,releaseAt=0,lastSpeechAt=0,frames=0,activeSamples=0,frameLimit=0;
  uint16_t peak=0;
  uint64_t squares=0;
  VoiceInputChannel channel=VoiceInputChannel::Undecided;
};
OfficialRecordingState officialRecording;

bool officialVoiceNetworkBusy() {
  return officialRecording.active || officialVoiceJob.busy() || officialVoiceDrainPending || officialVoiceOutputPlaying;
}

void resetListeningMonitor() { listeningMonitorReady=false; }


void initAudioHardware() {
  i2sReady = es8311Ready = es8311Found = cardputerAudioBegin();
  paEnabled = false;
  lastAudioDetail = i2sReady ? F("Cardputer ADV ES8311 via M5Unified") : F("Cardputer ADV audio initialization failed");
  Serial.printf("Native audio initialized=%d\n", i2sReady ? 1 : 0);
}



String httpFailureDetail(int code, const String &body) {
  String detail = String(F("HTTP ")) + code;
  if (code < 0) {
    detail += F(" ");
    detail += HTTPClient::errorToString(code);
  } else {
    String compactBody = compactDetail(jsonStringValue(body, F("error")));
    if (compactBody.length() == 0) compactBody = compactDetail(jsonStringValue(body, F("reason")));
    if (compactBody.length() == 0) compactBody = compactDetail(body);
    if (compactBody.length() > 0) {
      detail += F(" ");
      detail += compactBody;
    }
  }
  return compactDetail(detail);
}

String endpointLabel(const String &endpointKind, uint16_t httpPort) {
  if (endpointKind == F("web")) return F("Web 端");
  if (endpointKind == F("desktop")) return F("桌面端");
  return String(F("自定义端口 ")) + httpPort;
}

String lanDeviceSourceLabel(const LanDevice &device) {
  if (device.manualSource) return F("手动");
  if (device.remoteSource || device.remoteLogin) return F("远程");
  return F("局域网");
}

bool isActiveLanDevice(const LanDevice &device) {
  bool activeRemote = mcuSocketRelayUrl.length() > 0;
  if (device.remoteSource != activeRemote) return false;
  String deviceKey = device.id + F("|") + device.endpointKind + F("|") + String(device.httpPort);
  // Remote machines commonly advertise the same loopback base URL. Select by
  // the saved machine identity rather than whichever entry happens to be first.
  if (device.remoteSource && activeDeviceKey.length() > 0) return deviceKey == activeDeviceKey;
  if (activeDeviceUrl.length() > 0) return device.url == activeDeviceUrl;
  return activeDeviceKey.length() > 0 && deviceKey == activeDeviceKey;
}

String lanDeviceDisplayUrl(const LanDevice &device) {
  if (device.displayUrl.length() > 0) return device.displayUrl;
  return device.url;
}

String cleanAgentVersion(String version) {
  int upstreamIndex = version.indexOf(F(" · upstream "));
  if (upstreamIndex < 0) upstreamIndex = version.indexOf(F(" upstream "));
  if (upstreamIndex >= 0) version = version.substring(0, upstreamIndex);
  version.trim();
  return version;
}

bool shouldUseRemoteLogin(const LanDevice &) { return false; }

String lanDeviceKey(const String &id, const String &endpointKind, uint16_t httpPort) {
  return id + F("|") + endpointKind + F("|") + String(httpPort);
}

String lanDeviceKey(const LanDevice &device) {
  return lanDeviceKey(device.id, device.endpointKind, device.httpPort);
}

String lanAddressKey(const LanDevice &device) {
  return device.ip + F("|") + device.endpointKind + F("|") + String(device.httpPort);
}

const char* webDeviceSource(bool remote) { return remote?"remote":"lan"; }

String webDeviceTargetQuery(const LanDevice& device) {
  return String("device_key=")+officialUrlEncode(lanDeviceKey(device))+"&device_url="+
      officialUrlEncode(device.url)+"&device_source="+webDeviceSource(device.remoteSource);
}

void appendWebDeviceTarget(String& html,const String& key,const String& url,bool remote) {
  html+=F("<input type='hidden' name='device_key' value='");html+=escapeHtml(key);
  html+=F("'><input type='hidden' name='device_url' value='");html+=escapeHtml(url);
  html+=F("'><input type='hidden' name='device_source' value='");html+=webDeviceSource(remote);html+=F("'>");
}

bool requestWebDevice(LanDevice** output,bool allowLegacyDisplayIndex=false) {
  if(!output) return false;
  *output=nullptr;
  const bool hasTarget=server.hasArg("device_key")||server.hasArg("device_url")||server.hasArg("device_source");
  if(!hasTarget && allowLegacyDisplayIndex) {
    // Legacy GET links only display the selected name/address. Credential or
    // Profile POSTs NEVER trust a mutable discovery-array index.
    if(!server.hasArg("i")) return false;
    const int index=server.arg("i").toInt();
    if(index<0 || index>=lanDeviceCount) return false;
    *output=&lanDevices[index];return true;
  }
  const String key=server.arg("device_key"),url=server.arg("device_url"),source=server.arg("device_source");
  const device_web_target::View submitted{key.c_str(),url.c_str(),source.c_str()};
  if(!device_web_target::valid(submitted)) return false;
  for(int i=0;i<lanDeviceCount;++i) {
    const auto& candidate=lanDevices[i];const String candidateKey=lanDeviceKey(candidate);
    if(device_web_target::same(submitted,{candidateKey.c_str(),candidate.url.c_str(),webDeviceSource(candidate.remoteSource)})) {
      if(*output) {*output=nullptr;return false;} // An ambiguous target is rejected.
      *output=&lanDevices[i];
    }
  }
  return *output!=nullptr;
}

bool webDeviceIsLoggedInTarget(const LanDevice& device) {
  const String key=lanDeviceKey(device);
  return mcuAuthToken.length() && device_web_target::same(
      {key.c_str(),device.url.c_str(),webDeviceSource(device.remoteSource)},
      {activeDeviceKey.c_str(),activeDeviceUrl.c_str(),webDeviceSource(mcuSocketRelayUrl.length()>0)});
}

bool activeSourceMatchesDevice(const LanDevice &device, bool activeRemote) {
  return device.remoteSource == activeRemote;
}

bool activeIdentityMatchesDevice(const LanDevice &device, const String &activeKey, const String &activeAddr,
                                 const String &activeUrl, bool activeRemote) {
  if (!activeSourceMatchesDevice(device, activeRemote)) return false;
  if (device.remoteSource && activeKey.length() > 0) return activeKey == lanDeviceKey(device);
  if (activeUrl.length() > 0) return device.url == activeUrl;
  return (activeKey.length() > 0 && activeKey == lanDeviceKey(device)) ||
         (activeAddr.length() > 0 && activeAddr == lanAddressKey(device));
}

uint32_t fnv1a(const String &value) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < value.length(); ++i) {
    hash ^= static_cast<uint8_t>(value[i]);
    hash *= 16777619UL;
  }
  return hash;
}

String profilePrefKey(const String &deviceKey) {
  return String(F("p")) + String(fnv1a(deviceKey), HEX);
}

String accountPrefKey(const String &deviceKey) {
  return String(F("a")) + String(fnv1a(deviceKey), HEX);
}

String passwordPrefKey(const String &deviceKey) {
  return String(F("w")) + String(fnv1a(deviceKey), HEX);
}

void persistCredentialsForDevice(const String &deviceKey, const String &addressKey, const String &account,
                                 const String &password, bool activeRemote) {
  prefs.begin("stockLan", false);
  prefs.putString("active_key", deviceKey);
  prefs.putString("active_addr", addressKey);
  prefs.putBool("active_remote", activeRemote);
  prefs.putString("cur_account", account);
  prefs.putString("cur_password", password);
  prefs.end();
}

void persistProfileForDevice(const String &deviceKey, const String &addressKey, const String &profile,
                             const String &url, bool activeRemote) {
  prefs.begin("stockLan", false);
  prefs.putString("cur_profile", profile);
  prefs.putString("active_key", deviceKey);
  prefs.putString("active_addr", addressKey);
  prefs.putString("active_url", url);
  prefs.putBool("active_remote", activeRemote);
  prefs.end();
  activeDeviceKey = deviceKey;
  activeDeviceUrl = url;
}

void clearProfileForDevice(const LanDevice &device) {
  String deviceKey = lanDeviceKey(device);
  String addressKey = lanAddressKey(device);
  prefs.begin("stockLan", false);
  String activeKey = prefs.getString("active_key", "");
  String activeAddr = prefs.getString("active_addr", "");
  String activeUrl = prefs.getString("active_url", "");
  String relayUrl = prefs.getString("relay_url", "");
  bool activeRemote = prefs.getBool("active_remote", relayUrl.length() > 0);
  if (activeIdentityMatchesDevice(device, activeKey, activeAddr, activeUrl, activeRemote)) {
    prefs.remove("active_key");
    prefs.remove("active_addr");
    prefs.remove("active_url");
    prefs.remove("active_remote");
    prefs.remove("relay_url");
    prefs.remove("auth_token");
    prefs.remove("cur_account");
    prefs.remove("cur_password");
    prefs.remove("cur_profile");
  }
  prefs.end();
  if (isActiveLanDevice(device)) {
    activeDeviceKey = "";
    activeDeviceUrl = "";
    mcuSocketRelayUrl = "";
    mcuAuthToken = "";
    disconnectMcuSocketClient();
  }
}

void applySavedProfile(LanDevice &device) {
  prefs.begin("stockLan", true);
  String activeKey = prefs.getString("active_key", "");
  String activeAddr = prefs.getString("active_addr", "");
  String activeUrl = prefs.getString("active_url", "");
  String relayUrl = prefs.getString("relay_url", "");
  bool activeRemote = prefs.getBool("active_remote", relayUrl.length() > 0);
  String profile = prefs.getString("cur_profile", "");
  if (profile.length() == 0) profile = prefs.getString("current_profile", "");
  prefs.end();
  bool isCurrent = activeIdentityMatchesDevice(device, activeKey, activeAddr, activeUrl, activeRemote);
  profile.trim();
  device.profile = isCurrent ? profile : "";
  device.loggedIn = isCurrent && profile.length() > 0;
  device.remoteLogin = activeRemote && isCurrent;
  if (activeDeviceKey.length() == 0) {
    if (activeIdentityMatchesDevice(device, activeKey, activeAddr, activeUrl, activeRemote)) {
      activeDeviceKey = lanDeviceKey(device);
    }
  }
}

String activeDeviceLabel() {
  if (activeDeviceKey.length() == 0) return "";
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (isActiveLanDevice(lanDevices[i])) {
      return endpointLabel(lanDevices[i].endpointKind, lanDevices[i].httpPort) + F(" · ") +
             lanDeviceSourceLabel(lanDevices[i]) + F(" · ") + lanDevices[i].name + F(" · ") +
             lanDevices[i].profile;
    }
  }
  return activeDeviceKey;
}

int findLanDevice(const String &id, const String &endpointKind, uint16_t httpPort) {
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (lanDevices[i].id == id && lanDevices[i].endpointKind == endpointKind &&
        lanDevices[i].httpPort == httpPort && !lanDevices[i].remoteSource) {
      return i;
    }
  }
  return -1;
}

int findLanDeviceByAddress(const String &ip, const String &endpointKind, uint16_t httpPort) {
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (lanDevices[i].ip == ip && lanDevices[i].endpointKind == endpointKind &&
        lanDevices[i].httpPort == httpPort && !lanDevices[i].remoteSource) {
      return i;
    }
  }
  return -1;
}

int findRemoteLanDevice(const String &id, const String &endpointKind, uint16_t httpPort) {
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (lanDevices[i].id == id && lanDevices[i].endpointKind == endpointKind &&
        lanDevices[i].httpPort == httpPort && lanDevices[i].remoteSource) {
      return i;
    }
  }
  return -1;
}

int oldestLanDeviceSlot() {
  int slot = 0;
  for (int i = 1; i < kMaxLanDevices; ++i) {
    if (lanDevices[i].lastSeenMs < lanDevices[slot].lastSeenMs) slot = i;
  }
  return slot;
}

void rememberLanDeviceInfo(const String &json, const String &host, const String &baseUrl, uint32_t responseMs,
                           bool requireAnnouncement, bool manualSource = false, bool remoteSource = false) {
  if (requireAnnouncement) {
    if (jsonStringValue(json, F("type")) != F("hermes.announce")) return;
    if (jsonIntValue(json, F("version")) != 1) return;
  }
  String id = jsonStringValue(json, F("device_id"));
  String name = jsonStringValue(json, F("computer_name"));
  String endpointKind = jsonStringValue(json, F("endpoint_kind"));
  uint16_t httpPort = static_cast<uint16_t>(jsonIntValue(json, F("http_port")));
  if (id.length() == 0 || httpPort == 0) return;
  if (endpointKind.length() == 0) {
    endpointKind = httpPort == 8648 ? F("web") : (httpPort == 8748 ? F("desktop") : F("custom"));
  }

  int slot = remoteSource ? findRemoteLanDevice(id, endpointKind, httpPort) : findLanDevice(id, endpointKind, httpPort);
  if (!remoteSource && slot < 0 && host.length() > 0) {
    slot = findLanDeviceByAddress(host, endpointKind, httpPort);
  }
  if (slot < 0) {
    slot = lanDeviceCount < kMaxLanDevices ? lanDeviceCount++ : oldestLanDeviceSlot();
  }

  LanDevice &device = lanDevices[slot];
  device.id = id;
  device.name = name.length() > 0 ? name : id;
  device.ip = host;
  device.httpPort = httpPort;
  device.endpointKind = endpointKind;
  device.url = baseUrl.length() > 0 ? baseUrl : String(F("http://")) + device.ip + F(":") + httpPort;
  device.webVersion = jsonStringValue(json, F("hermes_web_ui_version"));
  device.agentVersion = jsonStringValue(json, F("hermes_agent_version"));
  device.relayUrl = jsonStringValue(json, F("relay_url"));
  device.relayUrl.trim();
  device.displayUrl = remoteSource ? jsonStringValue(json, F("remote_http_url")) : "";
  device.displayUrl.trim();
  if (remoteSource && device.displayUrl.length() == 0) device.displayUrl = device.relayUrl;
  device.responseMs = responseMs;
  device.lastSeenMs = millis();
  device.manualSource = manualSource;
  device.remoteSource = remoteSource;
  applySavedProfile(device);
}

void rememberLanDevice(const String &json, const IPAddress &remoteIp, uint32_t responseMs) {
  rememberLanDeviceInfo(json, remoteIp.toString(), "", responseMs, true, false);
}

String manualDevicePrefKey(int index) {
  return String(F("manual_")) + index;
}

void importLegacyLanPreferences() {
  Preferences target,legacy;
  if(!target.begin("stockLan",false)) {reportMcuPreferencesFailure("stock_config_open",ESP_FAIL);return;}
  if(target.getUChar("schema",0)==1) {target.end();return;}
  nvs_handle_t probe=0;const esp_err_t opened=nvs_open("mcu",NVS_READONLY,&probe);
  if(opened==ESP_ERR_NVS_NOT_FOUND) {target.putUChar("schema",1);target.end();return;}
  if(opened!=ESP_OK) {target.end();reportMcuPreferencesFailure("legacy_config_probe",opened);return;}
  nvs_close(probe);
  if(!legacy.begin("mcu",true)) {target.end();reportMcuPreferencesFailure("legacy_config_read",ESP_FAIL);return;}
  bool ok=true;
  const bool isLan=!legacy.getBool("active_remote",legacy.getString("relay_url","").length()>0);
  if(isLan) {
    const char* keys[]={"active_key","active_addr","active_url","cur_account","cur_password","cur_profile","auth_token"};
    for(const char* key:keys) {
      if(target.isKey(key)) continue;
      const String value=legacy.getString(key,"");
      if(value.length()) ok=(target.putString(key,value)==value.length()) && ok;
    }
    if(!target.isKey("active_remote")) ok=target.putBool("active_remote",false)>0 && ok;
  }
  if(!target.isKey("agent_id")) {
    const String agent=legacy.getString("agent_id","ekko-agent");ok=target.putString("agent_id",agent)>0 && ok;
  }
  if(!target.isKey("volume")) ok=target.putUChar("volume",legacy.getUChar("volume",kDefaultOutputVolumePercent))>0 && ok;
  if(!target.isKey("idle_min")) ok=target.putUChar("idle_min",legacy.getUChar("idle_min",kDefaultIdlePowerSaveMinutes))>0 && ok;
  legacy.end();if(ok) target.putUChar("schema",1);target.end();
  if(!ok) reportMcuPreferencesFailure("stock_config_import",ESP_FAIL);
}

String normalizedManualDeviceUrl(const String &input, String *hostOut = nullptr) {
  String raw = input;
  raw.trim();
  if (raw.length() == 0) return "";
  if (raw.indexOf(F("://")) < 0) raw = String(F("http://")) + raw;

  String scheme;
  String host;
  uint16_t port = 0;
  String path;
  if (!parseAudioUrl(raw, &scheme, &host, &port, &path)) return "";
  if (hostOut) *hostOut = host;
  return scheme + F("://") + host + F(":") + String(port);
}

bool manualDeviceUrlExists(const String &url) {
  prefs.begin("stockLan", true);
  int count = prefs.getInt("manual_count", 0);
  for (int i = 0; i < count && i < kMaxManualDevices; ++i) {
    if (prefs.getString(manualDevicePrefKey(i).c_str(), "") == url) {
      prefs.end();
      return true;
    }
  }
  prefs.end();
  return false;
}

void persistManualDeviceUrl(const String &url) {
  if (url.length() == 0 || manualDeviceUrlExists(url)) return;
  prefs.begin("stockLan", false);
  int count = prefs.getInt("manual_count", 0);
  if (count < 0) count = 0;
  if (count >= kMaxManualDevices) count = kMaxManualDevices - 1;
  prefs.putString(manualDevicePrefKey(count).c_str(), url);
  prefs.putInt("manual_count", min(count + 1, kMaxManualDevices));
  prefs.end();
}

bool fetchManualDeviceInfo(const String &input, String *error) {
  String host;
  String baseUrl = normalizedManualDeviceUrl(input, &host);
  if (baseUrl.length() == 0) {
    if (error) *error = F("机器地址无效");
    return false;
  }

  HTTPClient http;
  http.setTimeout(kMcuLoginTimeoutMs);
  String endpoint = baseUrl + F("/api/devices/link-info");
  uint32_t started = millis();
  if (!http.begin(endpoint)) {
    if (error) *error = F("无法打开机器信息接口");
    return false;
  }
  int code = http.GET();
  String body = http.getString();
  http.end();
  if (code < 200 || code >= 300) {
    if (error) *error = httpFailureDetail(code, body);
    return false;
  }
  if (jsonStringValue(body, F("device_id")).length() == 0 ||
      jsonStringValue(body, F("device_public_key")).length() == 0) {
    if (error) *error = F("机器信息缺少设备身份");
    return false;
  }
  rememberLanDeviceInfo(body, host, baseUrl, millis() - started, false, true);
  persistManualDeviceUrl(baseUrl);
  return true;
}

void refreshManualDevices() {
  if (!wifiReady || WiFi.status() != WL_CONNECTED) return;
  prefs.begin("stockLan", true);
  int count = prefs.getInt("manual_count", 0);
  String urls[kMaxManualDevices];
  for (int i = 0; i < count && i < kMaxManualDevices; ++i) {
    urls[i] = prefs.getString(manualDevicePrefKey(i).c_str(), "");
  }
  prefs.end();
  for (int i = 0; i < count && i < kMaxManualDevices; ++i) {
    if (urls[i].length() == 0) continue;
    String ignored;
    fetchManualDeviceInfo(urls[i], &ignored);
  }
}



IPAddress lanBroadcastIp() {
  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  return IPAddress(ip[0] | static_cast<uint8_t>(~mask[0]), ip[1] | static_cast<uint8_t>(~mask[1]),
                   ip[2] | static_cast<uint8_t>(~mask[2]), ip[3] | static_cast<uint8_t>(~mask[3]));
}

void sendLanDiscoveryPacket(const IPAddress &target, const String &requestId) {
  String packet = String(F("{\"type\":\"hermes.discover\",\"version\":1,\"request_id\":\"")) + requestId + F("\"}");
  lanUdp.beginPacket(target, kHermesDiscoveryPort);
  lanUdp.print(packet);
  lanUdp.endPacket();
}

void scanLanDevices() {
  if (!wifiReady || WiFi.status() != WL_CONNECTED) return;
  if (!lanUdpReady) {
    lanUdpReady = lanUdp.begin(kLanDiscoveryLocalPort);
  }
  if (!lanUdpReady) return;

  String requestId = String(millis(), HEX);
  uint32_t started = millis();
  sendLanDiscoveryPacket(IPAddress(255, 255, 255, 255), requestId);
  IPAddress broadcast = lanBroadcastIp();
  if (broadcast != IPAddress(255, 255, 255, 255)) {
    sendLanDiscoveryPacket(broadcast, requestId);
  }

  while (millis() - started < kLanDiscoveryTimeoutMs) {
    int packetSize = lanUdp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      yield();
      continue;
    }
    char buffer[1600];
    int len = lanUdp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0) continue;
    buffer[len] = '\0';
    String json(buffer);
    if (jsonStringValue(json, F("request_id")) != requestId) continue;
    rememberLanDevice(json, lanUdp.remoteIP(), millis() - started);
  }
  lastLanDiscoveryAtMs = millis();
}

void refreshDeviceDiscovery() {
  if(officialVoiceNetworkBusy() || officialSelectionJob.busy() || officialHistoryJob.busy()) return;
  lanDeviceCount=0;scanLanDevices();refreshManualDevices();
}

bool ssidAlreadyScanned(const String &ssid) {
  for (int i = 0; i < scannedNetworkCount; ++i) {
    if (scannedSsids[i] == ssid) return true;
  }
  return false;
}

void scanWifiList() {
  scannedNetworkCount = 0;
  for (int i = 0; i < kMaxScannedNetworks; ++i) {
    scannedSsids[i] = "";
    scannedRssi[i] = 0;
    scannedEncrypted[i] = false;
  }

  setOledStatus(OledMode::Think, F("WIFI"), F("SCAN"), 25);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(120);

  int found = WiFi.scanNetworks(false, true);
  if (found <= 0) {
    WiFi.scanDelete();
    return;
  }

  for (int i = 0; i < found && scannedNetworkCount < kMaxScannedNetworks; ++i) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0 || ssidAlreadyScanned(ssid)) continue;
    int target = scannedNetworkCount++;
    scannedSsids[target] = ssid;
    scannedRssi[target] = WiFi.RSSI(i);
    scannedEncrypted[target] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
}

bool scannedNetworkHasSsid(const String &ssid) {
  if (ssid.length() == 0) return false;
  for (int i = 0; i < scannedNetworkCount; ++i) {
    if (scannedSsids[i] == ssid) return true;
  }
  return false;
}

String pageStart(const String &title) {
  String html = String(F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
                         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                         "<title>")) +
                escapeHtml(title) +
                F("</title><style>"
                  "@font-face{font-family:CardputerPixel;src:url('/font/fusion-pixel.woff2') format('woff2');font-style:normal;font-weight:400;font-display:swap}"
                  ":root{--bg:#fafafa;--surface:#fff;--line:#e0e0e0;--ink:#1a1a1a;--muted:#666;--accent:#333;--warn:#b4432d;--good:#2f7d4c}"
                  "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font-family:CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;letter-spacing:0}"
	                  "main{max-width:760px;margin:auto;padding:28px 18px 40px}.panel,.card{background:var(--surface);border:1px solid var(--line);border-radius:6px}.panel{padding:22px}.card{padding:16px;margin-top:12px}"
	                  "h1{font-size:clamp(28px,5vw,44px);line-height:1.05;margin:0 0 10px}h2{font-size:18px;margin:0 0 10px}.lead,.hint{color:var(--muted);line-height:1.6}.lead{font-size:15px;margin:0 0 18px}.hint{font-size:12px;margin:0}.meta{font:12px/1.4 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;color:var(--muted);word-break:break-all}"
	                  "form{display:grid;gap:12px}.field{display:grid;gap:7px}.label{font-size:12px;color:var(--muted)}input,select{width:100%;min-height:42px;border:1px solid var(--line);border-radius:6px;background:#fff;padding:10px 11px;font:14px/1.2 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;color:var(--ink)}select{appearance:none;-webkit-appearance:none;background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 18px) 18px,calc(100% - 13px) 18px;background-size:5px 5px,5px 5px;background-repeat:no-repeat;padding-right:34px}input:focus,select:focus{outline:2px solid #cfcfcf;outline-offset:1px}"
	                  ".choice-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.choice{position:relative;display:block}.choice input{position:absolute;opacity:0;pointer-events:none}.choice-card{height:100%;min-height:96px;border:1px solid var(--line);border-radius:6px;background:#fff;padding:14px 14px 14px 42px;display:grid;align-content:start;gap:6px;cursor:pointer;transition:border-color .15s,background .15s,box-shadow .15s}.choice-dot{position:absolute;left:14px;top:16px;width:16px;height:16px;border:1px solid #aaa;border-radius:50%;background:#fff}.choice-title{font:700 15px/1.2 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif}.choice-copy{font-size:12px;line-height:1.5;color:var(--muted)}.choice-meta{margin-top:2px;font:700 10px/1 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;color:#888;text-transform:uppercase}.choice input:checked+.choice-card{border-color:var(--accent);background:#f7f7f7;box-shadow:inset 0 0 0 1px var(--accent)}.choice input:checked+.choice-card .choice-dot{border-color:var(--accent);box-shadow:inset 0 0 0 4px #fff;background:var(--accent)}.choice input:focus+.choice-card{outline:2px solid #cfcfcf;outline-offset:1px}@media(max-width:560px){.choice-grid{grid-template-columns:1fr}.choice-card{min-height:84px}}"
	                  ".tabs{display:flex;gap:8px;border-bottom:1px solid var(--line);margin:18px -22px 18px;padding:0 22px}.tab{border:1px solid var(--line);border-bottom:0;border-radius:6px 6px 0 0;background:#f4f4f4;min-height:38px;padding:10px 14px;font:600 13px/1 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;color:inherit;text-decoration:none}.tab.active{background:var(--surface);position:relative;top:1px}.info-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.provision-code{margin-bottom:18px}.info-row{border:1px solid var(--line);border-radius:6px;padding:12px;min-width:0}.info-row span{display:block;color:var(--muted);font-size:12px;margin-bottom:7px}.info-row strong{display:block;font:600 14px/1.35 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;word-break:break-all}@media(max-width:560px){.info-grid{grid-template-columns:1fr}}"
	                  ".btn{border:1px solid var(--line);background:var(--surface);border-radius:6px;min-height:38px;padding:9px 13px;font:600 13px/1 CardputerPixel,'PingFang SC','Noto Sans CJK SC',sans-serif;color:inherit;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;cursor:pointer}.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}.btn.warn{background:var(--warn);border-color:var(--warn);color:#fff}.btn-row{display:flex;gap:10px;flex-wrap:wrap}.ok{color:var(--good)}.bad{color:var(--warn)}"
	                  "</style></head><body><main>");
  return html;
}

String pageEnd() {
  return F("</main></body></html>");
}

void sendWifiPage() {
  String savedSsid = prefString("ssid");
  if (savedSsid.length() == 0 && wifiReady && WiFi.status() == WL_CONNECTED) {
    savedSsid = WiFi.SSID();
  }

  String html = pageStart(F("连接 Wi-Fi"));
  html += F("<section class='panel'><p class='meta'>Ekko Studio Cardputer ADV</p><h1>连接局域网 Wi-Fi</h1>");
  if (wifiReady) {
    html += F("<p class='lead ok'>当前已联网：");
    html += escapeHtml(WiFi.SSID());
    html += F(" · IP ");
    html += WiFi.localIP().toString();
    html += F("</p>");
  } else {
    html += F("<p class='lead'>设备当前处于配网模式。连接热点 <strong>");
    html += kApName;
    html += F("</strong> 后，在这里选择或输入家里的 Wi-Fi。</p>");
  }

  html += F("<div class='info-grid provision-code'>");
  appendInfoRow(html, F("设备码"), mcuDeviceCode());
  html += F("</div>");

  html += F("<form method='post' action='/wifi'>");
  if (scannedNetworkCount > 0) {
    html += F("<div class='field'><span class='label'>SSID</span><select name='ssid'>");
    html += F("<option value=''>选择 Wi-Fi</option>");
    for (int i = 0; i < scannedNetworkCount; ++i) {
      html += F("<option value='");
      html += escapeHtml(scannedSsids[i]);
      html += F("'");
      html += F(">");
      html += escapeHtml(scannedSsids[i]);
      html += F(" · ");
      html += scannedRssi[i];
      html += F(" dBm");
      if (!scannedEncrypted[i]) html += F(" · 开放");
      html += F("</option>");
    }
    html += F("</select></div>");
    html += F("<div class='field'><span class='label'>手动 SSID</span><input name='ssid_manual' autocomplete='off' value=''></div>");
  } else {
    html += F("<p class='hint'>没有扫到附近 Wi-Fi，请手动输入 SSID。</p>");
    html += F("<div class='field'><span class='label'>SSID</span><input name='ssid' autocomplete='off' required value=''></div>");
  }
  html += F("<div class='field'><span class='label'>密码</span><input name='pass' type='password' autocomplete='current-password' placeholder='开放网络可留空'></div>");
  html += F("<div class='btn-row'><button class='btn primary' type='submit'>保存并连接</button></div>");
  html += F("<p class='hint'>配网热点：");
  html += kApName;
  html += F("，无需密码。当前设备地址：http://");
  html += currentIp();
  html += F("/</p></form></section>");

  if (wifiReady || savedSsid.length() > 0) {
    html += F("<form method='post' action='/clear' class='card'><button class='btn primary' type='submit'>重新配置 Wi-Fi</button></form>");
    html += F("<form method='post' action='/wifi/forget' class='card'><button class='btn warn' type='submit'>清除所有 Wi-Fi 历史</button></form>");
  }
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void sendStatusPage() {
  String html = pageStart(F("设备已联网"));
  html += F("<section class='panel'><p class='meta'>Ekko Studio Cardputer ADV</p><h1>设备</h1><p class='lead'>");
  html += escapeHtml(WiFi.SSID());
  html += F(" · IP ");
  html += WiFi.localIP().toString();
  html += F("</p><nav class='tabs'><a class='tab active' href='/device'>设备</a><a class='tab' href='/ota'>OTA</a></nav>");

  html += F("<h2>本机</h2><div class='info-grid'>");
  appendInfoRow(html, F("Wi-Fi"), WiFi.SSID());
  appendInfoRow(html, F("IP"), WiFi.localIP().toString());
  appendInfoRow(html, F("MAC"), WiFi.macAddress());
  appendInfoRow(html, F("设备码"), mcuDeviceCode());
  appendInfoRow(html, F("固件版本"), String(kMcuFirmwareVersion));
  appendInfoRow(html, F("信号"), String(WiFi.RSSI()) + F(" dBm"));
  appendInfoRow(html, F("运行时间"), uptimeText());
  appendInfoRow(html, F("可用内存"), String(ESP.getFreeHeap()) + F(" bytes"));
  appendInfoRow(html, F("服务连接"), mcuSocketStateLabel());
  appendInfoRow(html, F("后台设备状态"), mcuStatusNamespaceReady?String("在线"):String("尚未同步"));
  if (mcuSocketRelayUrl.length() > 0) {
    appendInfoRow(html, F("远程转发"), mcuSocketRelayUrl);
  }
  appendInfoRow(html, F("音频硬件"), String(es8311Ready && i2sReady ? F("就绪") : F("未就绪")) + F(" · ") + userFacingDetail(lastAudioDetail,"音频状态已更新"));
  appendInfoRow(html, F("音量"), String(outputVolumePercent) + F("%"));
  appendInfoRow(html, F("语音模式"), listeningModeEnabled ? F("自动监听") : F("按住说话"));
  appendInfoRow(html, F("Agent"), mcuAgentRuntimeLabel());
  appendInfoRow(html, F("自动待机"), idlePowerSaveMinutes == 0 ? String(F("关闭")) : String(idlePowerSaveMinutes) + F(" 分钟"));
  if (selectedProfile.length() > 0) {
    appendInfoRow(html, F("最近 Profile"), selectedProfile);
  }
  String activeLabel = activeDeviceLabel();
  if (activeLabel.length() > 0) {
    appendInfoRow(html, F("当前交互"), activeLabel);
  }
  appendInfoRow(html, F("设备状态"), interactionStatusLabel());
  appendInfoRow(html, F("音频队列"), String(mcuAudioPlaying ? F("播放中 · ") : F("")) + String(mcuAudioCount));
  html += F("</div>");

  html += F("</section>");

  html += F("<section class='card'><h2>Agent</h2><form method='post' action='/device/agent'><select name='runtime'>");
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    html += F("<option value='"); html += kNativeAgents[i].id; html += F("'");
    if (i == agentSelection.active) html += F(" selected");
    html += F(">"); html += kNativeAgents[i].name; html += F("</option>");
  }
  html += F("</select><button class='btn primary' type='submit'>选择 Agent</button></form><p class='hint'>选择页左 , 或上 ; 选前一个，右 / 或下 . 选后一个，无需 Fn。OK 进入对话，Esc 返回。对话页直接打字、OK发送；Fn+空格或顶部G0长按说话，松开发送；Fn+方向编辑或翻记录。配置就绪后可以发送；实际模型或 Agent 错误会显示在聊天页。</p></section>");

  html += F("<section class='card'><h2>省电</h2>");
  html += F("<form method='post' action='/device/power-save'><div class='field'><span class='label'>无交互后自动待机 <output id='idle-output'>");
  html += idlePowerSaveMinutes;
  html += F(" 分钟</output></span><input name='minutes' type='range' min='0' max='");
  html += String(kMaxIdlePowerSaveMinutes);
  html += F("' step='1' value='");
  html += idlePowerSaveMinutes;
  html += F("' oninput=\"document.getElementById('idle-output').textContent=this.value+' 分钟'\"></div>");
  html += F("<div class='btn-row'><button class='btn primary' type='submit'>保存待机时间</button></div>");
  html += F("<p class='hint'>默认 3 分钟；设为 0 可关闭自动待机。待机不会断开 Wi-Fi、登录或 Socket。</p></form></section>");

  html += F("<section class='card'><h2>手动添加机器</h2>");
  html += F("<form method='post' action='/device/manual'><div class='field'><span class='label'>地址</span>");
  html += F("<input name='url' autocomplete='off' placeholder='http://192.168.1.10:8648' required></div>");
  html += F("<div class='btn-row'><button class='btn primary' type='submit'>添加</button></div>");
  html += F("<p class='hint'>会请求对端公开机器信息接口并保存，已存在的机器会自动去重。</p></form></section>");

  html += F("<section class='card'><h2>可连接机器</h2>");
  html += F("<p class='hint'>此版本使用局域网标准接口。请让 M5 与电脑连接同一网络，或手动添加电脑地址。</p>");
  int visibleDeviceCount = 0;
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (millis() - lanDevices[i].lastSeenMs < kLanDiscoveryStaleMs) ++visibleDeviceCount;
  }
  if (visibleDeviceCount == 0) {
    html += F("<p class='lead'>未发现设备</p>");
  } else {
    html += F("<div class='info-grid'>");
    for (int i = 0; i < lanDeviceCount; ++i) {
      LanDevice &device = lanDevices[i];
      bool online = millis() - device.lastSeenMs < kLanDiscoveryStaleMs;
      if (!online) continue;
      bool isCurrent = isActiveLanDevice(device);
      html += F("<div class='info-row'><span>");
      html += endpointLabel(device.endpointKind, device.httpPort);
      html += F(" · ");
      html += lanDeviceSourceLabel(device);
      if (!isCurrent) {
        html += F(" · 在线");
      }
      if (isCurrent) html += F(" · 当前");
      html += F("</span><strong>");
      html += escapeHtml(device.name);
      html += F("</strong><p class='hint'>");
      html += escapeHtml(lanDeviceDisplayUrl(device));
      if (device.profile.length() > 0) {
        html += F("<br>Profile ");
        html += escapeHtml(device.profile);
      }
      html += F("<br>响应 ");
      html += device.responseMs;
      html += F(" ms");
      if (device.webVersion.length() > 0) {
        html += F(" · Web ");
        html += escapeHtml(device.webVersion);
      }
      if (device.agentVersion.length() > 0) {
        html += F(" · Agent ");
        html += escapeHtml(cleanAgentVersion(device.agentVersion));
      }
      html += F("</p><div class='btn-row'>");
      if (isCurrent && device.loggedIn) {
        html += F("<a class='btn' href='/device/profile?");
        html += escapeHtml(webDeviceTargetQuery(device));
        html += F("'>切换 Profile</a>");
        html += F("<a class='btn primary' href='/device/logout?i=");
        html += i;
        html += F("'>退出登录</a>");
      } else {
        html += F("<a class='btn primary' href='/device/login?");
        html += escapeHtml(webDeviceTargetQuery(device));
        html += F("'>登录</a>");
      }
      html += F("</div></div>");
    }
    html += F("</div>");
  }
  html += F("</section>");

  html += F("<form method='post' action='/clear' class='card'><button class='btn primary' type='submit'>重新配置 Wi-Fi</button></form>");
  html += F("<form method='post' action='/wifi/forget' class='card'><button class='btn warn' type='submit'>清除所有 Wi-Fi 历史</button></form>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

String otaNextCheckText() {
  int32_t remaining = static_cast<int32_t>(nextMcuOtaCheckAtMs - millis());
  if (remaining <= 0) return F("即将检查");
  return String(static_cast<uint32_t>(remaining) / 1000UL) + F(" 秒后");
}

void sendOtaPage(const String &notice = "") {
  String html = pageStart(F("Cardputer ADV 固件"));
  html += F("<section class='card'><h2>ESP32-S3 / Cardputer ADV</h2><p>此硬件通过 USB 安装专用固件；已隔离 ESP32-C3 OTA，避免刷入不兼容镜像。</p><a href='/device'>返回设备管理</a></section>");
  html += escapeHtml(notice); html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void sendOtaUpdatingPage() {
  String html = pageStart(F("OTA"));
  html += F("<section class='panel'><p class='meta'>Ekko Studio Cardputer ADV</p><h1>OTA</h1><p class='lead'>固件正在更新</p>");
  html += F("<p class='hint'>固件正在下载并写入，请勿关闭单片机或断开电源。设备会自动重启，页面检测到恢复后会弹窗提示完成。</p>");
  html += F("<div class='info-grid'>");
  appendInfoRow(html, F("固件版本"), String(kMcuFirmwareVersion));
  appendInfoRow(html, F("当前状态"), F("正在更新，请勿关闭单片机"));
  appendInfoRow(html, F("服务端"), activeDeviceUrl.length() > 0 ? activeDeviceUrl : String(F("未连接")));
  html += F("</div><p id='ota-status' class='hint'>等待设备重启...</p>");
  html += F("<script>");
  html += F("let seenOffline=false,done=false,start=Date.now();");
  html += F("const s=document.getElementById('ota-status');");
  html += F("async function poll(){if(done)return;");
  html += F("try{const r=await fetch('/health?ota='+Date.now(),{cache:'no-store'});");
  html += F("if(r.ok&&seenOffline){done=true;s.textContent='固件更新完成，设备已恢复在线。';alert('固件更新完成，设备已重启。');location.href='/ota';return;}");
  html += F("if(r.ok){s.textContent='正在写入固件，请勿关闭单片机...';}");
  html += F("}catch(e){seenOffline=true;s.textContent='设备正在重启，请等待恢复...';}");
  html += F("if(Date.now()-start>120000&&!done){done=true;s.textContent='更新超时，请重新打开设备页面确认状态。';alert('更新状态确认超时，请重新打开设备页面确认。');return;}");
  html += F("setTimeout(poll,1500)}setTimeout(poll,3000);");
  html += F("</script>");
  html += F("</section>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void handleOtaCheck() {
  server.send(409, F("application/json"), F("{\"ok\":false,\"error\":\"cardputer_s3_usb_update_required\"}"));
}

void scanAndSendStatusPage() {
  if(officialVoiceNetworkBusy() || officialSelectionJob.busy() || officialHistoryJob.busy()) {
    server.sendHeader(F("Retry-After"),F("3"));
    server.send(503,F("text/plain; charset=utf-8"),F("语音或会话同步中，请稍后刷新"));return;
  }
  refreshDeviceDiscovery();
  sendStatusPage();
}

void handleDeviceAudio() {
  String rawVolume = server.arg(F("volume"));
  rawVolume.trim();
  if (rawVolume.length() == 0) {
    server.send(400, F("text/plain; charset=utf-8"), F("缺少音量"));
    return;
  }
  int volume = rawVolume.toInt();
  if (volume < 0) volume = 0;
  if (volume > kMaxOutputVolumePercent) volume = kMaxOutputVolumePercent;
  saveOutputVolume(static_cast<uint8_t>(volume));
  lastAudioDetail = String(F("output volume ")) + String(outputVolumePercent) + F("%");
  server.sendHeader(F("Location"), F("/device"), true);
  server.send(302, F("text/plain"), F(""));
}

void handleDevicePowerSave() {
  String rawMinutes = server.arg(F("minutes"));
  rawMinutes.trim();
  if (rawMinutes.length() == 0) {
    server.send(400, F("text/plain; charset=utf-8"), F("缺少待机时间"));
    return;
  }
  int minutes = rawMinutes.toInt();
  if (minutes < 0) minutes = 0;
  if (minutes > kMaxIdlePowerSaveMinutes) minutes = kMaxIdlePowerSaveMinutes;
  saveIdlePowerSaveMinutes(static_cast<uint8_t>(minutes));
  server.sendHeader(F("Location"), F("/device"), true);
  server.send(302, F("text/plain"), F(""));
}



void handleDeviceAgent() {
  String id = server.arg(F("runtime"));
  if (id == F("ekko")) id = F("ekko-agent");
  int index = nativeAgentIndex(id.c_str());
  if (index < 0 || !requestCardputerAgentSelection(static_cast<size_t>(index))) {
    server.send(409, F("text/plain; charset=utf-8"), F("Agent 选择未提交，请先登录并连接原生后台，或等待当前选择完成"));
    return;
  }
  // The native page and keyboard share one browse target. Otherwise the ACK
  // coalescer would immediately select the previous keyboard position again.
  agentSelection.browse = static_cast<size_t>(index);
  conversationView.back();
  oledDirty = true;
  server.sendHeader(F("Location"), F("/device"), true);
  server.send(303, F("text/plain"), F(""));
}

void addManualDevice() {
  String url = server.arg(F("url"));
  String error;
  if (!fetchManualDeviceInfo(url, &error)) {
    String html = pageStart(F("添加失败"));
    html += F("<section class='panel'><p class='meta'>MANUAL DEVICE</p><h1>添加失败</h1><p class='lead bad'>");
    html += escapeHtml(error.length() > 0 ? error : String(F("无法获取机器信息")));
    html += F("</p><div class='btn-row'><a class='btn primary' href='/device'>返回设备</a></div></section>");
    html += pageEnd();
    server.send(502, F("text/html; charset=utf-8"), html);
    return;
  }
  server.sendHeader(F("Location"), F("/device"), true);
  server.send(302, F("text/plain"), F(""));
}

bool lanDeviceIndex(int index, LanDevice **device) {
  if (!device || index < 0 || index >= lanDeviceCount) return false;
  *device = &lanDevices[index];
  return true;
}

String mcuLoginPayload(const String &account, const String &password) {
  JsonDocument body;const String instance=deviceId();
  official_connection::login(body,instance.c_str(),account.c_str(),password.c_str());
  String payload;serializeJson(body,payload);return payload;
}

class LoginResponse final:public Stream {
 public:
  String body;bool overflow=false;
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
  size_t write(uint8_t byte) override {return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count) override {
    if(count>8192-body.length()) {overflow=true;return 0;}
    const size_t before=body.length();body.concat(reinterpret_cast<const char*>(bytes),count);
    return body.length()==before+count?count:0;
  }
};

bool runMcuLogin(LanDevice &device, const String &account, const String &password,
                 bool useRemoteLogin = false, bool manualLogin = false) {
  if(useRemoteLogin || device.remoteSource || !mcuDeviceCode().length()) {
    rememberMcuLoginResult(-1,F("此固件仅支持局域网直连，请选择同网电脑"));return false;
  }
  if(manualLogin) profileFormProof="";
  cardputerTickKeyClick(false);disconnectMcuSocketClient();chatModel.hideContext();
  String endpoint=device.url;while(endpoint.endsWith("/")) endpoint.remove(endpoint.length()-1);
  endpoint+=F("/api/auth/mcu-login");
  HTTPClient http;http.setReuse(false);http.setConnectTimeout(5000);http.setTimeout(kMcuLoginTimeoutMs);
  if(!http.begin(endpoint)) {rememberMcuLoginResult(-1,F("无法打开登录接口"));return false;}
  http.addHeader(F("Content-Type"),F("application/json"));
  http.addHeader(F("X-Hermes-Device-Id"),deviceId());
  http.addHeader(F("X-Hermes-Device-Name"),F("Ekko Studio Cardputer ADV"));
  const int code=http.POST(mcuLoginPayload(account,password));
  // Login response is bounded; never store or log the body containing a JWT.
  LoginResponse received;
  const bool bounded=http.getSize()<=8192 && http.writeToStream(&received)>=0 && !received.overflow;
  http.end();String response=std::move(received.body);
  JsonDocument document;
  const bool ok=bounded && code>=200 && code<300 && response.length()<=8192 &&
      !deserializeJson(document,response) && document["token"].is<const char*>() &&
      document["token"].as<JsonString>().size()>0 && document["token"].as<JsonString>().size()<=4096;
  rememberMcuLoginResult(code,ok?String("登录成功"):String("登录失败，请核对账号、权限或网络"));
  Serial.printf("Official LAN login http=%d valid=%d\n",code,ok);
  if(!ok) {clearLoginProfiles();setOledStatus(OledMode::Error,F("LOGIN"),F("FAILED"),0);return false;}
  if(manualLogin) {
    if(!prefs.begin("stockLan",false)) {rememberMcuLoginResult(-1,F("登录配置存储不可用"));return false;}
    const bool staged=prefs.putBool("profile_pending",true)>0;prefs.end();
    if(!staged) {rememberMcuLoginResult(-1,F("登录配置保存失败"));return false;}
  }
  persistCredentialsForDevice(lanDeviceKey(device),lanAddressKey(device),account,password,false);
  pendingProfileDeviceKey=lanDeviceKey(device);pendingProfileDeviceUrl=device.url;pendingProfileRemoteSource=false;
  activeDeviceKey=pendingProfileDeviceKey;profileFormProof="";
  activeDeviceUrl=device.url;mcuAuthToken=document["token"].as<const char*>();mcuSocketRelayUrl="";
  device.remoteLogin=false;mcuSocketReconnectBlocked=false;
  prefs.begin("stockLan",false);prefs.putString("active_url",activeDeviceUrl);prefs.putBool("active_remote",false);
  prefs.putString("auth_token",mcuAuthToken);prefs.end();
  parseLoginProfiles(response);
  setOledStatus(OledMode::Ready,F("LOGIN"),F("OK"),100);return true;
}



bool savedCredentialsForDevice(const LanDevice &device, String *account, String *password) {
  if (!account || !password) return false;
  prefs.begin("stockLan", true);
  String activeKey = prefs.getString("active_key", "");
  String activeAddr = prefs.getString("active_addr", "");
  String activeUrl = prefs.getString("active_url", "");
  String relayUrl = prefs.getString("relay_url", "");
  bool activeRemote = prefs.getBool("active_remote", relayUrl.length() > 0);
  *account = prefs.getString("cur_account", "");
  *password = prefs.getString("cur_password", "");
  prefs.end();
  bool isCurrent = activeIdentityMatchesDevice(device, activeKey, activeAddr, activeUrl, activeRemote);
  if (!isCurrent) return false;
  account->trim();
  return account->length() > 0 && password->length() > 0;
}

bool autoLoginDevice(LanDevice &device) {
  if (mcuSocketReconnectBlocked && mcuSocketRelayUrl.length() > 0) return false;
  String account;
  String password;
  if (!savedCredentialsForDevice(device, &account, &password)) return false;
  Serial.printf("Auto MCU login target=%s url=%s profile=%s\n",
                lanDeviceKey(device).c_str(), device.url.c_str(), selectedProfile.c_str());
  bool ok = runMcuLogin(device, account, password, shouldUseRemoteLogin(device));
  if (ok && selectedProfile.length() > 0) {
    String key = lanDeviceKey(device);
    String addr = lanAddressKey(device);
    persistProfileForDevice(key, addr, selectedProfile, device.url, shouldUseRemoteLogin(device));
    device.profile = selectedProfile;
    device.loggedIn = true;
    device.remoteLogin = shouldUseRemoteLogin(device);
    pendingProfileDeviceKey = key;
    pendingProfileRemoteSource = shouldUseRemoteLogin(device);
    activeDeviceKey = key;
    activeDeviceUrl = device.url;
  }
  return ok;
}

void persistRelayReplaced(bool replaced) {
  prefs.begin("stockLan", false);
  if (replaced) {
    prefs.putBool("relay_replaced", true);
  } else {
    prefs.remove("relay_replaced");
  }
  prefs.end();
  mcuSocketReconnectBlocked = replaced;
}

native_reauth::Outcome reauthActiveDevice(const String &reason = "", const String &machineId = "") {
  // Reset this attempt before any early return: an old 401/403 must not turn
  // discovery failure, lost Wi-Fi or missing saved credentials into rejection.
  rememberMcuLoginResult(0, F("尚未发起重登请求"));
  if (!wifiReady || WiFi.status() != WL_CONNECTED || activeDeviceUrl.length() == 0 ||
      mcuSocketReconnectBlocked) return native_reauth::Outcome::NoAttempt;
  Serial.printf("Reauth active MCU device reason=%s machine=%s active=%s\n",
                reason.c_str(), machineId.c_str(), activeDeviceUrl.c_str());
  setOledStatus(OledMode::Think, F("LOGIN"), F("REAUTH"), 35);

  int activeIndex = -1;
  for (int i = 0; i < lanDeviceCount; ++i) {
    if (isActiveLanDevice(lanDevices[i])) {
      activeIndex = i;
      break;
    }
  }
  if (activeIndex < 0) {
    refreshDeviceDiscovery();
    for (int i = 0; i < lanDeviceCount; ++i) {
      if (isActiveLanDevice(lanDevices[i])) {
        activeIndex = i;
        break;
      }
    }
  }

  if (activeIndex < 0) {
    return native_reauth::Outcome::NoAttempt;
  }
  if (!native_reauth::sameDeviceBinding(activeDeviceKey.c_str(), lanDeviceKey(lanDevices[activeIndex]).c_str())) {
    Serial.println(F("Reauth skipped: discovered target does not match the complete saved machine identity"));
    return native_reauth::Outcome::NoAttempt;
  }
  if (machineId.length() > 0 && lanDevices[activeIndex].id != machineId) {
    Serial.printf("Skip reauth for non-active machine event=%s active=%s\n",
                  machineId.c_str(), lanDevices[activeIndex].id.c_str());
    return native_reauth::Outcome::NoAttempt;
  }
  const bool ok = autoLoginDevice(lanDevices[activeIndex]);
  return native_reauth::loginOutcome(lastMcuLoginCode != 0, ok && mcuAuthToken.length() > 0, lastMcuLoginCode);
}

native_reauth::Target currentMcuReauthTarget() {
  return {activeDeviceKey.c_str(), activeDeviceUrl.c_str(), mcuSocketRelayUrl.c_str(),
          selectedProfile.c_str(), mcuSocketRelayUrl.length() > 0};
}

void cancelMcuReauth() {
  mcuReauthPolicy.cancel();
  mcuReauthAudioCleanupPending = false;
  pendingMcuReauthAuthInvalid = false;
  pendingMcuReauthReason = "";
  pendingMcuReauthMachineId = "";
  pendingMcuReauthInteractionId = "";
}

bool mcuReauthBlocksSocket() {
  // Defer runMcuLogin()'s synchronous socket connect until complete(Success):
  // an immediate new auth.invalid must not be consumed by the old attempt.
  if (mcuReauthAttemptInProgress) return true;
  if (mcuReauthPolicy.active() && !mcuReauthPolicy.matches(currentMcuReauthTarget())) cancelMcuReauth();
  return mcuReauthPolicy.blocksSocket();
}

void queueMcuReauth(bool authInvalid, const String &reason, const String &machineId,
                    const String &interactionId, const String & /*promptUrl*/) {
  if (machineId.length() && !activeDeviceKey.startsWith(machineId + F("|"))) return;
  mcuReauthBlocksSocket(); // Cancel a retry queued for a previous target/Profile.
  if (mcuReauthPolicy.active() && (mcuReauthPolicy.rejected() ||
      (pendingMcuReauthAuthInvalid && !authInvalid))) return;
  if (!mcuReauthPolicy.request(currentMcuReauthTarget())) {
    setChatNotice("登录已失效，请在设备网页重新登录");
    return;
  }
  pendingMcuReauthAuthInvalid = authInvalid;
  pendingMcuReauthReason = reason;
  pendingMcuReauthMachineId = machineId;
  pendingMcuReauthInteractionId = interactionId;
  // Perform cleanup at the top-level tick, after synchronous playback/recording
  // and any reentrant Socket.IO handler have released their audio ownership.
  mcuReauthAudioCleanupPending = true;
}

void tickMcuReauth() {
  if (!mcuReauthBlocksSocket()) return;
  if (wsReady || mcuSocketConnected || mcuSocketNamespaceReady) disconnectMcuSocketClient();
  if (mcuReauthAudioCleanupPending && !audioBusy) {
    const int discarded = mcuAudioCount;
    cardputerAudioClear(); // Joins the SDK consumer before discarding its owner.
    resetListeningMonitor();
    cancelOfficialVoice();
    mcuReauthAudioCleanupPending = false;
    Serial.printf("MCU reauth discarded stale audio queued=%d; saved login preserved\n", discarded);
  }
  const native_reauth::Conditions conditions{
      wifiReady && WiFi.status() == WL_CONNECTED,
      audioBusy || officialVoiceNetworkBusy() || mcuAudioPlaying || mcuAudioCount > 0 || listeningMonitorReady ||
          chatPendingSend.length() > 0 || restartPending,
      mcuSocketReconnectBlocked};
  if (!mcuReauthPolicy.begin(millis(), conditions)) return;
  mcuReauthAttemptInProgress = true;
  const native_reauth::Outcome outcome = reauthActiveDevice(pendingMcuReauthReason, pendingMcuReauthMachineId);
  mcuReauthAttemptInProgress = false;
  mcuReauthPolicy.complete(outcome, millis());
  if (outcome == native_reauth::Outcome::Success) {
    cancelMcuReauth();
    connectMcuSocketClient();
    if (!mcuReauthPolicy.blocksSocket()) broadcastMcuStatus();
    return;
  }
  if (outcome == native_reauth::Outcome::Rejected) {
    lastAudioDetail = F("登录被拒绝，请重新登录");
    setChatNotice("登录已失效，请在设备网页重新登录");
    setOledStatus(OledMode::Error, F("LOGIN"), F("RELOGIN"), 0);
    if (pendingMcuReauthInteractionId.length())
      markMcuInteraction(pendingMcuReauthInteractionId, F("failed"), lastAudioDetail);
    Serial.printf("MCU reauth rejected http=%d; saved binding and credentials preserved\n", lastMcuLoginCode);
    return;
  }
  lastAudioDetail = F("重登暂时失败，将自动重试");
  setChatNotice(lastAudioDetail.c_str());
  setOledStatus(OledMode::Think, F("LOGIN"), F("RETRY"), 25);
  Serial.printf("MCU reauth deferred result=%s http=%d retry_ms=%lu; saved state preserved\n",
      outcome == native_reauth::Outcome::NoAttempt ? "not_attempted" : "transient",
      lastMcuLoginCode, static_cast<unsigned long>(mcuReauthPolicy.retryDelayMs()));
}

void autoLoginSavedDevice() {
  if (mcuReauthPolicy.active()) return;
  String scheme, host, path;
  uint16_t port = 0;
  const bool validUrl = activeDeviceUrl.indexOf(F("://")) > 0 &&
      parseAudioUrl(activeDeviceUrl, &scheme, &host, &port, &path);
  const bool remote = mcuSocketRelayUrl.length() > 0;
  const bool sourceConsistent = pendingProfileRemoteSource == remote &&
      (!remote || (mcuSocketRelayUrl.indexOf(F("://")) > 0 &&
          parseAudioUrl(mcuSocketRelayUrl, &scheme, &host, &port, &path)));
  native_reauth::BootstrapState state{
      wifiReady && WiFi.status() == WL_CONNECTED,
      native_reauth::completeDeviceBinding(activeDeviceKey.c_str()) && validUrl,
      selectedProfile.length() > 0, mcuAuthToken.length() > 0, sourceConsistent, false,
      mcuSocketReconnectBlocked};
  if (!state.hasToken && state.bindingComplete && state.hasProfile && sourceConsistent && !state.relayReplaced) {
    // Read saved credentials only for a missing-token bootstrap. Do not discover
    // or rotate a valid cached JWT while the native namespace is connecting.
    if (prefs.begin("stockLan", true)) {
      String profile = prefs.getString("cur_profile", "");
      if (!profile.length()) profile = prefs.getString("current_profile", "");
      const bool sameBinding = prefs.getString("active_key", "") == activeDeviceKey &&
          prefs.getString("active_url", "") == activeDeviceUrl && profile == selectedProfile &&
          prefs.getString("relay_url", "") == mcuSocketRelayUrl &&
          prefs.getBool("active_remote", remote) == pendingProfileRemoteSource;
      state.hasSavedCredentials = sameBinding && prefs.getString("cur_account", "").length() > 0 &&
          prefs.getString("cur_password", "").length() > 0;
      prefs.end();
    }
  }
  const native_reauth::BootstrapAction action = native_reauth::bootstrapAction(state);
  if (action == native_reauth::BootstrapAction::WaitForWifi) return;
  if (action == native_reauth::BootstrapAction::ResumeSocket) {
    Serial.println(F("Official LAN startup reusing saved token; validating account and Profile"));
    return; // connectWifiCredentials() already began WS; loop owns its ACK/retry.
  }
  if (action == native_reauth::BootstrapAction::QueueReauth) {
    queueMcuReauth(false, F("startup_missing_token"));
    return;
  }
  disconnectMcuSocketClient();
  mcuReauthPolicy.requireManual(currentMcuReauthTarget());
  setChatNotice(state.relayReplaced ? "远程连接已被接管，请打开设备网页重新登录" :
      "连接配置不完整，请打开设备网页重新登录");
  setOledStatus(OledMode::Error, F("LOGIN"), F("OPEN /device"), 0);
  Serial.println(F("MCU startup requires manual login; saved binding and credentials preserved"));
}

void sendMcuLoginPage() {
  LanDevice *device = nullptr;
  if (!requestWebDevice(&device,true)) {
    server.send(409, F("text/plain; charset=utf-8"), F("设备目标已变化，请刷新列表后重新选择"));
    return;
  }

  String html = pageStart(F("登录设备"));
  html += F("<section class='panel'><p class='meta'>MCU LOGIN</p><h1>登录 ");
  html += escapeHtml(endpointLabel(device->endpointKind, device->httpPort));
  html += F("</h1><p class='lead'>");
  html += escapeHtml(device->name);
  html += F(" · ");
  html += escapeHtml(lanDeviceSourceLabel(*device));
  html += F(" · ");
  html += escapeHtml(lanDeviceDisplayUrl(*device));
  html += F("</p><form method='post' action='/device/login'>");
  appendWebDeviceTarget(html,lanDeviceKey(*device),device->url,device->remoteSource);
  html += F("<div class='field'><span class='label'>账号</span><input name='account' autocomplete='username' required></div>");
  html += F("<div class='field'><span class='label'>密码</span><input name='password' type='password' autocomplete='current-password' required></div>");
  html += F("<div class='btn-row'><button class='btn primary' type='submit'>登录</button><a class='btn' href='/device'>返回</a></div>");
  html += F("</form></section>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void sendProfilePage() {
  String html = pageStart(F("选择 Profile"));
  html += F("<section class='panel'><p class='meta'>MCU LOGIN</p><h1>选择 Profile</h1>");
  if (lastMcuLoginCode >= 200 && lastMcuLoginCode < 300) {
    html += F("<p class='lead ok'>登录成功</p>");
  } else {
    html += F("<p class='lead bad'>登录失败：");
    html += escapeHtml(userFacingDetail(lastMcuLoginDetail));
    html += F("</p>");
  }

  const bool bound=mcuAuthToken.length() && device_web_target::same(
      {pendingProfileDeviceKey.c_str(),pendingProfileDeviceUrl.c_str(),webDeviceSource(pendingProfileRemoteSource)},
      {activeDeviceKey.c_str(),activeDeviceUrl.c_str(),webDeviceSource(mcuSocketRelayUrl.length()>0)});
  if (loginProfileCount > 0 && bound) {
    // An opaque per-page generation prevents an old Profile page from applying
    // to a later login, even for a different account on the same machine.
    uint8_t nonce[16];esp_fill_random(nonce,sizeof(nonce));
    constexpr char hex[]="0123456789abcdef";char proof[33]{};
    for(size_t n=0;n<sizeof(nonce);++n) {proof[n*2]=hex[nonce[n]>>4];proof[n*2+1]=hex[nonce[n]&15];}
    profileFormProof=proof;
    html += F("<form method='post' action='/device/profile'>");
    appendWebDeviceTarget(html,pendingProfileDeviceKey,pendingProfileDeviceUrl,pendingProfileRemoteSource);
    html+=F("<input type='hidden' name='profile_form' value='");html+=escapeHtml(profileFormProof);html+=F("'>");
    html += F("<div class='field'><span class='label'>Profile</span><select name='profile' required>");
    for (int i = 0; i < loginProfileCount; ++i) {
      html += F("<option value='");
      html += escapeHtml(loginProfiles[i]);
      html += F("'>");
      html += escapeHtml(native_ui::profileLabel(loginProfiles[i].c_str()));
      html += F("</option>");
    }
    html += F("</select></div><div class='btn-row'><button class='btn primary' type='submit'>使用这个 Profile</button><a class='btn' href='/device'>返回设备</a></div></form>");
  } else {
    html += F("<p class='hint'>没有拿到可选 profile。</p><div class='btn-row'><a class='btn primary' href='/device'>返回设备</a></div>");
  }
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void switchProfilePage() {
  LanDevice *device = nullptr;
  if (!requestWebDevice(&device,true)) {
    server.send(409, F("text/plain; charset=utf-8"), F("设备目标已变化，请刷新列表后重新选择"));
    return;
  }
  if (!device->loggedIn || !webDeviceIsLoggedInTarget(*device)) {
    server.sendHeader(F("Location"), String(F("/device/login?")) + webDeviceTargetQuery(*device), true);
    server.send(302, F("text/plain"), F(""));
    return;
  }
  if (loginProfileCount == 0) {
    String html = pageStart(F("需要重新登录"));
    html += F("<section class='panel'><p class='meta'>MCU LOGIN</p><h1>需要重新登录</h1><p class='lead'>Profile 列表没有缓存，请重新登录后切换。</p><div class='btn-row'><a class='btn primary' href='/device/login?");
    html += escapeHtml(webDeviceTargetQuery(*device));
    html += F("'>重新登录</a><a class='btn' href='/device'>返回设备</a></div></section>");
    html += pageEnd();
    server.send(200, F("text/html; charset=utf-8"), html);
    return;
  }
  pendingProfileDeviceKey = lanDeviceKey(*device);
  pendingProfileDeviceUrl = device->url;
  pendingProfileRemoteSource = shouldUseRemoteLogin(*device);
  sendProfilePage();
}

void handleMcuLogin() {
  String account = server.arg(F("account"));
  String password = server.arg(F("password"));
  account.trim();
  LanDevice *device = nullptr;
  if (!requestWebDevice(&device)) {
    server.send(409, F("text/plain; charset=utf-8"), F("登录目标已变化，请刷新列表后重新选择；未提交账号密码"));
    return;
  }
  if (account.length() == 0 || password.length() == 0) {
    server.send(400, F("text/plain; charset=utf-8"), F("缺少账号或密码"));
    return;
  }
  cancelMcuReauth();
  const String oldProfile = selectedProfile, oldKey = activeDeviceKey;
  const String oldUrl = activeDeviceUrl, oldRelay = mcuSocketRelayUrl;
  const native_reauth::Target oldTarget{oldKey.c_str(), oldUrl.c_str(), oldRelay.c_str(),
      oldProfile.c_str(), oldRelay.length() > 0};
  selectedProfile = ""; // A new manual login must explicitly choose its Profile.
  const bool loggedIn = runMcuLogin(*device, account, password, shouldUseRemoteLogin(*device), true);
  if (native_reauth::restoreManualProfileOnFailure(loggedIn, oldTarget, currentMcuReauthTarget()))
    selectedProfile = oldProfile;
  sendProfilePage();
}

void saveProfile() {
  String requestedProfile = server.arg(F("profile"));
  requestedProfile.trim();
  LanDevice* device = nullptr;
  const String key=server.arg("device_key"),url=server.arg("device_url"),source=server.arg("device_source");
  const String proof=server.arg("profile_form");
  if (!requestWebDevice(&device) || !mcuAuthToken.length() ||
      !device_web_target::profileMatches({key.c_str(),url.c_str(),source.c_str()},
          {pendingProfileDeviceKey.c_str(),pendingProfileDeviceUrl.c_str(),webDeviceSource(pendingProfileRemoteSource)},
          {activeDeviceKey.c_str(),activeDeviceUrl.c_str(),webDeviceSource(mcuSocketRelayUrl.length()>0)},
          proof.c_str(),profileFormProof.c_str())) {
    server.send(409, F("text/plain; charset=utf-8"), F("登录目标或页面已变化，请重新选择 Profile；原有设置已保留"));
    return;
  }
  bool allowed=false;
  for(int i=0;i<loginProfileCount;++i) if(loginProfiles[i]==requestedProfile) allowed=true;
  if(!allowed) {server.send(403,F("text/plain; charset=utf-8"),F("此账号无权使用所选配置档，请重新登录"));return;}
  cancelMcuReauth();
  selectedProfile = requestedProfile;
  persistProfileForDevice(pendingProfileDeviceKey, lanAddressKey(*device), selectedProfile,
                          device->url, pendingProfileRemoteSource);
  bool committed=false;
  if(prefs.begin("stockLan",false)) {
    committed=prefs.getString("active_key","")==pendingProfileDeviceKey &&
        prefs.getString("active_url","")==device->url && prefs.getString("cur_profile","")==selectedProfile &&
        prefs.getString("auth_token","")==mcuAuthToken && prefs.putBool("profile_pending",false)>0;
    prefs.end();
  }
  if(!committed) {
    selectedProfile="";server.send(503,F("text/plain; charset=utf-8"),F("配置保存失败，请重新登录；原有设置仍保留"));return;
  }
  profileFormProof=""; // A successful form cannot be replayed after commit.
  device->profile = selectedProfile;
  device->loggedIn = true;
  device->remoteLogin = pendingProfileRemoteSource;
  connectMcuSocketClient();
  String html = pageStart(F("Profile 已选择"));
  html += F("<section class='panel'><p class='meta'>MCU LOGIN</p><h1>Profile 已选择</h1><p class='lead ok'>");
  html += escapeHtml(native_ui::profileLabel(selectedProfile.c_str()));
  html += F("</p><div class='btn-row'><a class='btn primary' href='/device'>返回设备</a></div></section>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void logoutDevice() {
  int index = server.arg(F("i")).toInt();
  LanDevice *device = nullptr;
  if (!lanDeviceIndex(index, &device)) {
    server.send(404, F("text/plain; charset=utf-8"), F("设备不存在，请先刷新探测"));
    return;
  }
  cancelMcuReauth();
  String key = lanDeviceKey(*device);
  bool wasCurrent = isActiveLanDevice(*device);
  clearProfileForDevice(*device);
  device->profile = "";
  device->loggedIn = false;
  device->remoteLogin = false;
  if (pendingProfileDeviceKey == key && pendingProfileRemoteSource == device->remoteSource) pendingProfileDeviceKey = "";
  if (wasCurrent) selectedProfile = "";
  connectMcuSocketClient();
  server.sendHeader(F("Location"), F("/device"), true);
  server.send(302, F("text/plain"), F(""));
}

String mcuStatusJson() {
  updateBatteryReading();
  String json;
  json.reserve(380);
  json += F("{\"type\":\"mcu.status\",\"interactionId\":\"");
  json += escapeJson(mcuInteractionId);
  json += F("\",\"status\":\"");
  json += escapeJson(mcuInteractionStatus);
  json += F("\",\"audioPlaying\":");
  json += mcuAudioPlaying ? F("true") : F("false");
  json += F(",\"queueLength\":");
  json += mcuAudioCount;
  json += F(",\"socketClients\":");
  json += mcuSocketNamespaceReady ? 1 : 0;
  json += F(",\"socketConnected\":");
  json += mcuSocketNamespaceReady ? F("true") : F("false");
  json += F(",\"powerSaveActive\":");
  json += idlePowerSaveActive ? F("true") : F("false");
  json += F(",\"powerSaveMinutes\":");
  json += idlePowerSaveMinutes;
  json += F(",\"active_device\":\"");
  json += escapeJson(activeDeviceKey);
  json += F("\",\"profile\":\"");
  json += escapeJson(selectedProfile);
  json += F("\",\"agentRuntime\":\"");
  json += mcuAgentRuntimeValue();
  json += F("\",\"agentId\":\"");
  json += mcuAgentRuntimeValue();
  json += F("\",\"selectionVersion\":"); json += agentSelection.version;
  json += F(",\"text\":\"");
  json += escapeJson(mcuInteractionText);
  json += F("\",\"tool\":\"");
  json += escapeJson(mcuToolName);
  json += F("\",\"toolStatus\":\"");
  json += escapeJson(mcuToolStatus);
  json += F("\",\"batteryKnown\":true,\"batteryLevel\":");
  json += batteryLevelPercent;
  json += F(",\"batteryVoltageMv\":");
  json += batteryVoltageMv;
  json += F("}");
  return json;
}

bool writeMcuWsBytes(const uint8_t *data, size_t length, uint32_t timeoutMs = 1500) {
  if (!data && length > 0) return false;
  size_t written = 0;
  uint32_t startedAt = millis();
  while (written < length) {
    if (!mcuWsClient->connected()) return false;
    size_t n = mcuWsClient->write(data + written, length - written);
    if (n > 0) {
      written += n;
      startedAt = millis();
      continue;
    }
    if (millis() - startedAt > timeoutMs) return false;
    delay(2);
    yield();
  }
  return true;
}

bool sendRawWsFrame(uint8_t opcode, const uint8_t *data, size_t length) {
  if (!mcuWsClient->connected()) return false;
  uint8_t header[14];
  size_t headerLen = 0;
  header[headerLen++] = 0x80 | (opcode & 0x0F);
  if (length < 126) {
    header[headerLen++] = 0x80 | static_cast<uint8_t>(length);
  } else if (length <= 0xFFFF) {
    header[headerLen++] = 0x80 | 126;
    header[headerLen++] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[headerLen++] = static_cast<uint8_t>(length & 0xFF);
  } else {
    return false;
  }

  uint8_t mask[4];
  for (uint8_t i = 0; i < 4; ++i) mask[i] = static_cast<uint8_t>(esp_random() & 0xFF);
  for (uint8_t i = 0; i < 4; ++i) header[headerLen++] = mask[i];
  if (!writeMcuWsBytes(header, headerLen)) {
    closeMcuSocketTransport(F("partial outgoing frame header")); return false;
  }

  constexpr size_t kChunk = 256;
  uint8_t buffer[kChunk];
  size_t offset = 0;
  while (offset < length) {
    size_t n = min(kChunk, length - offset);
    for (size_t i = 0; i < n; ++i) {
      buffer[i] = data[offset + i] ^ mask[(offset + i) & 3];
    }
    if (!writeMcuWsBytes(buffer, n)) {
      closeMcuSocketTransport(F("partial outgoing frame payload")); return false;
    }
    offset += n;
    yield();
  }
  return true;
}

bool sendRawWsText(const String &payload) {
  return sendRawWsFrame(0x1, reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
}



bool sendMcuSocketEvent(const String &event, const String &json) {
  if(!wsReady || !mcuStatusNamespaceReady || (event!="mcu.ready" && event!="mcu.status")) return false;
  JsonDocument body;if(deserializeJson(body,json)) return false;
  body["apiToken"]=mcuAuthToken;body["profile"]=selectedProfile;
  const String prefix=String("42/global-agent,[\"")+event+"\",";
  return sendRawWsText(official_connection::objectFrame<String>(prefix.c_str(),body,"]"));
}

bool sendMcuSocketJson(const String &json) {
  String type = jsonStringValue(json, F("type"));
  if (type.length() == 0) type = F("mcu.event");
  return sendMcuSocketEvent(type, json);
}

void sendWsJson(uint8_t, const String &json) {
  sendMcuSocketJson(json);
}

bool requestCardputerAgentSelection(size_t index) {
  if(!mcuSocketNamespaceReady || !mcuAuthToken.length() || !agentSelection.request(index)) return false;
  pendingAgentProfile=selectedProfile;agentSelectionStartedMs=millis();agentSelectionError="";
  cancelOfficialVoice();officialSelectionQueued=true;officialSelectionNotBeforeMs=millis();officialSelectionRetries=0;
  oledDirty=true;return true;
}

String nextChatRequestId() {
  if (!chatRequestNonce) chatRequestNonce = esp_random();
  char id[48];
  snprintf(id, sizeof(id), "card-%08lx-%08lx", static_cast<unsigned long>(chatRequestNonce),
           static_cast<unsigned long>(++chatRequestSequence));
  return String(id);
}

void setChatNotice(const String& notice) {
  if (chatNotice == notice) return;
  chatNotice = notice;
  // Keep a valid UTF-8 prefix even for a long provider error. The complete
  // diagnostic remains in the native desktop session; no prompt is logged.
  if (chatNotice.length() > 512) {
    const size_t prefix = native_chat::prefixBytes(chatNotice.c_str(), chatNotice.length(), 512);
    chatNotice.remove(prefix);
  }
  ++chatUiRevision; oledDirty = true;
}

bool chatDraftMatchesSent(const NativeChatInput* draft) {
  return draft && draft->size() == chatSentTextBytes &&
      memcmp(draft->data(), chatSentText, chatSentTextBytes) == 0;
}

String userFacingDetail(const String& detail,const char* fallback) {
  if(!detail.length()) return String(fallback);
  if(const char* label=native_ui::knownDetailLabel(detail.c_str())) return String(label);
  for(size_t i=0;i<detail.length();++i) if(static_cast<uint8_t>(detail[i])>=0x80) return detail;
  return String(fallback);
}

String chatFailureNotice(const String& error) {
  return String(official_run_failure::notice(error.c_str()).text);
}

bool requestChatHistory(const char* messageId, uint32_t offset, const char* beforeMessageId,
                        const char* afterMessageId) {
  return officialRequestHistory(messageId,offset,beforeMessageId,afterMessageId);
}

void submitChatDraft() { officialSendDraft(); }

void scrollNativeChat(NativeChatBrowseDirection direction) {
  if (chatHistoryRequest.length()) return;
  const auto actual = cardputerChatViewport();
  NativeChatBrowseViewport viewport;
  viewport.firstMessage = actual.first; viewport.lastMessage = actual.last;
  viewport.maxBottomOffset = actual.maxOffset;
  viewport.topWindowBoundary = actual.topWindowBoundary;
  viewport.bottomWindowBoundary = actual.bottomWindowBoundary;
  const auto plan = nativeChatBrowsePlan(chatModel, viewport, chatBottomOffset, direction);
  if (plan.action == NativeChatBrowseAction::Scroll) chatBottomOffset = plan.bottomOffset;
  else if (plan.action == NativeChatBrowseAction::PreviousWindow || plan.action == NativeChatBrowseAction::NextWindow) {
    const auto* message = chatModel.messageAt(plan.messageIndex);
    if (message) {
      const String id(message->id);
      if (requestChatHistory(id.c_str(), plan.byteOffset)) {
        chatFocusMessage = id; chatFocusAtEnd = plan.focus == NativeChatBrowseFocus::MessageEnd;
      }
    }
  } else if (plan.action == NativeChatBrowseAction::OlderPage || plan.action == NativeChatBrowseAction::NewerPage) {
    if (mcuInteractionActive && chatLiveEventsSeen) setChatNotice("回复完成后，可跨页翻记录");
    else {
      const auto* message = chatModel.messageAt(plan.messageIndex);
      if (message) {
        const String id(message->id);
        const bool older = plan.action == NativeChatBrowseAction::OlderPage;
        if (requestChatHistory(nullptr, 0, older ? id.c_str() : nullptr, older ? nullptr : id.c_str()))
          chatPageFocus = plan.focus;
      }
    }
  } else if (direction == NativeChatBrowseDirection::OlderPage) chatBottomOffset = actual.maxOffset;
  else if (direction == NativeChatBrowseDirection::NewerPage) chatBottomOffset = 0;
  ++chatUiRevision; oledDirty = true;
}

void tickNativeChat() {
  const uint32_t dropped = cardputerChatKeyDropped();
  if (dropped != chatObservedDroppedKeys) {
    chatObservedDroppedKeys = dropped; setChatNotice("输入过快，部分按键未接收，请核对草稿");
  }
  if (agentSelection.ready() && selectedProfile.length() &&
      (chatModel.selectionReady() || chatActivationAttemptVersion != agentSelection.version)) {
    const String binding = officialStudioDirect() ? officialBinding : String();
    if (!chatModel.selectionReady() || chatModel.version() != agentSelection.version ||
        strcmp(chatModel.agentId(), kNativeAgents[agentSelection.active].id) != 0 ||
        !chatModel.context() || selectedProfile != chatModel.context()->profile ||
        binding != chatModel.context()->binding) {
      const NativeChatResult result = chatModel.activate(kNativeAgents[agentSelection.active].id,
                                                        selectedProfile.c_str(), agentSelection.version, binding.c_str());
      chatActivationAttemptVersion = agentSelection.version;
      if (chatPendingSend.length()) { chatPendingSend = ""; chatSendUnknown = true; }
      setChatNotice("");
      if (result != NativeChatResult::Ok) setChatNotice(result == NativeChatResult::DraftCapacity ?
          "草稿已满，请先发送或清空旧草稿" : "当前会话不可用");
      chatHistoryRequest = ""; chatBottomOffset = 0; chatHistoryLoadedVersion = 0;
      chatFocusMessage = ""; chatRemoteRunActive = chatLiveEventsSeen = false;
      chatLiveRequest = "";
      chatHistoryMode = false; cardputerSetChatHistoryMode(false);
      chatPageFocus = NativeChatBrowseFocus::Preserve;
      ++chatUiRevision;
    }
  }
  const bool opened = conversationView.conversation();
  if (opened && !chatWasOpen) {
    if (!mcuInteractionActive || !chatModel.messageCount()) chatHistoryLoadedVersion = 0;
    chatBottomOffset = 0;
    chatHistoryMode = false; cardputerSetChatHistoryMode(false);
  }
  chatWasOpen = opened;
  if (opened && chatHistoryLoadedVersion != agentSelection.version && !chatHistoryRequest.length())
    requestChatHistory();
  if (chatHistoryRequest.length() && millis() - chatHistoryAtMs > (officialStudioDirect()?45000U:12000U)) {
    if(officialStudioDirect()) officialHistoryJob.cancel();
    chatHistoryRequest = ""; chatFocusMessage = ""; chatPageFocus = NativeChatBrowseFocus::Preserve;
    setChatNotice("记录同步超时，Tab 后 R 重试");
  }
  if (chatPendingSend.length() && millis() - chatSendAtMs > 15000) {
    chatPendingSend = ""; chatSendUnknown = true;
    setChatNotice("发送结果未知，请 Fn+R 核对记录");
  }
  if (opened && chatRemoteRunActive && !chatLiveEventsSeen && !chatHistoryRequest.length() &&
      millis() - chatRecoveryPollAtMs >= 5000) {
    chatRecoveryPollAtMs = millis(); requestChatHistory();
  }
  NativeChatKey key;
  while (cardputerChatKey(key)) {
    if (!opened) continue;
    noteMcuActivity();
    if (key.kind == NativeChatKeyKind::ToggleHistory) {
      chatHistoryMode = !chatHistoryMode;
      cardputerSetChatHistoryMode(chatHistoryMode); ++chatHistoryToggleCount;
      if (!chatHistoryMode) chatBottomOffset = 0;
      ++chatUiRevision; oledDirty = true; continue;
    }
    auto* draft = chatModel.draft();
    if (!draft || !chatModel.context() || strcmp(chatModel.agentId(), kNativeAgents[agentSelection.active].id) != 0 ||
        selectedProfile != chatModel.context()->profile) {
      setChatNotice("会话尚未就绪，未写入其他草稿"); continue;
    }
    if (key.kind == NativeChatKeyKind::Refresh) {
      if (mcuInteractionActive && chatLiveEventsSeen) setChatNotice("回复中，结束后可刷新记录");
      else { if(officialStudioDirect()) officialHistorySkippingTools=false;requestChatHistory(); chatBottomOffset = 0; }
      continue;
    }
    if (key.kind == NativeChatKeyKind::ScrollUp || key.kind == NativeChatKeyKind::ScrollDown) {
      const bool up = key.kind == NativeChatKeyKind::ScrollUp;
      if (up) ++chatScrollUpCount; else ++chatScrollDownCount;
      scrollNativeChat(up ? NativeChatBrowseDirection::Up : NativeChatBrowseDirection::Down); continue;
    }
    if (key.kind == NativeChatKeyKind::HistoryOlder || key.kind == NativeChatKeyKind::HistoryNewer) {
      const bool older = key.kind == NativeChatKeyKind::HistoryOlder;
      if (older) ++chatHistoryOlderCount; else ++chatHistoryNewerCount;
      scrollNativeChat(older ? NativeChatBrowseDirection::OlderPage : NativeChatBrowseDirection::NewerPage); continue;
    }
    if (chatHistoryMode && key.kind != NativeChatKeyKind::None) {
      chatHistoryMode = false; cardputerSetChatHistoryMode(false); chatBottomOffset = 0;
    }
    if (auto* ime = chatModel.inputMethod()) {
      const size_t beforeBytes = draft->size();
      const bool wasComposing = ime->composing();
      const auto result = ime->handle(key, *draft);
      if (result.handled) {
        ++chatInputMethodEvents;
        if (key.kind == NativeChatKeyKind::ToggleChinese) ++chatLanguageToggles;
        if (wasComposing && draft->size() > beforeBytes) ++chatInputMethodCommits;
        const char* notice = "";
        switch (ime->feedback()) {
          case NativeChineseInputFeedback::NoCandidate: notice = "未找到候选，OK可保留拼音"; break;
          case NativeChineseInputFeedback::PreeditFull: notice = "拼音已满，请先选字"; break;
          case NativeChineseInputFeedback::DraftFull: notice = "草稿已满，请先发送或删减"; break;
          case NativeChineseInputFeedback::InvalidCandidate: notice = "此页没有这个候选字"; break;
          default: break;
        }
        if (*notice) { chatInputNotice = notice; setChatNotice(chatInputNotice); }
        else if (chatInputNotice.length() && chatNotice == chatInputNotice) {
          setChatNotice(""); chatInputNotice = "";
        }
        ++chatUiRevision; oledDirty = true; continue;
      }
    }
    if (key.kind == NativeChatKeyKind::Send) { submitChatDraft(); continue; }
    NativeChatEditResult edit = NativeChatEditResult::Ok;
    switch (key.kind) {
      case NativeChatKeyKind::Insert: edit = draft->insert(key.character); break;
      case NativeChatKeyKind::Newline: edit = draft->insert('\n'); break;
      case NativeChatKeyKind::Backspace: draft->backspace(); break;
      case NativeChatKeyKind::Delete: draft->deleteForward(); break;
      case NativeChatKeyKind::Left: draft->moveLeft(); break;
      case NativeChatKeyKind::Right: draft->moveRight(); break;
      case NativeChatKeyKind::Home: draft->lineHome(); break;
      case NativeChatKeyKind::End: draft->lineEnd(); break;
      default: break;
    }
    if (edit == NativeChatEditResult::Full || edit == NativeChatEditResult::Truncated)
      setChatNotice("草稿已达 512 字节，请先发送");
    else if (chatNotice == "草稿已达 512 字节，请先发送") setChatNotice("");
    ++chatUiRevision; oledDirty = true;
  }
}



void returnToFactoryMenu() {
  factoryBootError = cardputerSelectFactoryBoot();
  if (factoryBootError != ESP_OK) {
    agentSelectionError = "factory_unavailable";
    oledDirty = true;
    return;
  }
  cancelOfficialVoice();
  cardputerSpeakerEnable(false); // Joins SDK DMA/audio ownership before restart.
  M5Cardputer.Mic.end();
  mcuWsClient->stop();
  server.stop();
  WiFi.disconnect(false, false);
  delay(100);
  ESP.restart();
}

void tickCardputerInput() {
  cardputerSetConversationMode(conversationView.conversation());
  cardputerPoll();
  int key;
  while ((key = cardputerNavigation()) != 0) {
    noteMcuActivity();
    if (key == 3) {
      if (conversationView.conversation() || conversationView.waiting()) conversationView.back();
      else returnToFactoryMenu();
    } else if (key == 2) {
      if (!conversationView.conversation() || !cardputerChatAvailable() ||
          !agentSelection.ready() || agentSelectionError.length() != 0) {
        agentSelectionError = "";
        conversationView.requestOpen(agentSelection, selectedProfile.c_str(), cardputerChatAvailable());
        if (!conversationView.conversation() && !agentSelection.pending)
          requestCardputerAgentSelection(agentSelection.browse);
      }
    } else if (key == -1 || key == 1) {
      conversationView.back();
      agentSelection.move(key);
      if (!agentSelection.pending)
        requestCardputerAgentSelection(agentSelection.browse);
    }
    oledDirty = true;
  }
  const uint32_t agentAckTimeoutMs = officialStudioDirect() ? 100000U : kMcuRemoteAgentAckTimeoutMs;
  if (native_remote::requestExpired(millis(), agentSelection.pending, agentSelectionStartedMs, agentAckTimeoutMs)) {
    agentSelection.pending = agentSelection.confirmed = false;
    agentSelectionError = F("native_agent_ack_timeout");
    // A relay can keep MCU TCP alive while its desktop reconnects. Re-open
    // with the saved binding and re-select the Agent after normal backoff.
    closeMcuSocketTransport(F("native Agent ACK timeout"));
    oledDirty = true;
  }
  if (conversationView.synchronize(agentSelection, selectedProfile.c_str(),
                                  cardputerChatAvailable(), agentSelectionError.length() != 0))
    oledDirty = true;
  cardputerSetConversationMode(conversationView.conversation());
  tickNativeChat();
}

void broadcastMcuStatus() {
  sendMcuSocketJson(mcuStatusJson());
}

void stopMcuAudioQueueByButton() {
  cancelOfficialVoice();
  if(chatRemoteRunActive || chatPendingSend.length()) {
    officialControlEvent("abort");officialResumeQueued=true;
  }
}

void clearMcuSessionByButton() { officialControlEvent("new_session"); }

void triggerBootVoiceTurn() { startOfficialRecording(); }

void handleBootButton() {
  bool bootPressed = cardputerTalkPressed();
  uint32_t now = millis();
  if (bootPressed) noteMcuActivity();

  if (!bootInputArmed) {
    bootWasPressed = false;
    bootLongPressHandled = false;
    bootClickPending = false;
    bootSecondClickStarted = false;
    audioInterruptPressStartedAtMs = 0;
    if (now < kBootInputArmDelayMs || bootPressed) {
      bootReleaseStartedAtMs = 0;
      return;
    }
    if (bootReleaseStartedAtMs == 0) {
      bootReleaseStartedAtMs = now;
      return;
    }
    if (now - bootReleaseStartedAtMs < kBootDebounceMs) return;
    bootInputArmed = true;
    lastBootButtonAtMs = now;
    Serial.println(F("BOOT button armed after startup release"));
    return;
  }

  if (bootPressed && !bootWasPressed && now - lastBootButtonAtMs > kBootDebounceMs) {
    if (bootClickPending && now - bootClickPendingAtMs > kBootDoubleClickMs) {
      bootClickPending = false;
      bootSecondClickStarted = false;
      lastBootButtonAtMs = now;
      stopMcuAudioQueueByButton();
      return;
    }
    if (bootClickPending && now - bootClickPendingAtMs <= kBootDoubleClickMs) {
      bootSecondClickStarted = true;
    }
    bootWasPressed = true;
    bootLongPressHandled = false;
    bootPressedAtMs = now;
    return;
  }

  if (bootPressed && bootWasPressed && !bootLongPressHandled &&
      now - bootPressedAtMs >= kBootLongPressMs) {
    bootLongPressHandled = true;
    bootClickPending = false;
    bootSecondClickStarted = false;
    lastBootButtonAtMs = now;
    triggerBootVoiceTurn();
    return;
  }

  if (!bootPressed && bootWasPressed) {
    bootWasPressed = false;
    uint32_t heldMs = now - bootPressedAtMs;
    if (!bootLongPressHandled && heldMs >= kBootDebounceMs) {
      if (bootClickPending && (bootSecondClickStarted || now - bootClickPendingAtMs <= kBootDoubleClickMs)) {
        bootClickPending = false;
        bootSecondClickStarted = false;
        lastBootButtonAtMs = now;
        clearMcuSessionByButton();
      } else {
        bootClickPending = true;
        bootSecondClickStarted = false;
        bootClickPendingAtMs = now;
      }
    }
  }

  if (!bootWasPressed && bootClickPending && now - bootClickPendingAtMs > kBootDoubleClickMs) {
    bootClickPending = false;
    bootSecondClickStarted = false;
    lastBootButtonAtMs = now;
    stopMcuAudioQueueByButton();
  }
}



String mcuSocketAuthJson() {
  JsonDocument auth;const String instance=deviceId(),code=mcuDeviceCode();
  official_connection::statusAuth(auth,mcuAuthToken.c_str(),instance.c_str(),code.c_str(),selectedProfile.c_str());
  String json;serializeJson(auth,json);return json;
}

void sendMcuSocketNamespaceConnect() {
  if(!mcuSocketConnected || !mcuAuthToken.length() || !selectedProfile.length()) return;
  JsonDocument auth;official_connection::chatAuth(auth,mcuAuthToken.c_str());
  const String frame=official_connection::objectFrame<String>("40/chat-run,",auth);
  if(!sendRawWsText(frame)) return;
  statusReconnectAtMs=millis()+8000;
  sendRawWsText(String("40/global-agent,")+mcuSocketAuthJson());
}

void sendMcuReady() {
  JsonDocument body;body["type"]="mcu.ready";body["id"]=deviceId();body["profile"]=selectedProfile;
  body["agentRuntime"]=mcuAgentRuntimeValue();body["capabilities"]["display"]=true;
  body["capabilities"]["audio_playback"]=true;
  String json;serializeJson(body,json);sendMcuSocketEvent(F("mcu.ready"),json);broadcastMcuStatus();
}

bool parseSocketIoEvent(const String &message, String *event, String *json) {
  int arrayStart = message.indexOf('[');
  if (arrayStart < 0) return false;
  int firstQuote = message.indexOf('"', arrayStart);
  if (firstQuote < 0) return false;
  int secondQuote = message.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return false;
  int comma = message.indexOf(',', secondQuote + 1);
  int arrayEnd = message.lastIndexOf(']');
  if (comma < 0 || arrayEnd <= comma) return false;
  String parsedEvent = message.substring(firstQuote + 1, secondQuote);
  int payloadStart = comma + 1;
  while (payloadStart < arrayEnd && isspace(static_cast<unsigned char>(message[payloadStart]))) ++payloadStart;
  int payloadEnd = arrayEnd;
  if (payloadStart < arrayEnd && message[payloadStart] == '{') {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = payloadStart; i < arrayEnd; ++i) {
      char c = message[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        inString = !inString;
        continue;
      }
      if (inString) continue;
      if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
        if (depth == 0) {
          payloadEnd = i + 1;
          break;
        }
      }
    }
  }
  String parsedJson = message.substring(payloadStart, payloadEnd);
  parsedJson.trim();
  if (!parsedJson.startsWith("{")) {
    parsedJson = String(F("{\"type\":\"")) + escapeJson(parsedEvent) + F("\"}");
  } else if (jsonStringValue(parsedJson, F("type")).length() == 0) {
    parsedJson = String(F("{\"type\":\"")) + escapeJson(parsedEvent) + F("\",") + parsedJson.substring(1);
  }
  if (event) *event = parsedEvent;
  if (json) *json = parsedJson;
  return parsedEvent.length() > 0;
}

void handleSocketIoText(const String &message) {
  if (message == F("2")) {
    sendRawWsText(String(F("3")));
    return;
  }
  if (message.startsWith(F("0"))) {
    JsonDocument opened;
    if (!deserializeJson(opened, message.substring(1)) &&
        opened["pingInterval"].is<uint32_t>() && opened["pingTimeout"].is<uint32_t>())
      mcuSocketLiveness.heartbeat(opened["pingInterval"].as<uint32_t>(), opened["pingTimeout"].as<uint32_t>());
    sendMcuSocketNamespaceConnect();
    return;
  }
  if (official_connection::Namespaces::packet(message.c_str(),"40/global-agent")) {
    officialNamespaces.acknowledge(message.c_str());
    mcuStatusNamespaceReady = officialNamespaces.status();
    statusReconnectAtMs=0;statusReconnectDelayMs=3000;
    Serial.println(F("Socket.IO status namespace /global-agent connected"));
    sendMcuReady();
    return;
  }
  if (official_connection::Namespaces::packet(message.c_str(),"40/chat-run")) {
    officialNamespaces.acknowledge(message.c_str());
    mcuSocketNamespaceReady=officialNamespaces.chat(); chatProtocolReady=mcuSocketNamespaceReady;
    Serial.println(F("Socket.IO official namespace /chat-run connected"));
    requestCardputerAgentSelection(agentSelection.browse);return;
  }
  const bool statusGone=official_connection::Namespaces::packet(message.c_str(),"41/global-agent") ||
      official_connection::Namespaces::packet(message.c_str(),"44/global-agent");
  if(statusGone) {
    officialNamespaces.statusDisconnected();mcuStatusNamespaceReady=false;
    statusReconnectAtMs=millis()+statusReconnectDelayMs;
    statusReconnectDelayMs=std::min<uint32_t>(60000,statusReconnectDelayMs*2);
    Serial.println(F("Studio presence unavailable; chat connection preserved"));return;
  }
  if(official_connection::Namespaces::packet(message.c_str(),"41/chat-run") || message=="1") {
    closeMcuSocketTransport(F("chat namespace or Engine.IO disconnected"));return;
  }
  if(official_connection::Namespaces::packet(message.c_str(),"44/chat-run")) {
    mcuSocketNamespaceReady = false;mcuStatusNamespaceReady=false;officialNamespaces.reset();statusReconnectAtMs=0;
    Serial.printf("Socket.IO namespace error bytes=%u\n", static_cast<unsigned>(message.length()));
    native_reauth::NamespaceAction action = native_reauth::NamespaceAction::Reconnect;
    JsonDocument error;
    const int comma = message.indexOf(',');
    if (comma >= 0 && !deserializeJson(error, message.substring(comma + 1)) && error.is<JsonObject>()) {
      action = native_reauth::combineNamespaceActions(
          native_reauth::namespaceAction(error["code"] | "", error["message"] | ""),
          native_reauth::namespaceAction(error["data"]["code"] | "", error["data"]["message"] | ""));
    }
    closeMcuSocketTransport(F("namespace error"));
    if (action == native_reauth::NamespaceAction::RelayReplaced) persistRelayReplaced(true);
    if (action == native_reauth::NamespaceAction::RequireManual || action == native_reauth::NamespaceAction::RelayReplaced) {
      mcuReauthPolicy.requireManual(currentMcuReauthTarget());
      lastAudioDetail = F("设备或 Profile 被拒绝，请打开设备网页重新登录");
      setChatNotice(lastAudioDetail);
      setOledStatus(OledMode::Error, F("LOGIN"), F("OPEN /device"), 0);
      Serial.println(F("MCU namespace denied; manual login required, saved state preserved"));
    } else if (action == native_reauth::NamespaceAction::Reauthenticate && !mcuSocketReconnectBlocked) {
      queueMcuReauth(true, F("namespace_auth_invalid"));
    }
    return;
  }

}

bool readMcuWsBytes(uint8_t *buffer, size_t length, uint32_t timeoutMs = 100) {
  size_t read = 0;
  uint32_t startedAt = millis();
  while (read < length && millis() - startedAt < timeoutMs) {
    int available = mcuWsClient->available();
    if (available > 0) {
      int n = mcuWsClient->read(buffer + read, min(static_cast<size_t>(available), length - read));
      if (n > 0) read += static_cast<size_t>(n);
    } else {
      delay(1);
      yield();
    }
  }
  return read == length;
}

void resetMcuSocketReconnect() {
  nextMcuSocketReconnectAtMs = 0;
  mcuSocketReconnectDelayMs = kMcuSocketReconnectMs;
}

void scheduleMcuSocketReconnect() {
  if (!wifiReady || WiFi.status() != WL_CONNECTED ||
      activeDeviceUrl.length() == 0 || mcuAuthToken.length() == 0 ||
      mcuSocketReconnectBlocked) {
    nextMcuSocketReconnectAtMs = 0;
    return;
  }
  uint32_t delayMs = max<uint32_t>(mcuSocketReconnectDelayMs, kMcuSocketReconnectMs);
  nextMcuSocketReconnectAtMs = millis() + delayMs;
  mcuSocketReconnectDelayMs = min<uint32_t>(delayMs * 2, kMcuSocketReconnectMaxMs);
  Serial.printf("Socket.IO reconnect scheduled in %lu ms\n", static_cast<unsigned long>(delayMs));
}

void closeMcuSocketTransport(const __FlashStringHelper *reason) {
  cancelOfficialVoice();
  officialHistoryJob.cancel();
  officialResumePending=false;
  officialSelectionJob.cancel();
  officialSendRunObserved=false;
  officialRunGuard.disconnect();
  officialSocketResetQueued=true;
  officialSelectionQueued=officialResumeQueued=officialHistoryQueued=false;
  if(!++officialEpoch) ++officialEpoch;
  agentSelection.disconnect();
  chatModel.disconnect(); chatProtocolReady = false; chatHistoryRequest = "";
  chatActivationAttemptVersion = 0;
  chatRemoteRunActive = chatLiveEventsSeen = false; mcuInteractionActive = false;
  if (chatPendingSend.length()) {
    chatPendingSend = ""; chatSendUnknown = true;
    setChatNotice("连接中断，草稿保留；请核对发送结果");
  }
  if (mcuWsClient->connected()) mcuWsClient->stop();
  wsReady = false;
  mcuSocketConnected = false;
  mcuSocketNamespaceReady = false;mcuStatusNamespaceReady=false;officialNamespaces.reset();statusReconnectAtMs=0;
  Serial.print(F("Socket.IO transport disconnected"));
  if (reason) {
    Serial.print(F(": "));
    Serial.print(reason);
  }
  Serial.println();
  scheduleMcuSocketReconnect();
}

void mcuSocketLoop() {
  if (!wsReady) return;
  if (!mcuWsClient->connected()) {
    closeMcuSocketTransport(F("tcp closed"));
    return;
  }
  const uint32_t now = millis();
  const uint32_t namespaceTimeout = mcuSocketRelayUrl.length() ? kMcuRemoteHttpTimeoutMs : 8000U;
  if (mcuSocketLiveness.namespaceExpired(now, mcuSocketNamespaceReady, namespaceTimeout)) {
    closeMcuSocketTransport(F("native namespace ACK timeout"));
    return;
  }
  if (mcuSocketLiveness.heartbeatExpired(now)) {
    closeMcuSocketTransport(F("Engine.IO heartbeat timeout"));
    return;
  }

  officialSocketLoop();
}

void disconnectMcuSocketClient() {
  cancelOfficialVoice();
  officialHistoryJob.cancel();
  officialResumePending=false;
  officialSelectionJob.cancel();
  officialSendRunObserved=false;
  officialRunGuard.disconnect();
  officialSocketResetQueued=true;
  officialSelectionQueued=officialResumeQueued=officialHistoryQueued=false;
  if(!++officialEpoch) ++officialEpoch;
  agentSelection.disconnect();
  chatModel.disconnect(); chatProtocolReady = false; chatHistoryRequest = "";
  chatActivationAttemptVersion = 0;
  chatRemoteRunActive = chatLiveEventsSeen = false; mcuInteractionActive = false;
  if (chatPendingSend.length()) {
    chatPendingSend = ""; chatSendUnknown = true;
    setChatNotice("连接中断，草稿保留；请核对发送结果");
  }
  if (mcuWsPlainClient.connected()) mcuWsPlainClient.stop();
  if (mcuWsSecureClient.connected()) mcuWsSecureClient.stop();
  mcuWsClient = &mcuWsPlainClient;
  wsReady = false;
  mcuSocketConnected = false;
  mcuSocketNamespaceReady = false;mcuStatusNamespaceReady=false;officialNamespaces.reset();statusReconnectAtMs=0;
  mcuSocketTargetKey = "";
}

void tickMcuSocketReconnect() {
  if (mcuReauthBlocksSocket()) { nextMcuSocketReconnectAtMs = 0; return; }
  if (wsReady || mcuSocketConnected || mcuSocketNamespaceReady) return;
  if (!wifiReady || WiFi.status() != WL_CONNECTED ||
      activeDeviceUrl.length() == 0 || mcuAuthToken.length() == 0 ||
      mcuSocketReconnectBlocked) {
    nextMcuSocketReconnectAtMs = 0;
    return;
  }
  if (nextMcuSocketReconnectAtMs == 0) {
    scheduleMcuSocketReconnect();
    return;
  }
  if (static_cast<int32_t>(millis() - nextMcuSocketReconnectAtMs) < 0) return;
  nextMcuSocketReconnectAtMs = 0;
  connectMcuSocketClient();
}

String activeMcuSocketUrl() {
  String url = mcuSocketRelayUrl;
  url.trim();
  if (url.length() > 0) return url;
  return activeDeviceUrl;
}

String expectedMcuSocketTargetKey() {
  String scheme;
  String host;
  uint16_t port = 0;
  String path;
  String socketUrl = activeMcuSocketUrl();
  if (!parseAudioUrl(socketUrl, &scheme, &host, &port, &path)) return "";
  return scheme + F("://") + host + F(":") + String(port) + F("|") + selectedProfile + F("|") + mcuAuthToken;
}

bool mcuSocketMatchesActiveTarget() {
  if (!wsReady || !mcuSocketNamespaceReady) return false;
  String expected = expectedMcuSocketTargetKey();
  return expected.length() > 0 && mcuSocketTargetKey == expected;
}

void connectMcuSocketClient() {
  if (!mcuDeviceCode().length()) {
    disconnectMcuSocketClient();
    nextMcuSocketReconnectAtMs = 0;
    return;
  }
  if (mcuReauthBlocksSocket()) return;
  // pendingProfileRemoteSource also belongs to the profile-picker form. An
  // unsubmitted form must not change the active connection's saved source.
  bool savedRemote = false, sourceKnown = false, savedBindingMatches = false;
  if(prefs.begin("stockLan",true)) {
    savedRemote=prefs.getBool("active_remote",false);sourceKnown=true;
    savedBindingMatches=native_reauth::sameDeviceBinding(activeDeviceKey.c_str(),prefs.getString("active_key","").c_str()) &&
        prefs.getString("active_url","")==activeDeviceUrl;prefs.end();
  }
  if (!sourceKnown || !savedBindingMatches || activeDeviceUrl.indexOf(F("://")) <= 0 ||
      !native_reauth::mayConnectSavedSocket(activeDeviceKey.c_str(), selectedProfile.length() > 0,
          savedRemote, mcuSocketRelayUrl.length() > 0)) {
    disconnectMcuSocketClient();
    nextMcuSocketReconnectAtMs = 0;
    return;
  }
  if (!wifiReady || WiFi.status() != WL_CONNECTED || activeDeviceUrl.length() == 0 || mcuAuthToken.length() == 0) {
    disconnectMcuSocketClient();
    resetMcuSocketReconnect();
    return;
  }
  if (mcuSocketReconnectBlocked && mcuSocketRelayUrl.length() > 0) {
    nextMcuSocketReconnectAtMs = 0;
    Serial.println(F("Socket.IO reconnect blocked after relay replacement"));
    return;
  }

  String scheme;
  String host;
  uint16_t port = 0;
  String path;
  String socketUrl = activeMcuSocketUrl();
  if (!parseAudioUrl(socketUrl, &scheme, &host, &port, &path)) {
    disconnectMcuSocketClient();
    scheduleMcuSocketReconnect();
    return;
  }

  String targetKey = scheme + F("://") + host + F(":") + String(port) + F("|") + selectedProfile + F("|") + mcuAuthToken;
  if (wsReady && mcuSocketTargetKey == targetKey) return;
  disconnectMcuSocketClient();

  if (scheme != F("http") && scheme != F("https")) {
    Serial.printf("Socket.IO client unsupported scheme=%s\n", scheme.c_str());
    scheduleMcuSocketReconnect();
    return;
  }

  if (scheme == F("https")) {
    cardputerTickKeyClick(false);
    native_tls::configureTrust(mcuWsSecureClient);
    mcuWsSecureClient.setHandshakeTimeout(kMcuRemoteTlsHandshakeTimeoutSec);
    mcuWsClient = &mcuWsSecureClient;
  } else {
    mcuWsClient = &mcuWsPlainClient;
  }
  // The TLS connection can succeed while the remote HTTP upgrade is delayed.
  // Use the same remote read budget as discovery/login; keep the LAN budget.
  const uint32_t socketConnectStarted = millis();
  if (!mcuWsClient->connect(host.c_str(), port, scheme == F("https") ? 15000 : 5000)) {
      char tlsDetail[96] = {};
    const int tlsError = scheme == F("https") ? mcuWsSecureClient.lastError(tlsDetail, sizeof(tlsDetail)) : 0;
    Serial.printf("Socket.IO connect failed host=%s port=%u tls=%d elapsed=%lu\n", host.c_str(), port,
                  tlsError < 0 ? tlsError : 0, static_cast<unsigned long>(millis() - socketConnectStarted));
    scheduleMcuSocketReconnect();
    return;
  }
  // Configure socket I/O only after connect; an unconnected secure client may
  // still expose fd 0 and make the SDK apply socket options to an invalid fd.
  mcuWsClient->setTimeout(scheme == F("https") ? kMcuRemoteHttpTimeoutMs / 1000 : 5);
  mcuWsClient->setNoDelay(true);

  String socketPath = F("/socket.io/?EIO=4&transport=websocket");
  if (officialStudioDirect()) socketPath+=String("&profile=")+officialUrlEncode(selectedProfile);
  uint8_t nonce[16];esp_fill_random(nonce,sizeof(nonce));
  char encoded[25]{};size_t encodedBytes=0;
  if(mbedtls_base64_encode(reinterpret_cast<uint8_t*>(encoded),sizeof(encoded),&encodedBytes,nonce,sizeof(nonce))!=0) {
    mcuWsClient->stop();scheduleMcuSocketReconnect();return;
  }
  String key(encoded);
  String request;
  request.reserve(host.length() + socketPath.length() + 260);
  request += F("GET ");
  request += socketPath;
  request += F(" HTTP/1.1\r\nHost: ");
  request += host;
  request += F(":");
  request += port;
  request += F("\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ");
  request += key;
  request += F("\r\nUser-Agent: Ekko Studio-CardputerADV\r\n\r\n");
  mcuWsClient->print(request);

  String statusLine = mcuWsClient->readStringUntil('\n');
  statusLine.trim();
  if (!statusLine.startsWith(F("HTTP/1.1 101")) && !statusLine.startsWith(F("HTTP/1.0 101"))) {
      Serial.printf("Socket.IO websocket upgrade failed: %s\n", statusLine.c_str());
    mcuWsClient->stop();
    scheduleMcuSocketReconnect();
    return;
  }
  uint32_t headerStartedAt = millis();
  while (mcuWsClient->connected() && millis() - headerStartedAt < 3000) {
    String line = mcuWsClient->readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }
  wsReady = true;
  resetOfficialSocketReader();
  mcuSocketConnected = true;
  mcuSocketNamespaceReady = false;mcuStatusNamespaceReady=false;officialNamespaces.reset();statusReconnectAtMs=0;
  mcuSocketTargetKey = targetKey;
  lastMcuSocketConnectAtMs = millis();
  mcuSocketLiveness.connected(lastMcuSocketConnectAtMs);
  nextMcuSocketReconnectAtMs = 0;
  Serial.printf("Socket.IO client connecting host=%s port=%u profile=%s relay=%d\n",
                host.c_str(), port, selectedProfile.c_str(), mcuSocketRelayUrl.length() > 0 ? 1 : 0);
  mcuSocketLoop();
}

// All runs use the official web-client protocol; no MCU chat extensions.
bool officialStudioDirect() { return true; }

String officialUrlEncode(const String& input) {
  const char* hex="0123456789ABCDEF"; String out;
  for(size_t i=0;i<input.length();++i) {
    const uint8_t c=static_cast<uint8_t>(input[i]);
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') out+=char(c);
    else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
  }
  return out;
}

String officialSessionIdentity() {
  mbedtls_sha256_context hash; mbedtls_sha256_init(&hash); mbedtls_sha256_starts_ret(&hash,0);
  const String device=mcuDeviceCode();
  const String machine=activeDeviceKey.substring(0,activeDeviceKey.indexOf('|')<0?activeDeviceKey.length():activeDeviceKey.indexOf('|'));
  const char* values[]{device.c_str(),machine.c_str(),officialAccount.c_str(),selectedProfile.c_str(),
      kNativeAgents[agentSelection.requested].id,officialMode==official_studio::Mode::Scoped?"scoped":"global"};
  for(const char* value:values) {
    const uint32_t n=std::strlen(value); uint8_t length[]{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};
    mbedtls_sha256_update_ret(&hash,length,sizeof(length));
    mbedtls_sha256_update_ret(&hash,reinterpret_cast<const uint8_t*>(value),n);
  }
  uint8_t digest[32]; mbedtls_sha256_finish_ret(&hash,digest); mbedtls_sha256_free(&hash);
  const char* hex="0123456789abcdef"; String id="m5-";
  for(const uint8_t byte:digest) { id+=hex[byte>>4]; id+=hex[byte&15]; }
  return id;
}

official_studio::Context officialContext() {
  official_studio::Context context;
  context.machineId=activeDeviceKey.c_str(); context.accountId=officialAccount.c_str();
  context.profile=selectedProfile.c_str(); context.agentId=kNativeAgents[agentSelection.active].id;
  context.sessionId=officialSession.c_str(); context.mode=officialMode; context.connectionEpoch=officialEpoch;
  return context;
}

bool officialChatContextActivated() {
  return chatModel.selectionReady() && chatModel.context() && agentSelection.ready() &&
      chatModel.version()==agentSelection.version && officialBinding==chatModel.context()->binding &&
      selectedProfile==chatModel.context()->profile &&
      !std::strcmp(chatModel.agentId(),kNativeAgents[agentSelection.active].id);
}

bool officialEmit(const char* event,const JsonDocument& document) {
  if(!wsReady||!mcuSocketNamespaceReady||!officialStudioDirect()) return false;
  const String prefix=String("42/chat-run,[\"")+event+"\",";
  const String payload=official_connection::objectFrame<String>(prefix.c_str(),document,"]");
  return sendRawWsText(payload);
}

void officialPublishWindow(const char* id,NativeChatRole role,const official_incoming::Window& window,bool final) {
  if(!chatModel.selectionReady() || !id || !*id) return;
  const String request=nextChatRequestId();
  const NativeChatRoute route(chatModel.agentId(),selectedProfile.c_str(),agentSelection.version,officialSession.c_str());
  if(chatModel.expectHistory(request.c_str(),false)!=NativeChatResult::Ok ||
     chatModel.beginHistory(route,request.c_str())!=NativeChatResult::Ok) return;
  chatModel.beginMessage(route,request.c_str(),id,role,window.start,window.total,final,true);
  size_t used=0;
  do {
    const size_t bytes=native_chat::prefixBytes(window.text.data()+used,window.bytes-used,kNativeChatChunkBytes);
    const bool last=used+bytes==window.bytes;
    chatModel.appendChunk(route,request.c_str(),id,role,window.start+used,
        window.text.data()+used,bytes,last,window.total);
    used+=bytes;
    if(last) break;
  } while(used<window.bytes);
  chatModel.endHistory(route,request.c_str(),false,false);
  ++chatUiRevision; oledDirty=true;
}

bool officialRequestHistory(const char* messageId,uint32_t byteOffset,const char* before,const char* after) {
  if(!officialChatContextActivated() || !mcuSocketNamespaceReady || !officialSession.length()) return false;
  if(officialHistoryJob.busy() || officialSelectionJob.busy() || officialVoiceNetworkBusy() || chatHistoryRequest.length()) return false;
  resetListeningMonitor();
  uint32_t page=officialHistorySkippingTools?officialHistoryOffset:0;
  int8_t direction=officialHistorySkippingTools?officialHistoryDirection:(after?-1:1);
  officialHistorySkippingTools=false;
  if(messageId||before||after) {
    const char* wanted=messageId?messageId:before?before:after; bool found=false;
    for(const auto& pos:officialMessagePositions) if(pos.id==wanted) {
      page=pos.offset; found=true; break;
    }
    if(!found) {setChatNotice("记录索引已变化，请先刷新最新记录");return false;}
    if(before) ++page;
    if(after) page=page>12?page-12:0;
  }
  auto input=std::unique_ptr<official_history::Input>(new(std::nothrow)official_history::Input);
  if(!input) {setChatNotice("内存不足，未加载记录");return false;}
  if(official_history::makeInput(*input,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),selectedProfile.c_str(),
      officialSession.c_str(),officialEpoch,page,byteOffset)!=official_history::Result::Ok) {
    setChatNotice("记录连接信息不完整");return false;
  }
  if(officialHistoryJob.start(*input)!=official_history::StartResult::Started) {
    setChatNotice("记录任务暂不可用，请稍后重试");return false;
  }
  officialHistoryMessageId=messageId?messageId:"";officialHistoryRequestId=nextChatRequestId();
  officialHistoryRequestPage=page;officialHistoryRequestDirection=direction;
  officialHistoryRequestRevision=officialHistoryRevision;
  officialHistoryForSpeech=false;
  chatHistoryRequest=officialHistoryRequestId;chatHistoryAtMs=millis();return true;
}

bool officialApplyHistory(official_history::Output& output) {
  if(!officialChatContextActivated() || !mcuSocketNamespaceReady || output.epoch!=officialEpoch ||
      officialSession!=output.session.data() || selectedProfile!=output.profile.data() ||
      chatHistoryRequest!=officialHistoryRequestId) return false;
  chatHistoryRequest="";
  if(officialHistoryRequestRevision!=officialHistoryRevision) {
    chatFocusMessage="";chatPageFocus=NativeChatBrowseFocus::Preserve;
    if(!chatRemoteRunActive) officialHistoryQueued=true;
    return false; // A snapshot started before newer live text cannot replace it.
  }
  const int code=output.httpCode;
  if(output.result!=official_history::Result::Ok &&
      !(output.result==official_history::Result::Empty && !officialSessionExists && officialHistoryTotal==0)) {
    if(output.result==official_history::Result::AuthInvalid) queueMcuReauth(true,F("standard_history_auth_invalid"));
    if(output.result==official_history::Result::Transient || output.result==official_history::Result::NoMemory) {
      officialHistoryQueued=true;officialHistoryNotBeforeMs=millis()+3000;
    }
    setChatNotice(String("记录加载失败 HTTP ")+code);return false;
  }
  if(!output.snapshot) output.snapshot.reset(new(std::nothrow)official_history::Snapshot(official_incoming::Kind::History));
  if(!output.snapshot) {setChatNotice("内存不足，未加载记录");return false;}
  auto& snapshot=output.snapshot;
  const String request=officialHistoryRequestId;
  const char* messageId=officialHistoryMessageId.length()?officialHistoryMessageId.c_str():nullptr;
  const bool replace=messageId==nullptr;
  const uint32_t page=officialHistoryRequestPage;
  const int8_t direction=officialHistoryRequestDirection;
  if(code==200 && !officialSessionExists) officialResumeQueued=true;
  officialSessionExists=code==200;
  if(code==404) officialResumeQueued=officialResumePending=false;
  if(!messageId && snapshot->count==0 && snapshot->rows &&
      ((direction>0 && snapshot->hasMore) || (direction<0 && page>0))) {
    officialHistoryOffset=direction>0?page+snapshot->rows:(page>snapshot->rows?page-snapshot->rows:0);
    officialHistoryDirection=direction;officialHistorySkippingTools=true;
    officialHistoryQueued=true;officialHistoryNotBeforeMs=millis()+50;
    chatHistoryLoadedVersion=agentSelection.version;setChatNotice("正在读取更早的消息");return false;
  }
  if(officialExpectedMessage.length()) {
    bool found=false;for(size_t i=0;i<snapshot->count;++i) if(officialExpectedMessage==snapshot->messages[i].id.data())found=true;
    if(!found && !messageId && page==0 && millis()-officialExpectedMessageAtMs<5000) {
      officialHistoryQueued=true;officialHistoryNotBeforeMs=millis()+1000;return false;
    }
    if(found || millis()-officialExpectedMessageAtMs>=5000) officialExpectedMessage="";
  }
  const NativeChatRoute route(chatModel.agentId(),selectedProfile.c_str(),agentSelection.version,officialSession.c_str());
  if(chatModel.expectHistory(request.c_str(),replace)!=NativeChatResult::Ok ||
      chatModel.beginHistory(route,request.c_str())!=NativeChatResult::Ok) return false;
  officialHistoryTotal=snapshot->total;
  if(officialSpeechPlan.phase()==official_speech::Phase::Reading && officialSpeechNeedsFreshTotal && !chatRemoteRunActive) {
    if(snapshot->total>officialSpeechBaseline) {
      officialSpeechTotal=snapshot->total;officialSpeechNeedsFreshTotal=false;
    } else if(millis()-officialSpeechCompletedAtMs<10000) {
      officialHistoryQueued=true;officialHistoryNotBeforeMs=millis()+1000;
    } else {
      officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
      setChatNotice("尚未找到可朗读的回复，文字记录已保留");
    }
  }
  official_studio::InputView sentView;sentView.data=chatSentText;sentView.bytes=chatSentTextBytes;sentView.valid=true;
  const auto* historyAgent=official_studio::agentRoute(chatModel.agentId());
  if(historyAgent && !std::strcmp(historyAgent->source,"coding_agent")) sentView=official_studio::storedCodingInput(chatSentText);
  if(replace) for(auto& pos:officialMessagePositions) {pos.id="";pos.offset=0;}
  for(size_t i=0;i<snapshot->count;++i) {
    const auto& msg=snapshot->messages[i];
    if(messageId && std::strcmp(msg.id.data(),messageId)) continue;
    chatModel.beginMessage(route,request.c_str(),msg.id.data(),!std::strcmp(msg.role.data(),"user")?
        NativeChatRole::User:NativeChatRole::Assistant,msg.content.start,msg.content.total,true,true);
    size_t used=0;
    do {
      size_t bytes=native_chat::prefixBytes(msg.content.text.data()+used,msg.content.bytes-used,kNativeChatChunkBytes);
      bool last=used+bytes==msg.content.bytes;
      chatModel.appendChunk(route,request.c_str(),msg.id.data(),!std::strcmp(msg.role.data(),"user")?
          NativeChatRole::User:NativeChatRole::Assistant,msg.content.start+used,msg.content.text.data()+used,bytes,last,msg.content.total);
      used+=bytes; if(last) break;
    } while(used<msg.content.bytes);
    if(replace) {officialMessagePositions[i].id=msg.id.data();officialMessagePositions[i].offset=page+snapshot->rows-1-msg.index;}
    // An uncorrelated coding start never clears a draft. Confirm it against
    // persisted user text only after this run's history has been reloaded.
    const uint64_t ordinal=snapshot->total>=page+snapshot->rows?snapshot->total-page-snapshot->rows+msg.index+1:0;
    if(chatSendUnknown && officialSendRunObserved && !chatRemoteRunActive &&
       chatSentAgent==chatModel.agentId() && chatSentProfile==selectedProfile && chatModel.context() &&
       chatSentBinding==chatModel.context()->binding && ordinal>officialSentHistoryBaseline &&
       sentView.valid && !std::strcmp(msg.role.data(),"user") && msg.content.total==sentView.bytes &&
       msg.content.bytes==sentView.bytes && !std::memcmp(msg.content.text.data(),sentView.data,sentView.bytes)) {
      if(officialSendWasDraft && chatDraftMatchesSent(chatModel.draft())) chatModel.draft()->clear();
      chatSendUnknown=false;
    }
  }
  chatModel.endHistory(route,request.c_str(),snapshot->hasMore,page>0);
  chatHistoryRequest=""; chatHistoryLoadedVersion=agentSelection.version;
  if(chatFocusMessage.length()) { chatBottomOffset=cardputerChatMessageScroll(chatModel,chatFocusMessage.c_str(),chatFocusAtEnd,chatNotice.length()!=0);chatFocusMessage=""; }
  if(chatPageFocus!=NativeChatBrowseFocus::Preserve) {
    chatBottomOffset=nativeChatPageBottomOffset(chatPageFocus,
        cardputerChatMaxScroll(chatModel,chatNotice.length()!=0),chatBottomOffset);
    chatPageFocus=NativeChatBrowseFocus::Preserve;
  }
  ++chatUiRevision;oledDirty=true;return true;
}

bool officialSendDraft() {
  const auto* draft=chatModel.draft();
  return draft && draft->hasContent() ? officialSendInput(draft->data(),true) : false;
}

bool officialSendInput(const char* text,bool fromDraft) {
  if(!agentSelection.ready()||!chatModel.ready()||!mcuSocketNamespaceReady) {setChatNotice("会话尚未就绪，草稿已保留");return false;}
  if(officialResumeQueued || officialResumePending) {setChatNotice("正在核对后台运行状态，草稿已保留");return false;}
  if(!text || !*text || std::strlen(text)>kNativeChatDraftBytes || !native_chat::validUtf8(text,std::strlen(text))) return false;
  if(chatPendingSend.length()||chatRemoteRunActive||officialVoiceNetworkBusy()) {setChatNotice("上一条仍在处理，输入已保留");return false;}
  if(officialHistoryQueued || officialHistoryJob.busy() || chatHistoryRequest.length() ||
      chatHistoryLoadedVersion!=agentSelection.version) {
    setChatNotice("正在同步记录，输入已保留，请稍后发送");return false;
  }
  if(fromDraft && chatSendUnknown && chatSentAgent==chatModel.agentId() && chatSentProfile==selectedProfile && chatModel.context() &&
     chatSentBinding==chatModel.context()->binding && chatDraftMatchesSent(chatModel.draft())) {
    setChatNotice("发送结果未知，请先刷新记录；未自动重发");return false;}
  resetListeningMonitor();
  JsonDocument body; const String request=nextChatRequestId();
  auto context=officialContext(); official_studio::Result result;
  if(officialMode==official_studio::Mode::Scoped)
    result=official_studio::buildScopedRun(body,context,officialModel.identity(),text,request.c_str());
  else {
    result=official_studio::buildNativeGlobalRun(body,context,officialNativeReadiness,text,request.c_str());
  }
  if(result!=official_studio::Result::Ok) {setChatNotice("模型配置不可用，请检查 Studio 配置");return false;}
  chatSentTextBytes=std::strlen(text);std::memmove(chatSentText,text,chatSentTextBytes+1);officialSendWasDraft=fromDraft;
  chatSentAgent=chatModel.agentId();chatSentProfile=selectedProfile;chatSentBinding=chatModel.context()->binding;
  officialSentHistoryBaseline=officialHistoryTotal;officialSendRunObserved=false;
  if(!officialRunGuard.startPending(request.c_str())) {setChatNotice("上一条尚未确认，请先同步会话");return false;}
  chatPendingSend=request;chatSendAtMs=millis();chatSendUnknown=false;
  ++officialHistoryRevision;
  if(!officialEmit("run",body)) {
    officialRunGuard.disconnect();chatPendingSend="";chatSendUnknown=true;
    setChatNotice("发送结果未知，草稿已保留；未自动重发");return false;
  }
  officialRun="";officialLiveId="";officialLiveText.reset();
  armOfficialSpeech();
  chatBottomOffset=0;setChatNotice("");return true;
}

void tickOfficialStudio() {
  // A close can happen inside a WebSocket callback. Release the staged parser
  // only after that callback and its frame reader have returned.
  if(officialSocketResetQueued) {resetOfficialSocketReader();officialSocketResetQueued=false;}
  {
    official_history::Output history;
    if(officialHistoryJob.poll(history)==official_history::Poll::Ready) {
      if(officialHistoryForSpeech) applyOfficialSpeechHistory(history);else officialApplyHistory(history);
    }
  }
  official_selection::Output selection;
  const auto polled=officialSelectionJob.poll(selection);
  if(!officialStudioDirect()||!mcuSocketNamespaceReady) return;
  if(polled==official_selection::Poll::Ready && selection.epoch==officialEpoch && agentSelection.pending &&
      pendingAgentProfile==selection.profile.data() &&
      !std::strcmp(kNativeAgents[agentSelection.requested].id,selection.agentId.data())) {
    const size_t index=agentSelection.requested;
    const auto selectionNotice=official_selection_notice::describe(official_selection_notice::classify(selection));
    // Fixed reason and numeric enums only: no server text, account/Profile,
    // model identity, URL or credentials. Once per accepted worker result.
    Serial.printf("[official-selection] reason=%s result=%u stage=%u http=%d native=%d model=%d\n",
        selectionNotice.code,static_cast<unsigned>(selection.result),static_cast<unsigned>(selection.stage),
        selection.httpStatus,official_selection_notice::nativeDiagnostic(selection),
        official_selection_notice::modelDiagnostic(selection));
    if(selection.result==official_selection::Result::Transient || selection.result==official_selection::Result::NoMemory) {
      officialSelectionQueued=true;
      if(officialSelectionRetries<5) ++officialSelectionRetries;
      const uint32_t backoff=std::min<uint32_t>(30000,1000U<<officialSelectionRetries);
      officialSelectionNotBeforeMs=millis()+std::max(backoff,selection.retryAfterMs);
      setChatNotice(selectionNotice.text);
    } else if(selection.result!=official_selection::Result::Ok) {
      agentSelection.pending=agentSelection.confirmed=false;agentSelectionError="standard_agent_unavailable";
      if(selection.result==official_selection::Result::AuthInvalid) {
        setChatNotice(selectionNotice.text);queueMcuReauth(true,F("standard_selection_auth_invalid"));
      } else setChatNotice(selectionNotice.text);
    } else {
      officialAccount=selection.accountId.data();officialModel=selection.selection;officialMode=selection.mode;
      officialBinding=officialSessionIdentity().substring(3);
      // A previous explicit rotation may still own a durable-write candidate.
      // Resolve that transaction before changing bindings, without exposing or
      // activating its session in the new account/Agent context.
      if(officialSessionStore.pending()) officialSessionStore.ensure(officialSessionStore.binding());
      const auto stored=officialSessionStore.ensure(officialBinding.c_str());
      if(stored!=official_sessions::Result::Ok) {
        // A failed durable write never enables a fresh session identity. Keep
        // its one pending candidate and retry without deleting another key.
        if(officialSessionStore.pending()) {
          officialSelectionQueued=true;officialSelectionNotBeforeMs=millis()+5000;
          setChatNotice("会话尚未保存，正在重试；原有记录已保留");
        } else {
          agentSelection.pending=agentSelection.confirmed=false;agentSelectionError="standard_session_storage_failed";
          setChatNotice("会话存储不可用，未新建对话；原有记录已保留");
        }
        return;
      }
      officialSession=String("m5-")+officialBinding+"-"+officialSessionStore.nonce();
      officialRun="";officialLiveText.reset();officialHistoryTotal=0;
      officialSessionExists=officialResumePending=officialResumeQueued=false;
      officialRunGuard=official_run_guard::Guard{};
      officialSendRunObserved=false;officialExpectedMessage="";officialHistorySkippingTools=false;
      for(auto& pos:officialMessagePositions) {pos.id="";pos.offset=0;}
      if(!++officialEpoch) ++officialEpoch;
      if(!++officialSelectionVersion) ++officialSelectionVersion;
      agentSelection.acknowledge(index,officialSelectionVersion);agentSelectionError="";chatProtocolReady=true;
      resetMcuSocketReconnect();
      prefs.begin("stockLan",false);prefs.putString("agent_id",kNativeAgents[index].id);prefs.end();
      if(officialMode==official_studio::Mode::Global) {
        // readiness() checks the validated worker epoch. Bind to the new epoch
        // only after the main loop has accepted this same immutable identity.
        auto validated=officialContext();validated.connectionEpoch=selection.epoch;
        officialNativeReadiness=selection.readiness(validated);
        officialNativeReadiness.context=officialContext();
      }
      // A fresh ID is created by the first run; resuming it before existence
      // has been confirmed would produce a misleading session-not-found error.
      officialHistoryQueued=true;
      oledDirty=true;
      if(agentSelection.browse!=agentSelection.active) requestCardputerAgentSelection(agentSelection.browse);
    }
  }
  if(officialSelectionQueued && !officialSelectionJob.busy() && !officialHistoryJob.busy() && !officialVoiceNetworkBusy() && agentSelection.pending &&
      static_cast<int32_t>(millis()-officialSelectionNotBeforeMs)>=0) {
    resetListeningMonitor();
    auto input=std::unique_ptr<official_selection::Input>(new(std::nothrow)official_selection::Input);
    if(!input) {officialSelectionNotBeforeMs=millis()+5000;setChatNotice("内存暂不足，稍后自动重试");return;}
    const auto valid=official_selection::makeInput(*input,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),
        selectedProfile.c_str(),kNativeAgents[agentSelection.requested].id,officialEpoch);
    const auto started=valid==official_selection::Result::Ok ? officialSelectionJob.start(*input) :
        official_selection::StartResult::Invalid;
    if(started==official_selection::StartResult::Started) {
      officialSelectionQueued=false;agentSelectionStartedMs=millis();
    } else if(started==official_selection::StartResult::NoMemory) {
      officialSelectionNotBeforeMs=millis()+5000;setChatNotice("内存暂不足，稍后自动重试");
    } else {
      officialSelectionQueued=false;agentSelection.pending=agentSelection.confirmed=false;
      agentSelectionError="standard_binding_invalid";setChatNotice("后台连接信息不完整，已保留原有绑定");
    }
  }
  if(officialResumePending && millis()-officialResumeAtMs>15000) {
    closeMcuSocketTransport(F("standard session resume timeout"));return;
  }
  if(officialResumeQueued && !officialResumePending && agentSelection.ready() && officialSessionExists) {
    resetListeningMonitor();
    officialResumeQueued=false;JsonDocument resume;resume["session_id"]=officialSession;
    if(officialEmit("resume",resume)) {officialResumePending=true;officialResumeAtMs=millis();}
  }
  if(officialHistoryQueued && officialChatContextActivated() && !officialHistoryJob.busy() && !officialSelectionJob.busy() && !officialVoiceNetworkBusy() &&
      static_cast<int32_t>(millis()-officialHistoryNotBeforeMs)>=0) {
    if(officialRequestHistory(nullptr,0,nullptr,nullptr)) officialHistoryQueued=false;
  }
}

bool officialControlEvent(const String& event) {
  if(!officialSession.length() || !mcuSocketNamespaceReady) return false;
  if(event=="abort") {
    if(!officialSessionExists) return true;
    JsonDocument body;body["session_id"]=officialSession;return officialEmit("abort",body);
  }
  if(event=="new_session") {
    if(!agentSelection.ready() || !officialBinding.length()) return false;
    cancelOfficialVoice();
    if(officialSessionStore.rotate(officialBinding.c_str())!=official_sessions::Result::Ok) {
      setChatNotice("新会话未能保存，仍保留当前对话；可再次双击重试");return false;
    }
    if(chatRemoteRunActive || chatPendingSend.length()) {
      JsonDocument abort;abort["session_id"]=officialSession;officialEmit("abort",abort);
    }
    officialSession=String("m5-")+officialBinding+"-"+officialSessionStore.nonce();
    officialHistoryJob.cancel();
    officialSessionExists=officialResumePending=officialResumeQueued=false;
    if(!++officialEpoch) ++officialEpoch;
    officialNativeReadiness.context=officialContext();officialRunGuard=official_run_guard::Guard{};
    officialRun=officialLiveId=officialExpectedMessage="";officialLiveText.reset();
    officialHistoryTotal=officialHistoryOffset=0;officialHistorySkippingTools=false;
    for(auto& pos:officialMessagePositions) {pos.id="";pos.offset=0;}
    chatPendingSend=chatHistoryRequest="";chatSendUnknown=chatRemoteRunActive=chatLiveEventsSeen=false;
    chatModel.clearMessages();chatModel.disconnect();chatActivationAttemptVersion=0;
    chatHistoryLoadedVersion=0;chatBottomOffset=0;mcuInteractionActive=false;
    officialHistoryQueued=true;officialHistoryNotBeforeMs=millis();
    setChatNotice("已新建会话，旧记录保留在 Studio");++chatUiRevision;oledDirty=true;return true;
  }
  return false;
}

void officialApplyEvent(const official_incoming::Snapshot<0>& frame) {
  if(officialSession!=frame.session.data() || !agentSelection.ready()) return;
  const String event=frame.event.data();
  if(event=="run.started" || event=="message.delta" || event=="run.completed" || event=="run.failed" ||
      event=="abort.completed" || event=="session.command") ++officialHistoryRevision;
  if(event=="resumed") {
    if(!frame.hasWorking) {setChatNotice("后台会话状态格式错误");return;}
    officialResumePending=false;
    chatRemoteRunActive=frame.working;mcuInteractionActive=frame.working;
    officialHistorySkippingTools=false;
    if(!frame.working) {
      officialRun="";officialLiveId="";mcuInteractionActive=false;
      if(!chatPendingSend.length()) officialRunGuard.disconnect();
    }
    officialHistoryQueued=true;oledDirty=true;return;
  }
  if(event=="abort.completed") {
    if(officialRunGuard.acceptAbort(frame.run.data(),frame.queue.data())==official_run_guard::Decision::Reject) return;
    officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
    // Coding abort IDs can identify the managed process instead of the
    // response run. Only an authoritative resumed.isWorking clears busy.
    officialResumeQueued=officialHistoryQueued=true;
    setChatNotice("正在同步停止后的会话状态");return;
  }
  if(event=="session.command") {
    // Commands may finish without a run.started and omit queue_id entirely.
    // Matching their name lets us stop the pending spinner, but does not
    // constitute a request-correlated ACK or erase the user's draft.
    String sent(chatSentText);sent.trim();
    const int space=sent.indexOf(' ');const String name=sent.startsWith("/")?sent.substring(1,space<0?sent.length():space):String();
    if(chatPendingSend.length() && name.length() && name==frame.command.data()) {
      const bool terminal=!frame.hasCommandTerminal || frame.commandTerminal ||
          (frame.hasCommandOk && !frame.commandOk);
      if(terminal) {
        chatPendingSend="";chatSendUnknown=true;officialRunGuard.disconnect();
        officialResumeQueued=true;
      }
    }
    if(frame.commandMessage.closed && frame.commandMessage.bytes) setChatNotice(frame.commandMessage.text.data());
    officialHistoryQueued=true;return;
  }
  const String run=frame.run.data();
  if(event=="run.started") {
    const bool duplicate=officialRunGuard.hasActive() && run==officialRunGuard.activeRun();
    const auto decision=officialRunGuard.acceptStarted(run.c_str(),frame.queue.data());
    if(decision==official_run_guard::Decision::Reject) return;
    if(decision==official_run_guard::Decision::Reconcile) {officialResumeQueued=true;return;}
    if(duplicate) return;
    ++chatAcceptedCount;
    Serial.printf("Official chat run accepted=%lu\n",static_cast<unsigned long>(chatAcceptedCount));
    officialSessionExists=true;
    mcuInteractionActive=true;mcuInteractionStatus="thinking";mcuInteractionId=run;
    officialHistorySkippingTools=false;
    officialRun=run;officialLiveId=String("live:")+run;officialLiveText.reset();
    if(officialLiveId.length()>kNativeChatIdBytes) {setChatNotice("后台运行标识过长，使用记录同步");officialLiveId="";}
    chatRemoteRunActive=true;chatLiveRequest=chatPendingSend;
    if(chatPendingSend.length()) {
      officialSendRunObserved=true;
      const bool correlated=frame.queue[0] && chatPendingSend==frame.queue.data();
      if(officialSendWasDraft && correlated && chatDraftMatchesSent(chatModel.draft())) chatModel.draft()->clear();
      chatSendUnknown=!correlated;
      official_incoming::Window user;user.began=user.closed=true;user.append(chatSentText,chatSentTextBytes);
      const String id=String("pending:")+chatPendingSend;
      officialPublishWindow(id.c_str(),NativeChatRole::User,user,true);
      chatPendingSend="";
    }
    ++chatUiRevision;oledDirty=true;return;
  }
  if(event=="message.delta") {
    if(!run.length() || run!=officialRun || !officialLiveId.length() || !frame.text.closed) return;
    if(frame.text.total) ++chatReceivedChunks;
    const uint32_t oldTotal=officialLiveText.total;
    if(frame.text.total>UINT32_MAX-oldTotal) {setChatNotice("回复长度超出设备索引范围，请在电脑查看");return;}
    if(!officialLiveText.capped) officialLiveText.append(frame.text.text.data(),frame.text.bytes);
    officialLiveText.capped=officialLiveText.capped || frame.text.capped || frame.text.total>frame.text.bytes;
    officialLiveText.total=oldTotal+frame.text.total;
    officialPublishWindow(officialLiveId.c_str(),NativeChatRole::Assistant,officialLiveText,false);
    chatLiveEventsSeen=true;return;
  }
  if(event=="run.completed" || event=="run.failed") {
    const bool correlated=frame.queue[0] && chatPendingSend.length() && chatPendingSend==frame.queue.data();
    const auto decision=officialRunGuard.acceptTerminal(run.c_str(),frame.queue.data());
    if(decision==official_run_guard::Decision::Reject) return;
    if(decision==official_run_guard::Decision::Reconcile) {
      if(chatPendingSend.length()) {chatPendingSend="";chatSendUnknown=true;}
      officialResumeQueued=officialHistoryQueued=true;
      if(event=="run.failed") setChatNotice(chatFailureNotice(frame.error[0]?frame.error.data():"正在核对后台状态"));
      return;
    }
    // Session-scoped failures without IDs reconcile authoritative state and
    // preserve an uncertain draft. They never acknowledge a particular send.
    if(!run.length() && !frame.queue[0]) chatSendUnknown=chatPendingSend.length() || chatSendUnknown;
    if(chatPendingSend.length()) {
      if(officialSendWasDraft && event=="run.completed" && correlated && chatDraftMatchesSent(chatModel.draft())) chatModel.draft()->clear();
      chatSendUnknown=!(event=="run.completed" && correlated);chatPendingSend="";
    }
    if(event=="run.completed" && frame.text.closed && officialLiveId.length())
      officialPublishWindow(officialLiveId.c_str(),NativeChatRole::Assistant,frame.text,true);
    officialExpectedMessage=event=="run.completed"?String(frame.messageId.data()):String();
    if(event=="run.completed" && officialSpeechPlan.complete()) {
      officialSpeechCompletedAtMs=millis();officialSpeechNeedsFreshTotal=true;
    } else if(event=="run.failed") {officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;}
    officialHistorySkippingTools=false;
    officialExpectedMessageAtMs=millis();
    officialHistoryNotBeforeMs=millis()+400;
    if(event=="run.failed") setChatNotice(chatFailureNotice(frame.error[0]?frame.error.data():"请检查 Studio 的模型与登录"));
    else setChatNotice("");
    chatRemoteRunActive=false;mcuInteractionActive=false;officialRun="";officialLiveId="";
    officialHistoryQueued=true;++chatUiRevision;oledDirty=true;
  }
}

void cancelOfficialVoice(const char* notice) {
  const bool owned=officialVoiceNetworkBusy();
  resetListeningMonitor();
  officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
  if(officialHistoryForSpeech) officialHistoryJob.cancel();
  officialVoiceCancelled=true;
  officialRecordingRequested=false;
  officialVoiceJob.cancel();
  if(officialRecording.active) {
    officialRecording.active=false;officialVoiceCleanupPending=true;
    officialRecordingStore.discard();
  }
  officialVoiceDrainPending=officialVoiceOutputPlaying=false;
  officialVoiceRate=officialVoiceTailFrames=officialVoiceDrainAtMs=officialVoiceTailAtMs=0;
  if(owned) {
    cardputerAudioClear();M5Cardputer.Mic.end();audioBusy=false;resetListeningMonitor();
    if(!chatRemoteRunActive) {mcuInteractionActive=false;mcuInteractionStatus="aborted";mcuInteractionUpdatedAtMs=millis();}
  }
  if(notice && *notice) setChatNotice(notice);
}

std::unique_ptr<official_voice_job::Context> officialVoiceContext() {
  auto context=std::unique_ptr<official_voice_job::Context>(new(std::nothrow)official_voice_job::Context);
  if(!context || official_voice_job::makeContext(*context,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),
      selectedProfile.c_str(),officialSession.c_str(),officialEpoch)!=official_voice::Error::Ok) return {};
  return context;
}

void finishOfficialRecording(bool success,const char* failure=nullptr) {
  if(!officialRecording.active) return;
  officialRecording.active=false;cardputerAudioClear();M5Cardputer.Mic.end();resetListeningMonitor();audioBusy=false;
  const auto capture=cardputerCaptureStats();
  if(success && (capture.failed || capture.storageRetained)) {
    success=false;failure="录音缓冲溢出或采集失败，本次未发送";
  }
  sampleOfficialRecordingMemory();
  officialRecordingLastPcmBytes=officialRecordingStore.recordedBytes();
  officialRecordingLastEncodedBytes=officialRecordingStore.encodedBytes();
  voiceRecordPeak=officialRecording.peak;
  voiceRecordRms=officialRecording.frames?uint32_t(std::sqrt(double(officialRecording.squares)/officialRecording.frames)):0;
  voiceRecordActiveSamples=officialRecording.activeSamples;
  voiceRecordHeardSpeech=officialRecording.heardSpeech || (voiceRecordRms>=kVoiceVadRmsStart &&
      voiceRecordPeak>=kVoiceVadPeakStart && voiceRecordActiveSamples>=kVoiceVadMinActiveSamples);
  if(!success || !voiceRecordHeardSpeech) {
    officialRecordingStore.discard();mcuInteractionActive=false;
    setChatNotice(failure?failure:"没有检测到清晰声音，请重试");return;
  }
  if(officialRecordingStore.finish()!=ram_recording::Error::Ok) {
    officialRecordingStore.discard();mcuInteractionActive=false;setChatNotice("录音保存失败，本次未发送");return;
  }
  auto context=officialVoiceContext();ram_recording::Result error;
  auto reader=officialRecordingStore.openReader(error);
  const size_t bytes=officialRecordingStore.recordedBytes();
  const uint64_t nonce=(uint64_t(esp_random())<<32)|esp_random();
  if(!context || !reader || error!=ram_recording::Error::Ok) {
    officialRecordingAdmissionError=3;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    setChatNotice("语音识别内存不足，本次未发送");return;
  }
  // Capture has been joined and freed. Account for only one worker stack,
  // its actual SharedState and LAN HTTP headroom while RAM audio stays alive.
  officialRecordingWorkerFree=heap_caps_get_free_size(kOfficialVoiceHeapCaps);officialRecordingWorkerLargest=heap_caps_get_largest_free_block(kOfficialVoiceHeapCaps);
  const size_t workerBytes=official_voice_job::kTaskStackBytes+official_voice_job::Job::fixedJobHeapBytes();
  sampleOfficialRecordingMemory();
  if(officialRecordingWorkerFree<workerBytes+12*1024 || officialRecordingWorkerLargest<std::max<size_t>(official_voice_job::kTaskStackBytes,official_voice_job::Job::fixedJobHeapBytes())+512) {
    officialRecordingAdmissionError=4;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    setChatNotice("语音识别内存不足，本次未发送");return;
  }
  const auto started=officialVoiceJob.startStt(*context,std::move(reader),bytes,kVoiceInputSampleRate,1,nonce);
  sampleOfficialRecordingMemory();
  if(started!=official_voice_job::StartResult::Started) {
    officialRecordingAdmissionError=5;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    setChatNotice(started==official_voice_job::StartResult::NoMemory?
        "语音识别内存不足，本次未发送":"识别任务暂不可用，本次语音未发送");return;
  }
  officialVoiceEpoch=officialEpoch;officialVoiceCancelled=false;
  officialVoiceCleanupPending=true;audioBusy=true;
  mcuInteractionActive=true;mcuInteractionStatus="transcribing";mcuInteractionUpdatedAtMs=millis();
  setChatNotice(failure?failure:"");oledDirty=true;
}

void startOfficialRecording() {
  if(officialRecording.active) return;
  if(!conversationView.conversation() || !officialStudioDirect() || !officialChatContextActivated() ||
      !chatModel.ready() || !mcuSocketNamespaceReady || !wifiReady || WiFi.status()!=WL_CONNECTED) {
    setChatNotice("当前会话尚未就绪，请连接后台后再说话");return;
  }
  if(officialVoiceNetworkBusy() || chatRemoteRunActive || chatPendingSend.length() || officialResumePending || officialResumeQueued) {
    cancelOfficialVoice();officialRecordingRequested=true;
    if(chatRemoteRunActive || chatPendingSend.length()) {
      if(chatPendingSend.length()) {chatPendingSend="";chatSendUnknown=true;}
      officialControlEvent("abort");officialResumeQueued=true;
    }
    setChatNotice("正在停止上一条，保持按住即可说话");return;
  }
  if(officialSelectionJob.busy() || officialHistoryJob.busy() || officialHistoryQueued ||
      chatHistoryLoadedVersion!=agentSelection.version) {
    officialRecordingRequested=true;setChatNotice("正在同步记录，保持按住即可说话");
    return;
  }
  officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;officialRecordingRequested=false;
  if(!i2sReady || !es8311Ready) {setChatNotice("麦克风尚未就绪");return;}
  // Stop idle speaker/Mic tasks and join all DMA ownership before reserving
  // the compressed take. RAM recording never mounts or writes a filesystem.
  cardputerTickKeyClick(false);cardputerAudioClear();M5Cardputer.Mic.end();resetListeningMonitor();
  officialRecordingHeapStart=officialRecordingHeapMinimum=officialRecordingLargestMinimum=0;
  officialRecordingLastPcmBytes=officialRecordingLastEncodedBytes=0;
  officialRecordingWorkerFree=officialRecordingWorkerLargest=0;officialRecordingAdmissionError=0;
  sampleOfficialRecordingMemory();
  const auto storage=officialRecordingStore.prepare();
  sampleOfficialRecordingMemory();
  officialRecordingPrepareResult=storage;
  Serial.printf("Official recording prepare error=%s capacity=%lu\n",ram_recording::errorName(storage),
      static_cast<unsigned long>(officialRecordingStore.capacityBytes()));
  if(storage!=ram_recording::Error::Ok ||
      officialRecordingStore.capacityBytes()<kVoiceInputSampleRate*2*kVoiceRecordMinMs/1000) {
    officialRecordingAdmissionError=1;
    setChatNotice("录音内存不足，文字聊天仍可使用");return;
  }
  const auto beginResult=officialRecordingStore.begin();
  officialRecordingBeginResult=beginResult;
  if(beginResult!=ram_recording::Error::Ok) {
    Serial.printf("Official recording begin error=%s\n",ram_recording::errorName(beginResult));
    officialRecordingStore.discard();
    setChatNotice("录音存储不可用，本次未发送");return;
  }
  // The advertised limit follows the actual compressed RAM allocation.
  const uint32_t limitMs=5000;
  const uint32_t frameLimit=std::min<uint32_t>(kVoiceInputSampleRate*limitMs/1000,
      officialRecordingStore.capacityBytes()/sizeof(int16_t));
  if(frameLimit<kVoiceInputSampleRate*kVoiceRecordMinMs/1000) {
    officialRecordingStore.discard();
    setChatNotice("录音空间不足以保留本次语音，本次未发送");return;
  }
  officialRecording=OfficialRecordingState{};officialRecording.active=true;officialRecording.automatic=false;
  officialRecording.frameLimit=frameLimit;officialRecording.channel=VoiceInputChannel::Undecided;
  officialRecording.epoch=officialEpoch;officialRecording.keys=cardputerChatKeyCount();
  officialRecording.navigation=cardputerNavigationCount();officialRecording.startedAt=officialRecording.lastSpeechAt=millis();
  cardputerTickKeyClick(false);
  cardputerAudioClear();
  if(!setI2sSampleRate(kVoiceInputSampleRate)) {
    finishOfficialRecording(false,"麦克风采样率设置失败，本次未发送");return;
  }
  // A failed allocation must not let the codec or network starve silently.
  // Keep SDK microphone DMA/task and network/UI headroom beyond the queue.
  if(heap_caps_get_free_size(kOfficialVoiceHeapCaps)<54*1024 || heap_caps_get_largest_free_block(kOfficialVoiceHeapCaps)<sizeof(mic_capture::Queue) ||
      heap_caps_get_free_size(MALLOC_CAP_DMA)<8*1024 || heap_caps_get_largest_free_block(MALLOC_CAP_DMA)<512) {
    officialRecordingAdmissionError=2;
    finishOfficialRecording(false,"连续录音内存不足，文字聊天仍可使用");return;
  }
  audioBusy=true;mcuInteractionActive=true;mcuInteractionStatus="listening";mcuInteractionUpdatedAtMs=millis();
  if(!cardputerCaptureBegin(kVoiceInputSampleRate) || cardputerCaptureStats().failed) {
    finishOfficialRecording(false,"连续录音缓冲不可用，本次未发送");return;
  }
  sampleOfficialRecordingMemory();
  setChatNotice(String("录音可用约 ")+String(double(officialRecording.frameLimit)/kVoiceInputSampleRate,1)+" 秒，松开发送");oledDirty=true;
}

void tickOfficialRecording() {
  if(!officialRecording.active) return;
  const uint32_t now=millis();
  if(officialRecording.epoch!=officialEpoch || !conversationView.conversation() || !officialChatContextActivated() ||
      cardputerChatKeyCount()!=officialRecording.keys || cardputerNavigationCount()!=officialRecording.navigation ||
      WiFi.status()!=WL_CONNECTED) {
    finishOfficialRecording(false,"录音已取消，本次未发送");return;
  }
  if(officialRecording.frames>=officialRecording.frameLimit) {
    finishOfficialRecording(true,"已达录音时长上限，发送已录内容");return;
  }
  if(now-officialRecording.startedAt>=kVoiceRecordHardTimeoutMs) {
    finishOfficialRecording(false,"录音超时，本次未发送");return;
  }
  if(!officialRecording.automatic && now-officialRecording.startedAt>=kVoiceRecordMinMs) {
    if(!cardputerTalkPressed()) {
      if(!officialRecording.releaseAt) officialRecording.releaseAt=now;
      if(now-officialRecording.releaseAt>=kVoiceStreamReleaseDebounceMs && !officialRecording.stopping) {
        // Stop requeue and join, then write every already completed block.
        // An unfinished SDK request is discarded, never labelled complete.
        cardputerCaptureStop();officialRecording.stopping=true;
      }
    } else officialRecording.releaseAt=0;
  }
  const auto captureBeforeRead=cardputerCaptureStats();
  if(captureBeforeRead.failed || (!officialRecording.stopping && !captureBeforeRead.active)) {
    finishOfficialRecording(false,"录音缓冲溢出或采集失败，本次未发送");return;
  }
  auto captured=cardputerCaptureTake();
  if(!captured) {
    if(officialRecording.stopping) finishOfficialRecording(true);
    return;
  }
  const auto* stereo=captured.data();const size_t count=captured.samples();
  updateVoiceInputChannel(officialRecording.channel,stereo,count);
  int16_t mono[512];size_t frames=0,used=0;uint64_t squares=0;uint16_t peak=0;uint32_t active=0,activeForListening=0;
  for(size_t i=0;i+1<count && frames<officialRecording.frameLimit-officialRecording.frames;i+=2) {
    const int16_t sample=voiceInputMonoSample(stereo[i],stereo[i+1],officialRecording.channel);
    mono[used++]=sample;++frames;const uint16_t magnitude=sampleMagnitude(sample);
    peak=std::max(peak,magnitude);squares+=uint64_t(magnitude)*magnitude;
    if(magnitude>=kVoiceVadActiveThreshold) ++active;
    if(magnitude>=kListeningVadActiveThreshold) ++activeForListening;
    if(used==512 || i+3>=count || frames==officialRecording.frameLimit-officialRecording.frames) {
      if(officialRecordingStore.append(reinterpret_cast<const uint8_t*>(mono),used*sizeof(int16_t))!=ram_recording::Error::Ok) {
        captured.reset();finishOfficialRecording(false,"录音空间不足或写入失败，本次未发送");return;
      }
      used=0;
    }
  }
  captured.reset(); // Must release the consumer before any stop/free path.
  sampleOfficialRecordingMemory();
  officialRecording.frames+=frames;officialRecording.squares+=squares;
  officialRecording.activeSamples+=active;officialRecording.peak=std::max(officialRecording.peak,peak);
  const uint32_t rms=frames?uint32_t(std::sqrt(double(squares)/frames)):0;
  const uint32_t requiredActive=(kListeningVadMinActiveSamples*frames+127)/128;
  if(rms>=kListeningVadRmsStart && peak>=kListeningVadPeakStart && activeForListening>=requiredActive) {
    officialRecording.heardSpeech=true;officialRecording.lastSpeechAt=now;
  }
  const auto capture=cardputerCaptureStats();
  if(capture.failed) {finishOfficialRecording(false,"录音缓冲溢出或采集失败，本次未发送");return;}
  if(officialRecording.automatic && officialRecording.heardSpeech && !capture.queue.readyBlocks &&
      now-officialRecording.lastSpeechAt>=kListeningSilenceStopMs) finishOfficialRecording(true);
  mcuInteractionUpdatedAtMs=now;
}

void armOfficialSpeech() {
  officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
  officialSpeechBaseline=officialSentHistoryBaseline;officialSpeechTotal=0;
  // Slash commands can finish without an assistant message. They must never
  // cause an older response to be replayed as the result of this request.
  if(outputVolumePercent && chatSentText[0]!='/' &&
      officialSpeechPlan.arm(officialEpoch,officialSession.c_str(),selectedProfile.c_str(),officialSpeechBaseline))
    officialSpeechCompletedAtMs=0;
}

void applyOfficialSpeechHistory(official_history::Output& output) {
  officialHistoryForSpeech=false;
  // A cancelled or newly armed plan may share the same session/epoch. Even a
  // late HTTP failure from the old page must not cancel this new request.
  if(officialSpeechPlan.phase()!=official_speech::Phase::AwaitingPage) return;
  if(!officialStudioDirect() || !conversationView.conversation() || !mcuSocketNamespaceReady ||
      output.epoch!=officialEpoch || officialSession!=output.session.data() || selectedProfile!=output.profile.data()) return;
  if(output.result!=official_history::Result::Ok || !output.snapshot) {
    officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
    setChatNotice("朗读记录加载失败，文字回复已保留");
    if(output.httpCode==401) queueMcuReauth(true,F("standard_voice_history_auth_invalid"));
    return;
  }
  const auto offered=officialSpeechPlan.offer(*output.snapshot,officialSpeechHistoryRequest,officialEpoch,
      officialSession.c_str(),selectedProfile.c_str());
  if(offered==official_speech::Offer::Silent) officialSpeechPlan.skipSilent();
  else if(offered==official_speech::Offer::Invalid) setChatNotice("回复记录已变化，已停止朗读");
  // Do not start the decoder here: this 19KiB history snapshot is released
  // before the separate main-loop voice tick allocates its task/decoder.
}

void finishOfficialPlayback(bool success,const char* failure=nullptr) {
  cardputerAudioClear(); // Join consumer before reusing a hardware source.
  officialVoiceDrainPending=officialVoiceOutputPlaying=false;
  officialVoiceRate=officialVoiceTailFrames=officialVoiceDrainAtMs=officialVoiceTailAtMs=0;
  audioBusy=false;resetListeningMonitor();
  officialSpeechPlan.played(success);
  mcuInteractionActive=false;mcuInteractionStatus=success?"completed":"failed";
  mcuInteractionUpdatedAtMs=millis();
  if(failure) setChatNotice(failure);
  oledDirty=true;
}

void tickOfficialVoice() {
  const bool current=officialStudioDirect() && conversationView.conversation() && officialChatContextActivated() &&
      mcuSocketNamespaceReady && wifiReady && WiFi.status()==WL_CONNECTED;
  if(!current || (officialVoiceJob.busy() && officialVoiceEpoch!=officialEpoch)) cancelOfficialVoice();
  tickOfficialRecording();

  official_voice_job::PcmBlock block;
  if(officialVoiceJob.tryTakePcm(block)) {
    if(!current || officialVoiceCancelled || block.epoch()!=officialEpoch || !outputVolumePercent) {
      block.reset();cancelOfficialVoice();
    } else {
      bool ready=block.samples()<=official_voice_job::kPcmSamples && block.sampleRate()>=8000 && block.sampleRate()<=96000;
      if(ready && !officialVoiceOutputPlaying) {
        cardputerTickKeyClick(false);cardputerAudioClear();resetListeningMonitor();
        officialVoiceRate=block.sampleRate();
        ready=setI2sSampleRate(officialVoiceRate) && cardputerSpeakerEnable(true);
        officialVoiceOutputPlaying=ready;
      } else if(block.sampleRate()!=officialVoiceRate) ready=false;
      if(ready) {
        for(size_t i=0;i<block.samples();++i) {
          const int16_t sample=shapeOutputSample(block.data()[i]);
          officialStereoPcm[i*2]=officialStereoPcm[i*2+1]=sample;
        }
        const size_t bytes=block.samples()*sizeof(int16_t)*2;size_t written=0;
        ready=cardputerAudioWrite(officialStereoPcm,bytes,&written,pdMS_TO_TICKS(250))==ESP_OK && written==bytes;
      }
      block.reset(); // Never hold the network queue while updating UI/state.
      if(!ready) cancelOfficialVoice("语音播放失败，文字回复已保留");
      else {audioBusy=true;mcuInteractionActive=true;mcuInteractionStatus="speaking";mcuInteractionUpdatedAtMs=millis();}
    }
  }

  // Output lives in fixed main-owned storage. pollDone releases the worker,
  // Reader and JWT only after the worker and both PCM leases have finished.
  if(officialVoiceJob.pollDone(officialVoiceCompletion)==official_voice_job::Poll::Ready) {
    const auto& out=officialVoiceCompletion;
    Serial.printf("Official voice type=%u error=%u http=%d samples=%llu rate=%lu stack_free=%lu heap_min=%lu largest_min=%lu boot_heap_min=%lu\n",
        unsigned(out.type),unsigned(out.result.error),out.result.httpStatus,
        static_cast<unsigned long long>(out.result.samples),static_cast<unsigned long>(out.result.sampleRate),
        static_cast<unsigned long>(out.workerStackFreeBytes),static_cast<unsigned long>(out.internalHeapMinObservedBytes),
        static_cast<unsigned long>(out.internalLargestBlockMinObservedBytes),static_cast<unsigned long>(out.systemHeapLowWaterBytes));

    const bool valid=current && !officialVoiceCancelled && out.epoch==officialEpoch &&
        officialSession==out.session.data() && selectedProfile==out.profile.data();
    if(!valid) {
      if(officialVoiceOutputPlaying) cardputerAudioClear();
      officialVoiceOutputPlaying=officialVoiceDrainPending=false;audioBusy=false;
    } else if(!out.result.ok()) {
      finishOfficialPlayback(false,out.result.error==official_voice::Error::MissingAudioMetadata ?
          "音频缺少采样率或声道信息，文字回复已保留" : out.type==official_voice_job::Type::Stt ?
          "语音识别失败，本次未发送":"语音播放失败，文字回复已保留");
      if(out.result.httpStatus==401) queueMcuReauth(true,F("standard_voice_auth_invalid"));
    } else if(out.type==official_voice_job::Type::Stt) {
      audioBusy=false;mcuInteractionActive=false;
      if(!officialSendInput(out.transcript.data(),false)) {
        auto* draft=chatModel.draft();const auto* ime=chatModel.inputMethod();
        if(draft && !draft->hasContent() && (!ime || !ime->composing()) &&
            draft->insert(out.transcript.data())==NativeChatEditResult::Ok)
          setChatNotice("识别已放入输入框，请按 OK 发送");
        else setChatNotice("识别完成但未发送，原输入已保留，请重试");
      }
    } else if(!officialVoiceOutputPlaying || !out.result.samples) {
      finishOfficialPlayback(false,"没有收到可播放的音频，文字回复已保留");
    } else {
      officialVoiceDrainPending=true;officialVoiceDrainAtMs=millis();officialVoiceTailAtMs=0;
      officialVoiceTailFrames=officialVoiceRate*kMcuAudioTailSilenceMs/1000;
    }
    officialVoiceCompletion.transcript.fill(0);
  }

  if(officialVoiceDrainPending) {
    // Incremental silent tail keeps input/network handling alive. A source
    // release precedes the physical DMA tail: retain the codec for 180ms too.
    if(officialVoiceTailFrames) {
      const size_t frames=std::min<uint32_t>(officialVoiceTailFrames,128);
      std::memset(officialStereoPcm,0,frames*4);size_t written=0;
      if(cardputerAudioWrite(officialStereoPcm,frames*4,&written,pdMS_TO_TICKS(250))!=ESP_OK || written!=frames*4)
        finishOfficialPlayback(false,"音频收尾失败，文字回复已保留");
      else officialVoiceTailFrames-=frames;
    } else if(!cardputerAudioStats().in_flight && !M5Cardputer.Speaker.isPlaying(0)) {
      if(!officialVoiceTailAtMs) officialVoiceTailAtMs=millis();
      if(millis()-officialVoiceTailAtMs>=kMcuAudioDrainMinMs) finishOfficialPlayback(true);
    }
    if(officialVoiceDrainPending && millis()-officialVoiceDrainAtMs>5000)
      finishOfficialPlayback(false,"音频收尾超时，文字回复已保留");
  }

  if(officialVoiceCleanupPending && !officialRecording.active && !officialRecordingStore.readerActive()) {
    const auto result=officialRecordingStore.discard();
    // A worker retains its own encoded buffer until its Reader is released.
    if(result!=ram_recording::Error::Ok) setChatNotice("录音缓存仍在使用，请稍后重试");
    officialVoiceCleanupPending=false;
  }
  if(officialRecordingRequested) {
    if(!cardputerTalkPressed() || !current) officialRecordingRequested=false;
    else if(!officialVoiceNetworkBusy() && !officialSelectionJob.busy() && !officialHistoryJob.busy() &&
        !officialHistoryQueued && chatHistoryLoadedVersion==agentSelection.version &&
        !chatRemoteRunActive && !chatPendingSend.length() && !officialResumePending && !officialResumeQueued)
      startOfficialRecording();
  }
  if(!current || officialVoiceNetworkBusy() || officialRecordingRequested || officialSelectionJob.busy() ||
      officialHistoryJob.busy() || chatRemoteRunActive || chatPendingSend.length() || officialResumePending || officialResumeQueued) return;
  if(!outputVolumePercent) {officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;return;}
  if(const auto* pending=officialSpeechPlan.pending()) {
    // Release idle UI audio and capture task/DMA BEFORE the worker can race
    // to allocate its decoder/TLS buffers on this no-PSRAM target.
    cardputerTickKeyClick(false);cardputerAudioClear();M5Cardputer.Mic.end();resetListeningMonitor();
    auto context=officialVoiceContext();
    if(!context || officialVoiceJob.startTts(*context,pending->text.data())!=official_voice_job::StartResult::Started) {
      officialSpeechPlan.played(false);setChatNotice("朗读任务暂不可用，文字回复已保留");return;
    }
    officialVoiceEpoch=officialEpoch;officialVoiceCancelled=false;audioBusy=true;
    mcuInteractionActive=true;mcuInteractionStatus="speaking";mcuInteractionUpdatedAtMs=millis();
  } else if(!officialSpeechNeedsFreshTotal &&
      (officialSpeechPlan.phase()==official_speech::Phase::Reading || officialSpeechPlan.phase()==official_speech::Phase::AwaitingPage)) {
    const auto request=officialSpeechPlan.request(officialSpeechTotal);
    if(!request.needed()) return;
    resetListeningMonitor();
    auto input=std::unique_ptr<official_history::Input>(new(std::nothrow)official_history::Input);
    if(!input || official_history::makeInput(*input,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),selectedProfile.c_str(),
        officialSession.c_str(),officialEpoch,request.offset,request.byteOffset)!=official_history::Result::Ok ||
        officialHistoryJob.start(*input)!=official_history::StartResult::Started) {
      officialSpeechPlan.cancel();setChatNotice("朗读记录暂不可用，文字回复已保留");return;
    }
    officialSpeechHistoryRequest=request;officialHistoryForSpeech=true;
  }
}

class OfficialSocketListener : public studio_ws::Listener {
 public:
  void reset() {parser_.reset();snapshot_.reset();used_=0;event_=false;ignored_=false;closeRequested=false;}
  bool messageBegin(uint8_t opcode) override {
    if(opcode!=1) return false;
    used_=0;event_=false;ignored_=false;epoch_=officialEpoch;
    parser_.reset();snapshot_.reset();return true;
  }
  bool messageByte(uint8_t byte) override {
    if(ignored_) return true;
    if(event_) return parser_ && parser_->feed(&byte,1);
    if(byte=='[' && used_>=12 && !std::memcmp(control_,"42/chat-run,",12)) {
      for(size_t n=12;n<used_;++n) if(control_[n]<'0'||control_[n]>'9') return false;
      snapshot_.reset(new(std::nothrow)official_incoming::Snapshot<0>(official_incoming::Kind::SocketEvent));
      if(!snapshot_) return false;
      parser_.reset(new(std::nothrow)bounded_json::Reader<32,256,64>(*snapshot_));
      event_=true;return parser_ && parser_->feed(&byte,1);
    }
    if(used_==16 && !std::memcmp(control_,"42/global-agent,",16)) {ignored_=true;return true;}
    if(used_>=sizeof(control_)-1) return false;
    control_[used_++]=char(byte);control_[used_]=0;return true;
  }
  bool messageEnd() override {
    bool ok=true;
    if(ignored_) {}
    else if(event_) {ok=parser_ && parser_->finish();if(ok&&epoch_==officialEpoch)officialApplyEvent(*snapshot_);}
    else if(used_ && control_[0]=='2') {control_[0]='3';ok=sendRawWsText(String(control_));}
    else handleSocketIoText(String(control_));
    parser_.reset();snapshot_.reset();return ok;
  }
  bool control(uint8_t opcode,const uint8_t* bytes,size_t length) override {
    if(opcode==9) return sendRawWsFrame(10,bytes,length);
    if(opcode==8) {closeRequested=true;return false;}
    return true;
  }
  bool closeRequested=false;
  size_t controlBytes() const {return used_;}
  bool parsingEvent() const {return event_;}
 private:
  char control_[512]{};size_t used_=0;bool event_=false,ignored_=false;uint32_t epoch_=0;
  std::unique_ptr<official_incoming::Snapshot<0>> snapshot_;
  std::unique_ptr<bounded_json::Reader<32,256,64>> parser_;
};

OfficialSocketListener officialSocketListener;
studio_ws::Reader officialFrameReader(officialSocketListener);

void resetOfficialSocketReader() {
  official_connection::resetParser(officialFrameReader,officialSocketListener,officialSocketResetQueued);
}

void officialSocketLoop() {
  uint8_t bytes[256];size_t budget=8192;
  while(wsReady && budget && mcuWsClient->available()>0) {
    const size_t count=std::min<size_t>(sizeof(bytes),std::min<size_t>(budget,mcuWsClient->available()));
    const int read=mcuWsClient->read(bytes,count);if(read<=0) break;budget-=read;
    if(!officialFrameReader.feed(bytes,read) || officialSocketListener.closeRequested) {
      if(!officialSocketListener.closeRequested) ++chatRejectedFrames;
      Serial.printf("Official WS parse error=%u close=%d control_bytes=%u event=%d reset_pending=%d\n",
          unsigned(officialFrameReader.error()),officialSocketListener.closeRequested,
          unsigned(officialSocketListener.controlBytes()),officialSocketListener.parsingEvent(),officialSocketResetQueued);
      setChatNotice("后台连接中断，未确认的发送不会自动重发");
      closeMcuSocketTransport(F("standard frame closed or invalid"));return;
    }
    mcuSocketLiveness.received(millis());yield();
  }
}

void sendConnectSuccessPage(const String &ssid, const IPAddress &ip) {
  String target = deviceUrl(ip);
  String html = pageStart(F("Wi-Fi 已连接"));
  html += F("<section class='panel'><p class='meta'>Ekko Studio Cardputer ADV</p><h1>Wi-Fi 已连接</h1>");
  html += F("<p class='lead ok'>");
  html += escapeHtml(ssid);
  html += F(" · IP ");
  html += ip.toString();
  html += F("</p><p class='hint'>设备将自动重启并关闭配网热点，随后打开局域网地址。如果手机没有自动切回同一个 Wi-Fi，请手动切回后再打开。</p>");
  html += F("<div class='btn-row'><a class='btn primary' href='");
  html += target;
  html += F("'>打开设备页面</a><a class='btn' href='/wifi'>重新配置</a></div></section>");
  html += F("<script>setTimeout(function(){location.href='");
  html += target;
  html += F("';},");
  html += kProvisionRedirectDelayMs;
  html += F(");</script>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

void sendConnectFailedPage(const String &ssid) {
  String html = pageStart(F("Wi-Fi 连接失败"));
  html += F("<section class='panel'><p class='meta'>Ekko Studio Cardputer ADV</p><h1>Wi-Fi 连接失败</h1>");
  html += F("<p class='lead bad'>没有连上 ");
  html += escapeHtml(ssid);
  html += F("。请检查 SSID 和密码后重试。</p>");
  html += F("<div class='btn-row'><a class='btn primary' href='/wifi'>返回配网</a><a class='btn primary' href='/clear'>重新配置</a></div></section>");
  html += pageEnd();
  server.send(200, F("text/html; charset=utf-8"), html);
}

bool connectWifiCredentials(const String &ssid, const String &pass, wifi_mode_t mode) {
  setOledStatus(OledMode::Think, F("WIFI"), F("CONNECT"), 25);
  WiFi.mode(mode);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kConnectTimeoutMs) {
    delay(250);
    refreshOled();
    yield();
  }

  wifiReady = WiFi.status() == WL_CONNECTED;
  if (wifiReady) {
    savedNetworkRetry.connected();
    wifiDisconnectedSinceMs = 0;
    if (mode == WIFI_STA) setupApMode = false;
    connectMcuSocketClient();
    showLanIpOnOled();
    Serial.printf("WiFi connected ssid=%s ip=%s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    esp_rom_printf("WiFi connected ssid=%s ip=%s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
  } else {
    setOledStatus(OledMode::Error, F("WIFI"), F("FAILED"), 0);
    delay(500);
  }
  return wifiReady;
}

void saveWifi() {
  String ssid = server.arg(F("ssid"));
  ssid.trim();
  String manualSsid = server.arg(F("ssid_manual"));
  manualSsid.trim();
  if (ssid == F("__manual__") || ssid.length() == 0) {
    ssid = manualSsid;
  }
  String pass = server.arg(F("pass"));
  if (ssid.length() == 0) {
    server.send(400, F("text/plain; charset=utf-8"), F("缺少 SSID"));
    return;
  }

  if (pass.length() == 0) {
    savedWifiPasswordForSsid(ssid, pass);
  }

  // A submitted replacement network takes precedence even if its connection
  // fails. Never let an old automatic-AP retry take this form away afterward.
  savedNetworkRetry.cancel();
  if (connectWifiCredentials(ssid, pass, WIFI_AP_STA)) {
    rememberWifiCredential(ssid, pass);
    sharedWifiSaveFailed = !publishFactoryWifi(ssid, pass);
    connectedWifiSource = "ekko_manual";
    if (sharedWifiSaveFailed) {
      restartPending = false;
      String html = pageStart(F("Wi-Fi 已连接，共享保存失败"));
      html += F("<section class='panel'><h1>已联网，共享保存失败</h1><p>当前网络可用，但原厂菜单尚未保存这次配置。设备会保持当前状态，请重新提交，或返回原厂 SetWiFi 保存。</p><a class='btn' href='/wifi'>重新保存</a></section>");
      html += pageEnd();
      server.send(507, F("text/html; charset=utf-8"), html);
      setOledStatus(OledMode::Error, F("WIFI"), F("SHARE SAVE ERROR"), 0);
      return;
    }
    sendConnectSuccessPage(ssid, WiFi.localIP());
    restartPending = true;
    restartAtMs = millis() + kProvisionRestartDelayMs;
    setOledStatus(OledMode::Think, F("WIFI"), F("RESTART"), 90);
    return;
  }
  sendConnectFailedPage(ssid);
}

void startSetupAp(bool refreshScan, const char *reason);

void clearWifi() {
  // Reconfiguration is not "forget all". Preserve the shared factory record
  // and history until a replacement network has connected and been saved.
  Serial.printf("WiFi reconfigure requested method=%d uptime=%lu\n", static_cast<int>(server.method()),
                static_cast<unsigned long>(millis()));
  setOledStatus(OledMode::Think, F("WIFI"), F("SETUP"), 40);
  server.send(200, F("text/html; charset=utf-8"),
              F("<!doctype html><meta charset='utf-8'><p>已保存的网络将保留。设备正在进入配网模式，请连接 Ekko Studio-WIFI 热点。</p>"));
  delay(300);
  startSetupAp(true, "user_reconfigure");
}

void forgetWifiHistory() {
  Serial.printf("WiFi forget-all requested method=%d uptime=%lu\n", static_cast<int>(server.method()),
                static_cast<unsigned long>(millis()));
  forgetAllWifiCredentials();
  setOledStatus(OledMode::Think, F("WIFI"), F("FORGET"), 40);
  server.send(200, F("text/html; charset=utf-8"),
              F("<!doctype html><meta charset='utf-8'><p>已清除所有 Wi-Fi 历史，设备正在进入配网模式...</p>"));
  delay(300);
  startSetupAp(true, "user_forget_all");
}

bool connectSavedWifi() {
  scanWifiList();

  String ssids[kMaxWifiCandidates];
  String passes[kMaxWifiCandidates];
  int count = 0;
  loadWifiCredentials(ssids, passes, count);
  if (count == 0) {
    Serial.println(F("No saved WiFi credentials, entering setup AP"));
    esp_rom_printf("No saved WiFi credentials, entering setup AP\n");
    return false;
  }

  bool matched = false;
  for (int i = 0; i < count; ++i) {
    if (!scannedNetworkHasSsid(ssids[i])) continue;
    matched = true;
    Serial.printf("Saved WiFi found in scan ssid=%s\n", ssids[i].c_str());
    esp_rom_printf("Saved WiFi found in scan ssid=%s\n", ssids[i].c_str());
    if (connectWifiCredentials(ssids[i], passes[i], WIFI_STA)) {
      SharedWifiConfig::Credentials factory;
      const bool fromFactory = readFactoryWifi(factory) && ssids[i] == factory.ssid && passes[i] == factory.password;
      connectedWifiSource = fromFactory ? factoryWifiSource : "ekko_history";
      rememberWifiCredential(ssids[i], passes[i]);
      return true;
    }
  }

  if (!matched) {
    Serial.println(F("No saved WiFi SSID found in scan, entering setup AP"));
    esp_rom_printf("No saved WiFi SSID found in scan, entering setup AP\n");
  }
  return false;
}

void startSetupAp(bool refreshScan, const char *reason) {
  String ssids[kMaxWifiCandidates], passes[kMaxWifiCandidates];
  int savedCount = 0;
  loadWifiCredentials(ssids, passes, savedCount);
  savedNetworkRetry.setup(reason, millis(), savedCount > 0);
  Serial.printf("WiFi setup AP requested reason=%s status=%d uptime=%lu\n", reason,
                static_cast<int>(WiFi.status()), static_cast<unsigned long>(millis()));
  setOledStatus(OledMode::Think, F("SETUP"), F("AP START"), 35);
  if (lanUdpReady) {
    lanUdp.stop();
    lanUdpReady = false;
  }
  lanDeviceCount = 0;
  lastLanDiscoveryAtMs = 0;
  if (refreshScan) scanWifiList();
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  delay(120);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool configOk = WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  bool apOk = WiFi.softAP(kApName, nullptr, 1, false, 4);
  bool ok = configOk && apOk;
  setupApMode = ok;
  wifiReady = false;
  setOledStatus(ok ? OledMode::Ready : OledMode::Error, ok ? F("SETUP") : F("AP ERR"),
                ok ? F("OPEN WIFI AP") : F("AP FAILED"), ok ? 100 : 0);
  Serial.printf("Setup AP config=%d ap=%d ssid=%s ip=%s open=1\n", configOk ? 1 : 0,
                apOk ? 1 : 0, kApName, WiFi.softAPIP().toString().c_str());
  esp_rom_printf("Setup AP config=%d ap=%d ssid=%s ip=%s open=1\n", configOk ? 1 : 0,
                 apOk ? 1 : 0, kApName, WiFi.softAPIP().toString().c_str());
}

void handleRoot() {
  if (wifiReady && WiFi.status() == WL_CONNECTED) {
    scanAndSendStatusPage();
    return;
  }
  sendWifiPage();
}

void handleScreenBmp() {
  if(officialVoiceNetworkBusy() || officialSelectionJob.busy() || officialHistoryJob.busy()) {
    server.send(503,F("text/plain; charset=utf-8"),F("语音或会话同步中，请稍后查看屏幕"));return;
  }
  if (!M5Cardputer.Display.isReadable()) {
    server.send(503, F("application/json"), F("{\"error\":\"lcd_readback_unavailable\"}"));
    return;
  }
  constexpr uint32_t width = 240, height = 135, rowBytes = width * 3;
  uint8_t header[54] = {'B', 'M'};
  putLe32(header + 2, sizeof(header) + rowBytes * height);
  putLe32(header + 10, sizeof(header)); putLe32(header + 14, 40);
  putLe32(header + 18, width); putLe32(header + 22, static_cast<uint32_t>(-static_cast<int32_t>(height)));
  putLe16(header + 26, 1); putLe16(header + 28, 24);
  putLe32(header + 34, rowBytes * height);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.setContentLength(sizeof(header) + rowBytes * height);
  server.send(200, F("image/bmp"), "");
  WiFiClient client = server.client();
  if (client.write(header, sizeof(header)) != sizeof(header)) return;
  uint8_t row[rowBytes];
  for (uint32_t y = 0; y < height; ++y) {
    M5Cardputer.Display.readRectRGB(0, y, width, 1, row);
    for (uint32_t x = 0; x < rowBytes; x += 3) {
      uint8_t red = row[x]; row[x] = row[x + 2]; row[x + 2] = red;
    }
    if (client.write(row, sizeof(row)) != sizeof(row)) return;
    yield();
  }
}

void handleHealth() {
  updateBatteryReading();
  String json = F("{\"status\":\"ok\",\"wifi_connected\":");
  json.reserve(3072);
  json += (wifiReady && WiFi.status() == WL_CONNECTED) ? F("true") : F("false");
  json += F(",\"ip\":\"");
  json += currentIp();
  json += F("\",\"setup_ap\":\"");
  json += setupApMode ? kApName : "";
  json += F("\",\"hardware\":\"cardputer-adv\",\"firmware\":\"");
  json += kMcuFirmwareVersion;
  json += F("\",\"identity\":{\"state\":\""); json += mcuDeviceCodeStateLabel();
  json += F("\",\"storage_error\":"); json += static_cast<int>(mcuPreferencesLastError);
  json += F("},\"connection\":{\"transport\":\"lan-standard\",\"namespace_ready\":");
  json += mcuSocketNamespaceReady?F("true"):F("false");
  json += F(",\"management_online\":");json+=mcuStatusNamespaceReady?F("true"):F("false");
  String healthScheme,healthHost,healthPath;uint16_t healthPort=0;
  if(!parseAudioUrl(activeMcuSocketUrl(),&healthScheme,&healthHost,&healthPort,&healthPath)) healthPort=0;
  bool bindingPersisted=false;Preferences binding;
  if(binding.begin("stockLan",true)) {
    bindingPersisted=activeDeviceKey.length()>0 && activeDeviceUrl.length()>0 &&
        selectedProfile.length()>0 && mcuAuthToken.length()>0 &&
        binding.getString("active_key","")==activeDeviceKey &&
        binding.getString("active_url","")==activeDeviceUrl &&
        binding.getString("cur_profile","")==selectedProfile &&
        binding.getString("auth_token","")==mcuAuthToken &&
        !binding.getBool("active_remote",false) && !binding.getBool("profile_pending",false);
    binding.end();
  }
  json += F(",\"http_port\":");json+=healthPort;
  json += F(",\"saved_binding_matches\":");json+=bindingPersisted?F("true"):F("false");json+=F("}");
  const FactoryFirmwareState factory = cardputerFactoryFirmwareState();
  json += F(",\"dual_boot\":{\"factory_header_found\":");
  json += factory.header_found ? F("true") : F("false");
  json += F(",\"factory_address\":"); json += factory.address;
  json += F(",\"last_switch_error\":"); json += static_cast<int>(factoryBootError);
  json += F(",\"next_boot_factory\":"); json += cardputerFactoryIsNextBoot() ? F("true") : F("false");
  json += F(",\"blocked_nvs_full_erases\":"); json += cardputerBlockedNvsEraseCount();
  json += F("},\"wifi_config\":{\"source\":\""); json += connectedWifiSource;
  json += F("\",\"factory_record\":\""); json += factoryWifiSource;
  json += F("\",\"shared_save_failed\":"); json += sharedWifiSaveFailed ? F("true") : F("false");
  json += F("}");
  json += F(",\"agentId\":\""); json += mcuAgentRuntimeValue();
  json += F("\",\"agentLabel\":\""); json += mcuAgentRuntimeLabel();
  json += F("\",\"agentCount\":"); json += kNativeAgentCount;
  json += F(",\"browseAgentId\":\""); json += kNativeAgents[agentSelection.browse].id;
  json += F("\",\"selectionPending\":"); json += agentSelection.pending ? F("true") : F("false");
  json += F(",\"nativeAgentConfirmed\":"); json += agentSelection.confirmed ? F("true") : F("false");
  json += F(",\"selectionVersion\":"); json += agentSelection.version;
  json += F(",\"physicalNavigationCount\":"); json += cardputerNavigationCount();
  const NativeNavigationStats navigation = cardputerNavigationStats();
  json += F(",\"navigation\":{\"left\":"); json += navigation.left;
  json += F(",\"right\":"); json += navigation.right;
  json += F(",\"up\":"); json += navigation.up;
  json += F(",\"down\":"); json += navigation.down;
  json += F(",\"ok\":"); json += navigation.ok;
  json += F(",\"back\":"); json += navigation.back;
  json += F(",\"queued\":"); json += navigation.queued;
  json += F(",\"dropped\":"); json += navigation.dropped;
  json += F("}");
  json += F(",\"ui\":{\"page\":\"");
  json += conversationView.conversation() ? F("conversation") : F("agents");
  json += F("\",\"open_pending\":"); json += conversationView.waiting() ? F("true") : F("false");
  json += F("}");
  json += F(",\"chat\":{\"protocol\":"); json += chatProtocolReady ? 1 : 0;
  json += F(",\"ready\":"); json += chatModel.ready() ? F("true") : F("false");
  json += F(",\"messages\":"); json += chatModel.messageCount();
  json += F(",\"draft_bytes\":"); json += chatModel.draft() ? chatModel.draft()->size() : 0;
  json += F(",\"sending\":"); json += chatPendingSend.length() ? F("true") : F("false");
  json += F(",\"send_unknown\":"); json += chatSendUnknown ? F("true") : F("false");
  json += F(",\"accepted\":"); json += chatAcceptedCount;
  json += F(",\"received_chunks\":"); json += chatReceivedChunks;
  json += F(",\"rejected_frames\":"); json += chatRejectedFrames;
  json += F(",\"key_count\":"); json += cardputerChatKeyCount();
  json += F(",\"key_dropped\":"); json += cardputerChatKeyDropped();
  json += F(",\"cache_bytes\":"); json += sizeof(chatModel);
  const auto* ime = chatModel.inputMethod();
  json += F(",\"chinese_input\":"); json += ime && ime->enabled() ? F("true") : F("false");
  json += F(",\"preedit_bytes\":"); json += ime ? strlen(ime->preedit()) : 0;
  json += F(",\"candidate_count\":"); json += ime ? ime->candidates().count : 0;
  json += F(",\"candidate_page\":"); json += ime ? ime->page() : 0;
  json += F(",\"ime_events\":"); json += chatInputMethodEvents;
  json += F(",\"ime_commits\":"); json += chatInputMethodCommits;
  json += F(",\"language_toggles\":"); json += chatLanguageToggles;
  json += F(",\"history_mode\":"); json += chatHistoryMode ? F("true") : F("false");
  json += F(",\"history_pending\":"); json += chatHistoryRequest.length() ? F("true") : F("false");
  json += F(",\"bottom_offset\":"); json += chatBottomOffset;
  const CardputerChatViewport chatViewport = cardputerChatViewport();
  json += F(",\"max_bottom_offset\":"); json += chatViewport.maxOffset;
  json += F(",\"first_visible_message\":"); json += chatViewport.first;
  json += F(",\"last_visible_message\":"); json += chatViewport.last;
  json += F(",\"scroll_up\":"); json += chatScrollUpCount;
  json += F(",\"scroll_down\":"); json += chatScrollDownCount;
  json += F(",\"history_toggles\":"); json += chatHistoryToggleCount;
  json += F(",\"older_page_keys\":"); json += chatHistoryOlderCount;
  json += F(",\"newer_page_keys\":"); json += chatHistoryNewerCount;
  json += F("}");
  json += F(",\"selectionError\":\""); json += agentSelectionError;
  json += F("\",\"uptime_ms\":"); json += millis();
  json += F(",\"free_heap\":"); json += ESP.getFreeHeap();
  json += F(",\"min_free_heap\":"); json += ESP.getMinFreeHeap();
  json += F(",\"max_alloc_heap\":"); json += ESP.getMaxAllocHeap();
  // ESP-IDF reports this watermark in bytes. It includes the display path
  // reached during streaming/playback, not merely this health handler.
  json += F(",\"loop_stack_bytes\":"); json += ARDUINO_LOOP_STACK_SIZE;
  json += F(",\"loop_stack_min_free_bytes\":"); json += uxTaskGetStackHighWaterMark(nullptr);
  const NativeBootVideoStats boot = cardputerBootVideoStats();
  json += F(",\"boot_video\":{\"attempted\":"); json += boot.attempted ? F("true") : F("false");
  json += F(",\"completed\":"); json += boot.completed ? F("true") : F("false");
  json += F(",\"skipped\":"); json += boot.skipped ? F("true") : F("false");
  json += F(",\"asset_version\":\"ekko-boot-v1\",\"asset_bytes\":"); json += boot.asset_bytes;
  json += F(",\"frames_total\":"); json += boot.frames_total;
  json += F(",\"frames_presented\":"); json += boot.frames_presented;
  json += F(",\"target_ms\":"); json += boot.target_ms;
  json += F(",\"elapsed_ms\":"); json += boot.elapsed_ms;
  json += F(",\"max_frame_ms\":"); json += boot.max_frame_ms;
  json += F(",\"max_decode_us\":"); json += boot.max_decode_us;
  json += F(",\"overruns\":"); json += boot.overruns;
  json += F(",\"decode_failures\":"); json += boot.decode_failures;
  json += F(",\"last_error\":\""); json += escapeJson(boot.last_error); json += F("\"}");
  const CardputerRenderStats render = cardputerRenderStats();
  json += F(",\"render\":{\"ready\":"); json += render.ready ? F("true") : F("false");
  json += F(",\"color_depth\":"); json += render.color_depth;
  json += F(",\"frame_count\":"); json += render.frame_count;
  json += F(",\"skipped_same_state\":"); json += render.skipped_same_state;
  json += F(",\"skipped_throttled\":"); json += render.skipped_throttled;
  json += F(",\"draw_calls\":"); json += render.draw_calls;
  json += F(",\"render_us_last\":"); json += render.render_us_last;
  json += F(",\"render_us_max\":"); json += render.render_us_max;
  json += F(",\"buffer_bytes\":"); json += render.buffer_bytes;
  json += F(",\"allocation_attempts\":"); json += render.allocation_attempts;
  json += F(",\"allocation_failures\":"); json += render.allocation_failures;
  json += F(",\"fallback_notices\":"); json += render.fallback_notices;
  json += F(",\"free_heap\":"); json += render.free_heap;
  json += F(",\"min_free_heap\":"); json += render.min_free_heap;
  json += F(",\"dma_free_heap\":"); json += render.dma_free_heap;
  json += F(",\"largest_dma_block\":"); json += render.largest_dma_block;
  json += F(",\"last_error\":\""); json += escapeJson(render.last_error);
  json += F("\"}");
  json += F(",\"mcu\":{\"interaction_active\":");
  json += mcuInteractionActive ? F("true") : F("false");
  json += F(",\"status\":\"");
  json += escapeJson(mcuInteractionStatus);
  json += F("\",\"audio_playing\":");
  json += mcuAudioPlaying ? F("true") : F("false");
  json += F(",\"queue_length\":");
  json += mcuAudioCount;
  json += F(",\"power_save_active\":");
  json += idlePowerSaveActive ? F("true") : F("false");
  json += F(",\"power_save_minutes\":");
  json += idlePowerSaveMinutes;
  json += F(",\"listening_mode\":");
  json += listeningModeEnabled ? F("true") : F("false");
  json += F("},\"audio\":{\"i2s_ready\":");
  json += i2sReady ? F("true") : F("false");
  json += F(",\"es8311_ready\":");
  json += es8311Ready ? F("true") : F("false");
  json += F(",\"volume\":");
  json += outputVolumePercent;
  const CardputerAudioStats audio = cardputerAudioStats();
  json += F(",\"stream\":{\"queued\":"); json += audio.queued;
  json += F(",\"released\":"); json += audio.released;
  json += F(",\"in_flight\":"); json += audio.in_flight;
  json += F(",\"claim_failures\":"); json += audio.claim_failures;
  json += F(",\"cancelled_unqueued\":"); json += audio.cancelled_unqueued;
  json += F(",\"rejected_releases\":"); json += audio.rejected_releases;
  json += F(",\"discarded_after_join\":"); json += audio.discarded_after_join;
  json += F(",\"source_empty_before_submit\":"); json += audio.source_empty_before_submit;
  json += F(",\"max_submit_gap_us\":"); json += audio.max_submit_gap_us;
  json += F(",\"write_timeouts\":"); json += audio.write_timeouts;
  json += F(",\"buffer_bytes\":"); json += audio.buffer_bytes;
  json += F("}");
  const CardputerKeyClickStats keyClick = cardputerKeyClickStats();
  json += F(",\"key_click\":{\"enabled\":"); json += keyClick.enabled ? F("true") : F("false");
  json += F(",\"playing\":"); json += keyClick.playing ? F("true") : F("false");
  json += F(",\"pressed\":"); json += keyClick.pressed;
  json += F(",\"suppressed\":"); json += keyClick.suppressed;
  json += F(",\"played\":"); json += keyClick.played;
  json += F(",\"completed\":"); json += keyClick.completed;
  json += F(",\"preempted\":"); json += keyClick.preempted;
  json += F(",\"failures\":"); json += keyClick.failures;
  json += F(",\"buffer_bytes\":"); json += keyClick.buffer_bytes;
  json += F(",\"speaker_owned\":"); json += keyClick.speaker_owned ? F("true") : F("false");
  json += F(",\"warming\":"); json += keyClick.warming ? F("true") : F("false");
  json += F(",\"buffer_free\":"); json += keyClick.buffer_free ? F("true") : F("false");
  json += F(",\"speaker_starts\":"); json += keyClick.speaker_starts;
  json += F(",\"warmups\":"); json += keyClick.warmups;
  json += F(",\"buffer_releases\":"); json += keyClick.buffer_releases;
  json += F("}");
  const auto& voice=officialVoiceCompletion;
  json += F(",\"official_voice\":{\"error\":");json+=unsigned(voice.result.error);
  json += F(",\"http\":");json+=voice.result.httpStatus;
  json += F(",\"metrics_available\":");json+=voice.resourcesAvailable?F("true"):F("false");
  json += F(",\"stack_min_free_bytes\":");json+=voice.workerStackFreeBytes;
  json += F(",\"heap_min_observed_bytes\":");json+=voice.internalHeapMinObservedBytes;
  json += F(",\"largest_min_observed_bytes\":");json+=voice.internalLargestBlockMinObservedBytes;
  json += F(",\"boot_heap_min_bytes\":");json+=voice.systemHeapLowWaterBytes;json+=F("}");
  json += F(",\"recording_store\":{\"kind\":\"ram_ima4\",\"sd_required\":false,\"limit_ms\":5000,\"prepare_error\":");json+=unsigned(officialRecordingPrepareResult);
  json += F(",\"begin_error\":");json+=unsigned(officialRecordingBeginResult);
  json += F(",\"result\":");json+=unsigned(officialRecordingStore.result());
  json += F(",\"state\":");json+=unsigned(officialRecordingStore.state());
  json += F(",\"capacity_bytes\":");json+=officialRecordingStore.capacityBytes();
  json += F(",\"recorded_bytes\":");json+=officialRecordingStore.recordedBytes();
  json += F(",\"encoded_capacity_bytes\":");json+=officialRecordingStore.encodedCapacityBytes();
  json += F(",\"encoded_bytes\":");json+=officialRecordingStore.encodedBytes();
  json += F(",\"last_pcm_bytes\":");json+=officialRecordingLastPcmBytes;
  json += F(",\"last_encoded_bytes\":");json+=officialRecordingLastEncodedBytes;
  json += F(",\"heap_start_bytes\":");json+=officialRecordingHeapStart;
  json += F(",\"heap_min_observed_bytes\":");json+=officialRecordingHeapMinimum;
  json += F(",\"largest_min_observed_bytes\":");json+=officialRecordingLargestMinimum;
  json += F(",\"worker_free_before_bytes\":");json+=officialRecordingWorkerFree;
  json += F(",\"worker_largest_before_bytes\":");json+=officialRecordingWorkerLargest;
  json += F(",\"admission_error\":");json+=officialRecordingAdmissionError;json+='}';
  const auto capture=cardputerCaptureStats();
  json += F(",\"capture\":{\"active\":");json += capture.active?F("true"):F("false");
  json += F(",\"error\":");json += static_cast<uint32_t>(capture.error);
  json += F(",\"sample_rate\":");json += capture.sampleRate;
  json += F(",\"buffer_bytes\":");json += capture.bufferBytes;
  json += F(",\"storage_retained\":");json += capture.storageRetained?F("true"):F("false");
  json += F(",\"produced_blocks\":");json += capture.queue.producedBlocks;
  json += F(",\"consumed_blocks\":");json += capture.queue.consumedBlocks;
  json += F(",\"max_ready_blocks\":");json += capture.queue.maxReadyBlocks;
  json += F(",\"overruns\":");json += capture.queue.overruns;
  json += F(",\"invalid_callbacks\":");json += capture.queue.invalidCallbacks;
  json += F(",\"discarded_after_join\":");json += capture.queue.discardedAfterJoin;
  json += F("}");
  json += F(",\"battery_known\":true,\"battery_level\":");
  json += batteryLevelPercent;
  json += F(",\"battery_voltage_mv\":");
  json += batteryVoltageMv;
  json += F(",\"battery_charging\":false");
  json += F(",\"busy\":");
  json += audioBusy ? F("true") : F("false");
  json += F(",\"last_detail\":\"");
  json += escapeJson(lastAudioDetail);
  json += F("\"}}");
  server.send(200, F("application/json"), json);
}

void markMcuInteraction(const String &id,const String &status,const String &text) {
  mcuInteractionId=id;mcuInteractionStatus=status;mcuInteractionText=text;
  mcuInteractionActive=true;mcuInteractionUpdatedAtMs=millis();oledDirty=true;
}

void tickMcuInteraction() {
  if(mcuInteractionActive && !officialVoiceNetworkBusy() && !chatRemoteRunActive &&
      (mcuInteractionStatus=="completed" || mcuInteractionStatus=="failed" || mcuInteractionStatus=="aborted") &&
      millis()-mcuInteractionUpdatedAtMs>kMcuInteractionIdleDelayMs) {
    mcuInteractionActive=false;mcuInteractionStatus="idle";oledDirty=true;
  }
}

void handleNoContent() {
  server.send(204, F("text/plain"), F(""));
}

void handleAppleCaptiveCheck() {
  server.send(200, F("text/html"), F("Success"));
}

void handleWindowsConnectCheck() {
  server.send(200, F("text/plain"), F("Microsoft Connect Test"));
}

void handleWindowsNcsiCheck() {
  server.send(200, F("text/plain"), F("Microsoft NCSI"));
}

void setupRoutes() {
  server.on(F("/font/fusion-pixel.woff2"), HTTP_GET, []() {
    server.sendHeader(F("Cache-Control"),F("public, max-age=31536000, immutable"));
    server.send_P(200, PSTR("font/woff2"), reinterpret_cast<const char*>(fusion_web_font::kData), fusion_web_font::kSize);
  });
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/ping"), HTTP_GET, []() {
    server.send(200, F("text/plain"), F("ok"));
  });
  server.on(F("/generate_204"), HTTP_GET, handleNoContent);
  server.on(F("/gen_204"), HTTP_GET, handleNoContent);
  server.on(F("/hotspot-detect.html"), HTTP_GET, handleAppleCaptiveCheck);
  server.on(F("/library/test/success.html"), HTTP_GET, handleAppleCaptiveCheck);
  server.on(F("/connecttest.txt"), HTTP_GET, handleWindowsConnectCheck);
  server.on(F("/ncsi.txt"), HTTP_GET, handleWindowsNcsiCheck);
  server.on(F("/favicon.ico"), HTTP_GET, handleNoContent);
  server.on(F("/device"), HTTP_GET, scanAndSendStatusPage);
  server.on(F("/device/scan"), HTTP_GET, scanAndSendStatusPage);
  server.on(F("/device/audio"), HTTP_POST, handleDeviceAudio);
  server.on(F("/device/agent"), HTTP_POST, handleDeviceAgent);
  server.on(F("/device/power-save"), HTTP_POST, handleDevicePowerSave);
  server.on(F("/device/manual"), HTTP_POST, addManualDevice);
  server.on(F("/device/login"), HTTP_GET, sendMcuLoginPage);
  server.on(F("/device/login"), HTTP_POST, handleMcuLogin);
  server.on(F("/device/profile"), HTTP_GET, switchProfilePage);
  server.on(F("/device/profile"), HTTP_POST, saveProfile);
  server.on(F("/device/logout"), HTTP_GET, logoutDevice);
  server.on(F("/ota"), HTTP_GET, []() { sendOtaPage(); });
  server.on(F("/ota/check"), HTTP_POST, handleOtaCheck);
  server.on(F("/wifi"), HTTP_GET, sendWifiPage);
  server.on(F("/wifi"), HTTP_POST, saveWifi);
  server.on(F("/wifi/forget"), HTTP_POST, forgetWifiHistory);
  // GET (including browser prefetch/reload) must never clear credentials or
  // disconnect the device. Only the explicit POST enters provisioning mode.
  server.on(F("/clear"), HTTP_GET, sendWifiPage);
  server.on(F("/clear"), HTTP_POST, clearWifi);
  server.on(F("/health"), HTTP_GET, handleHealth);
  server.on(F("/screen.bmp"), HTTP_GET, handleScreenBmp);
  server.onNotFound(handleRoot);
  server.begin();
}
}  // namespace

void setup() {
  // Share credentials at application level, not SDK-private Wi-Fi driver blobs.
  WiFi.persistent(false);
  // Ekko is a session launched from the original menu. Arm the next factory
  // boot before the video, so a power cycle during the animation returns home.
  factoryBootError = cardputerSelectFactoryBoot();
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(2048);
  cardputerBegin();
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Ekko Studio WiFi setup firmware boot"));
  Serial.printf("Firmware=%s stock=0.7.36 chat_font=12 transport=lan-standard\n", kMcuFirmwareVersion);
  Serial.printf("Reset reason=%d free_heap=%lu min_free_heap=%lu\n",
                static_cast<int>(esp_reset_reason()),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  esp_rom_printf("Ekko Studio WiFi setup firmware boot\n");
  cardputerPlayBootVideo();
  const NativeBootVideoStats boot = cardputerBootVideoStats();
  Serial.printf("Boot video frames=%u/%u elapsed=%lu skipped=%d error=%s\n",
                boot.frames_presented, boot.frames_total,
                static_cast<unsigned long>(boot.elapsed_ms), boot.skipped, boot.last_error);
  migrateLegacyWifi();
  updateBatteryReading(true);
  initOledDisplay();
  importLegacyLanPreferences();
  loadAudioPreferences();
  initAudioHardware();
  prefs.begin("stockLan", true);
  pendingProfileDeviceKey = prefs.getString("active_key", "");
  activeDeviceKey = prefs.getString("active_key", "");
  activeDeviceUrl = prefs.getString("active_url", "");
  mcuSocketRelayUrl = "";
  pendingProfileRemoteSource = false;
  mcuAuthToken = prefs.getString("auth_token", "");
  mcuSocketReconnectBlocked = false;
  selectedProfile = prefs.getString("cur_profile", "");
  if (selectedProfile.length() == 0) selectedProfile = prefs.getString("current_profile", "");
  if(prefs.getBool("profile_pending",false)) selectedProfile="";
  prefs.end();
  setOledStatus(OledMode::Boot, F("BOOT"), F("WIFI ONLY"), 15);
  if (kForceSetupAp) {
    startSetupAp(true, "forced_setup");
  } else if (!connectSavedWifi()) {
    startSetupAp(false, "startup_wifi_unavailable");
  }
  setupRoutes();
  if (wifiReady && !setupApMode) {
    autoLoginSavedDevice();
  }
}

void loop() {
  tickCardputerInput();
  cardputerTickKeyClick(i2sReady && !audioBusy && !officialVoiceNetworkBusy() && !mcuAudioPlaying && mcuAudioCount == 0 &&
      !listeningModeEnabled && !listeningMonitorReady && !restartPending &&
      !idlePowerSaveActive &&
      outputVolumePercent > 0 && !cardputerTalkPressed());
  if (restartPending && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    ESP.restart();
  }

  // Header parsing is synchronous; never let a management request stall audio.
  if(!officialVoiceNetworkBusy()) server.handleClient();
  if (wsReady) mcuSocketLoop();
  tickOfficialStudio();
  tickOfficialVoice();
  tickMcuReauth();
  tickMcuSocketReconnect();
  if(wsReady && mcuSocketNamespaceReady && !mcuStatusNamespaceReady && statusReconnectAtMs &&
      static_cast<int32_t>(millis()-statusReconnectAtMs)>=0 && !officialVoiceNetworkBusy()) {
    statusReconnectAtMs=millis()+statusReconnectDelayMs;
    sendRawWsText(String("40/global-agent,")+mcuSocketAuthJson());
  }

  tickMcuInteraction();
  tickOledStatusReturn();
  handleBootButton();
  tickIdlePowerSave();
  refreshOled();

  if (wifiReady && WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (wifiDisconnectedSinceMs == 0) {
      wifiDisconnectedSinceMs = now;
      setOledStatus(OledMode::Think, F("WIFI"), F("RECONNECT"), 60);
      Serial.printf("WiFi disconnected status=%d, waiting %lu ms before setup AP\n",
                    static_cast<int>(WiFi.status()),
                    static_cast<unsigned long>(kWifiDisconnectGraceMs));
      WiFi.reconnect();
    } else if (now - wifiDisconnectedSinceMs >= kWifiDisconnectGraceMs) {
      setOledStatus(OledMode::Error, F("WIFI"), F("LOST"), 0);
      delay(500);
      wifiDisconnectedSinceMs = 0;
      if (!connectSavedWifi()) {
        startSetupAp(false, "wifi_reconnect_failed");
      }
    }
  } else if (wifiReady) {
    wifiDisconnectedSinceMs = 0;
  }
  if (!wifiReady && savedNetworkRetry.begin(millis(), audioBusy || mcuAudioPlaying || restartPending)) {
    Serial.println(F("WiFi retrying saved networks after automatic setup; login preserved"));
    if (connectSavedWifi()) {
      autoLoginSavedDevice();
    } else {
      savedNetworkRetry.failed(millis());
      Serial.printf("WiFi saved retry failed; next attempt in %lu ms\n",
                    static_cast<unsigned long>(savedNetworkRetry.delayMs()));
      startSetupAp(false, "wifi_reconnect_failed");
    }
  }
}
