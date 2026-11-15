#include "OfficialStudioSelection.h"
#include "OfficialStudioSelectionNotice.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace official_selection;
using NoticeReason = official_selection_notice::Reason;
struct Flag final:Cancellation {bool value=false;bool cancelled()const override{return value;}};
struct Reply {
  Stage stage;std::string body;int status=200;std::string type="application/json; charset=utf-8";
  bool complete=true;int64_t length=-2;uint32_t retry=0;
  Reply(Stage s,std::string b):stage(s),body(std::move(b)){}
};
struct Fixture final:Transport {
  std::vector<Reply> replies;
  std::vector<std::string> paths;
  size_t call=0,step=7,cancelAt=SIZE_MAX;Flag* cancellation=nullptr;
  bool get(const Request& request,ResponseSink& sink)override {
    assert(call<replies.size()&&request.input&&request.path);
    const Reply& reply=replies[call++];assert(reply.stage==request.stage);
    assert(request.timeoutMs==(request.stage==Stage::NativeModels?25000:request.stage==Stage::Installed?12000:10000));
    assert(!std::strncmp(request.path,"/api/",5));paths.emplace_back(request.path);
    Headers headers;headers.status=reply.status;headers.contentType=reply.type.c_str();headers.retryAfterMs=reply.retry;
    headers.contentLength=reply.length==-2?int64_t(reply.body.size()):reply.length;
    if(!sink.begin(headers))return false;
    for(size_t at=0;at<reply.body.size();at+=step) {
      if(cancellation&&at>=cancelAt)cancellation->value=true;
      if(!sink.write(reinterpret_cast<const uint8_t*>(reply.body.data()+at),std::min(step,reply.body.size()-at)))return false;
    }
    return reply.complete;
  }
};
Input input(const char* agent="hermes",const char* profile="default",Preference preference=Preference::Automatic) {
  Input value;assert(makeInput(value,"https://studio.example/base/","fixture.jwt.signature",profile,agent,27,preference)==Result::Ok);return value;
}
std::string models(const std::string& provider="my-provider",const std::string& model="my-model",const std::string& profile="default") {
  return "{\"profiles\":[{\"profile\":\""+profile+"\",\"default\":\""+model+"\",\"default_provider\":\""+provider+
      "\",\"groups\":[{\"provider\":\""+provider+"\",\"api_mode\":\"chat_completions\",\"models\":[\""+model+
      "\"],\"api_key\":\"PRIVATE_API_SENTINEL\",\"base_url\":\"https://private.invalid\",\"model_meta\":{\""+model+
      "\":{\"disabled\":false,\"hidden\":false}}}]}]}";
}
const char* emptyModels="{\"profiles\":[{\"profile\":\"default\",\"default\":\"\",\"default_provider\":\"\",\"groups\":[]}]}";
std::string perModelResponse(const std::string& map,const std::string& scalar);
Reply account(){return Reply(Stage::Account,"{\"user\":{\"id\":17,\"username\":\"tester\",\"role\":\"user\",\"avatar\":\"ignored\"}}");}
Reply tools(const char* agent,bool installed=true){return Reply(Stage::Installed,std::string("{\"tools\":[{\"id\":\"")+agent+"\",\"installed\":"+(installed?"true":"false")+",\"path\":\"ignored\"}]}");}
Reply catalog(const char* agent,const char* status="ready") {
  return Reply(Stage::NativeModels,std::string("{\"agents\":[{\"agentId\":\"")+agent+"\",\"status\":\""+status+
      "\",\"models\":[{\"id\":\"native-default\",\"hidden\":false,\"resolvedModel\":\"not-an-id\"}]}]}");
}
void allRoutes() {
  for(const auto& route:official_studio::kAgentRoutes) {
    for(size_t step:{size_t(1),size_t(17),size_t(65536)}) {
      Fixture fixture;fixture.step=step;fixture.replies.push_back(account());
      if(route.scoped)fixture.replies.emplace_back(Stage::Models,models());
      if(!std::strcmp(route.source,"coding_agent"))fixture.replies.push_back(tools(route.id));
      if(!route.scoped)fixture.replies.push_back(catalog(route.id,"unsupported"));
      auto binding=input(route.id);const auto output=resolve(binding,fixture);
      if(output.result!=Result::Ok)std::fprintf(stderr,"Agent %s failed result=%u stage=%u\n",route.id,unsigned(output.result),unsigned(output.stage));
      assert(output.result==Result::Ok&&output.stage==Stage::Complete&&fixture.call==fixture.replies.size());
      assert(output.epoch==27&&std::strcmp(output.accountId.data(),"17")==0);
      assert(output.preference==Preference::Automatic);
      assert(output.jsonPeakBytes>0&&output.jsonPeakBytes<=kJsonMemoryBytes);
      assert(std::strcmp(output.agentId.data(),route.id)==0&&std::strcmp(output.profile.data(),"default")==0);
      assert(fixture.paths.front()=="/api/auth/me");
      assert(output.mode==(route.scoped?official_studio::Mode::Scoped:official_studio::Mode::Global));
      if(route.scoped) {
        assert(output.selection.result==official_studio_models::Result::Ok);
        assert(std::strcmp(output.selection.provider.data(),"my-provider")==0);
        assert(std::strcmp(output.selection.model.data(),"my-model")==0);
        assert(std::strcmp(output.selection.apiMode.data(),"chat_completions")==0);
      } else {
        official_studio::Context context;context.machineId="machine";context.accountId="17";context.profile="default";
        context.agentId=route.id;context.sessionId="real-session";context.connectionEpoch=27;context.mode=output.mode;
        assert(output.readiness(context).result==official_studio::Result::Ok);
        ++context.connectionEpoch;assert(output.readiness(context).result==official_studio::Result::StaleContext);
        --context.connectionEpoch;context.profile="other";assert(output.readiness(context).result==official_studio::Result::StaleContext);
      }
    }
  }
}
void scopedAndNative() {
  {
    // Metadata may precede models/provider/default fields in any JSON order.
    const std::string response="{\"profiles\":[{\"groups\":[{\"model_meta\":{\"first\":{\"disabled\":true},\"second\":{\"hidden\":false}},\"models\":[\"first\",\"second\"],\"api_mode\":\"anthropic_messages\",\"provider\":\"p\"}],\"default_provider\":\"p\",\"default\":\"first\",\"profile\":\"default\"}]}";
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,response)};
    const auto output=resolve(input(),fixture);
    assert(output.result==Result::Ok&&std::strcmp(output.selection.model.data(),"second")==0);
    assert(std::strcmp(output.selection.apiMode.data(),"anthropic_messages")==0);
    assert(output.selection.origin==official_studio_models::Origin::ProviderFallback);
  }
  for(const char* status:{"ready","empty","unsupported","not_installed","auth_required","timeout","error"}) {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,emptyModels),tools("codex"),catalog("codex",status)};
    const auto output=resolve(input("codex"),fixture);
    const bool ready=!std::strcmp(status,"ready")||!std::strcmp(status,"empty")||!std::strcmp(status,"unsupported");
    const bool temporary=!std::strcmp(status,"timeout")||!std::strcmp(status,"error");
    assert(output.result==(ready?Result::Ok:temporary?Result::Transient:Result::Unavailable));
    assert(output.mode==official_studio::Mode::Global&&fixture.call==4);
    if(temporary) {
      assert(output.retryAfterMs==5000);
      assert(official_selection_notice::classify(output)==NoticeReason::NativeUnavailable);
    }
    if(!std::strcmp(status,"auth_required")) {
      assert(output.nativeResult==official_studio::Result::AuthenticationRequired);
      assert(official_selection_notice::classify(output)==NoticeReason::AgentAuth);
    }
    if(!std::strcmp(status,"not_installed"))
      assert(official_selection_notice::classify(output)==NoticeReason::NotInstalled);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models()),tools("claude-code",false)};
    const auto output=resolve(input("claude-code"),fixture);
    assert(output.result==Result::Unavailable&&fixture.call==3);
    assert(official_selection_notice::classify(output)==NoticeReason::NotInstalled);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models("my-provider","my-model","other"))};
    const auto output=resolve(input("codex"),fixture);
    assert(output.result==Result::Unavailable&&fixture.call==2);
    assert(official_selection_notice::classify(output)==NoticeReason::ProfileUnavailable);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,"{\"profiles\":false}")};
    assert(resolve(input("codex"),fixture).result==Result::Invalid&&fixture.call==2);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models("claude-oauth")),tools("claude-code"),catalog("claude-code")};
    const auto output=resolve(input("claude-code"),fixture);
    assert(output.result==Result::Ok&&output.mode==official_studio::Mode::Global);
  }
}
void explicitStudioStaysScoped() {
  // Exact public schema of the observed failure: the official Profile uses
  // OAuth Codex, while a compatible configured provider also exists. Match
  // the official UI, mark the compatible identity, and never switch to Native.
  const char* body=R"({"profiles":[{"profile":"default","default":"gpt-6.1-sol",
    "default_provider":"openai-codex","groups":[
    {"provider":"openai-codex","api_mode":"codex_responses","models":["gpt-6.1-sol"]},
    {"provider":"custom:other-provider","api_mode":"chat_completions","models":["other-model"]}]}]})";
  Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body),tools("codex")};
  const auto output=resolve(input("codex","default",Preference::Studio),fixture);
  assert(output.result==Result::Ok&&fixture.call==3);
  assert(output.preference==Preference::Studio);
  assert(output.selection.origin==official_studio_models::Origin::CompatibleDefault);
  assert(std::strcmp(output.selection.provider.data(),"custom:other-provider")==0);
  assert(std::strcmp(output.selection.model.data(),"other-model")==0);
  assert(output.mode==official_studio::Mode::Scoped);
}
void officialFrontendCompatibleStudio() {
  // Signed official ChatPanel filters incompatible OAuth groups, then uses
  // response order. A later Ling group must not override the first compatible
  // configured group or replace its matching Profile default model.
  const char* body=R"({"profiles":[{"profile":"default","default":"gpt-6.1-sol",
    "default_provider":"openai-codex","groups":[
    {"provider":"openai-codex","api_mode":"codex_responses","models":["gpt-6.1-sol"]},
    {"provider":"claude-oauth","api_mode":"anthropic_messages","models":["claude-default"]},
    {"provider":"custom:empty","api_mode":"chat_completions","models":[]},
    {"provider":"custom:fun-codex","api_mode":null,
      "api_mode_by_model":{"hidden-first":"chat_completions","disabled-first":"chat_completions","gpt-6.1-sol":"codex_responses"},
      "models":["hidden-first","disabled-first","gpt-6.1-sol"],
      "model_meta":{"hidden-first":{"hidden":true},"disabled-first":{"disabled":true}},
      "api_key":"PRIVATE_COMPATIBLE_KEY","base_url":"https://private.invalid"},
    {"provider":"custom:ling","api_mode":"chat_completions","models":["Ling-3.2-flash"]}]}]})";
  for(const char* agent:{"claude-code","codex"})for(size_t step:{size_t(1),size_t(17),size_t(65536)}) {
    Fixture fixture;fixture.step=step;fixture.replies={account(),Reply(Stage::Models,body),tools(agent)};
    const auto output=resolve(input(agent,"default",Preference::Studio),fixture);
    if(output.result!=Result::Ok)std::fprintf(stderr,"Official UI compatible Profile group rejected: result=%u calls=%zu\n",unsigned(output.result),fixture.call);
    assert(output.result==Result::Ok&&fixture.call==3&&output.mode==official_studio::Mode::Scoped);
    assert(std::strcmp(output.selection.provider.data(),"custom:fun-codex")==0);
    assert(std::strcmp(output.selection.model.data(),"gpt-6.1-sol")==0);
    assert(std::strcmp(output.selection.apiMode.data(),"codex_responses")==0);
    assert(output.selection.origin==official_studio_models::Origin::CompatibleDefault);
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
    const std::string raw(reinterpret_cast<const char*>(&output),sizeof(output));
    assert(raw.find("PRIVATE_COMPATIBLE_KEY")==std::string::npos&&raw.find("private.invalid")==std::string::npos);
  }
}
void compatibleStudioOrderAndFailureBoundaries() {
  const auto response=[]() {
    JsonDocument document;assert(!deserializeJson(document,models("openai-codex","gpt-6.1-sol")));
    JsonArray groups=document["profiles"][0]["groups"].as<JsonArray>();
    JsonObject first=groups.add<JsonObject>();first["provider"]="custom:fun-codex";
    first["models"][0]="first-visible";first["models"][1]="gpt-6.1-sol";
    first["api_mode"]=nullptr;first["api_mode_by_model"]["first-visible"]="anthropic_messages";
    first["api_mode_by_model"]["gpt-6.1-sol"]="codex_responses";
    JsonObject second=groups.add<JsonObject>();second["provider"]="custom:ling";
    second["models"][0]="gpt-6.1-sol";second["api_mode"]="chat_completions";
    return document;
  };
  for(int scenario=0;scenario<8;++scenario)for(size_t step:{size_t(1),size_t(65536)}) {
    auto document=response();JsonObject profile=document["profiles"][0].as<JsonObject>();
    JsonObject first=profile["groups"][1].as<JsonObject>();
    const char* expectedProvider="custom:fun-codex";const char* expectedModel="gpt-6.1-sol";
    const char* expectedMode="codex_responses";bool exact=false,unavailable=false;
    if(scenario==0) { // Server group order, never a provider-name allowlist.
      first["provider"]="custom:ling";profile["groups"][2]["provider"]="custom:fun-codex";
      expectedProvider="custom:ling";
    } else if(scenario==1) { // Default model is retained only within chosen group.
      first["models"][1]="another-visible";first["api_mode_by_model"]["another-visible"]="chat_completions";
      expectedModel="first-visible";expectedMode="anthropic_messages";
    } else if(scenario==2) {
      first["model_meta"]["gpt-6.1-sol"]["hidden"]=true;
      expectedModel="first-visible";expectedMode="anthropic_messages";
    } else if(scenario==3) { // Fully filtered groups are not the first usable group.
      first["model_meta"]["first-visible"]["disabled"]=true;
      first["model_meta"]["gpt-6.1-sol"]["hidden"]=true;
      expectedProvider="custom:ling";expectedMode="chat_completions";
    } else if(scenario==4) { // Legal configured default wins over response order.
      profile["default_provider"]="custom:ling";expectedProvider="custom:ling";
      expectedMode="chat_completions";exact=true;
    } else if(scenario==5) { // Existing but empty default provider stays selected, as ChatPanel.
      profile["default_provider"]="custom:fun-codex";first["models"].to<JsonArray>();unavailable=true;
    } else if(scenario==6) { // Missing chosen API mode must not try a later provider.
      first["api_mode"]="chat_completions";first["api_mode_by_model"].remove("gpt-6.1-sol");unavailable=true;
    } else { // The legal provider stays selected and its first selectable model is used.
      profile["default_provider"]="custom:fun-codex";
      first["model_meta"]["gpt-6.1-sol"]["disabled"]=true;
      expectedModel="first-visible";expectedMode="anthropic_messages";
    }
    std::string body;serializeJson(document,body);
    Fixture fixture;fixture.step=step;fixture.replies={account(),Reply(Stage::Models,body),tools("claude-code")};
    const auto output=resolve(input("claude-code","default",Preference::Studio),fixture);
    if(unavailable) {
      assert(output.result==Result::Unavailable&&fixture.call==2);
      assert(output.selection.identity().provider==nullptr&&output.mode==official_studio::Mode::Scoped);
      assert(official_selection_notice::classify(output)==(scenario==6?NoticeReason::ModelConfiguration:NoticeReason::NoCompatibleModel));
    } else {
      assert(output.result==Result::Ok&&fixture.call==3&&output.mode==official_studio::Mode::Scoped);
      assert(std::strcmp(output.selection.provider.data(),expectedProvider)==0);
      assert(std::strcmp(output.selection.model.data(),expectedModel)==0&&std::strcmp(output.selection.apiMode.data(),expectedMode)==0);
      assert(output.selection.origin==(exact?official_studio_models::Origin::ProfileDefault:official_studio_models::Origin::CompatibleDefault));
    }
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
  }
  Fixture noCompatible;noCompatible.replies={account(),Reply(Stage::Models,models("openai-codex","gpt-6.1-sol"))};
  const auto output=resolve(input("codex","default",Preference::Studio),noCompatible);
  assert(output.result==Result::Unavailable&&noCompatible.call==2&&output.mode==official_studio::Mode::Scoped);
  assert(output.selection.identity().provider==nullptr);
}
void explicitPreferenceRouteMatrix() {
  for(const auto& route:official_studio::kAgentRoutes) {
    for(auto preference:{Preference::Studio,Preference::Native}) {
      const bool supported=preference==Preference::Studio?route.scoped:route.global;
      Fixture fixture;fixture.step=1;
      if(supported) {
        fixture.replies.push_back(account());
        if(preference==Preference::Studio)fixture.replies.emplace_back(Stage::Models,models());
        if(!std::strcmp(route.source,"coding_agent"))fixture.replies.push_back(tools(route.id));
        if(preference==Preference::Native)fixture.replies.push_back(catalog(route.id));
      }
      const auto output=resolve(input(route.id,"default",preference),fixture);
      assert(output.preference==preference&&fixture.call==fixture.replies.size());
      if(!supported) {
        assert(output.result==Result::PreferenceUnavailable&&output.stage==Stage::Input&&fixture.call==0);
        assert(official_selection_notice::classify(output)==NoticeReason::PreferenceUnavailable);
      } else {
        assert(output.result==Result::Ok&&output.stage==Stage::Complete);
        assert(output.mode==(preference==Preference::Native?official_studio::Mode::Global:official_studio::Mode::Scoped));
        if(preference==Preference::Studio)assert(output.selection.origin==official_studio_models::Origin::ProfileDefault);
        else {
          assert(output.selection.provider[0]==0&&output.selection.model[0]==0&&output.selection.apiMode[0]==0);
          for(const auto& path:fixture.paths)assert(path.find("available-models")==std::string::npos);
        }
        assert(output.jsonPeakBytes<=kJsonMemoryBytes);
      }
    }
  }
}
void studioCompatibleDefaultsAndFailures() {
  for(int reason=0;reason!=4;++reason) {
    JsonDocument document;assert(!deserializeJson(document,models()));
    JsonObject profile=document["profiles"][0].as<JsonObject>();
    JsonObject group=profile["groups"][0].as<JsonObject>();
    if(reason==0)profile["default"]="not-available";
    if(reason==1)profile["default_provider"]="not-available";
    if(reason==2)group["model_meta"]["my-model"]["disabled"]=true;
    if(reason==3)group["model_meta"]["my-model"]["hidden"]=true;
    std::string body;serializeJson(document,body);
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body),tools("codex")};
    const auto output=resolve(input("codex","default",Preference::Studio),fixture);
    if(reason<2) {
      assert(output.result==Result::Ok&&fixture.call==3&&output.mode==official_studio::Mode::Scoped);
      assert(output.selection.origin==official_studio_models::Origin::CompatibleDefault);
      assert(std::strcmp(output.selection.provider.data(),"my-provider")==0&&std::strcmp(output.selection.model.data(),"my-model")==0);
    } else {
      assert(output.result==Result::Unavailable&&fixture.call==2);
      assert(official_selection_notice::classify(output)==NoticeReason::NoCompatibleModel);
    }
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,emptyModels)};
    const auto output=resolve(input("codex","default",Preference::Studio),fixture);
    assert(output.result==Result::Unavailable&&fixture.call==2); // No silent native fallback.
    assert(official_selection_notice::classify(output)==NoticeReason::ModelConfiguration);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models("my-provider","my-model","other-profile"))};
    const auto output=resolve(input("codex","default",Preference::Studio),fixture);
    assert(output.result==Result::Unavailable&&fixture.call==2);
    assert(official_selection_notice::classify(output)==NoticeReason::ProfileUnavailable);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,perModelResponse("{\"m\":\"anthropic_messages\"}","null"))};
    const auto output=resolve(input("hermes","default",Preference::Studio),fixture);
    assert(output.result==Result::Ok&&output.selection.origin==official_studio_models::Origin::ProfileDefault);
    assert(std::strcmp(output.selection.apiMode.data(),"anthropic_messages")==0);
  }
}
void nativeNeverFetchesProviderConfiguration() {
  for(size_t step:{size_t(1),size_t(17),size_t(65536)}) {
    auto native=catalog("codex");
    native.body.pop_back();native.body+=",\"api_key\":\"NATIVE_PRIVATE_SENTINEL\",\"ignored\":\""+std::string(50000,'x')+"\"}";
    Fixture fixture;fixture.step=step;fixture.replies={account(),tools("codex"),native};
    const auto output=resolve(input("codex","default",Preference::Native),fixture);
    assert(output.result==Result::Ok&&output.preference==Preference::Native&&output.mode==official_studio::Mode::Global);
    assert(fixture.call==3&&fixture.paths[1]=="/api/coding-agents?profile=default");
    assert(fixture.paths[2]=="/api/coding-agents/models?agent=codex&profile=default");
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
    const std::string raw(reinterpret_cast<const char*>(&output),sizeof(output));
    assert(raw.find("NATIVE_PRIVATE_SENTINEL")==std::string::npos&&raw.find("fixture.jwt")==std::string::npos);
    official_studio::Context context;context.machineId="machine";context.accountId="17";context.profile="default";
    context.agentId="codex";context.sessionId="existing-native-session";context.mode=output.mode;context.connectionEpoch=27;
    JsonDocument run;
    assert(official_studio::buildNativeGlobalRun(run,context,output.readiness(context),"hello","q1")==official_studio::Result::Ok);
    for(const char* absent:{"provider","apiMode","apiKey","api_key","baseUrl","base_url"})assert(run[absent].isUnbound());
  }
  for(const char* status:{"auth_required","not_installed","timeout","error"}) {
    Fixture fixture;fixture.replies={account(),tools("codex"),catalog("codex",status)};
    const auto output=resolve(input("codex","default",Preference::Native),fixture);
    const bool advisory=!std::strcmp(status,"auth_required")||!std::strcmp(status,"error");
    if(advisory) {
      assert(output.result==Result::Ok&&output.nativeDefaultAttemptPermitted&&fixture.call==3);
      assert(official_selection_notice::classify(output)==NoticeReason::None);continue;
    }
    const bool transient=!std::strcmp(status,"timeout");
    assert(output.result==(transient?Result::Transient:Result::Unavailable)&&fixture.call==3);
    assert(official_selection_notice::classify(output)==(transient?NoticeReason::NativeUnavailable:
        !std::strcmp(status,"auth_required")?NoticeReason::AgentAuth:NoticeReason::NotInstalled));
  }
  Fixture missing;missing.replies={account(),tools("codex",false)};
  assert(resolve(input("codex","default",Preference::Native),missing).result==Result::Unavailable&&missing.call==2);
}
void explicitNativeDefaultCatalogAdvisory() {
  // Official Cursor UI accepts the native CLI default even when this advisory
  // catalog says auth_required. Qoder may still fail its REAL run; admission
  // never proves provider authorization or changes to a different model.
  for(const char* agent:{"cursor","qoder"})for(const char* status:{"auth_required","error"}) {
    Fixture fixture;fixture.step=1;
    auto native=catalog(agent,status);
    const auto start=native.body.find("\"models\":[");assert(start!=std::string::npos);
    native.body.erase(start+10);native.body+="]}]}";
    fixture.replies={account(),tools(agent),native};
    const auto output=resolve(input(agent,"default",Preference::Native),fixture);
    assert(output.result==Result::Ok&&output.stage==Stage::Complete&&fixture.call==3);
    assert(output.nativeDefaultAttemptPermitted&&output.nativeResult==(!std::strcmp(status,"auth_required")?
        official_studio::Result::AuthenticationRequired:official_studio::Result::RetryableUnavailable));
    official_studio::Context context;context.machineId="machine";context.accountId="17";context.profile="default";
    context.agentId=agent;context.sessionId="owned-native-session";context.mode=output.mode;context.connectionEpoch=27;
    JsonDocument run;const auto readiness=output.readiness(context);assert(readiness.model==nullptr);
    assert(official_studio::buildNativeGlobalRun(run,context,readiness,"hello","q1")==official_studio::Result::Ok);
    assert(run["model"].isUnbound()&&run["provider"].isUnbound()&&run["apiMode"].isUnbound());
    assert(output.retryAfterMs==0&&output.jsonPeakBytes<=kJsonMemoryBytes);
    auto stale=context;++stale.connectionEpoch;
    assert(output.readiness(stale).result==official_studio::Result::StaleContext);
    stale=context;stale.accountId="18";
    assert(output.readiness(stale).result==official_studio::Result::StaleContext);
    stale=context;stale.profile="other";
    assert(output.readiness(stale).result==official_studio::Result::StaleContext);
    stale=context;stale.agentId="codex";
    assert(output.readiness(stale).result==official_studio::Result::StaleContext);
    auto automatic=output;automatic.preference=Preference::Automatic;
    assert(automatic.readiness(context).result==output.nativeResult);
  }
  for(const char* status:{"auth_required","error"}) {
    Fixture automatic;automatic.replies={account(),tools("cursor"),catalog("cursor",status)};
    const auto output=resolve(input("cursor","default",Preference::Automatic),automatic);
    assert(output.result!=Result::Ok&&!output.nativeDefaultAttemptPermitted);
  }
  for(int http:{401,403,408,429,500}) {
    Fixture denied;auto native=catalog("cursor","auth_required");native.status=http;
    denied.replies={account(),tools("cursor"),native};
    const auto output=resolve(input("cursor","default",Preference::Native),denied);
    assert(output.result!=Result::Ok&&!output.nativeDefaultAttemptPermitted&&output.httpStatus==http);
  }
  for(const char* status:{"not_installed","timeout"}) {
    Fixture denied;denied.replies={account(),tools("cursor"),catalog("cursor",status)};
    const auto output=resolve(input("cursor","default",Preference::Native),denied);
    assert(output.result!=Result::Ok&&!output.nativeDefaultAttemptPermitted);
  }
  for(const auto& body:{
      std::string("{\"agents\":[{\"agentId\":\"other\",\"status\":\"auth_required\",\"models\":[]}]}"),
      std::string("{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"auth_required\",\"models\":null}]}"),
      std::string("{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"auth_required\",\"models\":[]},{\"agentId\":\"cursor\",\"status\":\"auth_required\",\"models\":[]}]}"),
      std::string("{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"auth_required\",\"models\":[{\"id\":\"m\",\"hidden\":null}]}]}"),
      std::string("{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"auth_required\",\"models\":[]}]} trailing")}) {
    Fixture invalid;invalid.replies={account(),tools("cursor"),Reply(Stage::NativeModels,body)};
    const auto output=resolve(input("cursor","default",Preference::Native),invalid);
    assert(output.result==Result::Invalid&&!output.nativeDefaultAttemptPermitted);
  }
  Fixture interrupted;auto native=catalog("cursor","auth_required");native.complete=false;
  interrupted.replies={account(),tools("cursor"),native};
  const auto output=resolve(input("cursor","default",Preference::Native),interrupted);
  assert(output.result==Result::Transient&&!output.nativeDefaultAttemptPermitted);
}
void invalidPreferenceRejected() {
  auto value=input();value.preference=static_cast<Preference>(255);Fixture fixture;
  assert(!validInput(value)&&resolve(value,fixture).result==Result::Invalid&&fixture.call==0);
  assert(makeInput(value,"http://host","token","default","codex",1,static_cast<Preference>(255))==Result::Invalid);
  assert(value.userJwt[0]==0&&value.epoch==0&&value.preference==Preference::Automatic);
}
void studio0822PerModelProjection() {
  const char* body=R"({"profiles":[{"profile":"default","default":"deepseek-flash",
    "default_provider":"custom:deepseek1017","groups":[{"provider":"custom:deepseek1017",
    "api_mode":null,"api_mode_by_model":{"deepseek-flash":"anthropic_messages",
    "deepseek-v4-pro":"chat_completions"},"models":["deepseek-flash","deepseek-v4-pro"]}]}]})";
  for(size_t step:{size_t(1),size_t(17),size_t(65536)}) {
    Fixture fixture;fixture.step=step;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input("ekko-agent"),fixture);
    if(output.result!=Result::Ok)std::fprintf(stderr,"Official 0.8.22 stream rejected: result=%u stage=%u model=%u\n",
        unsigned(output.result),unsigned(output.stage),unsigned(output.selection.result));
    assert(output.result==Result::Ok&&output.stage==Stage::Complete&&fixture.call==2);
    assert(std::strcmp(output.selection.apiMode.data(),"anthropic_messages")==0);
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
  }
}
std::string perModelResponse(const std::string& map, const std::string& scalar="\"chat_completions\"") {
  return "{\"profiles\":[{\"profile\":\"default\",\"default\":\"m\",\"default_provider\":\"p\",\"groups\":[{\"provider\":\"p\",\"models\":[\"m\"],\"api_mode\":"+
      scalar+",\"api_mode_by_model\":"+map+"}]}]}";
}
void perModelProjectionBoundaries() {
  using M=official_studio_models::Result;
  for(size_t step:{size_t(1),size_t(17),size_t(65536)}) {
    for(const char* map:{R"({"m":null})",R"({})",R"({"other-model":"codex_responses"})",R"({"m":""})"}) {
      Fixture fixture;fixture.step=step;fixture.replies={account(),Reply(Stage::Models,perModelResponse(map))};
      const auto output=resolve(input("ekko-agent"),fixture);
      assert(output.result==Result::Unavailable&&output.selection.result==M::NeedsConfiguration&&fixture.call==2);
    }
    for(const char* map:{R"({"m":"anthropic_messages"})",R"({"m":"codex_responses","unused":null})"}) {
      Fixture fixture;fixture.step=step;
      fixture.replies={account(),Reply(Stage::Models,perModelResponse(map,"\"codex_app_server\""))};
      const auto output=resolve(input("ekko-agent"),fixture);
      assert(output.result==Result::Ok&&output.selection.result==M::Ok);
      assert(std::strcmp(output.selection.apiMode.data(),"codex_app_server")!=0);
      assert(output.jsonPeakBytes<=kJsonMemoryBytes);
    }
    for(const char* map:{"null","[]","true","42",R"("chat_completions")",
        R"({"m":true})",R"({"m":42})",R"({"m":{}})",R"({"m":[]})",
        R"({"m":"chat_completions\u0000evil"})",R"({"m\u0000evil":"chat_completions"})",
        R"({"":"chat_completions"})",R"({"m":null,"m":"chat_completions"})",
        R"({"m":"chat_completions","m":null})"}) {
      Fixture fixture;fixture.step=step;fixture.replies={account(),Reply(Stage::Models,perModelResponse(map))};
      const auto output=resolve(input("ekko-agent"),fixture);
      assert(output.result==Result::Invalid&&output.selection.result!=M::Ok&&fixture.call==2);
    }
  }
  {
    // A map can precede models/defaults. Disabled default falls back only
    // within its provider, and mode lookup follows that actual selected model.
    const char* body=R"({"profiles":[{"groups":[{"api_mode_by_model":{"disabled":"chat_completions","enabled":"anthropic_messages"},
      "model_meta":{"disabled":{"disabled":true}},"models":["disabled","enabled"],"provider":"p"}],
      "default":"disabled","default_provider":"p","profile":"default"}]})";
    Fixture fixture;fixture.step=1;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input("ekko-agent"),fixture);
    assert(output.result==Result::Ok&&std::strcmp(output.selection.model.data(),"enabled")==0);
    assert(std::strcmp(output.selection.apiMode.data(),"anthropic_messages")==0);
  }
  {
    const auto map=R"({"m":"bedrock_converse"})";
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,perModelResponse(map))};
    const auto output=resolve(input("ekko-agent"),fixture);
    assert(output.result==Result::Invalid&&output.selection.result==M::UnsupportedApiMode);
  }
  for(size_t bytes:{official_studio_models::kApiModeMaxBytes,official_studio_models::kApiModeMaxBytes+1}) {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,perModelResponse("{\"m\":\""+std::string(bytes,'x')+"\"}"))};
    const auto output=resolve(input("ekko-agent"),fixture);
    assert(output.result==Result::Invalid);
    if(bytes==official_studio_models::kApiModeMaxBytes)assert(output.selection.result==M::UnsupportedApiMode);
  }
  for(size_t bytes:{official_studio_models::kModelMaxBytes,official_studio_models::kModelMaxBytes+1}) {
    JsonDocument document;assert(!deserializeJson(document,perModelResponse("{}")));
    const std::string model(bytes,'m');JsonObject group=document["profiles"][0]["groups"][0].as<JsonObject>();
    document["profiles"][0]["default"]=model;group["models"][0]=model;
    group["api_mode_by_model"][model]="anthropic_messages";
    std::string body;serializeJson(document,body);
    Fixture fixture;fixture.step=1;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input("ekko-agent"),fixture);
    assert(output.result==(bytes==official_studio_models::kModelMaxBytes?Result::Ok:Result::Invalid));
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
  }
  {
    auto body=perModelResponse("{\"m\":\"chat_completions\"}");
    const auto field=body.find("\"api_mode_by_model\":");assert(field!=std::string::npos);
    body.insert(field,"\"api_mode_by_model\":null,");
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    assert(resolve(input("ekko-agent"),fixture).result==Result::Invalid);
  }
  {
    // Retaining mode metadata uses the same 32KiB aggregate allocator cap.
    // More map entries must fail explicitly, never create an unbounded cache.
    std::string map="{\"m\":\"anthropic_messages\"";
    for(unsigned n=0;n!=5000;++n)map+=",\"unused-"+std::to_string(n)+"\":\"chat_completions\"";
    map+='}';Fixture fixture;fixture.step=1023;
    fixture.replies={account(),Reply(Stage::Models,perModelResponse(map))};
    const auto output=resolve(input("ekko-agent"),fixture);
    assert(output.result==Result::NoMemory&&output.selection.result!=M::Ok);
    assert(output.jsonPeakBytes<=kJsonMemoryBytes);
  }
  std::puts("Per-model mode projection: official 0.8.22 shape, precedence/null/visibility, bounded map, malformed/NUL/duplicate/oversize rejection passed");
}
void invalidAndCompleteResponses() {
  const std::vector<std::string> invalidAccounts{
    "{\"user\":{\"id\":17,\"id\":null}}", "{\"user\":{\"id\":null,\"id\":17}}",
    "{\"user\":{\"id\":\"17\"}}", "{\"user\":{\"id\":0}}", "{\"user\":{\"id\":-1}}",
    "{\"user\":{\"id\":1.2}}", "{\"user\":{\"id\":1e2}}", "{\"user\":{\"id\":9007199254740992}}",
    "{\"user\":{\"id\":17},\"user\":null}", "[]", "null", "{\"user\":{\"id\":17}} garbage"
  };
  for(const auto& body:invalidAccounts) {
    Fixture fixture;fixture.replies.emplace_back(Stage::Account,body);
    assert(resolve(input(),fixture).result==Result::Invalid&&fixture.call==1);
  }
  const std::vector<std::string> invalidModels{
    "{\"profiles\":[],\"profiles\":null}",
    "{\"profiles\":[{\"profile\":\"default\",\"default\":\"m\",\"default\":null,\"default_provider\":\"p\",\"groups\":[]}]}",
    "{\"profiles\":[{\"profile\":\"default\",\"default\":\"m\",\"default_provider\":\"p\",\"groups\":[{\"provider\":\"p\",\"models\":[\"m\"],\"api_mode\":\"chat_completions\",\"model_meta\":{\"m\":{\"disabled\":false,\"disabled\":null}}}]}]}",
    models()+"{}", models()+std::string("\0",1), models().substr(0,models().size()-1)
  };
  for(const auto& body:invalidModels) {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input(),fixture);assert(output.result==Result::Invalid&&fixture.call==2);
    assert(output.selection.result!=official_studio_models::Result::Ok);
  }
  for(const char* body:{
      "{\"tools\":[{\"id\":\"codex\",\"installed\":true,\"installed\":null}]}",
      "{\"tools\":[{\"id\":\"codex\",\"installed\":true},{\"id\":\"codex\",\"installed\":false}]}",
      "{\"tools\":[{\"id\":\"codex\",\"installed\":\"true\"}]}"}) {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models()),Reply(Stage::Installed,body)};
    assert(resolve(input("codex"),fixture).result==Result::Invalid);
  }
  for(const char* body:{
      "{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"surprise\",\"models\":[]}]}",
      "{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"ready\",\"models\":[{}]}]}",
      "{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"ready\",\"models\":false}]}",
      "{\"agents\":[{\"agentId\":\"cursor\",\"status\":\"ready\",\"models\":[]},{\"agentId\":\"cursor\",\"status\":\"empty\",\"models\":[]}]}"}) {
    Fixture fixture;fixture.replies={account(),tools("cursor"),Reply(Stage::NativeModels,body)};
    assert(resolve(input("cursor"),fixture).result==Result::Invalid);
  }
  for(const std::string& type:{std::string("text/html"),std::string("application/jsonp"),std::string("")}) {
    Fixture fixture;auto reply=account();reply.type=type;fixture.replies.push_back(reply);
    assert(resolve(input(),fixture).result==Result::Invalid&&fixture.call==1);
  }
  for(int mismatch:{-1,1}) {
    Fixture fixture;auto reply=account();reply.length=int64_t(reply.body.size())+mismatch;fixture.replies.push_back(reply);
    const auto output=resolve(input(),fixture);
    assert(output.result==(mismatch<0?Result::Invalid:Result::Transient));
  }
  {
    Fixture fixture;auto reply=account();reply.complete=false;fixture.replies.push_back(reply);
    assert(resolve(input(),fixture).result==Result::Transient);
  }
  {
    Fixture fixture;auto reply=account();reply.length=-1;reply.type="APPLICATION/JSON";
    fixture.replies={reply,Reply(Stage::Models,models())};assert(resolve(input(),fixture).result==Result::Ok);
  }
}
void classificationAndCancellation() {
  for(int code:{401,403,404,302,400,408,429,500,-1,-11}) {
    Fixture fixture;auto reply=account();reply.status=code;reply.retry=90000;fixture.replies.push_back(reply);
    const auto output=resolve(input(),fixture);
    const auto expected=code==401||code==403||code==404?Result::AuthInvalid:
        code<=0||code==408||code==429||code>=500?Result::Transient:Result::Invalid;
    assert(output.result==expected&&output.httpStatus==code&&fixture.call==1);
    if(expected==Result::Transient)assert(output.retryAfterMs==30000);
  }
  {
    Fixture fixture;auto denied=Reply(Stage::Models,"{}");denied.status=403;fixture.replies={account(),denied};
    const auto output=resolve(input(),fixture);
    assert(output.result==Result::Unavailable); // Profile denial must not expire a valid account token.
    assert(official_selection_notice::classify(output)==NoticeReason::AccessDenied);
  }
  {
    Flag flag;flag.value=true;Fixture fixture;
    assert(resolve(input(),fixture,&flag).result==Result::Cancelled&&fixture.call==0);
  }
  {
    Flag flag;Fixture fixture;fixture.cancellation=&flag;fixture.cancelAt=10;fixture.step=1;
    fixture.replies={account()};assert(resolve(input(),fixture,&flag).result==Result::Cancelled&&fixture.call==1);
  }
}
void selectionNotices() {
  using N=official_studio::Result;
  using M=official_studio_models::Result;
  struct Case {Result result;Stage stage;int http;N native;M model;NoticeReason reason;};
  const Case cases[]{
    {Result::Ok,Stage::Complete,200,N::NativeNotReady,M::InvalidResponse,NoticeReason::None},
    {Result::Cancelled,Stage::NativeModels,403,N::AuthenticationRequired,M::NotAvailable,NoticeReason::Cancelled},
    {Result::AuthInvalid,Stage::Account,403,N::NotInstalled,M::NotAvailable,NoticeReason::StudioAuth},
    {Result::NoMemory,Stage::Models,200,N::NotInstalled,M::OutOfMemory,NoticeReason::NoMemory},
    {Result::Transient,Stage::NativeModels,429,N::RetryableUnavailable,M::NotAvailable,NoticeReason::RateLimited},
    {Result::Transient,Stage::NativeModels,200,N::RetryableUnavailable,M::NotAvailable,NoticeReason::NativeUnavailable},
    {Result::Transient,Stage::Models,-1,N::RetryableUnavailable,M::NotAvailable,NoticeReason::BackendUnavailable},
    {Result::Unavailable,Stage::Models,403,N::NotInstalled,M::NotAvailable,NoticeReason::AccessDenied},
    {Result::Unavailable,Stage::Installed,404,N::NotInstalled,M::NotAvailable,NoticeReason::MissingResource},
    {Result::Unavailable,Stage::Installed,200,N::NotInstalled,M::Ok,NoticeReason::NotInstalled},
    {Result::Unavailable,Stage::NativeModels,200,N::AuthenticationRequired,M::NotAvailable,NoticeReason::AgentAuth},
    {Result::Unavailable,Stage::NativeModels,200,N::NativeModelUnavailable,M::Ok,NoticeReason::NativeModelUnavailable},
    {Result::Unavailable,Stage::Models,200,N::NativeNotReady,M::ProfileNotAvailable,NoticeReason::ProfileUnavailable},
    {Result::Unavailable,Stage::Models,200,N::NativeNotReady,M::NeedsConfiguration,NoticeReason::ModelConfiguration},
    {Result::Unavailable,Stage::Models,200,N::NativeNotReady,M::NotAvailable,NoticeReason::NoCompatibleModel},
    {Result::Invalid,Stage::Models,200,N::NativeNotReady,M::UnsupportedApiMode,NoticeReason::ApiModeUnsupported},
    {Result::Invalid,Stage::Models,200,N::NativeNotReady,M::IdentityTooLong,NoticeReason::IdentityTooLong},
    {Result::Invalid,Stage::Account,200,N::NativeNotReady,M::InvalidResponse,NoticeReason::InvalidConfiguration},
    {Result::Unavailable,Stage::Input,0,N::NativeNotReady,M::InvalidResponse,NoticeReason::AgentUnavailable},
    {Result::DefaultModelIncompatible,Stage::Models,200,N::NativeNotReady,M::Ok,NoticeReason::DefaultModelIncompatible},
    {Result::PreferenceUnavailable,Stage::Input,0,N::NativeNotReady,M::InvalidResponse,NoticeReason::PreferenceUnavailable},
    // Details from a different/not-yet-run stage cannot override this result.
    {Result::Unavailable,Stage::Models,200,N::NotInstalled,M::InvalidResponse,NoticeReason::AgentUnavailable},
    {Result::Unavailable,Stage::Installed,200,N::NativeNotReady,M::NeedsConfiguration,NoticeReason::AgentUnavailable},
    {Result::Unavailable,Stage::NativeModels,0,N::AuthenticationRequired,M::NotAvailable,NoticeReason::AgentUnavailable},
  };
  for(const auto& item:cases) {
    Output output;output.result=item.result;output.stage=item.stage;output.httpStatus=item.http;
    output.nativeResult=item.native;output.selection.result=item.model;
    std::strcpy(output.accountId.data(),"PRIVATE_ACCOUNT");std::strcpy(output.profile.data(),"PRIVATE_PROFILE");
    std::strcpy(output.agentId.data(),"PRIVATE_AGENT");std::strcpy(output.selection.provider.data(),"PRIVATE_PROVIDER");
    std::strcpy(output.selection.model.data(),"PRIVATE_MODEL");
    assert(official_selection_notice::classify(output)==item.reason);
    const auto notice=official_selection_notice::describe(item.reason);
    assert(notice.code&&*notice.code&&notice.text);
    for(const char* p=notice.code;*p;++p)assert((*p>='a'&&*p<='z')||*p=='_');
    assert(!std::strstr(notice.code,"PRIVATE")&&!std::strstr(notice.text,"PRIVATE"));
    assert(native_chat::validUtf8(notice.text,std::strlen(notice.text)));
    // Accepted 12px font: carousel 168px and chat 219px are both one line.
    unsigned width=0;
    for(size_t at=0;notice.text[at];) {
      const auto lead=static_cast<uint8_t>(notice.text[at]);
      width+=lead<128?6:12;at+=native_chat::codepointBytes(lead);
    }
    assert(width<=168);
  }
  Output output;
  assert(official_selection_notice::nativeDiagnostic(output)==-1&&official_selection_notice::modelDiagnostic(output)==-1);
  output.stage=Stage::Models;output.httpStatus=200;output.selection.result=M::NeedsConfiguration;
  assert(official_selection_notice::modelDiagnostic(output)==int(M::NeedsConfiguration));
  assert(official_selection_notice::nativeDiagnostic(output)==-1);
  output.stage=Stage::NativeModels;output.nativeResult=N::AuthenticationRequired;
  assert(official_selection_notice::nativeDiagnostic(output)==int(N::AuthenticationRequired));
  assert(official_selection_notice::modelDiagnostic(output)==-1);
  output.stage=Stage::Installed;output.nativeResult=N::NotInstalled;
  assert(official_selection_notice::nativeDiagnostic(output)==int(N::NotInstalled));
  output.httpStatus=403;assert(official_selection_notice::nativeDiagnostic(output)==-1);
  for(const auto& fixtureCase:std::vector<std::pair<std::string,NoticeReason>>{
      {emptyModels,NoticeReason::ModelConfiguration},
      {models("p","m","other"),NoticeReason::ProfileUnavailable},
      {models("p","m"),NoticeReason::NoCompatibleModel}}) {
    std::string body=fixtureCase.first;
    if(fixtureCase.second==NoticeReason::NoCompatibleModel) {
      const auto flag=body.find("\"disabled\":false");assert(flag!=std::string::npos);
      body.replace(flag,std::strlen("\"disabled\":false"),"\"disabled\":true");
    }
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    assert(official_selection_notice::classify(resolve(input(),fixture))==fixtureCase.second);
  }
  std::puts("Selection notices: exact CLI/auth/model/Profile causes, stage guards and fixed safe diagnostics passed");
}
void memoryAndIdentity() {
  {
    std::string privateValue(1100000,'s');
    std::string modelResponse=models();const auto secret=modelResponse.find("PRIVATE_API_SENTINEL");
    assert(secret!=std::string::npos);modelResponse.replace(secret,std::strlen("PRIVATE_API_SENTINEL"),privateValue);
    Fixture fixture;fixture.step=1023;
    fixture.replies={Reply(Stage::Account,"{\"user\":{\"id\":17,\"avatar\":\""+privateValue+"\"}}"),
      Reply(Stage::Models,modelResponse)};
    auto output=resolve(input(),fixture);assert(output.result==Result::Ok&&output.jsonPeakBytes<=kJsonMemoryBytes);
    assert(std::strcmp(output.selection.model.data(),"my-model")==0);
    const std::string raw(reinterpret_cast<const char*>(&output),sizeof(output));
    assert(raw.find("PRIVATE_API_SENTINEL")==std::string::npos&&raw.find("private.invalid")==std::string::npos&&raw.find("fixture.jwt")==std::string::npos);
  }
  {
    const std::string longestModel(512,'m'),longestProvider(256,'p');
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,models(longestProvider,longestModel))};
    const auto output=resolve(input(),fixture);assert(output.result==Result::Ok);
    assert(std::string(output.selection.provider.data())==longestProvider&&std::string(output.selection.model.data())==longestModel);
  }
  {
    // Opaque model IDs, dictionary keys and values use decoded UTF-8, not JSON
    // path syntax, aliases or the native resolvedModel display metadata.
    JsonDocument document;auto profile=document["profiles"][0].to<JsonObject>();
    profile["profile"]="default";profile["default"]="模型/\"quoted\"\\path";profile["default_provider"]="厂商/one";
    auto group=profile["groups"][0].to<JsonObject>();group["provider"]="厂商/one";group["api_mode"]="codex_responses";
    group["models"][0]="模型/\"quoted\"\\path";group["model_meta"]["模型/\"quoted\"\\path"]["hidden"]=false;
    std::string body;serializeJson(document,body);Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input(),fixture);
    assert(output.result==Result::Ok&&std::strcmp(output.selection.model.data(),"模型/\"quoted\"\\path")==0);
    assert(std::strcmp(output.selection.provider.data(),"厂商/one")==0);
  }
  {
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,"{\"profiles\":[],\"ignored\":\"bad\\uD800\"}")};
    assert(resolve(input(),fixture).result==Result::Invalid); // Even ignored strings must be valid JSON/Unicode.
  }
  for(size_t depth:{size_t(15),size_t(16)}) {
    std::string nested="0";
    for(size_t n=0;n<depth;++n)nested="{\"x\":"+nested+"}";
    std::string body=models();body.pop_back();body+=",\"ignored\":"+nested+"}";
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    assert(resolve(input(),fixture).result==(depth==15?Result::Ok:Result::Invalid));
  }
  {
    std::string body="{\"profiles\":[{\"profile\":\"default\",\"default\":\"m0\",\"default_provider\":\"p\",\"groups\":[{\"provider\":\"p\",\"api_mode\":\"chat_completions\",\"models\":[";
    for(unsigned n=0;n<5000;++n){if(n)body+=',';body+='"';body+="model-"+std::to_string(n);body+='"';}
    body+="]}]}]}";
    Fixture fixture;fixture.replies={account(),Reply(Stage::Models,body)};
    const auto output=resolve(input(),fixture);assert(output.result==Result::NoMemory&&output.jsonPeakBytes<=kJsonMemoryBytes);
  }
  {
    auto binding=input("hermes","二楼 A");Fixture fixture;
    fixture.replies={account(),Reply(Stage::Models,models("p","m","二楼 A"))};
    assert(resolve(binding,fixture).result==Result::Ok);
    assert(fixture.paths[1]=="/api/hermes/available-models?profile=%E4%BA%8C%E6%A5%BC%20A");
  }
  for(const char* base:{"http://user:pass@host","http://host/?token=x","http://host/#x","http://host/\r\nx","ftp://host","http:///bad","http://:80"}) {
    auto value=input();assert(makeInput(value,base,"token","default","hermes",1)==Result::Invalid&&!validInput(value));
  }
  for(const char* token:{"a b","a\rb","token:abc",""}) {
    Input value;assert(makeInput(value,"http://host",token,"default","hermes",1)==Result::Invalid);
  }
  auto value=input();assert(makeInput(value,"http://host","token","default","Claude",1)==Result::Invalid);
  assert(makeInput(value,"http://host","token","default","hermes",0)==Result::Invalid);
  // Input fields may alias the previous owned value, including across fields.
  assert(makeInput(value,"http://host","token","codex","hermes",1)==Result::Ok);
  assert(makeInput(value,value.baseUrl.data(),value.userJwt.data(),value.agentId.data(),value.profile.data(),1)==Result::Ok);
  assert(std::strcmp(value.profile.data(),"hermes")==0&&std::strcmp(value.agentId.data(),"codex")==0);
  Job job;Output output;assert(!job.busy()&&job.poll(output)==Poll::Idle);
#ifndef ARDUINO
  assert(job.start(input())==StartResult::NotSupported&&!job.busy());
#endif
}
#ifndef ARDUINO
struct Gate {
  std::atomic<bool> entered{false},release{false},destroyed{false};
};
class GatedFixture final:public Transport {
 public:
  explicit GatedFixture(std::shared_ptr<Gate> gate):gate_(std::move(gate)) {
    fixture_.replies={account(),Reply(Stage::Models,models())};
  }
  ~GatedFixture()override{gate_->destroyed.store(true,std::memory_order_release);}
  bool get(const Request& request,ResponseSink& sink)override {
    if(!fixture_.call) {
      gate_->entered.store(true,std::memory_order_release);
      while(!gate_->release.load(std::memory_order_acquire))std::this_thread::yield();
      assert(std::strcmp(request.input->agentId.data(),"hermes")==0);
      assert(std::strcmp(request.input->userJwt.data(),"fixture.jwt.signature")==0);
      assert(std::strcmp(request.input->profile.data(),"default")==0);
    }
    return fixture_.get(request,sink);
  }
 private:
  std::shared_ptr<Gate> gate_;Fixture fixture_;
};
template<typename Predicate> void waitFor(Predicate predicate) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!predicate()) {
    assert(std::chrono::steady_clock::now()<end);
    std::this_thread::yield();
  }
}
void jobLifecycle() {
  for(unsigned pass=0;pass<20;++pass) {
    auto gate=std::make_shared<Gate>();Job job;auto original=input();
    auto transport=std::unique_ptr<Transport>(new GatedFixture(gate));
    assert(job.start(original,std::move(transport))==StartResult::Started&&job.busy());
    waitFor([&](){return gate->entered.load(std::memory_order_acquire);});
    Output output;assert(job.poll(output)==Poll::Pending);
    assert(job.start(input())==StartResult::Busy); // A second request cannot overwrite the first.
    original.userJwt.fill('z');original.userJwt.back()=0;original.profile[0]='X';original.agentId[0]='X';original.preference=Preference::Native;
    if(pass&1)job.cancel();
    gate->release.store(true,std::memory_order_release);
    waitFor([&](){return job.poll(output)==Poll::Ready;});
    assert(output.result==((pass&1)?Result::Cancelled:Result::Ok));
    assert(output.epoch==27&&std::strcmp(output.profile.data(),"default")==0&&std::strcmp(output.agentId.data(),"hermes")==0);
    assert(output.preference==Preference::Automatic);
    assert(gate->destroyed.load(std::memory_order_acquire)&&!job.busy()&&job.poll(output)==Poll::Idle);
  }
  for(unsigned pass=0;pass<20;++pass) {
    auto gate=std::make_shared<Gate>();
    {
      Job job;
      assert(job.start(input(),std::unique_ptr<Transport>(new GatedFixture(gate)))==StartResult::Started);
      waitFor([&](){return gate->entered.load(std::memory_order_acquire);});
    } // Nonblocking destructor only cancels/releases owner; worker still owns all data.
    assert(!gate->destroyed.load(std::memory_order_acquire));
    gate->release.store(true,std::memory_order_release);
    waitFor([&](){return gate->destroyed.load(std::memory_order_acquire);});
  }
}
#endif
} // namespace
int main(){explicitNativeDefaultCatalogAdvisory();officialFrontendCompatibleStudio();compatibleStudioOrderAndFailureBoundaries();allRoutes();scopedAndNative();explicitStudioStaysScoped();explicitPreferenceRouteMatrix();
  studioCompatibleDefaultsAndFailures();nativeNeverFetchesProviderConfiguration();invalidPreferenceRejected();
  studio0822PerModelProjection();perModelProjectionBoundaries();invalidAndCompleteResponses();classificationAndCancellation();selectionNotices();memoryAndIdentity();
#ifndef ARDUINO
  jobLifecycle();
#endif
}
