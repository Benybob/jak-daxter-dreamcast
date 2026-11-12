"""Burn a self-booting Dreamcast CD-R through Windows SCSI pass-through (AI-assisted), ISC.

The two-session layout of `cdrecord -multi -audio` then `cdrecord -xa`:
session 1 is one silent audio track (300 sectors, the Red Book minimum),
closed with the next session allowed; session 2 is make_boot_cdr.py's track,
written track-at-once as CD-ROM XA Mode 2 Form 1 (the console does not boot
Mode 1), and closing it finalizes the disc. The second session's ISO extents
are absolute, so it is built for the address `nwa` prints after session 1 and
`session2` refuses to write anywhere else. --test sets the Test Write bit: the
drive runs the whole write with the laser off and the disc stays blank.

  py burn_cdr.py D: info
  py burn_cdr.py D: session1 [--test]
  py burn_cdr.py D: nwa
  py burn_cdr.py D: session2 build/cdr/session2.iso [--test]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import struct
import sys
import time
from pathlib import Path

IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014
DATA_OUT, DATA_IN, NO_DATA = 0, 1, 2


class SPTD(ctypes.Structure):
    _fields_ = [('Length', ctypes.c_ushort), ('ScsiStatus', ctypes.c_ubyte), ('PathId', ctypes.c_ubyte),
                ('TargetId', ctypes.c_ubyte), ('Lun', ctypes.c_ubyte), ('CdbLength', ctypes.c_ubyte),
                ('SenseInfoLength', ctypes.c_ubyte), ('DataIn', ctypes.c_ubyte),
                ('DataTransferLength', ctypes.c_ulong), ('TimeOutValue', ctypes.c_ulong),
                ('DataBuffer', ctypes.c_void_p), ('SenseInfoOffset', ctypes.c_ulong), ('Cdb', ctypes.c_ubyte * 16)]


class SPTDS(ctypes.Structure):
    _fields_ = [('sptd', SPTD), ('pad', ctypes.c_ulong), ('sense', ctypes.c_ubyte * 32)]


class ScsiError(Exception):
    def __init__(self, cdb, status, sense):
        self.key, self.asc, self.ascq = sense[2] & 15, sense[12], sense[13]
        super().__init__('SCSI %02x status %d sense %x/%02x/%02x' % (cdb[0], status, self.key, self.asc, self.ascq))


kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
kernel32.CreateFileW.restype = wt.HANDLE
kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
kernel32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, wt.DWORD,
                                     ctypes.POINTER(wt.DWORD), ctypes.c_void_p]


class Drive:
    def __init__(self, letter):
        self.h = kernel32.CreateFileW('\\\\.\\' + letter.rstrip('\\'), 0xC0000000, 3, None, 3, 0, None)
        if self.h in (None, wt.HANDLE(-1).value):
            raise OSError('open %s failed: %d' % (letter, ctypes.get_last_error()))

    def cmd(self, cdb, length=0, direction=NO_DATA, data=None, timeout=60):
        buf = ctypes.create_string_buffer(data if data is not None else length, max(length, len(data or b'')) or 1)
        s = SPTDS()
        s.sptd.Length = ctypes.sizeof(SPTD)
        s.sptd.CdbLength = len(cdb)
        s.sptd.SenseInfoLength = 32
        s.sptd.DataIn = direction
        s.sptd.DataTransferLength = len(data) if data is not None else length
        s.sptd.TimeOutValue = timeout
        s.sptd.DataBuffer = ctypes.cast(buf, ctypes.c_void_p).value if s.sptd.DataTransferLength else None
        s.sptd.SenseInfoOffset = SPTDS.sense.offset
        for i, b in enumerate(cdb):
            s.sptd.Cdb[i] = b
        got = wt.DWORD()
        if not kernel32.DeviceIoControl(self.h, IOCTL_SCSI_PASS_THROUGH_DIRECT, ctypes.byref(s), ctypes.sizeof(s),
                                        ctypes.byref(s), ctypes.sizeof(s), ctypes.byref(got), None):
            raise OSError('DeviceIoControl %02x failed: %d' % (cdb[0], ctypes.get_last_error()))
        if s.sptd.ScsiStatus:
            raise ScsiError(cdb, s.sptd.ScsiStatus, bytes(s.sense))
        return buf.raw[:length]

    def retrying(self, cdb, *a, **k):
        """Retry while the drive is busy (2/04/xx becoming ready, long write in progress)."""
        for attempt in range(600):
            try:
                return self.cmd(cdb, *a, **k)
            except ScsiError as e:
                if not (e.key == 2 and e.asc == 4) and not (e.key == 6):
                    raise
                time.sleep(0.5)
        raise TimeoutError('drive stayed busy')

    def ready(self):
        return self.retrying(bytes(6))

    def disc_info(self):
        d = self.cmd(bytes([0x51, 0, 0, 0, 0, 0, 0, 0, 34, 0]), 34, DATA_IN)
        return dict(status=d[2] & 3, last_session=(d[2] >> 2) & 3, erasable=bool(d[2] & 16),
                    sessions=d[4] | d[9] << 8, first_track_last_session=d[5] | d[10] << 8,
                    last_track_last_session=d[6] | d[11] << 8, disc_type=d[8])

    def track_info(self, track=0xff):
        d = self.cmd(bytes([0x52, 1, 0, 0, 0, track, 0, 0, 36, 0]), 36, DATA_IN)
        start, nwa, free = struct.unpack_from('>III', d, 8)
        return dict(track=d[2] | d[32] << 8, session=d[3] | d[33] << 8, blank=bool(d[6] & 0x40),
                    nwa_valid=bool(d[7] & 1), start=start, nwa=nwa, free=free)

    def write_params(self, test, audio, multisession):
        sense = self.cmd(bytes([0x5a, 0, 0x05, 0, 0, 0, 0, 0, 255, 0]), 255, DATA_IN)
        n = struct.unpack_from('>H', sense)[0] + 2
        page = bytearray(sense[8:n])
        if page[0] & 0x3f != 5 or len(page) < 52:
            raise ValueError('drive returned no write parameters page')
        page[0] &= 0x3f
        page[2] = 0x40 | (0x10 if test else 0) | 1  # BUFE, test write, track-at-once
        page[3] = (3 if multisession else 0) << 6 | (0 if audio else 4)  # next session allowed; audio / data
        page[4] = 0 if audio else 10  # 2352 raw audio / Mode 2 Form 1, 2048 bytes, subheader from the page
        page[5] = page[7] = page[9] = 0
        page[8] = 0 if audio else 0x20  # CD-DA or CD-ROM session / CD-ROM XA session
        page[10:14] = bytes(4)
        page[14:16] = (150).to_bytes(2, 'big')
        page[48:52] = bytes([0, 0, 0x08, 0])  # XA subheader: file 0, channel 0, data, coding 0
        out = bytes(8) + bytes(page)
        self.cmd(bytes([0x55, 0x10, 0, 0, 0, 0, 0]) + len(out).to_bytes(2, 'big') + bytes(1), direction=DATA_OUT, data=out)
        check = self.cmd(bytes([0x5a, 0, 0x05, 0, 0, 0, 0, 0, 255, 0]), 255, DATA_IN)[8:]
        if (check[2] & 0x1f, check[3], check[4], check[8]) != (page[2] & 0x1f, page[3], page[4], page[8]):
            raise ValueError('drive did not take the write parameters: %s' % check[2:9].hex())

    def write_speeds(self):
        """(offered write speeds for this disc, current write speed), KB/s, from page 2A."""
        p = self.cmd(bytes([0x5a, 0, 0x2a, 0, 0, 0, 0, 1, 0, 0]), 256, DATA_IN)[8:]
        n = struct.unpack_from('>H', p, 30)[0]
        return [struct.unpack_from('>H', p, 32 + 4 * i + 2)[0] for i in range(n)], struct.unpack_from('>H', p, 28)[0]

    def slowest(self):
        """The drive's slowest offered speed for this disc, set and read back. The
        drive rounds other requests up (an 8x request burned at 48x on the LG), and
        a Dreamcast's laser reads slow burns best."""
        offered, _ = self.write_speeds()
        if not offered:
            raise ValueError('the drive offers no write speeds for this disc')
        want = min(offered)
        self.cmd(bytes([0xbb, 0, 0xff, 0xff]) + want.to_bytes(2, 'big') + bytes(6))
        _, current = self.write_speeds()
        if abs(current - want) > want // 100 + 1:
            raise ValueError('asked for %d KB/s, the drive is at %d KB/s' % (want, current))
        print('write speed %d KB/s (%dx); offered %s' % (current, round(current / 176.4), offered))

    def write_track(self, data, block, lba, label):
        per = (64 * 1024) // block
        total = len(data) // block
        done, t0 = 0, time.monotonic()
        while done < total:
            n = min(per, total - done)
            chunk = data[done * block:(done + n) * block]
            cdb = bytes([0x2a, 0]) + (lba + done).to_bytes(4, 'big') + bytes([0]) + n.to_bytes(2, 'big') + bytes(1)
            for attempt in range(200):
                try:
                    self.cmd(cdb, direction=DATA_OUT, data=chunk, timeout=120)
                    break
                except ScsiError as e:
                    if not (e.key == 2 and e.asc == 4 and e.ascq in (7, 8)):  # operation / long write in progress
                        raise
                    time.sleep(0.05)
            else:
                raise TimeoutError('drive buffer never drained')
            done += n
            if done % (per * 64) < per or done == total:
                print('  %s %d/%d sectors, %.0f s' % (label, done, total, time.monotonic() - t0), flush=True)

    def finish(self, track, close_session):
        self.retrying(bytes([0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0]), timeout=600)
        self.retrying(bytes([0x5b, 0, 1, 0]) + track.to_bytes(2, 'big') + bytes(4), timeout=600)
        if close_session:
            self.retrying(bytes([0x5b, 0, 2, 0, 0, 0, 0, 0, 0, 0]), timeout=900)


def session1(drive, test):
    info = drive.disc_info()
    if info['status'] != 0:
        raise SystemExit('session1 needs a blank disc: %s' % info)
    drive.write_params(test, audio=True, multisession=True)
    t = drive.track_info()
    if not t['nwa_valid']:
        raise SystemExit('no next writable address: %s' % t)
    print('session 1: 300 silent audio sectors at %d%s' % (t['nwa'], ' (test write)' if test else ''))
    drive.write_track(bytes(300 * 2352), 2352, t['nwa'], 'audio')
    drive.finish(t['track'], close_session=True)
    print('session 1 closed; disc %s' % drive.disc_info())


def session2(drive, iso, test):
    manifest = json.loads((iso.parent / 'manifest.json').read_text())
    data = iso.read_bytes()
    if len(data) % 2048 or len(data) // 2048 != manifest['sectors']:
        raise SystemExit('track size does not match its manifest')
    info = drive.disc_info()
    t = drive.track_info()
    if test:
        print('test: the track was built for LBA %d; the next writable address is %s'
              % (manifest['lba'], t['nwa'] if t['nwa_valid'] else 'not valid'))
    else:
        if info['status'] != 1 or info['sessions'] < 2:
            raise SystemExit('session2 needs a disc with session 1 closed: %s' % info)
        if not t['nwa_valid'] or t['nwa'] != manifest['lba']:
            raise SystemExit('the track was built for LBA %d but the next writable address is %s'
                             % (manifest['lba'], t['nwa'] if t['nwa_valid'] else 'not valid'))
        if t['free'] < manifest['sectors']:
            raise SystemExit('not enough room: %d free' % t['free'])
    drive.write_params(test, audio=False, multisession=False)
    t = drive.track_info()
    print('session 2: %d Mode 2 Form 1 sectors at %d%s' % (len(data) // 2048, t['nwa'], ' (test write)' if test else ''))
    drive.write_track(data, 2048, t['nwa'], 'data')
    drive.finish(t['track'], close_session=True)
    print('session 2 closed; disc %s' % drive.disc_info())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('drive')
    p.add_argument('step', choices=('info', 'session1', 'nwa', 'session2'))
    p.add_argument('iso', nargs='?', type=Path)
    p.add_argument('--test', action='store_true', help='Test Write: the laser stays off')
    a = p.parse_args()
    d = Drive(a.drive)
    d.ready()
    inq = d.cmd(bytes([0x12, 0, 0, 0, 36, 0]), 36, DATA_IN)
    print('drive:', inq[8:36].decode(errors='replace').strip())
    if a.step == 'info':
        print('disc:', d.disc_info())
        print('next track:', d.track_info())
        return
    if a.step == 'nwa':
        t = d.track_info()
        print('next writable address:', t['nwa'] if t['nwa_valid'] else 'not valid', t)
        return
    d.slowest()
    if a.step == 'session1':
        session1(d, a.test)
    else:
        if not a.iso:
            p.error('session2 needs the track')
        session2(d, a.iso, a.test)


if __name__ == '__main__':
    try:
        main()
    except (ScsiError, OSError, ValueError, TimeoutError) as e:
        sys.exit('burn_cdr: %s' % e)
