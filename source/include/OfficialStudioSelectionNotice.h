#pragma once

#include "OfficialStudioSelection.h"

// Display-only projection. Never use response text or binding/model strings in
// a notice or diagnostic; no connection/retry/auth policy is changed here.
namespace official_selection_notice {
enum class Reason : uint8_t {
  None, Cancelled, StudioAuth, NoMemory, RateLimited, NativeUnavailable,
  BackendUnavailable, AccessDenied, MissingResource, NotInstalled,
  AgentAuth, NativeModelUnavailable, ProfileUnavailable, ModelConfiguration,
  NoCompatibleModel, ApiModeUnsupported, IdentityTooLong, InvalidConfiguration,
  AgentUnavailable
};

inline Reason classify(const official_selection::Output& output) {
  using R = official_selection::Result;
  using S = official_selection::Stage;
  using N = official_studio::Result;
  using M = official_studio_models::Result;
  if(output.result==R::Ok)return Reason::None;
  if(output.result==R::Cancelled)return Reason::Cancelled;
  if(output.result==R::AuthInvalid)return Reason::StudioAuth;
  if(output.result==R::NoMemory)return Reason::NoMemory;
  if(output.result==R::Transient) {
    if(output.httpStatus==429)return Reason::RateLimited;
    if(output.stage==S::NativeModels && output.httpStatus==200 &&
        output.nativeResult==N::RetryableUnavailable)return Reason::NativeUnavailable;
    return Reason::BackendUnavailable;
  }
  if(output.stage!=S::Account && output.httpStatus==403)return Reason::AccessDenied;
  if(output.stage!=S::Account && output.httpStatus==404)return Reason::MissingResource;
  if(output.result==R::Unavailable && output.httpStatus==200) {
    if((output.stage==S::Installed || output.stage==S::NativeModels) &&
        output.nativeResult==N::NotInstalled)return Reason::NotInstalled;
    if(output.stage==S::NativeModels) {
      if(output.nativeResult==N::AuthenticationRequired)return Reason::AgentAuth;
      if(output.nativeResult==N::NativeModelUnavailable)return Reason::NativeModelUnavailable;
    }
  }
  if(output.stage==S::Models && output.httpStatus==200) {
    switch(output.selection.result) {
      case M::ProfileNotAvailable:return Reason::ProfileUnavailable;
      case M::NeedsConfiguration:return Reason::ModelConfiguration;
      case M::NotAvailable:return Reason::NoCompatibleModel;
      case M::UnsupportedApiMode:return Reason::ApiModeUnsupported;
      case M::IdentityTooLong:return Reason::IdentityTooLong;
      default:break;
    }
  }
  return output.result==R::Invalid ? Reason::InvalidConfiguration : Reason::AgentUnavailable;
}

struct Notice { const char* code; const char* text; };
inline Notice describe(Reason reason) {
  switch(reason) {
    case Reason::None:return {"ok", ""};
    case Reason::Cancelled:return {"cancelled", "已取消读取配置"};
    case Reason::StudioAuth:return {"studio_auth", "登录正在恢复，草稿已保留"};
    case Reason::NoMemory:return {"no_memory", "内存暂不足，稍后自动重试"};
    case Reason::RateLimited:return {"rate_limited", "后台请求频繁，稍后自动重试"};
    case Reason::NativeUnavailable:return {"native_unavailable", "Agent 状态不可用，正在重试"};
    case Reason::BackendUnavailable:return {"backend_unavailable", "后台暂未响应，正在自动重试"};
    case Reason::AccessDenied:return {"access_denied", "当前 Profile 无访问权限"};
    case Reason::MissingResource:return {"missing_resource", "后台配置或接口不存在"};
    case Reason::NotInstalled:return {"not_installed", "CLI 未安装，请在电脑安装"};
    case Reason::AgentAuth:return {"agent_auth", "Agent 未授权，请在电脑登录"};
    case Reason::NativeModelUnavailable:return {"native_model_unavailable", "Agent 原生模型不可用"};
    case Reason::ProfileUnavailable:return {"profile_unavailable", "Profile 不可用，请检查配置"};
    case Reason::ModelConfiguration:return {"model_configuration", "Studio 的模型配置不完整"};
    case Reason::NoCompatibleModel:return {"no_compatible_model", "Profile 没有兼容的可用模型"};
    case Reason::ApiModeUnsupported:return {"api_mode_unsupported", "模型接口模式不受支持"};
    case Reason::IdentityTooLong:return {"identity_too_long", "模型配置名称过长"};
    case Reason::InvalidConfiguration:return {"invalid_configuration", "后台配置格式不受支持"};
    case Reason::AgentUnavailable:return {"agent_unavailable", "Agent 配置不可用，请检查电脑"};
  }
  return {"agent_unavailable", "Agent 配置不可用，请检查电脑"};
}

// -1 means the stage did not produce this detail. In particular the default
// NativeNotReady / InvalidResponse fields are not evidence of a CLI/model fault.
inline int nativeDiagnostic(const official_selection::Output& output) {
  using S = official_selection::Stage;
  if(output.httpStatus==200 &&
      ((output.stage==S::Installed && output.nativeResult==official_studio::Result::NotInstalled) ||
       (output.stage==S::NativeModels && output.nativeResult!=official_studio::Result::NativeNotReady)))
    return static_cast<int>(output.nativeResult);
  return -1;
}
inline int modelDiagnostic(const official_selection::Output& output) {
  if(output.stage==official_selection::Stage::Models && output.httpStatus==200 &&
      output.selection.result!=official_studio_models::Result::InvalidResponse)
    return static_cast<int>(output.selection.result);
  return -1;
}
} // namespace official_selection_notice
