#!/usr/bin/env python3
"""Pinned serializer corrections. Preserve upstream license and original file."""
import hashlib,sys
from pathlib import Path
if len(sys.argv)!=3:raise SystemExit('usage: patch_avrcp_browsing_controller.py INPUT OUTPUT')
a,b=map(Path,sys.argv[1:])
if a.resolve()==b.resolve():raise SystemExit('Input and output must differ')
raw=a.read_bytes()
if hashlib.sha256(raw).hexdigest()!='757c73749ab11470399c0c63f365c4b9adc9e0a0fa395302f8fd4803e3a36c2e':raise SystemExit('Unknown browsing Controller SDK hash')
s=raw.decode();a1='attribute_count = AVRCP_MEDIA_ATTR_NONE; // 0xFFFFFFFF';a2='9 + 1 + (attribute_count*4)'
if s.count(a1)!=1 or s.count(a2)!=1:raise SystemExit('Nonunique serializer anchors')
s=s.replace(a1,'attribute_count = 0xff; // Protocol no-attributes marker, not SDK enum value').replace(a2,'9 + 1 + (attributes_to_copy*4)')
b.parent.mkdir(parents=True,exist_ok=True);b.write_text(s);print('Browsing Controller overlay:',hashlib.sha256(s.encode()).hexdigest())
