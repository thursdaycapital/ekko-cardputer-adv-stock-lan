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
#include "ExperienceController.h"
#include "DeviceGesture.h"
#include "StudioConnectJob.h"
#include "StudioAuthJob.h"
#include "SavedWifiJoin.h"
#include "SendRecovery.h"
#include "BoundedManagementServer.h"
#include <lwip/sockets.h>
#include <cerrno>
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
#define HERMES_MCU_FIRMWARE_VERSION "cardputer-adv-v10.0.0-alpha.6"
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
constexpr uint32_t kVoiceStreamReleaseDebounceMs = 240;
constexpr uint32_t kWifiDisconnectGraceMs = 8000;
constexpr uint32_t kBatteryReadIntervalMs = 5000;
constexpr bool kAutoOtaEnabled = false;
constexpr uint32_t kVoiceRecordMs = 4000;
constexpr uint32_t kVoiceRecordMinMs = 300;
constexpr uint32_t kVoiceRecordLimitMs = 5000;
constexpr uint32_t kVoiceRecordDrainTimeoutMs = 1000;
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
using DeviceWebServer=BoundedManagementServer;
DeviceWebServer server(80);
WiFiUDP lanUdp;
WiFiClient emptySocketClient;
std::unique_ptr<WiFiClient> connectedSocket;
WiFiClient* mcuWsClient = &emptySocketClient;
studio_connect::Job studioConnectJob;
studio_auth::Job studioAuthJob;
uint32_t studioAuthEpoch=0,studioAuthStackFree=0,studioAuthElapsedMs=0;
uint8_t studioAuthLastError=255,studioAuthLastStage=0;
uint32_t loopLastAtMs=0,loopLongestGapMs=0;
String studioAuthTarget;
uint32_t studioConnectEpoch=0;
String studioConnectTarget;
studio_connect::Output studioConnectDiagnostics;
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
bool idlePowerSaveActive = false;
bool listeningModeEnabled = false;
NativeAgentSelection agentSelection;
std::array<official_selection::Preference,kNativeAgentCount> agentPreferences{};
official_selection::Preference agentPreference(size_t index) {
  if(index>=kNativeAgentCount)return official_selection::Preference::Studio;
  const auto value=agentPreferences[index];
  if(value==official_selection::Preference::Studio || value==official_selection::Preference::Native)return value;
  const auto* route=official_studio::agentRoute(kNativeAgents[index].id);
  return route && !route->scoped?official_selection::Preference::Native:official_selection::Preference::Studio;
}
const char* agentModeLabel(size_t index) {
  return agentPreference(index)==official_selection::Preference::Native?"原生CLI":"Studio模型";
}
bool setAgentPreference(size_t index,official_selection::Preference value);
experience::Controller experienceController;
experience::Generation activationGeneration=0,selectionWorkerGeneration=0,selectionRetryGeneration=0;
String experienceBinding;
bool experienceBindingObserved=false;
bool chatPageOpen() { return experienceController.snapshot().page==experience::Page::Chat; }
void tickExperience();
void returnToFactoryMenu();
void applyExperienceEffects();
void tickStudioConnect();
bool queueAgentActivation(size_t index);
const char* experienceNoticeText();
bool recordingActive();
uint32_t recordingElapsedMs();
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
uint8_t officialRecordingStartResult=255,officialRecordingStartStage=0; // 255: no start attempted.
official_voice_job::StartDiagnostics officialRecordingStartDiagnostics;
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
uint32_t officialVoiceCompletions=0;
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
send_recovery::Recovery sendRecovery;
send_recovery::Ticket historyReviewTicket=0,resumeReviewTicket=0;
void synchronizeSendRecovery() {
  sendRecovery.scopeChanged(officialEpoch);
  if(chatSendUnknown && sendRecovery.clear())sendRecovery.markUnknown(officialEpoch);
  else if(!chatSendUnknown && !sendRecovery.clear())sendRecovery.observeSettled(officialEpoch);
  chatModel.setSendUnknown(chatSendUnknown);
}
void leaveConversationScope() {
  chatModel.setSendUnknown(chatSendUnknown);chatModel.hideContext();chatSendUnknown=false;
  // A new account/Profile view starts with its own cached state. This does
  // not settle the previous context, whose unknown flag remains preserved.
  sendRecovery=send_recovery::Recovery();historyReviewTicket=resumeReviewTicket=0;
}
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
native_wifi::SavedWifiJoin savedWifiJoin;
String joiningSsids[kMaxWifiCandidates],joiningPasswords[kMaxWifiCandidates];
int joiningCount=0;
void beginSavedWifiConnection();
void tickSavedWifiConnection();
void clearJoiningCredentials() {
  for(int i=0;i<kMaxWifiCandidates;++i) {
    for(size_t j=0;j<joiningPasswords[i].length();++j)joiningPasswords[i][j]=0;
    joiningPasswords[i]="";joiningSsids[i]="";
  }
  joiningCount=0;
}
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

// Legacy MCU audio queue is not part of the official /chat-run pipeline.
constexpr int mcuAudioCount = 0;
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
  if (mcuReauthPolicy.bindingChanged()) return "需重新绑定";
  NativeChatStatusState state;
  state.wifiConnected = wifiReady && WiFi.status() == WL_CONNECTED;
  state.loginRequired = mcuSocketReconnectBlocked || mcuReauthPolicy.rejected() ||
      activeDeviceUrl.length() == 0 || selectedProfile.length() == 0 ||
      (!mcuAuthToken.length() && !mcuReauthPolicy.active());
  state.reauthenticating = mcuReauthAttemptInProgress || mcuReauthAudioCleanupPending ||
      mcuReauthPolicy.blocksSocket();
  state.namespaceReady = mcuSocketNamespaceReady;
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
  if (chatPageOpen() && (!previousLabel || std::strcmp(previousLabel, label) != 0)) {
    Serial.printf("Chat display status=%s wifi=%d native=%d audio_pause=%d\n", label,
                  state.wifiConnected, state.namespaceReady, state.audioTransportPaused);
    previousLabel = label;
  }
  return label;
}

bool oledFlushPages(uint8_t, uint8_t) {
  const auto& state=experienceController.snapshot();
  String address=setupApMode?String("192.168.4.1/device"):
      (wifiReady?WiFi.localIP().toString()+"/device":String());
  CardputerPresentation frame(agentSelection,chatModel);
  frame.conversation=chatPageOpen();frame.waiting=state.openIntent;frame.exiting=state.exitPending;
  frame.online=mcuSocketNamespaceReady;frame.sending=chatPendingSend.length()!=0;
  frame.sendUncertain=chatSendUnknown && !chatRemoteRunActive && !chatPendingSend.length();frame.sendReviewable=sendRecovery.phase()==send_recovery::Phase::Reviewable;
  frame.historyMode=chatHistoryMode;frame.status=frame.conversation?nativeChatStatusLabel():
      state.connection==experience::Connection::Ready?"电脑已连接":
      state.connection==experience::Connection::NeedsBinding?"需要登录":
      state.connection==experience::Connection::Authenticating?"验证登录":
      state.connection==experience::Connection::Offline?"网络断开":"连接后台";
  const char* appNotice=experienceNoticeText();
  frame.notice=chatNotice.length()?chatNotice.c_str():appNotice;
  frame.message=state.exitPending?"正在停止后台任务，请稍等":
      agentSelectionError.length() && chatNotice.length()?chatNotice.c_str():appNotice;
  frame.modeLabel=agentModeLabel(state.desiredAgent);
  frame.address=address.c_str();frame.bottomOffset=chatBottomOffset;
  frame.recording=recordingActive();
  frame.recordingElapsedMs=recordingElapsedMs();
  frame.recordingLimitMs=kVoiceRecordLimitMs;
  cardputerDraw(frame);
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
  experienceController.dispatch(experience::Event::browse(int(agentSelection.browse)-int(experienceController.snapshot().desiredAgent)));
  for(size_t i=0;i<kNativeAgentCount;++i) {
    char key[16];snprintf(key,sizeof(key),"mode_%u",unsigned(i));
    const auto saved=static_cast<official_selection::Preference>(prefs.getUChar(key,0));
    const auto* route=official_studio::agentRoute(kNativeAgents[i].id);
    if((saved==official_selection::Preference::Native && route->global) ||
        (saved==official_selection::Preference::Studio && route->scoped))agentPreferences[i]=saved;
  }
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
  if (mcuReauthPolicy.bindingChanged()) return F("电脑身份变化，需重新绑定");
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

void putLe16(uint8_t* out,uint16_t value) {out[0]=uint8_t(value);out[1]=uint8_t(value>>8);}
void putLe32(uint8_t* out,uint32_t value) {for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}

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

enum class RecordingStopReason:uint8_t {None,Released,WallLimit,SampleLimit,Cancelled,StartFailed,CaptureFailed,NoSpeech,DrainTimeout};
enum class RecordingOutcome:uint8_t {None,Complete,Cancelled,Failed};
struct OfficialRecordingState {
  bool active=false,ready=false,captureStarted=false,storeOwned=false,heardSpeech=false,stopping=false,releaseTracked=false;
  uint32_t takeId=0,epoch=0,keys=0,navigation=0,startedAt=0,stoppedAt=0,drainAt=0,releaseAt=0,lastSpeechAt=0,frames=0,activeSamples=0,frameLimit=0;
  uint16_t peak=0;
  uint64_t squares=0;
  VoiceInputChannel channel=VoiceInputChannel::Undecided;
  RecordingStopReason stopReason=RecordingStopReason::None;
};
OfficialRecordingState officialRecording;
struct RecordingTakeSnapshot {
  uint32_t takeId=0,wallMs=0,frames=0,pcmBytes=0,encodedBytes=0;
  uint32_t produced=0,consumed=0,maxReady=0,overruns=0,invalidCallbacks=0,discarded=0,sampleRate=0,error=0,bufferBytes=0;
  RecordingStopReason reason=RecordingStopReason::None;
  RecordingOutcome outcome=RecordingOutcome::None;
  bool captureStarted=false,sttStarted=false,storageRetained=false;
};
RecordingTakeSnapshot lastRecordingTake;
uint32_t recordingTakeSequence=0,officialSttTakeId=0;
struct SttMetricsSnapshot {
  uint32_t takeId=0,stackFree=0,heapMin=0,largestMin=0,bootHeapMin=0;
  int http=0;uint8_t error=0;bool present=false,available=false;
};
SttMetricsSnapshot lastSttMetrics;
void latchOfficialSttMetrics(const official_voice_job::Output& out,bool valid) {
  if(!valid || out.type!=official_voice_job::Type::Stt || !officialSttTakeId)return;
  lastSttMetrics.takeId=officialSttTakeId;lastSttMetrics.error=static_cast<uint8_t>(out.result.error);
  lastSttMetrics.http=out.result.httpStatus;lastSttMetrics.available=out.resourcesAvailable;
  lastSttMetrics.stackFree=out.workerStackFreeBytes;lastSttMetrics.heapMin=out.internalHeapMinObservedBytes;
  lastSttMetrics.largestMin=out.internalLargestBlockMinObservedBytes;lastSttMetrics.bootHeapMin=out.systemHeapLowWaterBytes;
  lastSttMetrics.present=true;
}
const char* recordingStopName(RecordingStopReason reason) {
  switch(reason) {
    case RecordingStopReason::Released:return "released";
    case RecordingStopReason::WallLimit:return "wall_limit";
    case RecordingStopReason::SampleLimit:return "sample_limit";
    case RecordingStopReason::Cancelled:return "cancelled";
    case RecordingStopReason::StartFailed:return "start_failed";
    case RecordingStopReason::CaptureFailed:return "capture_failed";
    case RecordingStopReason::NoSpeech:return "no_speech";
    case RecordingStopReason::DrainTimeout:return "drain_timeout";
    default:return "none";
  }
}
uint32_t officialRecordingWallMs() {
  return officialRecording.ready?uint32_t((officialRecording.stopping?officialRecording.stoppedAt:millis())-officialRecording.startedAt):0;
}
void latchOfficialRecording(RecordingOutcome outcome,bool sttStarted=false) {
  RecordingTakeSnapshot completed;
  completed.takeId=officialRecording.takeId;completed.wallMs=officialRecordingWallMs();
  completed.pcmBytes=officialRecording.storeOwned?officialRecordingStore.recordedBytes():0;
  completed.encodedBytes=officialRecording.storeOwned?officialRecordingStore.encodedBytes():0;
  completed.frames=completed.pcmBytes/sizeof(int16_t);completed.reason=officialRecording.stopReason;
  completed.outcome=outcome;completed.captureStarted=officialRecording.captureStarted;completed.sttStarted=sttStarted;
  if(completed.captureStarted) {
    const auto capture=cardputerCaptureStats();
    completed.produced=capture.queue.producedBlocks;completed.consumed=capture.queue.consumedBlocks;
    completed.maxReady=capture.queue.maxReadyBlocks;completed.overruns=capture.queue.overruns;
    completed.invalidCallbacks=capture.queue.invalidCallbacks;completed.discarded=capture.queue.discardedAfterJoin;
    completed.sampleRate=capture.sampleRate;completed.error=static_cast<uint32_t>(capture.error);
    completed.storageRetained=capture.storageRetained;completed.bufferBytes=capture.bufferBytes;
  }
  lastRecordingTake=completed;
  officialRecordingLastPcmBytes=completed.pcmBytes;officialRecordingLastEncodedBytes=completed.encodedBytes;
}
CardputerCaptureStats recordingCaptureForHealth() {
  if(officialRecording.active && officialRecording.captureStarted)return cardputerCaptureStats();
  CardputerCaptureStats capture;
  capture.failed=lastRecordingTake.error!=0;capture.error=static_cast<mic_capture::Error>(lastRecordingTake.error);
  capture.sampleRate=lastRecordingTake.sampleRate;
  capture.storageRetained=lastRecordingTake.storageRetained;capture.bufferBytes=lastRecordingTake.bufferBytes;
  capture.queue.producedBlocks=lastRecordingTake.produced;capture.queue.consumedBlocks=lastRecordingTake.consumed;
  capture.queue.maxReadyBlocks=lastRecordingTake.maxReady;capture.queue.overruns=lastRecordingTake.overruns;
  capture.queue.invalidCallbacks=lastRecordingTake.invalidCallbacks;capture.queue.discardedAfterJoin=lastRecordingTake.discarded;
  return capture;
}
bool recordingActive(){return officialRecording.active;}
uint32_t recordingElapsedMs(){return officialRecording.active?officialRecordingWallMs():0;}

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
                           bool requireAnnouncement, bool manualSource = false, bool remoteSource = false,
                           bool identityVerified = false) {
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
  // Only a successful direct HTTP identity read may set this diagnostic bit;
  // an unauthenticated UDP announcement never establishes it.
  device.identityVerified = identityVerified;
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

class LoginResponse final:public Stream {
 public:
  explicit LoginResponse(uint32_t timeoutMs=0,uint32_t startedAtMs=millis())
      : timeoutMs_(timeoutMs),startedAtMs_(startedAtMs) {}
  String body;bool overflow=false,timedOut=false;
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
  size_t write(uint8_t byte) override {return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count) override {
    if(timeoutMs_ && static_cast<uint32_t>(millis()-startedAtMs_)>=timeoutMs_) {timedOut=true;return 0;}
    if(count>8192-body.length()) {overflow=true;return 0;}
    const size_t before=body.length();body.concat(reinterpret_cast<const char*>(bytes),count);
    return body.length()==before+count?count:0;
  }
 private:
  uint32_t timeoutMs_,startedAtMs_;
};

// Pinned HTTPClient can wait forever on available()==0. Gate the actual
// protocol-selected client as well as the bounded body loop, including headers.
template<typename Base> class IdentityDeadlineClient final:public Base {
 public:
  using Base::connect;using Base::read;
  explicit IdentityDeadlineClient(uint32_t started):started_(started){}
  int available()override{return alive()?std::max(0,Base::available()):0;}
  uint8_t connected()override{return alive()?Base::connected():0;}
  int read()override{return alive()?Base::read():-1;}
  int read(uint8_t* bytes,size_t count)override{return alive()?Base::read(bytes,count):-1;}
  int peek()override{return alive()?Base::peek():-1;}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count)override{return alive()?Base::write(bytes,count):0;}
  int connect(const char* host,uint16_t port,int32_t timeout)override {
    if(!alive())return 0;
    const uint32_t left=kMcuLoginTimeoutMs-uint32_t(millis()-started_);
    const int result=Base::connect(host,port,std::min<int32_t>(timeout>0?timeout:int32_t(left),int32_t(left)));
    return alive()?result:0;
  }
 private:
  uint32_t started_;bool stopped_=false;
  bool alive() {
    if(stopped_)return false;
    if(uint32_t(millis()-started_)>=kMcuLoginTimeoutMs) {
      stopped_=true;Base::stop();static_cast<Stream&>(*this).setTimeout(0);return false;
    }
    return true;
  }
};

bool readLanIdentityBody(WiFiClient& client,const String& endpoint,uint32_t started,String* body) {
  HTTPClient http;http.setReuse(false);http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(kMcuLoginTimeoutMs);http.setTimeout(kMcuLoginTimeoutMs);
  if(!http.begin(client,endpoint))return false;
  const char* names[]{"Transfer-Encoding","Content-Encoding"};http.collectHeaders(names,2);
  const int status=http.GET(),length=http.getSize();
  // Official LAN link-info has Content-Length. Unknown/chunked/compressed
  // responses are explicitly unsupported here; failure never authorizes login.
  if(status<200||status>=300||length<=0||length>8192||
      http.header("Transfer-Encoding").length()||http.header("Content-Encoding").length()) {http.end();return false;}
  LoginResponse received(kMcuLoginTimeoutMs,started);
  auto* input=http.getStreamPtr();
  while(input&&received.body.length()<static_cast<size_t>(length)) {
    if(uint32_t(millis()-started)>=kMcuLoginTimeoutMs)break;
    const int available=input->available();
    if(available>0) {
      uint8_t chunk[256];const size_t count=std::min<size_t>(sizeof(chunk),std::min<size_t>(available,length-received.body.length()));
      const int got=input->read(chunk,count);
      if(got<=0||received.write(chunk,got)!=static_cast<size_t>(got))break;
    } else {
      if(!input->connected())break;
      delay(1);yield();
    }
  }
  const bool ok=received.body.length()==static_cast<size_t>(length)&&!received.overflow&&!received.timedOut&&
      uint32_t(millis()-started)<kMcuLoginTimeoutMs;
  http.end();if(ok)*body=std::move(received.body);return ok;
}

// ArduinoJson stops at the first object; retain its exact input cursor so only
// whitespace, rather than a second/truncated object, may follow that object.
struct IdentityJsonInput {
  const String& body;size_t position=0;
  explicit IdentityJsonInput(const String& input):body(input) {}
  int read(){return position<body.length()?static_cast<uint8_t>(body[position++]):-1;}
  size_t readBytes(char* bytes,size_t count){size_t n=0;for(;n<count&&position<body.length();++n)bytes[n]=body[position++];return n;}
  bool finished()const {for(size_t i=position;i<body.length();++i)if(body[i]!=' '&&body[i]!='\r'&&body[i]!='\n'&&body[i]!='\t')return false;return true;}
};

// Read-only identity request; only the explicit manual wrapper persists URLs.
bool readLanDeviceInfo(const String& input,String* error,bool manualSource,LanDevice* identity=nullptr) {
  String host;const String baseUrl=normalizedManualDeviceUrl(input,&host);
  String scheme,parsedHost,path;uint16_t expectedPort=0;
  if(!baseUrl.length()||!parseAudioUrl(baseUrl,&scheme,&parsedHost,&expectedPort,&path))return false;
  const uint32_t started=millis();String body;bool read=false;
  const String endpoint=baseUrl+F("/api/devices/link-info");
  if(scheme==F("https")) {
    IdentityDeadlineClient<WiFiClientSecure> client(started);native_tls::configureTrust(client);
    client.setHandshakeTimeout((kMcuLoginTimeoutMs+999)/1000);
    read=readLanIdentityBody(client,endpoint,started,&body);
  } else if(scheme==F("http")) {
    IdentityDeadlineClient<WiFiClient> client(started);
    read=readLanIdentityBody(client,endpoint,started,&body);
  }
  if(!read){if(error)*error=F("机器信息读取失败或超时");return false;}
  JsonDocument filter,document;filter["device_id"]=true;filter["device_public_key"]=true;
  filter["http_port"]=true;filter["endpoint_kind"]=true;
  const char* displayFields[]{"computer_name","hermes_web_ui_version","hermes_agent_version","relay_url","remote_http_url"};
  for(const char* field:displayFields)filter[field]=true;
  IdentityJsonInput json{body};
  if(deserializeJson(document,json,DeserializationOption::Filter(filter))||!json.finished()||!document.is<JsonObjectConst>()||
      !document["device_id"].is<const char*>()||!document["device_public_key"].is<const char*>()||
      !document["http_port"].is<uint16_t>()||!document["endpoint_kind"].is<const char*>())return false;
  const JsonString id=document["device_id"].as<JsonString>(),publicKey=document["device_public_key"].as<JsonString>();
  const String kind=document["endpoint_kind"].as<const char*>();const uint16_t port=document["http_port"].as<uint16_t>();
  const String key=lanDeviceKey(String(id.c_str()),kind,port);
  if(!id.size()||id.size()>256||std::strlen(id.c_str())!=id.size()||!publicKey.size()||publicKey.size()>4096||
      std::strlen(publicKey.c_str())!=publicKey.size()||!kind.length()||port!=expectedPort||
      !native_reauth::completeDeviceBinding(key.c_str()))return false;
  if(identity) {
    identity->id=id.c_str();identity->endpointKind=kind;identity->httpPort=port;identity->url=baseUrl;
    identity->ip=host;identity->manualSource=manualSource;identity->remoteSource=false;
    identity->identityVerified=true;identity->lastSeenMs=millis();
  }
  // The legacy cache reader scans raw text. Give it a flat canonical root,
  // preserving friendly metadata but excluding nested same-name fields.
  for(const char* field:displayFields)if(!document[field].is<const char*>())document.remove(field);
  String canonical;const size_t count=serializeJson(document,canonical);
  if(!count||canonical.length()!=count)return false;
  rememberLanDeviceInfo(canonical,host,baseUrl,millis()-started,false,manualSource,false,true);
  return true;
}

bool fetchManualDeviceInfo(const String &input, String *error) {
  if (!readLanDeviceInfo(input, error, true)) return false;
  persistManualDeviceUrl(normalizedManualDeviceUrl(input));
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
  if (mcuReauthPolicy.bindingChanged())
    appendInfoRow(html, F("连接提示"), F("已保存绑定与当前电脑身份不一致，请重新登录绑定"));
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

  html += F("<section class='card'><h2>Agent</h2><form method='post' action='/device/agent'><select name='runtime' onchange=\"const o=this.options[this.selectedIndex],m=this.form.elements.mode;m.value=o.dataset.mode;m.options[0].disabled=o.dataset.studio!=='1';m.options[1].disabled=o.dataset.native!=='1';\">");
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    html += F("<option value='");html+=kNativeAgents[i].id;html+=F("' data-mode='");
    html+=agentPreference(i)==official_selection::Preference::Native?"native":"studio";
    const auto* route=official_studio::agentRoute(kNativeAgents[i].id);
    html+=F("' data-studio='");html+=route->scoped?"1":"0";
    html+=F("' data-native='");html+=route->global?"1":"0";html+=F("'");
    if (i == agentSelection.active) html += F(" selected");
    html += F(">"); html += kNativeAgents[i].name; html += F("</option>");
  }
  html+=F("</select><select name='mode'><option value='studio'");
  if(agentPreference(agentSelection.active)==official_selection::Preference::Studio)html+=F(" selected");
  html+=F(">Studio模型</option><option value='native'");
  if(agentPreference(agentSelection.active)==official_selection::Preference::Native)html+=F(" selected");
  html+=F(">原生CLI</option></select><button class='btn primary' type='submit'>选择 Agent</button></form><p class='hint'>首页 Tab 切换 Studio模型与原生CLI。选择页左 , 或上 ; 选前一个，右 / 或下 . 选后一个，无需 Fn。OK 进入对话，Esc 返回。对话页直接打字、OK发送；Fn+空格或顶部G0长按说话，松开发送；Fn+方向编辑或翻记录。配置就绪后可以发送；实际模型或 Agent 错误会显示在聊天页。</p></section>");

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
  const String agent=server.arg(F("runtime"));const int index=nativeAgentIndex(agent.c_str());
  const String mode=server.arg(F("mode"));
  if(index<0) {server.send(400,F("text/plain; charset=utf-8"),F("Agent 不存在"));return;}
  if(mode.length()) {
    if(mode!="studio" && mode!="native") {server.send(400,F("text/plain; charset=utf-8"),F("模式无效"));return;}
    if(!setAgentPreference(size_t(index),mode=="native"?official_selection::Preference::Native:official_selection::Preference::Studio)) {
      server.send(409,F("text/plain; charset=utf-8"),F("当前无法切换模式，请查看设备提示"));return;
    }
  }
  if(!requestCardputerAgentSelection(size_t(index))) {
    server.send(409,F("text/plain; charset=utf-8"),F("请先返回 Agent 首页再选择"));return;
  }
  oledDirty=true;server.sendHeader(F("Location"),F("/device"),true);server.send(303,F("text/plain"),"");
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

bool runMcuLogin(LanDevice &device, const String &account, const String &password,
                 bool useRemoteLogin = false, bool manualLogin = false) {
  if(useRemoteLogin || device.remoteSource || !mcuDeviceCode().length()) {
    rememberMcuLoginResult(-1,F("此固件仅支持局域网直连，请选择同网电脑"));return false;
  }
  if(studioAuthJob.busy()) {cancelMcuReauth();rememberMcuLoginResult(-1,F("正在停止旧登录，请稍后重试"));return false;}
  if(manualLogin) profileFormProof="";
  cardputerTickKeyClick(false);disconnectMcuSocketClient();leaveConversationScope();
  String endpoint=device.url;while(endpoint.endsWith("/")) endpoint.remove(endpoint.length()-1);
  endpoint+=F("/api/auth/mcu-login");
  HTTPClient http;http.setReuse(false);http.setConnectTimeout(5000);http.setTimeout(kMcuLoginTimeoutMs);
  if(!http.begin(endpoint)) {rememberMcuLoginResult(-1,F("无法打开登录接口"));return false;}
  http.addHeader(F("Content-Type"),F("application/json"));
  http.addHeader(F("X-Hermes-Device-Id"),deviceId());
  http.addHeader(F("X-Hermes-Device-Name"),F("M5Stack Cardputer-Adv"));
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

native_reauth::Target currentMcuReauthTarget() {
  return {activeDeviceKey.c_str(), activeDeviceUrl.c_str(), mcuSocketRelayUrl.c_str(),
          selectedProfile.c_str(), mcuSocketRelayUrl.length() > 0};
}

void cancelMcuReauth() {
  studioAuthJob.cancel();++studioAuthEpoch;
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
  if(studioAuthJob.busy() && !mcuReauthPolicy.matches(currentMcuReauthTarget())) cancelMcuReauth();
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

void applyReauthentication(native_reauth::Outcome outcome) {
  mcuReauthAttemptInProgress=false;mcuReauthPolicy.complete(outcome,millis());
  if(outcome==native_reauth::Outcome::BindingChanged) {
    mcuReauthPolicy.requireManual(currentMcuReauthTarget(),true);
    setChatNotice("电脑身份变化，请在设备网页重新绑定");return;
  }
  if(outcome==native_reauth::Outcome::Success) {
    cancelMcuReauth();connectMcuSocketClient();setChatNotice("");return;
  }
  if(outcome==native_reauth::Outcome::Rejected) {
    setChatNotice("登录已失效，请在设备网页重新登录");return;
  }
  setChatNotice("重登暂时失败，将自动重试");
}

void tickMcuReauth() {
  // Completion is polled even after cancellation or a changed native binding.
  studio_auth::Output output;
  if(studioAuthJob.poll(output)==studio_auth::Poll::Ready) {
    const bool current=output.epoch==studioAuthEpoch && mcuReauthPolicy.active() &&
        mcuReauthPolicy.matches(currentMcuReauthTarget()) && studioAuthTarget==activeDeviceKey+"|"+activeDeviceUrl+"|"+selectedProfile &&
        wifiReady && WiFi.status()==WL_CONNECTED;
    mcuReauthAttemptInProgress=false;
    studioAuthLastError=unsigned(output.error);studioAuthLastStage=unsigned(output.stage);
    studioAuthStackFree=output.workerStackFreeBytes;studioAuthElapsedMs=output.elapsedMs;
    if(current) {
      lastMcuLoginCode=output.httpStatus;
      auto outcome=output.error==studio_auth::Error::BindingChanged?native_reauth::Outcome::BindingChanged:
          output.error==studio_auth::Error::Rejected?native_reauth::Outcome::Rejected:native_reauth::Outcome::TransientFailure;
      if(output.error==studio_auth::Error::Ok) {
        String token(output.jwt.data());
        if(token.length()==std::strlen(output.jwt.data()) && token.length() && prefs.begin("stockLan",false)) {
          const bool saved=prefs.putString("auth_token",token)>0;prefs.end();
          if(saved) {mcuAuthToken=token;outcome=native_reauth::Outcome::Success;}
        }
        for(size_t i=0;i<token.length();++i)token[i]=0;
      }
      Serial.printf("[studio-auth] result=%u stage=%u http=%d stack_free=%lu\n",unsigned(output.error),
          unsigned(output.stage),output.httpStatus,static_cast<unsigned long>(output.workerStackFreeBytes));
      applyReauthentication(outcome);
    }
  }
  if(studioAuthJob.busy()) return;
  if(!mcuReauthBlocksSocket()) return;
  if(wsReady || mcuSocketConnected || mcuSocketNamespaceReady) disconnectMcuSocketClient();
  if(mcuReauthAudioCleanupPending && !audioBusy) {
    cardputerAudioClear();cancelOfficialVoice();mcuReauthAudioCleanupPending=false;
  }
  const native_reauth::Conditions conditions{
      wifiReady && WiFi.status()==WL_CONNECTED,
      audioBusy || officialVoiceNetworkBusy() || studioConnectJob.busy() || officialSelectionJob.busy() ||
          officialHistoryJob.busy() || chatPendingSend.length()>0 || restartPending,
      mcuSocketReconnectBlocked};
  if(!mcuReauthPolicy.begin(millis(),conditions)) return;
  String account,password;
  bool same=false;
  if(prefs.begin("stockLan",true)) {
    const String profile=prefs.getString("cur_profile",prefs.getString("current_profile",""));
    same=prefs.getString("active_key","")==activeDeviceKey && prefs.getString("active_url","")==activeDeviceUrl &&
        profile==selectedProfile && !prefs.getBool("active_remote",false) && !mcuSocketRelayUrl.length();
    if(same) {account=prefs.getString("cur_account","");password=prefs.getString("cur_password","");}
    prefs.end();
  }
  auto input=std::unique_ptr<studio_auth::Input>(new(std::nothrow)studio_auth::Input);
  const String instance=deviceId();
  const bool valid=input && same && studio_auth::makeInput(*input,activeDeviceUrl.c_str(),activeDeviceKey.c_str(),
      selectedProfile.c_str(),instance.c_str(),account.c_str(),password.c_str(),++studioAuthEpoch);
  for(size_t i=0;i<password.length();++i)password[i]=0;
  for(size_t i=0;i<account.length();++i)account[i]=0;
  const auto started=valid?studioAuthJob.start(*input):studio_auth::StartResult::Invalid;
  if(started==studio_auth::StartResult::Started) {
    studioAuthTarget=activeDeviceKey+"|"+activeDeviceUrl+"|"+selectedProfile;mcuReauthAttemptInProgress=true;
  } else applyReauthentication(valid?native_reauth::Outcome::TransientFailure:native_reauth::Outcome::Rejected);
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
    if(uint32_t(millis()-startedAt)>=timeoutMs)return false;
    int count=0;
    if(!mcuSocketRelayUrl.length() && activeDeviceUrl.startsWith("http://")) {
      count=::send(mcuWsClient->fd(),data+written,length-written,MSG_DONTWAIT);
      if(count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)return false;
    } else count=int(mcuWsClient->write(data+written,length-written));
    if (count > 0) {
      written += size_t(count);
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
  const uint32_t frameStarted=millis();
  constexpr uint32_t frameBudgetMs=1500;
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
    const uint32_t elapsed=millis()-frameStarted;
    if(elapsed>=frameBudgetMs || !writeMcuWsBytes(buffer,n,frameBudgetMs-elapsed)) {
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

const char* experienceNoticeText() {
  using N=experience::Notice;
  switch(experienceController.snapshot().notice) {
    case N::WaitingForConnection:return "后台暂未连接，正在恢复";
    case N::NeedsBinding:return "首次使用请打开设备网页登录";
    case N::Authenticating:return "正在恢复登录，草稿已保留";
    case N::Activating:return "正在读取所选 Agent 配置";
    case N::Retrying:return "后台暂未响应，正在重试";
    case N::TurnBusy:return "当前回复尚未结束，请稍后切换";
    case N::ReconcileTurn:return "正在核对上一条，未自动重发";
    case N::ActivationFailed:return "此 Agent 暂不可用，请检查配置";
    case N::FactoryUnavailable:return "原厂固件不可用";
    case N::GenerationExhausted:return "操作次数已达上限，请重启";
    case N::None:return "";
  }
  return "";
}

void applyExperienceEffects() {
  experience::Effect effect;
  while(experienceController.takeEffect(effect)) {
    if(effect.kind==experience::EffectKind::Activate) {
      if(selectionRetryGeneration!=effect.generation) {selectionRetryGeneration=effect.generation;officialSelectionRetries=0;}
      if(queueAgentActivation(effect.agent)) activationGeneration=effect.generation;
      else {
        experienceController.dispatch(experience::Event::activationDeferred(effect.generation));
        officialSelectionNotBeforeMs=millis()+500;
      }
    } else if(effect.kind==experience::EffectKind::CancelActivation) {
      officialSelectionQueued=false;agentSelection.pending=false;officialSelectionJob.cancel();
      if(!officialSelectionJob.busy()) {
        experienceController.dispatch(experience::Event::activationFinished(effect.generation,experience::Outcome::Cancelled));
        activationGeneration=0;
      }
    } else returnToFactoryMenu();
  }
  agentSelection.browse=experienceController.snapshot().desiredAgent;
}

void tickExperience() {
  const String binding=activeDeviceKey+"|"+selectedProfile+"|"+officialAccount;
  if(experienceBindingObserved && binding!=experienceBinding) {
    leaveConversationScope();experienceController.dispatch(experience::Event::bindingChanged());
  }
  experienceBinding=binding;experienceBindingObserved=true;
  const auto connection=!wifiReady || WiFi.status()!=WL_CONNECTED?experience::Connection::Offline:
      mcuReauthPolicy.bindingChanged() || mcuSocketReconnectBlocked || mcuReauthPolicy.rejected() ||
      !activeDeviceUrl.length() || !selectedProfile.length() || (!mcuAuthToken.length() && !mcuReauthPolicy.active())?
          experience::Connection::NeedsBinding:
      mcuReauthAttemptInProgress || mcuReauthAudioCleanupPending || mcuReauthPolicy.blocksSocket()?
          experience::Connection::Authenticating:
      mcuSocketNamespaceReady?experience::Connection::Ready:experience::Connection::Connecting;
  experienceController.dispatch(experience::Event::connection(connection));
  synchronizeSendRecovery();
  const auto turn=chatPendingSend.length()?experience::TurnPhase::Submitting:
      chatRemoteRunActive?experience::TurnPhase::Running:
      officialResumePending || officialResumeQueued?experience::TurnPhase::Reconciling:
      chatSendUnknown?experience::TurnPhase::Uncertain:
      officialVoiceNetworkBusy()?experience::TurnPhase::Running:experience::TurnPhase::Idle;
  experienceController.dispatch(experience::Event::turn(turn));
  const auto& state=experienceController.snapshot();
  if(state.activation==experience::ActivationPhase::RetryWait &&
      static_cast<int32_t>(millis()-officialSelectionNotBeforeMs)>=0)
    experienceController.dispatch(experience::Event::retryDue(state.activationGeneration));
  applyExperienceEffects();
}

bool setAgentPreference(size_t index,official_selection::Preference value) {
  if(index>=kNativeAgentCount || (value!=official_selection::Preference::Studio && value!=official_selection::Preference::Native))return false;
  const auto* route=official_studio::agentRoute(kNativeAgents[index].id);
  if((value==official_selection::Preference::Native && !route->global) ||
      (value==official_selection::Preference::Studio && !route->scoped)) {
    setChatNotice("所选 Agent 不支持此模式");return false;
  }
  if(agentPreference(index)==value)return true;
  if(experienceController.snapshot().exitPending)return false;
  if(chatPendingSend.length() || chatRemoteRunActive || chatSendUnknown || officialResumePending || officialResumeQueued || officialVoiceNetworkBusy()) {
    setChatNotice("当前对话尚未结束，暂不能切模式");return false;
  }
  char key[16];snprintf(key,sizeof(key),"mode_%u",unsigned(index));
  if(!prefs.begin("stockLan",false)) {setChatNotice("模式保存失败，请重试");return false;}
  const bool saved=prefs.putUChar(key,uint8_t(value))>0;prefs.end();
  if(!saved) {setChatNotice("模式保存失败，请重试");return false;}
  cancelOfficialVoice();officialHistoryJob.cancel();
  experienceController.dispatch(experience::Event::bindingChanged());applyExperienceEffects();
  leaveConversationScope();agentSelection.disconnect();agentPreferences[index]=value;
  agentSelectionError="";setChatNotice("");oledDirty=true;return true;
}

bool requestCardputerAgentSelection(size_t index) {
  if(index>=kNativeAgentCount) return false;
  if(chatPageOpen() && index!=experienceController.snapshot().desiredAgent) return false;
  if(!experienceController.dispatch(experience::Event::browse(int(index)-int(experienceController.snapshot().desiredAgent)))) return false;
  const bool accepted=experienceController.dispatch(experience::Event::open());
  applyExperienceEffects();oledDirty=true;return accepted;
}

bool queueAgentActivation(size_t index) {
  if(officialSelectionJob.busy() || !mcuSocketNamespaceReady || !mcuAuthToken.length() || !agentSelection.request(index)) return false;
  pendingAgentProfile=selectedProfile;agentSelectionStartedMs=millis();agentSelectionError="";
  cancelOfficialVoice();officialSelectionQueued=true;officialSelectionNotBeforeMs=millis();
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
      chatModel.setSendUnknown(chatSendUnknown);
      const NativeChatResult result = chatModel.activate(kNativeAgents[agentSelection.active].id,
                                                        selectedProfile.c_str(), agentSelection.version, binding.c_str());
      chatActivationAttemptVersion = agentSelection.version;
      chatSendUnknown=chatModel.context() && chatModel.context()->send_unknown;
      sendRecovery=send_recovery::Recovery();synchronizeSendRecovery();
      if (chatPendingSend.length()) { chatPendingSend = ""; chatSendUnknown = true; }
      setChatNotice(chatSendUnknown ? "发送结果未确认，请先 Fn+R 核对" : "");
      if (result != NativeChatResult::Ok) setChatNotice(result == NativeChatResult::DraftCapacity ?
          "草稿已满，请先发送或清空旧草稿" : "当前会话不可用");
      else if (!chatSendUnknown && officialMode == official_studio::Mode::Scoped &&
               officialModel.origin == official_studio_models::Origin::CompatibleDefault) {
        // The official client may select a compatible Studio provider when
        // this Agent cannot use the Profile default. Show the accepted public
        // identity once; a normal send clears this notice and restores space.
        setChatNotice(String("兼容模型 ") + officialModel.provider.data() + "/" + officialModel.model.data());
      }
      chatHistoryRequest = ""; chatBottomOffset = 0; chatHistoryLoadedVersion = 0;
      chatFocusMessage = ""; chatRemoteRunActive = chatLiveEventsSeen = false;
      chatLiveRequest = "";
      chatHistoryMode = false; cardputerSetChatHistoryMode(false);
      chatPageFocus = NativeChatBrowseFocus::Preserve;
      ++chatUiRevision;
    }
  }
  const bool opened = chatPageOpen();
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
    setChatNotice("记录同步超时，Fn+R 重试");
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
    if(key.kind==NativeChatKeyKind::AcknowledgeSend) {
      synchronizeSendRecovery();
      if(sendRecovery.acknowledge(officialEpoch)) {
        chatSendUnknown=false;chatModel.setSendUnknown(false);setChatNotice("草稿已保留，按 OK 可发送");
      } else setChatNotice(chatSendUnknown?"请先 Fn+R 同步并查看记录":"当前没有待核对的发送");
      ++chatUiRevision;oledDirty=true;continue;
    }
    if(key.kind==NativeChatKeyKind::Refresh) {
      if(mcuInteractionActive && chatLiveEventsSeen) setChatNotice("回复中，结束后可刷新记录");
      else if(officialHistoryJob.busy() || chatHistoryRequest.length() || officialResumePending)
        setChatNotice("正在同步记录，请稍后重试");
      else {
        officialHistorySkippingTools=false;synchronizeSendRecovery();
        if(chatSendUnknown) {
          sendRecovery.requestReview(officialEpoch);officialResumeQueued=true;
          setChatNotice("正在核对结果，未自动重发");
        }
        requestChatHistory();chatBottomOffset=0;
      }
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
    experienceController.dispatch(experience::Event::factoryFailed());
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
  cardputerSetConversationMode(chatPageOpen());cardputerPoll();
  tickExperience();
  int key;
  while((key=cardputerNavigation())!=0) {
    noteMcuActivity();
    if(key==3) {
      cancelOfficialVoice();cardputerSuppressGestureUntilReleased();
      experienceController.dispatch(experience::Event::back());
    } else if(key==2) experienceController.dispatch(experience::Event::open());
    else if(key==4) {
      const auto index=experienceController.snapshot().desiredAgent;
      const auto next=agentPreference(index)==official_selection::Preference::Native?
          official_selection::Preference::Studio:official_selection::Preference::Native;
      setAgentPreference(index,next);
    }
    else if(key==-1 || key==1) experienceController.dispatch(experience::Event::browse(key));
    applyExperienceEffects();oledDirty=true;
  }
  if(native_remote::requestExpired(millis(),agentSelection.pending,agentSelectionStartedMs,100000U)) {
    // Cancellation is not completion. The old worker keeps its generation
    // until poll releases it, even across a new transport connection.
    officialSelectionJob.cancel();agentSelection.pending=agentSelection.confirmed=false;
    agentSelectionError="native_agent_ack_timeout";
    closeMcuSocketTransport(F("Agent activation timeout"));oledDirty=true;
  }
  cardputerSetConversationMode(chatPageOpen());tickNativeChat();
}

void broadcastMcuStatus() {
  sendMcuSocketJson(mcuStatusJson());
}

void handleBootButton() {
  const uint8_t events=cardputerGesture();
  if(events==DeviceGesture::None) return;
  noteMcuActivity();
  if(events&DeviceGesture::ShortBack) {
    cancelOfficialVoice();
    experienceController.dispatch(experience::Event::back());applyExperienceEffects();
  }
  if((events&DeviceGesture::VoiceStart) && cardputerTalkPressed()) {
    if(chatPageOpen()) startOfficialRecording();
    else {setChatNotice("按 OK 进入聊天后说话");cardputerSuppressGestureUntilReleased();}
  }
  // Capture owns joining/draining before STT. Release is observed by its
  // existing bounded tick; no button handler directly starts a network task.
  oledDirty=true;
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
    tickExperience();return;
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
  studioConnectJob.cancel();++studioConnectEpoch;
  if(mcuWsClient->connected())mcuWsClient->stop();
  wsReady = false;
  mcuSocketConnected = false;
  mcuSocketNamespaceReady = false;mcuStatusNamespaceReady=false;officialNamespaces.reset();statusReconnectAtMs=0;
  mcuSocketTargetKey = "";
}

void tickMcuSocketReconnect() {
  if (mcuReauthBlocksSocket()) { nextMcuSocketReconnectAtMs = 0; return; }
  if (wsReady || mcuSocketConnected || mcuSocketNamespaceReady || studioConnectJob.busy()) return;
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

  if(studioAuthJob.busy() || studioConnectJob.busy() || officialSelectionJob.busy() || officialHistoryJob.busy() || officialVoiceNetworkBusy()) {
    scheduleMcuSocketReconnect();return;
  }
  const String socketPath=String("/socket.io/?EIO=4&transport=websocket&profile=")+officialUrlEncode(selectedProfile);
  studio_connect::Input input;
  if(!studio_connect::makeInput(input,host.c_str(),port,scheme=="https",socketPath.c_str(),++studioConnectEpoch)) {
    setChatNotice("后台地址不受支持，请重新绑定");return;
  }
  const auto started=studioConnectJob.start(input);
  if(started==studio_connect::StartResult::Started) studioConnectTarget=targetKey;
  else scheduleMcuSocketReconnect();
}

void tickStudioConnect() {
  studio_connect::Output output;
  if(studioConnectJob.poll(output)!=studio_connect::Poll::Ready) return;
  const bool current=output.epoch==studioConnectEpoch && wifiReady && WiFi.status()==WL_CONNECTED &&
      studioConnectTarget==expectedMcuSocketTargetKey() && !mcuReauthBlocksSocket();
  const bool accepted=current && output.error==studio_connect::Error::Ok && output.client;
  Serial.printf("[studio-connect] result=%u handshake=%u http=%u elapsed=%lu accepted=%d stack_free=%lu\n",
      unsigned(output.error),unsigned(output.handshakeError),output.httpStatus,
      static_cast<unsigned long>(output.elapsedMs),accepted,static_cast<unsigned long>(output.workerStackFreeBytes));
  if(accepted) {
    // Parser no longer borrows any previous transport here, at the outer tick.
    resetOfficialSocketReader();connectedSocket=std::move(output.client);mcuWsClient=connectedSocket.get();
    wsReady=mcuSocketConnected=true;mcuSocketNamespaceReady=mcuStatusNamespaceReady=false;
    officialNamespaces.reset();statusReconnectAtMs=0;mcuSocketTargetKey=studioConnectTarget;
    lastMcuSocketConnectAtMs=millis();mcuSocketLiveness.connected(lastMcuSocketConnectAtMs);
    nextMcuSocketReconnectAtMs=0;
  } else {
    if(output.client) {output.client->stop();output.client.reset();}
    if(current) scheduleMcuSocketReconnect();
  }
  studioConnectDiagnostics=std::move(output); // Fixed diagnostics; no client or credentials retained.
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
  // Only a durable, locally owned /chat-run identity can use the official
  // local-session 404 as proof of absence. Never infer ownership from a prefix
  // or reinterpret a failed page after this session has confirmed history.
  const bool localAbsenceAllowed=!officialSessionExists && officialHistoryTotal==0 &&
      officialSessionStore.ready() && officialBinding==officialSessionStore.binding() &&
      officialSession==String("m5-")+officialBinding+"-"+officialSessionStore.nonce();
  if(official_history::makeInput(*input,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),selectedProfile.c_str(),
      officialSession.c_str(),officialEpoch,page,byteOffset,localAbsenceAllowed)!=official_history::Result::Ok) {
    setChatNotice("记录连接信息不完整");return false;
  }
  if(officialHistoryJob.start(*input)!=official_history::StartResult::Started) {
    setChatNotice("记录任务暂不可用，请稍后重试");return false;
  }
  historyReviewTicket=sendRecovery.reviewTicket();
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
  sendRecovery.observeHistory(officialEpoch,historyReviewTicket,page==0);historyReviewTicket=0;
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
  if(chatSendUnknown) {setChatNotice("发送结果未确认，请先 Fn+R 核对");return false;}
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
  const auto finishActivation=[](experience::Outcome outcome) {
    const auto finished=selectionWorkerGeneration?selectionWorkerGeneration:activationGeneration;
    const bool accepted=experienceController.dispatch(experience::Event::activationFinished(finished,outcome));
    if(activationGeneration==finished)activationGeneration=0;
    selectionWorkerGeneration=0;return accepted;
  };
  const auto& app=experienceController.snapshot();
  const bool matching=polled==official_selection::Poll::Ready && activationGeneration &&
      app.openIntent && app.activationGeneration==activationGeneration && mcuSocketNamespaceReady &&
      selection.epoch==officialEpoch && agentSelection.pending && pendingAgentProfile==selection.profile.data() &&
      !std::strcmp(kNativeAgents[agentSelection.requested].id,selection.agentId.data());
  if(polled==official_selection::Poll::Ready && !matching) {
    finishActivation(experience::Outcome::Cancelled);agentSelection.pending=false;
  }
  if(!mcuSocketNamespaceReady) return;
  if(matching) {
    const size_t index=agentSelection.requested;
    const auto selectionNotice=official_selection_notice::describe(official_selection_notice::classify(selection));
    // Fixed reason and numeric enums only: no server text, account/Profile,
    // model identity, URL or credentials. Once per accepted worker result.
    Serial.printf("[official-selection] reason=%s result=%u stage=%u http=%d native=%d model=%d\n",
        selectionNotice.code,static_cast<unsigned>(selection.result),static_cast<unsigned>(selection.stage),
        selection.httpStatus,official_selection_notice::nativeDiagnostic(selection),
        official_selection_notice::modelDiagnostic(selection));
    if(selection.result==official_selection::Result::Transient || selection.result==official_selection::Result::NoMemory) {
      officialSelectionQueued=false;agentSelection.pending=false;
      finishActivation(experience::Outcome::RetryableFailure);
      if(officialSelectionRetries<5) ++officialSelectionRetries;
      const uint32_t backoff=std::min<uint32_t>(30000,1000U<<officialSelectionRetries);
      officialSelectionNotBeforeMs=millis()+std::max(backoff,selection.retryAfterMs);
      setChatNotice(selectionNotice.text);
    } else if(selection.result!=official_selection::Result::Ok) {
      agentSelection.pending=agentSelection.confirmed=false;agentSelectionError="standard_agent_unavailable";
      finishActivation(selection.result==official_selection::Result::AuthInvalid?
          experience::Outcome::RetryableFailure:experience::Outcome::PermanentFailure);
      if(selection.result==official_selection::Result::AuthInvalid) {
        setChatNotice(selectionNotice.text);queueMcuReauth(true,F("standard_selection_auth_invalid"));
      } else setChatNotice(selectionNotice.text);
    } else {
      if(officialAccount.length() && officialAccount!=selection.accountId.data()) {
        experienceController.dispatch(experience::Event::bindingChanged());
        finishActivation(experience::Outcome::Cancelled);agentSelection.pending=agentSelection.confirmed=false;
        officialAccount=selection.accountId.data();leaveConversationScope();
        experienceBinding=activeDeviceKey+"|"+selectedProfile+"|"+officialAccount;
        setChatNotice("账号已改变，请重新选择 Agent");return;
      }
      officialAccount=selection.accountId.data();officialModel=selection.selection;officialMode=selection.mode;
      experienceBinding=activeDeviceKey+"|"+selectedProfile+"|"+officialAccount;
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
          officialSelectionQueued=false;agentSelection.pending=false;
          finishActivation(experience::Outcome::RetryableFailure);officialSelectionNotBeforeMs=millis()+5000;
          setChatNotice("会话尚未保存，正在重试；原有记录已保留");
        } else {
          agentSelection.pending=agentSelection.confirmed=false;agentSelectionError="standard_session_storage_failed";
          finishActivation(experience::Outcome::PermanentFailure);
          setChatNotice("会话存储不可用，未新建对话；原有记录已保留");
        }
        return;
      }
      if(!finishActivation(experience::Outcome::Succeeded)) {
        agentSelection.pending=false;return;
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

    }
  }
  if(officialSelectionQueued && !studioAuthJob.busy() && !studioConnectJob.busy() && !officialSelectionJob.busy() && !officialHistoryJob.busy() && !officialVoiceNetworkBusy() && agentSelection.pending &&
      static_cast<int32_t>(millis()-officialSelectionNotBeforeMs)>=0) {
    resetListeningMonitor();
    auto input=std::unique_ptr<official_selection::Input>(new(std::nothrow)official_selection::Input);
    if(!input) {officialSelectionNotBeforeMs=millis()+5000;setChatNotice("内存暂不足，稍后自动重试");return;}
    const auto valid=official_selection::makeInput(*input,activeDeviceUrl.c_str(),mcuAuthToken.c_str(),
        selectedProfile.c_str(),kNativeAgents[agentSelection.requested].id,officialEpoch,
        agentPreference(agentSelection.requested));
    const auto started=valid==official_selection::Result::Ok ? officialSelectionJob.start(*input) :
        official_selection::StartResult::Invalid;
    if(started==official_selection::StartResult::Started) {
      selectionWorkerGeneration=activationGeneration;
      officialSelectionQueued=false;agentSelectionStartedMs=millis();
    } else if(started==official_selection::StartResult::NoMemory) {
      officialSelectionNotBeforeMs=millis()+5000;setChatNotice("内存暂不足，稍后自动重试");
    } else {
      officialSelectionQueued=false;agentSelection.pending=agentSelection.confirmed=false;
      finishActivation(experience::Outcome::PermanentFailure);
      agentSelectionError="standard_binding_invalid";setChatNotice("后台连接信息不完整，已保留原有绑定");
    }
  }
  if(officialResumePending && millis()-officialResumeAtMs>15000) {
    closeMcuSocketTransport(F("standard session resume timeout"));return;
  }
  if(officialResumeQueued && !officialResumePending && agentSelection.ready() && officialSessionExists) {
    resetListeningMonitor();
    officialResumeQueued=false;JsonDocument resume;resume["session_id"]=officialSession;
    if(officialEmit("resume",resume)) {
      resumeReviewTicket=sendRecovery.reviewTicket();officialResumePending=true;officialResumeAtMs=millis();
    }
  }
  if(officialHistoryQueued && officialChatContextActivated() && !studioAuthJob.busy() && !studioConnectJob.busy() && !officialHistoryJob.busy() && !officialSelectionJob.busy() && !officialVoiceNetworkBusy() &&
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
      setChatNotice("新会话未能保存，仍保留当前对话；可稍后重试");return false;
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
    sendRecovery.observeResume(officialEpoch,resumeReviewTicket,frame.working);resumeReviewTicket=0;
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
    if(event=="run.failed") {
      const auto reason=official_run_failure::notice(frame.error[0]?frame.error.data():"请检查 Studio 的模型与登录");
      Serial.printf("[official-run] failure=%s\n",reason.code);setChatNotice(reason.text);
    }
    else setChatNotice("");
    chatRemoteRunActive=false;mcuInteractionActive=false;officialRun="";officialLiveId="";
    officialHistoryQueued=true;++chatUiRevision;oledDirty=true;
  }
}

void cancelOfficialVoice(const char* notice) {
  const bool owned=officialVoiceNetworkBusy();
  const bool cancelledTake=officialRecording.active;
  resetListeningMonitor();
  officialSpeechPlan.cancel();officialSpeechNeedsFreshTotal=false;
  if(officialHistoryForSpeech) officialHistoryJob.cancel();
  officialVoiceCancelled=true;
  officialRecordingRequested=false;
  officialVoiceJob.cancel();
  if(cancelledTake) {
    if(officialRecording.ready && !officialRecording.stopping) {
      officialRecording.stoppedAt=millis();officialRecording.stopping=true;
    }
    officialRecording.stopReason=RecordingStopReason::Cancelled;
    officialRecording.active=false;officialVoiceCleanupPending=true;
  }
  officialVoiceDrainPending=officialVoiceOutputPlaying=false;
  officialVoiceRate=officialVoiceTailFrames=officialVoiceDrainAtMs=officialVoiceTailAtMs=0;
  if(owned) {
    cardputerAudioClear();M5Cardputer.Mic.end();audioBusy=false;resetListeningMonitor();
    if(!chatRemoteRunActive) {mcuInteractionActive=false;mcuInteractionStatus="aborted";mcuInteractionUpdatedAtMs=millis();}
  }
  if(cancelledTake) {
    latchOfficialRecording(RecordingOutcome::Cancelled);
    officialRecordingStore.discard();
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
  if(officialRecording.ready && !officialRecording.stopping) {
    officialRecording.stoppedAt=millis();officialRecording.stopping=true;
  }
  if(officialRecording.stopReason==RecordingStopReason::None)
    officialRecording.stopReason=success?RecordingStopReason::Released:RecordingStopReason::CaptureFailed;
  officialRecording.active=false;cardputerSuppressGestureUntilReleased();cardputerAudioClear();M5Cardputer.Mic.end();resetListeningMonitor();audioBusy=false;
  // Recording has ended even when admission or STT startup fails below. Do
  // not leave the header at "listening" while the error notice is displayed.
  mcuInteractionActive=false;mcuInteractionStatus=success?"failed":"aborted";mcuInteractionUpdatedAtMs=millis();
  const auto capture=cardputerCaptureStats();
  if(success && (capture.failed || capture.storageRetained)) {
    success=false;officialRecording.stopReason=RecordingStopReason::CaptureFailed;failure="录音缓冲溢出或采集失败，本次未发送";
  }
  sampleOfficialRecordingMemory();
  voiceRecordPeak=officialRecording.peak;
  voiceRecordRms=officialRecording.frames?uint32_t(std::sqrt(double(officialRecording.squares)/officialRecording.frames)):0;
  voiceRecordActiveSamples=officialRecording.activeSamples;
  voiceRecordHeardSpeech=officialRecording.heardSpeech || (voiceRecordRms>=kVoiceVadRmsStart &&
      voiceRecordPeak>=kVoiceVadPeakStart && voiceRecordActiveSamples>=kVoiceVadMinActiveSamples);
  if(!success || !voiceRecordHeardSpeech) {
    if(success) {
      officialRecording.stopReason=RecordingStopReason::NoSpeech;
      failure="未检测到清晰声音，本次未发送";
    }
    latchOfficialRecording(officialRecording.stopReason==RecordingStopReason::Cancelled?RecordingOutcome::Cancelled:RecordingOutcome::Failed);
    officialRecordingStore.discard();mcuInteractionActive=false;
    setChatNotice(failure?failure:"录音失败，本次未发送");return;
  }
  if(officialRecordingStore.finish()!=ram_recording::Error::Ok) {
    latchOfficialRecording(RecordingOutcome::Failed);
    officialRecordingStore.discard();mcuInteractionActive=false;setChatNotice("录音保存失败，本次未发送");return;
  }
  auto context=officialVoiceContext();ram_recording::Result error;
  auto reader=officialRecordingStore.openReader(error);
  const size_t bytes=officialRecordingStore.recordedBytes();
  const uint64_t nonce=(uint64_t(esp_random())<<32)|esp_random();
  if(!context || !reader || error!=ram_recording::Error::Ok) {
    officialRecordingAdmissionError=3;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    latchOfficialRecording(RecordingOutcome::Failed);setChatNotice("语音识别内存不足，本次未发送");return;
  }
  // Capture has been joined and freed. Account for only one worker stack,
  // its actual SharedState and LAN HTTP headroom while RAM audio stays alive.
  officialRecordingWorkerFree=heap_caps_get_free_size(kOfficialVoiceHeapCaps);officialRecordingWorkerLargest=heap_caps_get_largest_free_block(kOfficialVoiceHeapCaps);
  const size_t stackBytes=official_voice_job::taskStackBytes(official_voice_job::Type::Stt,*context);
  const size_t workerBytes=stackBytes+official_voice_job::Job::fixedJobHeapBytes();
  sampleOfficialRecordingMemory();
  if(officialRecordingWorkerFree<workerBytes+12*1024 || officialRecordingWorkerLargest<std::max<size_t>(stackBytes,official_voice_job::Job::fixedJobHeapBytes())+512) {
    officialRecordingAdmissionError=4;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    latchOfficialRecording(RecordingOutcome::Failed);setChatNotice("语音识别内存不足，本次未发送");return;
  }
  const auto started=officialVoiceJob.startStt(*context,std::move(reader),bytes,kVoiceInputSampleRate,1,nonce);
  officialRecordingStartResult=static_cast<uint8_t>(started);
  officialRecordingStartStage=static_cast<uint8_t>(officialVoiceJob.lastStartStage());
  officialRecordingStartDiagnostics=officialVoiceJob.lastStartDiagnostics();
  sampleOfficialRecordingMemory();
  if(started!=official_voice_job::StartResult::Started) {
    officialRecordingAdmissionError=5;
    officialVoiceCleanupPending=true;mcuInteractionActive=false;
    latchOfficialRecording(RecordingOutcome::Failed);
    setChatNotice(started==official_voice_job::StartResult::NoMemory?
        "语音识别内存不足，本次未发送":"识别任务暂不可用，本次语音未发送");return;
  }
  officialSttTakeId=officialRecording.takeId;
  latchOfficialRecording(RecordingOutcome::Complete,true);
  officialVoiceEpoch=officialEpoch;officialVoiceCancelled=false;
  officialVoiceCleanupPending=true;audioBusy=true;
  mcuInteractionActive=true;mcuInteractionStatus="transcribing";mcuInteractionUpdatedAtMs=millis();
  setChatNotice(failure?failure:"");oledDirty=true;
}

void startOfficialRecording() {
  if(officialRecording.active) return;
  if(chatSendUnknown) {setChatNotice("发送结果未确认，请先 Fn+R 核对");return;}
  if(studioAuthJob.busy() || studioConnectJob.busy()) {setChatNotice("正在恢复连接，请稍后说话");return;}
  if(!chatPageOpen() || !officialStudioDirect() || !officialChatContextActivated() ||
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
  officialRecording=OfficialRecordingState{};officialRecording.active=true;
  if(!++recordingTakeSequence)++recordingTakeSequence;
  officialRecording.takeId=recordingTakeSequence;
  officialRecordingHeapStart=officialRecordingHeapMinimum=officialRecordingLargestMinimum=0;
  officialRecordingWorkerFree=officialRecordingWorkerLargest=0;officialRecordingAdmissionError=0;
  officialRecordingStartResult=255;officialRecordingStartStage=0;
  officialRecordingStartDiagnostics=official_voice_job::StartDiagnostics{};
  sampleOfficialRecordingMemory();
  const auto storage=officialRecordingStore.prepare();
  sampleOfficialRecordingMemory();
  officialRecordingPrepareResult=storage;
  Serial.printf("Official recording prepare error=%s capacity=%lu\n",ram_recording::errorName(storage),
      static_cast<unsigned long>(officialRecordingStore.capacityBytes()));
  if(storage!=ram_recording::Error::Ok ||
      officialRecordingStore.capacityBytes()<kVoiceInputSampleRate*2*kVoiceRecordMinMs/1000) {
    officialRecordingAdmissionError=1;
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"录音内存不足，文字聊天仍可使用");return;
  }
  const auto beginResult=officialRecordingStore.begin();
  officialRecordingBeginResult=beginResult;
  if(beginResult!=ram_recording::Error::Ok) {
    Serial.printf("Official recording begin error=%s\n",ram_recording::errorName(beginResult));
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"录音存储不可用，本次未发送");return;
  }
  officialRecording.storeOwned=true;
  // The advertised limit follows the actual compressed RAM allocation.
  const uint32_t limitMs=kVoiceRecordLimitMs;
  const uint32_t frameLimit=std::min<uint32_t>(kVoiceInputSampleRate*limitMs/1000,
      officialRecordingStore.capacityBytes()/sizeof(int16_t));
  if(frameLimit<kVoiceInputSampleRate*kVoiceRecordMinMs/1000) {
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"录音空间不足以保留本次语音，本次未发送");return;
  }
  officialRecording.frameLimit=frameLimit;officialRecording.channel=VoiceInputChannel::Undecided;
  officialRecording.epoch=officialEpoch;officialRecording.keys=cardputerChatKeyCount();
  officialRecording.navigation=cardputerNavigationCount();
  cardputerTickKeyClick(false);
  cardputerAudioClear();
  if(!setI2sSampleRate(kVoiceInputSampleRate)) {
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"麦克风采样率设置失败，本次未发送");return;
  }
  // A failed allocation must not let the codec or network starve silently.
  // Keep SDK microphone DMA/task and network/UI headroom beyond the queue.
  if(heap_caps_get_free_size(kOfficialVoiceHeapCaps)<54*1024 || heap_caps_get_largest_free_block(kOfficialVoiceHeapCaps)<sizeof(mic_capture::Queue) ||
      heap_caps_get_free_size(MALLOC_CAP_DMA)<8*1024 || heap_caps_get_largest_free_block(MALLOC_CAP_DMA)<512) {
    officialRecordingAdmissionError=2;
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"连续录音内存不足，文字聊天仍可使用");return;
  }
  audioBusy=true;mcuInteractionActive=true;mcuInteractionStatus="listening";mcuInteractionUpdatedAtMs=millis();
  if(!cardputerCaptureBegin(kVoiceInputSampleRate) || cardputerCaptureStats().failed) {
    officialRecording.stopReason=RecordingStopReason::StartFailed;
    finishOfficialRecording(false,"连续录音缓冲不可用，本次未发送");return;
  }
  officialRecording.captureStarted=officialRecording.ready=true;
  // The five-second wall clock starts only after the SDK accepted the first
  // request on a running capture port. Codec/setup time is not recorded time.
  officialRecording.startedAt=officialRecording.lastSpeechAt=millis();
  sampleOfficialRecordingMemory();
  setChatNotice(String("录音可用约 ")+String(double(officialRecording.frameLimit)/kVoiceInputSampleRate,1)+" 秒，松开发送");oledDirty=true;
}

void stopOfficialRecordingCapture(RecordingStopReason reason,uint32_t now) {
  if(officialRecording.stopping)return;
  officialRecording.stopReason=reason;officialRecording.stoppedAt=now;
  officialRecording.stopping=true;
  cardputerCaptureStop(); // Join producer; keep only completed Ready blocks.
  officialRecording.drainAt=millis();
}

void tickOfficialRecording() {
  if(!officialRecording.active) return;
  const uint32_t now=millis();
  if(officialRecording.epoch!=officialEpoch || !chatPageOpen() || !officialChatContextActivated() ||
      cardputerChatKeyCount()!=officialRecording.keys || cardputerNavigationCount()!=officialRecording.navigation ||
      WiFi.status()!=WL_CONNECTED) {
    officialRecording.stopReason=RecordingStopReason::Cancelled;
    finishOfficialRecording(false,"录音已取消，本次未发送");return;
  }
  const auto captureBeforeRead=cardputerCaptureStats();
  if(captureBeforeRead.failed || (!officialRecording.stopping && !captureBeforeRead.active)) {
    officialRecording.stopReason=RecordingStopReason::CaptureFailed;
    finishOfficialRecording(false,"录音缓冲溢出或采集失败，本次未发送");return;
  }
  if(!officialRecording.stopping && officialRecording.frames>=officialRecording.frameLimit)
    stopOfficialRecordingCapture(RecordingStopReason::SampleLimit,now);
  if(!officialRecording.stopping && uint32_t(now-officialRecording.startedAt)>=kVoiceRecordLimitMs)
    stopOfficialRecordingCapture(RecordingStopReason::WallLimit,now);
  if(!officialRecording.stopping && uint32_t(now-officialRecording.startedAt)>=kVoiceRecordMinMs) {
    if(!cardputerTalkPressed()) {
      if(!officialRecording.releaseTracked) {officialRecording.releaseAt=now;officialRecording.releaseTracked=true;}
      if(now-officialRecording.releaseAt>=kVoiceStreamReleaseDebounceMs && !officialRecording.stopping) {
        // Stop requeue and join, then write every already completed block.
        // An unfinished SDK request is discarded, never labelled complete.
        stopOfficialRecordingCapture(RecordingStopReason::Released,now);
      }
    } else officialRecording.releaseTracked=false;
  }
  if(officialRecording.stopping && uint32_t(millis()-officialRecording.drainAt)>=kVoiceRecordDrainTimeoutMs) {
    officialRecording.stopReason=RecordingStopReason::DrainTimeout;
    finishOfficialRecording(false,"录音收尾超时，本次未发送");return;
  }
  auto captured=cardputerCaptureTake();
  if(!captured) {
    if(officialRecording.stopping)finishOfficialRecording(true,
        officialRecording.stopReason==RecordingStopReason::Released?nullptr:"已达录音时长上限，发送已录内容");
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
  if(capture.failed) {officialRecording.stopReason=RecordingStopReason::CaptureFailed;finishOfficialRecording(false,"录音缓冲溢出或采集失败，本次未发送");return;}
  if(!officialRecording.stopping && officialRecording.frames>=officialRecording.frameLimit)
    stopOfficialRecordingCapture(RecordingStopReason::SampleLimit,millis());
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
  if(!officialStudioDirect() || !chatPageOpen() || !mcuSocketNamespaceReady ||
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
  const bool current=officialStudioDirect() && chatPageOpen() && officialChatContextActivated() &&
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
    ++officialVoiceCompletions;
    const auto& out=officialVoiceCompletion;
    Serial.printf("Official voice type=%u error=%u http=%d samples=%llu rate=%lu stack_free=%lu heap_min=%lu largest_min=%lu boot_heap_min=%lu\n",
        unsigned(out.type),unsigned(out.result.error),out.result.httpStatus,
        static_cast<unsigned long long>(out.result.samples),static_cast<unsigned long>(out.result.sampleRate),
        static_cast<unsigned long>(out.workerStackFreeBytes),static_cast<unsigned long>(out.internalHeapMinObservedBytes),
        static_cast<unsigned long>(out.internalLargestBlockMinObservedBytes),static_cast<unsigned long>(out.systemHeapLowWaterBytes));

    const bool valid=current && !officialVoiceCancelled && out.epoch==officialEpoch &&
        officialSession==out.session.data() && selectedProfile==out.profile.data();
    latchOfficialSttMetrics(out,valid);
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
  savedNetworkRetry.cancel();savedWifiJoin.cancel();clearJoiningCredentials();
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

void beginSavedWifiConnection() {
  if(savedWifiJoin.active()) return;
  closeMcuSocketTransport(F("Wi-Fi recovery"));cancelMcuReauth();
  clearJoiningCredentials();loadWifiCredentials(joiningSsids,joiningPasswords,joiningCount);
  wifiReady=false;WiFi.persistent(false);WiFi.mode(setupApMode?WIFI_AP_STA:WIFI_STA);WiFi.setSleep(false);
  savedWifiJoin.begin(joiningCount,millis());
}

void tickSavedWifiConnection() {
  if(!savedWifiJoin.active()) return;
  const auto index=savedWifiJoin.index();
  const bool connected=WiFi.status()==WL_CONNECTED && index<size_t(joiningCount) && WiFi.SSID()==joiningSsids[index];
  const auto result=savedWifiJoin.poll(millis(),connected);
  if(result==native_wifi::SavedWifiJoin::Result::Attempt) {
    const auto next=savedWifiJoin.index();
    WiFi.disconnect(false,false);WiFi.begin(joiningSsids[next].c_str(),joiningPasswords[next].c_str());
    setOledStatus(OledMode::Think,F("WIFI"),F("CONNECT"),25);return;
  }
  if(result==native_wifi::SavedWifiJoin::Result::Connected) {
    wifiReady=true;setupApMode=false;savedNetworkRetry.connected();wifiDisconnectedSinceMs=0;
    WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
    SharedWifiConfig::Credentials factory;
    const bool fromFactory=readFactoryWifi(factory) && joiningSsids[index]==factory.ssid && joiningPasswords[index]==factory.password;
    connectedWifiSource=fromFactory?factoryWifiSource:"ekko_history";
    rememberWifiCredential(joiningSsids[index],joiningPasswords[index]);clearJoiningCredentials();
    showLanIpOnOled();autoLoginSavedDevice();connectMcuSocketClient();return;
  }
  if(result==native_wifi::SavedWifiJoin::Result::Exhausted) {
    clearJoiningCredentials();savedNetworkRetry.failed(millis());
    startSetupAp(false,"wifi_reconnect_failed");
  }
}

void startSetupAp(bool refreshScan, const char *reason) {
  savedWifiJoin.cancel();clearJoiningCredentials();
  disconnectMcuSocketClient();cancelMcuReauth();
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
  json += F(",\"binding_identity_changed\":");json+=mcuReauthPolicy.bindingChanged()?F("true"):F("false");
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
  const auto& application=experienceController.snapshot();
  json+=F(",\"application\":{\"revision\":10,\"connection\":");json+=unsigned(application.connection);
  json+=F(",\"page\":");json+=unsigned(application.page);
  json+=F(",\"activation\":");json+=unsigned(application.activation);
  json+=F(",\"turn\":");json+=unsigned(application.turnPhase);
  json+=F(",\"desired_mode\":");json+=unsigned(agentPreference(application.desiredAgent));
  json+=F(",\"active_mode\":");json+=unsigned(officialMode);
  json+=F(",\"review_phase\":");json+=unsigned(sendRecovery.phase());
  json+=F(",\"wifi_joining\":");json+=savedWifiJoin.active()?F("true"):F("false");
  json+=F(",\"loop_longest_gap_ms\":");json+=loopLongestGapMs;
  json+=F(",\"connect_busy\":");json+=studioConnectJob.busy()?F("true"):F("false");
  json+=F(",\"connect_error\":");json+=unsigned(studioConnectDiagnostics.error);
  json+=F(",\"connect_stack_free\":");json+=studioConnectDiagnostics.workerStackFreeBytes;
  json+=F(",\"auth_busy\":");json+=studioAuthJob.busy()?F("true"):F("false");
  json+=F(",\"auth_error\":");json+=studioAuthLastError;
  json+=F(",\"auth_stage\":");json+=studioAuthLastStage;
  json+=F(",\"auth_stack_free\":");json+=studioAuthStackFree;
  json+=F(",\"auth_elapsed_ms\":");json+=studioAuthElapsedMs;json+=F("}");
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
  json += chatPageOpen() ? F("conversation") : F("agents");
  json += F("\",\"open_pending\":"); json += experienceController.snapshot().openIntent ? F("true") : F("false");
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
  json += F(",\"type\":");json+=unsigned(voice.type);
  json += F(",\"completions\":");json+=officialVoiceCompletions;
  json += F(",\"http\":");json+=voice.result.httpStatus;
  json += F(",\"metrics_available\":");json+=voice.resourcesAvailable?F("true"):F("false");
  json += F(",\"stack_min_free_bytes\":");json+=voice.workerStackFreeBytes;
  json += F(",\"heap_min_observed_bytes\":");json+=voice.internalHeapMinObservedBytes;
  json += F(",\"largest_min_observed_bytes\":");json+=voice.internalLargestBlockMinObservedBytes;
  json += F(",\"boot_heap_min_bytes\":");json+=voice.systemHeapLowWaterBytes;json+=F("}");
  json += F(",\"stt_last\":{\"present\":");json+=lastSttMetrics.present?F("true"):F("false");
  json += F(",\"take_id\":");json+=lastSttMetrics.takeId;
  json += F(",\"metrics_available\":");json+=lastSttMetrics.available?F("true"):F("false");
  json += F(",\"error\":");json+=lastSttMetrics.error;
  json += F(",\"http\":");json+=lastSttMetrics.http;
  json += F(",\"stack_min_free_bytes\":");json+=lastSttMetrics.stackFree;
  json += F(",\"heap_min_observed_bytes\":");json+=lastSttMetrics.heapMin;
  json += F(",\"largest_min_observed_bytes\":");json+=lastSttMetrics.largestMin;
  json += F(",\"boot_heap_min_bytes\":");json+=lastSttMetrics.bootHeapMin;json+='}';
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
  json += F(",\"active_take_id\":");json+=officialRecording.active?officialRecording.takeId:0;
  json += F(",\"active_ready\":");json+=officialRecording.active&&officialRecording.ready?F("true"):F("false");
  json += F(",\"active_wall_ms\":");json+=officialRecording.active?officialRecordingWallMs():0;
  json += F(",\"last_take\":{\"take_id\":");json+=lastRecordingTake.takeId;
  json += F(",\"outcome\":");json+=static_cast<unsigned>(lastRecordingTake.outcome);
  json += F(",\"stop_reason\":\"");json+=recordingStopName(lastRecordingTake.reason);json+='"';
  json += F(",\"wall_ms\":");json+=lastRecordingTake.wallMs;
  json += F(",\"frames\":");json+=lastRecordingTake.frames;
  json += F(",\"pcm_bytes\":");json+=lastRecordingTake.pcmBytes;
  json += F(",\"encoded_bytes\":");json+=lastRecordingTake.encodedBytes;
  json += F(",\"stt_started\":");json+=lastRecordingTake.sttStarted?F("true"):F("false");json+='}';
  json += F(",\"heap_start_bytes\":");json+=officialRecordingHeapStart;
  json += F(",\"heap_min_observed_bytes\":");json+=officialRecordingHeapMinimum;
  json += F(",\"largest_min_observed_bytes\":");json+=officialRecordingLargestMinimum;
  json += F(",\"worker_free_before_bytes\":");json+=officialRecordingWorkerFree;
  json += F(",\"worker_largest_before_bytes\":");json+=officialRecordingWorkerLargest;
  json += F(",\"admission_error\":");json+=officialRecordingAdmissionError;
  json += F(",\"start_result\":");json+=officialRecordingStartResult;
  json += F(",\"start_stage\":");json+=officialRecordingStartStage;
  json += F(",\"task_stack_bytes\":");json+=officialRecordingStartDiagnostics.taskStackBytes;
  json += F(",\"start_metrics_available\":");json+=officialRecordingStartDiagnostics.resourcesAvailable?F("true"):F("false");
  json += F(",\"before_state_free_bytes\":");json+=officialRecordingStartDiagnostics.beforeStateFreeBytes;
  json += F(",\"before_state_largest_bytes\":");json+=officialRecordingStartDiagnostics.beforeStateLargestBlockBytes;
  json += F(",\"before_task_free_bytes\":");json+=officialRecordingStartDiagnostics.beforeTaskFreeBytes;
  json += F(",\"before_task_largest_bytes\":");json+=officialRecordingStartDiagnostics.beforeTaskLargestBlockBytes;json+='}';
  const auto capture=recordingCaptureForHealth();
  json += F(",\"capture\":{\"active\":");json += capture.active?F("true"):F("false");
  json += F(",\"take_id\":");json+=officialRecording.active&&officialRecording.captureStarted?officialRecording.takeId:lastRecordingTake.takeId;
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
  if(kForceSetupAp)startSetupAp(true,"forced_setup");
  else beginSavedWifiConnection();
  setupRoutes();
  if (wifiReady && !setupApMode) {
    autoLoginSavedDevice();
  }
}

void loop() {
  const uint32_t now=millis();
  if(loopLastAtMs)loopLongestGapMs=std::max(loopLongestGapMs,uint32_t(now-loopLastAtMs));
  loopLastAtMs=now;
  tickCardputerInput();
  cardputerTickKeyClick(i2sReady && !audioBusy && !studioAuthJob.busy() && !studioConnectJob.busy() &&
      !officialSelectionJob.busy() && !officialHistoryJob.busy() && !officialVoiceNetworkBusy() && !mcuAudioPlaying && mcuAudioCount == 0 &&
      !listeningModeEnabled && !listeningMonitorReady && !restartPending &&
      !idlePowerSaveActive &&
      outputVolumePercent > 0 && !cardputerTalkPressed());
  if (restartPending && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    ESP.restart();
  }

  // Header parsing is synchronous; never let a management request stall audio.
  if(!officialVoiceNetworkBusy()) server.handleClient();
  if (wsReady) mcuSocketLoop();
  tickStudioConnect();
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
  tickExperience();
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
    } else if(now-wifiDisconnectedSinceMs>=kWifiDisconnectGraceMs) {
      wifiDisconnectedSinceMs=0;beginSavedWifiConnection();
    }
  } else if (wifiReady) {
    wifiDisconnectedSinceMs = 0;
  }
  tickSavedWifiConnection();
  if(!wifiReady && !savedWifiJoin.active() && !studioAuthJob.busy() && !studioConnectJob.busy() &&
      savedNetworkRetry.begin(millis(),audioBusy || officialVoiceNetworkBusy() || restartPending))
    beginSavedWifiConnection();
}
