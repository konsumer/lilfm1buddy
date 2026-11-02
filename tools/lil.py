#!/usr/bin/env python3
# Drive lilfm1buddy on this computer, for development (or an agent working on it). It's the USB drive (LILFM1) when
# it's plugged in, and its serial port once that's ejected (esp32/main.cpp's console). Each command gets it where it
# needs to be first:
#
#   tools/lil.py disk          the drive: prints where it's mounted
#   tools/lil.py eject         the serial port (the drive ejected)
#   tools/lil.py info          lilfm1buddy VERSION free=BYTES loader=0|1
#   tools/lil.py ls            the files on the drive, and their sizes
#   tools/lil.py log           log.txt: what happened last time on the FM-1
#   tools/lil.py flash ENV     build and install the program (platformio.ini's ENV: zero, xiao, ...), then the drive
#
# Needs pyserial (PlatformIO has it: ~/.platformio/penv/bin/python tools/lil.py ...). The port: $PORT, or the first
# /dev/cu.usbmodem* or /dev/ttyACM*. macOS or Linux.
import glob, os, re, subprocess, sys, time
import serial

LABEL = 'LILFM1'
LOG_LINE = re.compile(r'^\s*\d+\.\d (lil|disk|midi|rom|usb|key|ble):')  # (the board's log, between answers)


def wait(what, fn, s=20):
    end = time.time() + s
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.25)
    sys.exit(f'no {what} after {s} s')


def mounted():
    for p in [f'/Volumes/{LABEL}'] + glob.glob(f'/media/*/{LABEL}') + glob.glob(f'/run/media/*/{LABEL}'):
        if os.path.isdir(p):
            return p


def port():
    return os.environ.get('PORT') or (glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*') + [None])[0]


def disk():
    end = time.time() + 5  # (it may be on its way: restarting, after an install)
    while not mounted() and time.time() < end:
        time.sleep(0.25)
    if not mounted():
        command('disk')
    return wait('drive', mounted)


def eject():
    m = mounted()
    if m:
        subprocess.run(['diskutil', 'eject', m] if sys.platform == 'darwin' else ['eject', m], check=True, capture_output=True)
        wait('drive gone', lambda: not mounted())
    wait('serial port', port)


# one command: its answer's lines (not the log's), up to OK; exits on ERR
def command(text):
    if mounted():
        eject()
    s = None
    for t in range(20):  # (the port comes a moment before it answers)
        try:
            s = port() and serial.Serial(port(), 115200, timeout=3)
            if s:
                break
        except serial.SerialException:
            pass
        time.sleep(0.5)
    if not s:
        sys.exit('no serial port: plug it in (or set PORT)')
    s.reset_input_buffer()
    s.write(text.encode() + b'\n')
    out = []
    end = time.time() + 10
    while time.time() < end:
        try:
            line = s.readline().decode(errors='replace').rstrip('\r\n')
        except serial.SerialException:  # (gone: it restarted, as "disk" does)
            if text == 'disk':
                return out
            raise
        if line == 'OK':
            return out
        if line.startswith('ERR'):
            sys.exit(line)
        if line and not LOG_LINE.match(line):
            out.append(line)
    sys.exit(f'no answer to {text}')


def flash(env):
    eject()
    subprocess.run(['pio', 'run', '-e', env, '-t', 'upload', '--upload-port', port()], check=True)
    return disk()


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ('disk', 'eject', 'info', 'ls', 'log', 'flash') or (a[0] == 'flash' and len(a) < 2):
        sys.exit(__doc__ or open(__file__).read().split('import')[0])
    if a[0] == 'disk':
        print(disk())
    elif a[0] == 'eject':
        eject()
        print(port())
    elif a[0] == 'flash':
        print(flash(a[1]))
    else:
        print('\n'.join(command(a[0])))


if __name__ == '__main__':
    main()
