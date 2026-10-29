#include "UiGeometry.h"
#include <cstdlib>
#include <iostream>
static void expect(bool ok,const char* msg){if(!ok){std::cerr<<"FAIL: "<<msg<<'\n';std::exit(1);}}
int main(){
 using namespace veysona::ui;
 for(int w:{1200,1366,1600,1920,2560})for(int h:{720,900,1080})for(bool browser:{false,true})for(bool editor:{false,true}){
  const auto l=makeWorkspaceLayout(w,h,browser,editor);
  expect(l.browserWidth==(browser?294:0),"browser dock visibility");
  expect(l.rightEdge+l.browserWidth==w,"dock fills width without overlap");
  expect(l.arrangementTop==137,"stable transport and arrangement origin");
  expect(l.arrangementBottom==l.editorTop,"editor directly beneath arrangement");
  expect(l.editorHeight==(editor?std::clamp(360,310,std::max(310,h-27-137-230)):0),"adjustable editor default");
  expect(l.footerTop==h-27,"persistent system status");
  expect(l.arrangementBottom-l.arrangementTop>=230,"arrangement remains usable");
  expect(l.rightEdge>=900,"track editing minimum width");
 }
 expect(measureX(16,180,110.0)==1940,"bar markers span full arrangement");
 expect(subdivisionX(0,1,4,180,110.0)==208,"beat markers span track lanes");
 std::cout<<"Workspace geometry and docking defaults passed\n";
}
