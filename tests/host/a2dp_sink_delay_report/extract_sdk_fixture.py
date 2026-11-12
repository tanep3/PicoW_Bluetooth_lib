"""Compile exact installed SDK function bodies, never a reimplemented serializer.
This isolated fixture is not the complete SDK transaction/event state machine.
"""
import pathlib, sys
bt, target = map(pathlib.Path, sys.argv[1:])
def function(path, signature):
    text = (bt / 'src/classic' / path).read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    # Keep each original source's complete copyright/license header verbatim.
    if not text.startswith('/*'):
        raise ValueError(f'Missing leading license header in {path}')
    license_header = text[:text.index('*/') + 2]
    return f'// Extracted without modification from {path}\n' + license_header + '\n' + text[start:end]
raw = function('avdtp_sink.c', 'uint8_t avdtp_sink_delay_report(')
wire = function('avdtp_initiator.c', 'static int avdtp_initiator_send_signaling_cmd_delay_report(')
prefix = r'''
#include "btstack.h"
#include "classic/avdtp_util.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static avdtp_connection_t conn{};
static avdtp_stream_endpoint_t ep{};
static bool have_conn=true, have_ep=true;
static unsigned scheduled=0, sends=0;
static uint8_t bytes[5]{};
extern "C" {
avdtp_connection_t *avdtp_get_connection_for_avdtp_cid(uint16_t cid){assert(cid==7);return have_conn?&conn:nullptr;}
avdtp_stream_endpoint_t *avdtp_get_stream_endpoint_for_seid(uint16_t seid){assert(seid==1);return have_ep?&ep:nullptr;}
uint8_t avdtp_request_can_send_now_initiator(avdtp_connection_t *c){assert(c==&conn);++scheduled;return 0;}
uint8_t l2cap_send(uint16_t cid,const uint8_t *data,uint16_t size){assert(cid==9 && size==5);memcpy(bytes,data,5);++sends;return 0;}
}
'''
main = r'''
int main(){
  conn.state=AVDTP_SIGNALING_CONNECTION_OPENED;
  ep.state=AVDTP_STREAM_ENDPOINT_OPENED;ep.remote_sep.seid=2;
  for(unsigned value:{0u,1u,0x1234u,65535u}){
    conn.initiator_connection_state=AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE;
    auto before=scheduled;
    assert(avdtp_sink_delay_report(7,1,value)==0 && scheduled==before+1);
    assert(conn.delay_ms==value && conn.initiator_local_seid==1 && conn.initiator_remote_seid==2);
    assert(conn.initiator_connection_state==AVDTP_SIGNALING_CONNECTION_INITIATOR_W2_SEND_DELAY_REPORT);
    assert(avdtp_sink_delay_report(7,1,42)==ERROR_CODE_COMMAND_DISALLOWED && scheduled==before+1 && conn.delay_ms==value);
    assert(avdtp_initiator_send_signaling_cmd_delay_report(9,conn.initiator_transaction_label,conn.initiator_remote_seid,conn.delay_ms)==0);
    assert(bytes[0]==(conn.initiator_transaction_label<<4) && bytes[1]==AVDTP_SI_DELAYREPORT && bytes[2]==8 && bytes[3]==value/256 && bytes[4]==value%256);
  }
  auto before=scheduled;
  have_conn=false;assert(avdtp_sink_delay_report(7,1,1)==ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);have_conn=true;
  conn.initiator_connection_state=AVDTP_SIGNALING_CONNECTION_INITIATOR_IDLE;
  have_ep=false;assert(avdtp_sink_delay_report(7,1,1)==ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);have_ep=true;
  ep.state=AVDTP_STREAM_ENDPOINT_IDLE;assert(avdtp_sink_delay_report(7,1,1)==ERROR_CODE_COMMAND_DISALLOWED);
  assert(scheduled==before && sends==4);
  puts("PASS exact installed SDK delay admission and big-endian serializer function fixtures (not complete transport)");
}
'''
target.write_text('#include <initializer_list>\n' + prefix + raw + '\n' + wire + '\n' + main)
