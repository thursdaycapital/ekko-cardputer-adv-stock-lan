#pragma once
#include "McuReauth.h"
#include <cstddef>
#include <cstring>

namespace device_web_target {
struct View { const char* key; const char* url; const char* source; };
inline bool bounded(const char* value, std::size_t maximum) {
  if (!value || !*value) return false;
  std::size_t size=0; while(size<=maximum && value[size]) ++size;
  return size<=maximum;
}
inline bool valid(View target) {
  if (!bounded(target.key,512) || !native_reauth::completeDeviceBinding(target.key) ||
      !bounded(target.url,512) || !target.source ||
      (std::strcmp(target.source,"lan") && std::strcmp(target.source,"remote"))) return false;
  const char* host=nullptr;
  if (!std::strncmp(target.url,"http://",7)) host=target.url+7;
  else if (!std::strncmp(target.url,"https://",8)) host=target.url+8;
  if (!host || !*host || *host=='/' || *host==':') return false;
  for (const unsigned char* p=reinterpret_cast<const unsigned char*>(target.url);*p;++p)
    if (*p<=0x20 || *p==0x7f || *p=='@' || *p=='\\' || *p=='#') return false;
  return true;
}
inline bool same(View submitted,View current) {
  return valid(submitted) && valid(current) && !std::strcmp(submitted.key,current.key) &&
      !std::strcmp(submitted.url,current.url) && !std::strcmp(submitted.source,current.source);
}
inline bool validProof(const char* proof) {
  if(!proof) return false;
  for(std::size_t n=0;n<32;++n) {
    const char c=proof[n];
    if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
  }
  return proof[32]=='\0';
}
inline bool profileMatches(View submitted,View pending,View loggedIn,
                           const char* formProof,const char* currentProof) {
  return same(submitted,pending) && same(submitted,loggedIn) && validProof(formProof) &&
      validProof(currentProof) && !std::strcmp(formProof,currentProof);
}
} // namespace device_web_target
