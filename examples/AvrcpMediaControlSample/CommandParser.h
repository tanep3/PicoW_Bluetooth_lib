#pragma once
#include <cstddef>
#include <cstring>
namespace avrcp_media_sample {
enum class Command { None, Invalid, Start, Listen, Pair, Connect, Pause, Play, Query, Status, Disconnect, Shutdown };
class CommandParser {
 char line_[40]{};
 std::size_t size_=0;
 bool invalid_=false, cr_=false;
public:
 bool empty() const { return size_==0 && !invalid_ && !cr_; }
 Command feed(int c) {
  if(c=='\n') {
   Command cmd=Command::Invalid;
   line_[size_]=0;
   if(!invalid_) {
    if(!size_) cmd=Command::None;
    else if(!std::strcmp(line_,"START")) cmd=Command::Start;
    else if(!std::strcmp(line_,"LISTEN")) cmd=Command::Listen;
    else if(!std::strcmp(line_,"PAIR")) cmd=Command::Pair;
    else if(!std::strcmp(line_,"CONNECT_AVRCP")) cmd=Command::Connect;
    else if(!std::strcmp(line_,"PAUSE")) cmd=Command::Pause;
    else if(!std::strcmp(line_,"PLAY")) cmd=Command::Play;
    else if(!std::strcmp(line_,"QUERY_STATUS")) cmd=Command::Query;
    else if(!std::strcmp(line_,"STATUS")) cmd=Command::Status;
    else if(!std::strcmp(line_,"DISCONNECT")) cmd=Command::Disconnect;
    else if(!std::strcmp(line_,"SHUTDOWN")) cmd=Command::Shutdown;
   }
   size_=0; invalid_=false; cr_=false; return cmd;
  }
  if(c=='\r') { if(cr_) invalid_=true; cr_=true; return Command::None; }
  if(cr_ || c<32 || c>126 || size_+1>=sizeof(line_)) invalid_=true;
  if(!invalid_) line_[size_++]=char(c);
  return Command::None;
 }
};
}
