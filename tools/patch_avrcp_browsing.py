#!/usr/bin/env python3
"""Hash-pinned empty-frame guard; preserve upstream license and original file."""
import hashlib,sys
from pathlib import Path
if len(sys.argv)!=3:raise SystemExit('usage: patch_avrcp_browsing.py INPUT OUTPUT')
a,b=map(Path,sys.argv[1:])
if a.resolve()==b.resolve():raise SystemExit('Input and output must differ')
raw=a.read_bytes()
if hashlib.sha256(raw).hexdigest()!='627a4103ab28b4bd74e627b313bfd1c02cf8fa7aa6ef89360666895825b02be8':raise SystemExit('Unknown browsing SDK hash; output untouched')
s=raw.decode();old='case L2CAP_DATA_PACKET:\n            switch (avrcp_get_frame_type(packet[0]))'
if s.count(old)!=1:raise SystemExit('Nonunique browsing patch anchor')
s=s.replace(old,'case L2CAP_DATA_PACKET:\n            /* PicoW: no transport header exists in an empty SDU. */\n            if (size == 0) return;\n            switch (avrcp_get_frame_type(packet[0]))')
s += '\n/* PicoW: expose registration outcome hidden by upstream void init. */\nbool picow_avrcp_browsing_service_ready(void){ return avrcp_browsing_l2cap_service_registered; }\n'
b.parent.mkdir(parents=True,exist_ok=True);b.write_text(s)
print('Browsing overlay generated:',hashlib.sha256(s.encode()).hexdigest())
