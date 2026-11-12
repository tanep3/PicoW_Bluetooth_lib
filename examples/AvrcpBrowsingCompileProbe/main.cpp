#include "AvrcpBrowsingController.h"
volatile bool initialize_for_link_check=false;
int main(){static picow::avrcp::AvrcpController control;static picow::avrcp::AvrcpTarget target;static picow::avrcp::AvrcpBrowsingController browser;
if(initialize_for_link_check){picow::avrcp::AvrcpController::Config c;c.peer_count=1;c.peers[0]={{1,2,3,4,5,6}};
target.begin(c,true);control.begin(c,true);control.connect(c.peers[0]);browser.begin(c,true);browser.connect(c.peers[0]);auto b=browser.bindingForPeer(c.peers[0]);
browser.folderItems(b,AVRCP_BROWSING_MEDIA_PLAYER_LIST,0,10);browser.setBrowsedPlayer(b,1);browser.totalItems(b,AVRCP_BROWSING_MEDIA_PLAYER_LIST);
std::array<uint8_t,8> uid{};browser.changePath(b,false,uid);browser.itemAttributes(b,uid,0,0,AVRCP_BROWSING_NOW_PLAYING);browser.search(b,"test",4);picow::avrcp::AvrcpBrowsingController::RequestToken token{};
uint8_t empty[2]={0,0};browser.reject(token,1);browser.respond(token,1,nullptr,0);
browser.respondSetBrowsedPlayer(token,1,0,106,0,nullptr,0);browser.respondFolderItems(token,1,0,nullptr,0);
browser.respondTotalItems(token,1,0);browser.respondChangePath(token,0);browser.respondItemAttributes(token,0,nullptr,0);browser.respondSearch(token,1,0);
(void)empty;browser.poll();browser.disconnect(b);browser.stop();control.stop();target.stop();}return 0;}
