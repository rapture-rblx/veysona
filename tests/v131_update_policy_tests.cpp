#include "UpdatePolicy.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace veysona::update;
int main(){
 Version v{};
 assert(parseVersion("1.31.0",v)&&v.major==1&&v.minor==31&&v.patch==0);
 assert(parseVersion("v2.0.14",v)&&v.major==2&&v.patch==14);
 for(auto bad:{"", "1", "1.2", "1.2.3.4", "1.-2.3", "1.2.beta", "1.2.3+1", "1..3", "999999999.1.1"})assert(!parseVersion(bad,v));
 assert(isNewer("1.31.1","1.31.0"));
 assert(isNewer("1.32.0","1.31.99"));
 assert(isNewer("2.0.0","1.999.999"));
 assert(!isNewer("1.31.0","1.31.0"));
 assert(!isNewer("1.30.9","1.31.0"));
 assert(!isNewer("garbage","1.31.0"));
 assert(isHttpsReleaseUrl("https://github.com/org/releases/download/v1.32.0/Setup.exe"));
 assert(isHttpsReleaseUrl("https://releases.example.org/stable.json"));
 for(auto bad:{"http://example.org/update.exe","https://localhost/update.exe","https://127.0.0.1/update.exe", "https://evil.com@github.com/file.exe", "https://site.com:443/file.exe", "https://site.com\\file.exe", "https://site.com/hi\n.exe", "file:///home/update.exe"}) assert(!isHttpsReleaseUrl(bad));
 const std::string digest(64,'a'), cert(40,'f');
 assert(isHexDigest(digest,64)&&!isHexDigest(digest,40));
 assert(canInstallAutomatically(cert));
 assert(!canInstallAutomatically("") && !canInstallAutomatically("unsigned"));
 std::cout << "v1.31 updater policy tests passed\n";
}
