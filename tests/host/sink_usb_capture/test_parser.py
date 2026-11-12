"""Offline protocol tests with synthetic bytes only; never open a serial port."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import wave
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('capture_usb', ROOT / 'examples/A2dpSinkPcmConsumer/capture_usb.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def packet(kind, seq, payload=b'', rate=16000, channels=1, epoch=1):
    data = m.MAGIC + struct.pack('<6I', kind, seq, len(payload), rate, channels, epoch) + payload
    return data + struct.pack('<I', zlib.crc32(data))


def stream(frames=80000, lost=0, reason=0, drops=0, failures=0, gap=False):
    parts = [packet(1, 0)]
    seq = 1 + int(gap)
    for i in range(0, frames, 512):
        n = min(512, frames-i)
        parts.append(packet(2, seq, b'\x01\x00' * n))
        seq += 1
    parts.append(packet(3, seq, struct.pack('<8I', frames+lost, lost, drops, failures, reason, 5000, 80000, seq)))
    return b''.join(parts)


class ParserTests(unittest.TestCase):
    def parse(self, data):
        p = m.CaptureParser()
        p.feed(data)
        return p

    def test_complete_fragmented_with_console_prefix(self):
        p = m.CaptureParser()
        data = b'Ready console\r\n' + stream() + b'ignored text after END'
        for i in range(0, len(data), 37):
            p.feed(data[i:i+37])
        self.assertTrue(p.result()['complete_no_reported_loss'])
        self.assertEqual(p.result()['received_frames'], 80000)

    def test_incomplete_and_drops(self):
        for kwargs in ({'frames':100, 'reason':2}, {'frames':79999, 'lost':1, 'gap':True},
                       {'drops':1}, {'failures':1}, {'gap':True}):
            self.assertFalse(self.parse(stream(**kwargs)).result()['complete_no_reported_loss'])

    def test_crc_rejected(self):
        data=bytearray(stream()); data[50] ^= 1
        with self.assertRaises(m.CaptureError): self.parse(data)

    def test_no_end(self):
        p=self.parse(packet(1,0))
        with self.assertRaises(m.CaptureError): p.result()

    def test_corrupt_headers_and_order(self):
        bad = [packet(2,0,b'\0\0'), packet(1,1), packet(1,0,b'x'),
               packet(1,0,rate=0), packet(1,0,channels=3),
               packet(1,0)+packet(2,1,b'x'), packet(1,0)+packet(2,0,b'\0\0'),
               packet(1,0)+packet(1,1), packet(1,0)+packet(2,1,b'\0\0',epoch=2),
               packet(1,0)+packet(2,1,b'\0'*1025), packet(1,0)+b'corrupt!'*5,
               packet(1,0)+packet(3,1,b'\0'*4),
               packet(1,0)+packet(3,1,struct.pack('<8I',0,0,0,0,0,0,80000,2))]
        for data in bad:
            with self.subTest(data=data[:40]):
                with self.assertRaises(m.CaptureError): self.parse(data)

    def test_accounting_and_target(self):
        for values in ((1,0,0,0,0,0,80000,1),(0,0,0,0,0,0,7,1)):
            p=self.parse(packet(1,0)+packet(3,1,struct.pack('<8I',*values)))
            with self.assertRaises(m.CaptureError): p.result()

    def test_overlong_capture(self):
        with self.assertRaises(m.CaptureError): self.parse(stream(frames=80001))

    def test_save_complete_incomplete_and_no_overwrite(self):
        for kwargs, name in (({}, 'received-5s.wav'),
                             ({'frames':100, 'reason':3}, 'received-INCOMPLETE.wav')):
            with tempfile.TemporaryDirectory(dir=ROOT / '.work/public-sink-usb') as temp:
                directory=Path(temp)
                parser=self.parse(stream(**kwargs))
                path, result=m.save_capture(parser, directory)
                self.assertEqual(path.name,name)
                with wave.open(str(path),'rb') as wav:
                    self.assertEqual(wav.getnframes(), result['received_frames'])
                    self.assertEqual(wav.getframerate(),16000)
                before=path.read_bytes()
                with self.assertRaises(FileExistsError): m.save_capture(parser,directory)
                self.assertEqual(path.read_bytes(),before)

    def test_receive_sends_record_only(self):
        class FakePort:
            in_waiting=200000
            sent=[]
            def write(self,data): self.sent.append(data)
            def read(self,count): return stream()
        port=FakePort()
        self.assertTrue(m.receive(port).result()['complete_no_reported_loss'])
        self.assertEqual(port.sent,[b'r\n'])

    def test_prefix_buffer_bounded(self):
        p=self.parse(b'x'*100000)
        self.assertLessEqual(len(p.buffer),7)


if __name__ == '__main__':
    unittest.main()
