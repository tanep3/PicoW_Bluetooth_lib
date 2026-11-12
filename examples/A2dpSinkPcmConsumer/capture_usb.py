#!/usr/bin/env python3
"""Explicit-port, explicit-command five-second USB PCM collector.
No device discovery, flashing, Source control or automatic Bluetooth commands.
Requires pyserial only for the live CLI; parser/tests use the standard library.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import time
import wave
import zlib

class CaptureError(ValueError):
    pass

MAGIC=b'PCMUSB01'
class CaptureParser:
    def __init__(self):
        self.buffer=bytearray();self.started=False;self.ended=False
        self.sequence=0;self.format=None;self.pcm=bytearray();self.gaps=[];self.end=None
    def feed(self,data):
        if self.ended:return
        self.buffer.extend(data)
        while len(self.buffer)>=32:
            if not self.started:
                at=self.buffer.find(MAGIC)
                if at<0:
                    self.buffer[:]=self.buffer[-7:];return
                del self.buffer[:at]
                if len(self.buffer)<32:return
            if self.buffer[:8]!=MAGIC:raise CaptureError('Binary framing corruption')
            kind,seq,size,rate,channels,epoch=struct.unpack_from('<6I',self.buffer,8)
            if size>1024 or rate not in (16000,32000,44100,48000) or channels not in (1,2):
                raise CaptureError('Invalid capture header')
            total=32+size+4
            if len(self.buffer)<total:return
            packet=bytes(self.buffer[:total]);del self.buffer[:total]
            if zlib.crc32(packet[:-4])!=struct.unpack_from('<I',packet,total-4)[0]:
                raise CaptureError('Capture CRC mismatch')
            fmt=(rate,channels,epoch)
            payload=packet[32:-4]
            if not self.started:
                if kind!=1 or seq!=0 or size:raise CaptureError('Missing fresh BEGIN')
                self.started=True;self.format=fmt
            else:
                if fmt!=self.format:raise CaptureError('Capture format changed')
                if seq<self.sequence:raise CaptureError('Repeated/out-of-order packet')
                if seq!=self.sequence:self.gaps.append([self.sequence,seq])
                if kind==2:
                    if not size or size%(2*channels):raise CaptureError('Partial PCM frame')
                    self.pcm.extend(payload)
                    if len(self.pcm)>rate*channels*2*5:raise CaptureError('Capture exceeds five seconds')
                elif kind==3:
                    if size!=32:raise CaptureError('Invalid END')
                    keys='frames transport_lost receiver_dropped decode_failures reason elapsed_ms target_frames final_sequence'.split()
                    self.end=dict(zip(keys,struct.unpack('<8I',payload)))
                    if self.end['final_sequence']!=seq:raise CaptureError('END sequence mismatch')
                    self.ended=True
                else:raise CaptureError('Unexpected packet type')
            self.sequence=seq+1
            if self.ended:return
    def result(self):
        if not self.ended:raise CaptureError('Missing END, no validated WAV')
        rate,channels,_=self.format
        frames=len(self.pcm)//(2*channels)
        if self.end['frames'] > rate*5 or self.end['reason'] not in (0, 1, 2, 3):
            raise CaptureError('Invalid END counters or reason')
        if self.end['frames']!=frames+self.end['transport_lost']:
            raise CaptureError('Frame accounting mismatch')
        if self.end['target_frames']!=rate*5:raise CaptureError('Invalid duration target')
        complete=not self.gaps and not any(self.end[k] for k in ('transport_lost','receiver_dropped','decode_failures','reason')) and frames==rate*5
        return dict(self.end,received_frames=frames,sample_rate=rate,channels=channels,
                    duration_seconds=frames/rate,sequence_gaps=self.gaps,complete_no_reported_loss=complete)


def save_capture(parser, directory):
    """Only a CRC-validated END permits a WAV; never overwrite an existing file."""
    result = parser.result()
    name = 'received-5s.wav' if result['complete_no_reported_loss'] else 'received-INCOMPLETE.wav'
    path = directory / name
    partial = directory / (name + '.partial')
    with partial.open('xb') as output:
        with wave.open(output, 'wb') as wav:
            wav.setnchannels(result['channels'])
            wav.setsampwidth(2)
            wav.setframerate(result['sample_rate'])
            wav.writeframes(parser.pcm)
    result['auditory_quality_verified'] = False
    with (directory / 'capture-result.json').open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    # Publish only a completely written WAV, without clobbering another take.
    os.link(partial, path)
    partial.unlink()
    return path, result


def receive(port):
    parser = CaptureParser()
    port.write(b'r\n')
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and not parser.ended:
        data = port.read(min(max(port.in_waiting, 1), 16384))
        if data:
            parser.feed(data)
    return parser


def show_console(port):
    # Bounded drain; output may have been dropped by the nonblocking firmware.
    deadline = time.monotonic() + 0.5
    while time.monotonic() < deadline:
        data = port.read(min(max(port.in_waiting, 1), 4096))
        if data:
            print(data.decode('utf-8', errors='replace'), end='', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True, help='Exact serial device, e.g. COM7 or /dev/ttyACM0; never auto-detected')
    ap.add_argument('--output-dir', required=True, type=Path, help='New private local directory; must not already exist')
    args = ap.parse_args()
    import serial  # Deliberately deferred so offline parser/tests need no pyserial.
    os.umask(0o077)
    if args.output_dir.exists():
        ap.error('output-dir already exists; choose a new directory')
    print('Open only the intended Pico USB port. Close other serial terminals first.')
    print('Commands: l=listen, c=connect once, i=identity, ?=status, d=disconnect, QUIT=exit.')
    print('Connect/play the Source yourself. Verify streaming=1, rate/channels and measured_frames using ?.\n'
          'CAPTURE explicitly records the NEXT five seconds. No Bluetooth command is sent automatically.')
    try:
        # 115200 avoids the SDK 1200-baud reset trigger. No port search/reset/flash.
        with serial.Serial(args.port, baudrate=115200, timeout=0.1, write_timeout=1) as port:
            while True:
                show_console(port)
                command = input('sink> ').strip()
                if command == 'QUIT':
                    return 0
                if command == 'CAPTURE':
                    args.output_dir.mkdir(mode=0o700, parents=False, exist_ok=False)
                    print('Recording requested now; maximum five seconds of PCM. Waiting at most 15 seconds.')
                    parser = receive(port)
                    path, result = save_capture(parser, args.output_dir)
                    print(str(path))
                    print(json.dumps(result, indent=2))
                    print('Port closed; this does not disconnect Bluetooth. Use d explicitly in a later console session if needed.')
                    return 0 if result['complete_no_reported_loss'] else 2
                if command in ('l', 'c', 'd', 'i', '?'):
                    port.write((command + '\n').encode('ascii'))
                elif command:
                    print('Unknown command; use CAPTURE exactly to authorize recording.')
    except (CaptureError, OSError, serial.SerialException, EOFError, KeyboardInterrupt) as error:
        print('Capture failed; no validated complete WAV: ' + str(error))
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
