import serial, sys, time

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
out  = sys.argv[3] if len(sys.argv) > 3 else "serial_capture.txt"

ser = serial.Serial(port, 115200, timeout=1)
end = time.time() + secs
buf = bytearray()
while time.time() < end:
    data = ser.read(4096)
    if data:
        buf.extend(data)
ser.close()

with open(out, "wb") as f:
    f.write(buf)
print("captured", len(buf), "bytes ->", out)
