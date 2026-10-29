#include "UpdatePolicy.h"
#include <array>
#include <cctype>
#include <limits>

namespace veysona::update {
bool parseVersion(std::string_view text, Version& result) noexcept {
 if (!text.empty() && (text.front()=='v'||text.front()=='V')) text.remove_prefix(1);
 std::array<unsigned,3> parts{}; size_t offset=0;
 for (size_t i=0;i<3;++i) {
  const auto dot=text.find('.',offset);
  if ((i<2 && dot==std::string_view::npos) || (i==2 && dot!=std::string_view::npos)) return false;
  const size_t stop=(dot==std::string_view::npos)?text.size():dot;
  if (stop==offset || stop-offset>7) return false;
  unsigned number=0;
  for (size_t j=offset;j<stop;++j) {
   const char c=text[j]; if (c<'0'||c>'9') return false;
   number=number*10+unsigned(c-'0');
  }
  parts[i]=number; offset=stop+1;
 }
 result={parts[0],parts[1],parts[2]}; return true;
}
bool isNewer(std::string_view candidate,std::string_view installed) noexcept {
 Version a,b; if(!parseVersion(candidate,a)||!parseVersion(installed,b))return false;
 if(a.major!=b.major)return a.major>b.major;
 if(a.minor!=b.minor)return a.minor>b.minor;
 return a.patch>b.patch;
}
bool isHexDigest(std::string_view value,size_t length) noexcept {
 if(value.size()!=length)return false;
 for(char c:value)if(!std::isxdigit(static_cast<unsigned char>(c)))return false;
 return true;
}
bool isHttpsReleaseUrl(std::string_view value) noexcept {
 constexpr std::string_view scheme="https://";
 if(value.substr(0,scheme.size())!=scheme || value.size()>2048)return false;
 value.remove_prefix(scheme.size());
 const size_t end=value.find_first_of("/?#");
 const auto host=value.substr(0,end);
 if(host.size()<4 || host.size()>253 || host.find('.')==std::string_view::npos)return false;
 if(host.front()=='.'||host.back()=='.'||host.front()=='-'||host.back()=='-')return false;
 bool numericHost=true;
 for(char c:host){
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;
  if(!((c>='0'&&c<='9')||c=='.'))numericHost=false;
 }
 if(numericHost)return false;
 for(char c:value)if(static_cast<unsigned char>(c)<=32 || c=='\\'||c=='\"'||c=='\'')return false;
 return true;
}
bool canInstallAutomatically(std::string_view publisherThumbprint) noexcept {
 // Windows Get-AuthenticodeSignature exposes the signing cert's 40-digit SHA-1 thumbprint;
 // the *installer* is additionally checked by SHA-256 and the OS trust chain.
 return isHexDigest(publisherThumbprint,40);
}
}
