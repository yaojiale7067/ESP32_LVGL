import serial, sys, time

# Usage: serial_cli.py PORT boot_wait "cmd1" wait1 "cmd2" wait2 ... outfile
# Opening the port pulses DTR/RTS and RESETS the ESP32 auto-reset board,
# so boot_wait seconds are drained first before any command is sent.
port = sys.argv[1]
boot_wait = float(sys.argv[2])
outfile = sys.argv[-1]
pairs = sys.argv[3:-1]

ser = serial.Serial(port, 115200, timeout=1)

def drain(secs):
    end = time.time() + secs
    while time.time() < end:
        d = ser.read(4096)
        if d:
            buf.extend(d)

buf = bytearray()
drain(boot_wait)          # let the board finish booting after the open-reset
buf.extend(b"\n--- boot done, sending commands ---\n")

i = 0
while i < len(pairs) - 1:
    cmd, wait = pairs[i], float(pairs[i + 1])
    ser.write((cmd + "\n").encode())
    drain(wait)
    i += 2
ser.close()

with open(outfile, "wb") as f:
    f.write(buf)
print("captured", len(buf), "bytes ->", outfile)
