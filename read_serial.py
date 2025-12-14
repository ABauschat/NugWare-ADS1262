import serial
import time
import sys

# Give system time to settle
time.sleep(2)

print("Attempting to open COM6...", flush=True)
try:
    # Open serial port
    ser = serial.Serial('COM6', 115200, timeout=1)
    
    print("Successfully opened COM6", flush=True)
    
    # Send reset
    print("Sending reset...", flush=True)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(2)
    
    # Read output
    print("Reading output for 20 seconds...", flush=True)
    start = time.time()
    buffer = ""
    while time.time() - start < 20:
        if ser.in_waiting:
            try:
                data = ser.read(ser.in_waiting)
                text = data.decode('utf-8', errors='replace')
                buffer += text
                print(text, end='', flush=True)
            except:
                pass
        time.sleep(0.05)
    
    ser.close()
    print("\n\nTotal output received:", len(buffer), "bytes", flush=True)
    
except Exception as e:
    print(f"Error: {e}", flush=True)
    import traceback
    traceback.print_exc()

