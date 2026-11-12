#!/usr/bin/env python3
"""Generate pinned GOEP cleanup overlay, leaving SDK source/license intact."""
import hashlib
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit('usage: patch_goep_client.py INPUT OUTPUT')
source, output = map(Path, sys.argv[1:])
if source.resolve() == output.resolve():
    raise SystemExit('Input and output must differ')
raw = source.read_bytes()
if hashlib.sha256(raw).hexdigest() != '92bca31c6dae183f69b8bb78b5bbbdb4ff57daabf0abfee9fe4ee32b954fd536':
    raise SystemExit('Unknown GOEP Client SDK hash')
s = raw.decode()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('Nonunique GOEP overlay anchor')
    s = s.replace(old, new)
replace('    return goep_client_start_connect(goep_client);', '''    uint8_t status = goep_client_start_connect(goep_client);
    if (status != ERROR_CODE_SUCCESS){
        btstack_linked_list_remove(&goep_clients, (btstack_linked_item_t *) goep_client);
        memset(goep_client, 0, sizeof(*goep_client));
        *out_cid = 0;
    }
    return status;''')
# Preserve failures originating from the asynchronous SDP route as events.
replace('            (void) goep_client_start_connect(goep_client);', '''            status = goep_client_start_connect(goep_client);
            if (status != ERROR_CODE_SUCCESS){
                goep_client_handle_connection_opened(goep_client, status, 0);
            }''')
replace('''        l2cap_disconnect(goep_client->bearer_cid);
    } else {
        rfcomm_disconnect(goep_client->bearer_cid);
    }
    return ERROR_CODE_SUCCESS;''', '''        return l2cap_disconnect(goep_client->bearer_cid);
    } else {
        return rfcomm_disconnect(goep_client->bearer_cid);
    }''')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(s)
print('GOEP Client overlay:', hashlib.sha256(s.encode()).hexdigest())
