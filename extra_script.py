import subprocess
import re
import os
import sys
from SCons.Script import Environment, Import
import threading
from queue import Queue
import urllib.request

def upload_all_devices(source, target, env):
    """
    This function discovers all remote OTA devices and uploads the firmware
    to each one sequentially using a multi-stage discovery process.
    """
    print("Starting multi-device OTA upload...")

    pio_exe = env.subst("$PIOEXE")
    python_exe = env.subst("$PYTHONEXE")

    # Get the path to espota.py from the UPLOADER environment variable,
    # which is set by PlatformIO for the 'espota' upload protocol.
    espotapy_path = env.subst("$UPLOADER")

    if not os.path.exists(espotapy_path):
        print(f"Error: Could not find espota.py (UPLOADER: {espotapy_path}). Make sure 'tool-espotapy' is installed for your platform.")
        env.Exit(1)

    # --- DISCOVERY PHASE ---
    ips = []
    
    # Check if a specific IP address was passed via the standard --upload-port flag.
    # This is the most reliable way to target a single device for debugging.
    upload_target = env.get("UPLOAD_PORT")
    is_ip_address = upload_target and re.match(r"^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$", upload_target)

    if is_ip_address:
        print(f"\nManual IP target specified via --upload-port: {upload_target}. Skipping discovery.")
        ips.append(upload_target)
    else:
        # If no specific IP is given, scan the local network for devices with a /health endpoint
        # using multiple threads to make the process significantly faster.
        print("\nNo specific IP provided. Scanning 192.168.1.2-255 for healthy devices...")
        
        ip_queue = Queue()
        live_ips = []
        
        # Worker function for each thread
        def check_device(q):
            while not q.empty():
                ip = q.get()
                url = f"http://{ip}/health"
                try:
                    # Use a short timeout. Because this is multi-threaded, a slightly
                    # longer timeout adds reliability without much overall speed penalty.
                    with urllib.request.urlopen(url, timeout=0.5) as response:
                        if response.status == 200:
                            print(f"  -> Found healthy device at {ip}")
                            live_ips.append(ip)
                except Exception:
                    # Ignore all errors (timeouts, connection refused, etc.)
                    pass
                finally:
                    q.task_done()

        # Populate the queue with all IPs to check
        for i in range(2, 256):
            ip_queue.put(f"192.168.1.{i}")

        # Start a pool of worker threads for a fast network scan
        for _ in range(50):
            thread = threading.Thread(target=check_device, args=(ip_queue,))
            thread.daemon = True
            thread.start()
            
        ip_queue.join() # Wait for all IPs to be processed
        ips = live_ips

    # --- END DISCOVERY ---

    if not ips:
        print("\nNo devices found to update.")
        env.Exit(1)

    unique_ips = sorted(list(set(ips)))
    print(f"\nFound {len(unique_ips)} device(s) to update: {', '.join(unique_ips)}")

    # The 'source' argument is a list of SCons nodes representing the files
    # to be uploaded. In this case, it's the firmware.bin file.
    # We get the path of the first source file, which is the correct binary.
    firmware_path = str(source[0])
    if not os.path.exists(firmware_path):
        print(f"Error: Firmware not found at '{firmware_path}'")
        print("Please build the project first (e.g., 'pio run -e esp32_cyd_ota').")
        env.Exit(1)

    # Get upload flags for auth password, if any
    upload_flags = env.get("UPLOAD_FLAGS", [])

    # Upload to each device
    success_count = 0
    failure_count = 0
    failed_ips = []

    for ip in unique_ips:
        print(f"\nUploading to {ip}...")
        try:
            cmd = [
                python_exe,
                espotapy_path,
                "--debug",
                "--progress",
                "-i", ip,
                "-f", firmware_path
            ]
            # Add any extra upload flags like --auth
            if upload_flags:
                cmd.extend(upload_flags)

            result = subprocess.run(
                cmd,
                check=True,
                capture_output=True,
                text=True,
                encoding='utf-8'
            )
            print(result.stdout)
            if result.stderr:
                print("--- stderr ---")
                print(result.stderr)
            print(f"\n✅ Successfully uploaded to {ip}")
            success_count += 1
        except subprocess.CalledProcessError as e:
            # Any failure is now considered a real failure.
            print(f"\n❌ Failed to upload to {ip}")
            print(f"Return code: {e.returncode}")
            if e.stdout:
                print("--- stdout ---")
                print(e.stdout)
            if e.stderr:
                print("--- stderr ---")
                print(e.stderr)
            failure_count += 1
            failed_ips.append(ip)

    print("\n" + "="*30)
    print("Multi-Device Upload Summary")
    print("="*30)
    print(f"Succeeded: {success_count}")
    print(f"Failed:    {failure_count}")
    if failed_ips:
        print(f"Failed IPs: {', '.join(failed_ips)}")
    print("="*30)

    if failure_count > 0:
        # A non-zero exit code indicates a failure to SCons.
        env.Exit(1)


# Import the 'env' object, which is the SCons construction environment for
# the current PlatformIO environment (e.g., 'esp32_cyd_ota').
Import("env")

# We only want to override the upload for the OTA environment.
# This check ensures the script doesn't interfere with other environments
# like 'esp32_cyd_serial'.
if env['PIOENV'] == 'esp32_cyd_ota':
    # Replace the default "upload" command with our custom function.
    # Now, when you run `pio run -t upload -e esp32_cyd_ota`,
    # our `upload_all_devices` function will be executed.
    env.Replace(UPLOADCMD=upload_all_devices)
