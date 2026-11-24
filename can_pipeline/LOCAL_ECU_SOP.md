# Run a hardware-free ECU test on Windows

Use this procedure to stream J1939 frames into the CAN logger, change telemetry, inject faults and capture the result in MF4. Start PowerShell in the repository root (the folder containing `can_pipeline`).

## 1. Prepare the workstation once

Install 64-bit Python 3.12 for Windows with the Python launcher. Obtain the team checkout and run the commands below. An existing project environment with these requirements can be reused.

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\can_pipeline\requirements.txt
```

For a WSL producer, check that the default distribution (marked with an asterisk) uses WSL 2 and has Python 3.8 or newer. The producer uses the standard library and the checked-in project files. Install the logger dependencies only on Windows.

```powershell
wsl --list --verbose
wsl --exec python3 --version
```

On a new WSL workstation, run `wsl --install -d Ubuntu` in an administrator PowerShell, restart if prompted, and open Ubuntu once to finish account setup. On a workstation without WSL access, use native Windows mode below. [Microsoft WSL installation guide](https://learn.microsoft.com/en-us/windows/wsl/install)

## 2. Start a session

Choose one command. Both start a background producer and a foreground Windows logger. Keep this PowerShell window open during capture.

| Producer location | PowerShell command |
| --- | --- |
| WSL, default distribution | `.\.venv\Scripts\python.exe .\can_pipeline\run_local.py --wsl` |
| Windows, existing Python environment | `.\.venv\Scripts\python.exe .\can_pipeline\run_local.py` |

Open the [local ECU dashboard](http://127.0.0.1:8081) when the logger prints its dashboard address. The producer listens on `127.0.0.1:19390`. The logger connects as a TCP client, sends `LIVE ON` and receives newline-delimited `Tx J1939` records. The baud rate applies only to physical COM ports. This setup needs neither a virtual COM driver nor a Linux CAN interface. [WSL localhost networking](https://learn.microsoft.com/en-us/windows/wsl/networking) · [pySerial socket transport](https://pyserial.readthedocs.io/en/latest/url_handlers.html#socket)

The main `can_pipeline/config.json` has two IDs. `device_id` identifies the hardware logger (`4E4BB624`) and `simulator_device_id` seeds the simulator profile (`EC000001`). These are logger IDs used in MF4 headers and S3 paths. At first start, `run_local.py` creates `can_pipeline/local_run/config.json` with uploads disabled and **After upload** set to **Archive local copy**. Subsequent starts use that local file's `device_id`, along with its own state, logs, and spool. Keep the simulator ID consistent while its spool contains files. Start `pipeline.py` for hardware capture.

## 3. Verify and operate the simulator

1. On **Telemetry**, confirm **Input connected** and new rows in **Data inspector → Raw frames**. The baseline averages about 51 frames/s over a complete trace. Open **Settings** to check input errors, the active decoder, file rotation and upload settings.
2. Open **Signal controls**. Find **Engine Speed** by its decoded identifier `EngineSpeed`, enter **1500** in **Target value (RPM)** and choose **Apply**. The decoded reading should reach **1,500 RPM** within about two seconds. The target and the decoded reading are separate. The bus resolution determines the reported value.
3. On **Telemetry**, open **Choose signals**, search for `EngineSpeed` or SPN `190`, select the signal and choose **Apply selection**. Each chart shows the decoder identifier, label, unit and latest decoded value. Compare another setpoint over a 30-second, one-minute or five-minute window. Charts initially show up to six observed signals. Pick up to twelve, including signals from different ECUs. Chart selections persist in this browser.
4. **Smoothed** applies a three-second exponential moving average to the trend line. Turn it off to inspect the chart samples. The logger retains the last decoded reading in each one-second interval for charting. Gaps and unavailable values break the line. The numeric readout shows the latest decoded value and the MF4 retains the captured frames. **Data inspector** also includes textual signals and unavailable parameters.
5. In **Fault diagnostics**, enter SPN **110**, FMI **0**, occurrence count **1**, select **Send once** and choose **Send fault**. Check the received events table. **Repeat each cycle** adds a recurring fault. Two or more active faults produce a multi-packet DM1. **Clear recurring faults** empties the list while retaining the observed event counts.
6. **Restore baseline** on **Signal controls** clears overrides and recurring faults. The logger keeps recording. **Activity log** shows the received events and file writes.

For scripted QA, run the same operations in a second PowerShell window. Successful commands print `ACK`. Invalid commands return a nonzero exit code.

```powershell
.\.venv\Scripts\python.exe .\can_pipeline\local_simulator.py --send SET COOLANT 95
.\.venv\Scripts\python.exe .\can_pipeline\local_simulator.py --send FAULT 110 0 1
.\.venv\Scripts\python.exe .\can_pipeline\local_simulator.py --send RESET
```

Other commands include `DTC ADD <spn> <fmi> [oc]`, `DTC LAMP <0..255>`, `DTC OFF`, `DTC ON` and `DTC CLEAR`. Engineering channel names are `COOLANT`, `OILPRESS`, `BOOST`, `VOLT`, `TRANSOIL`, `AMBIENT`, `FUEL`, `RPM` and `SPEED`.

## 4. Stop and collect the recording

Press **Ctrl+C** once in the launcher window. Wait for **Stopped** before closing it. The logger finishes the current MF4 and the launcher releases the producer port. The same start command begins a new session.

```powershell
Get-ChildItem .\can_pipeline\local_run\spool -Recurse -Filter *.MF4 |
    Sort-Object LastWriteTime -Descending | Select-Object -First 3 FullName,Length
```

Each MF4 has a companion `.meta.json` with frame and DTC counts. The first-run profile inherits file rotation settings from the hardware profile. A short test can produce its first closed file when you stop. Logs are in `can_pipeline/local_run/logs/pipeline.log`.

## Verify one recording in S3

Keep **Settings → Upload to S3** set to **Off** for a one-file check. Enabling it processes the entire simulator spool using the selected **After upload** policy.

1. In PowerShell, list completed simulator recordings and select the session and filename you want to verify.

   ```powershell
   Get-ChildItem .\can_pipeline\local_run\spool -Recurse -Filter *.MF4 |
       Select-Object DirectoryName,Name,Length
   ```

2. From the repository root, run the following command with your chosen session and filename.

   ```powershell
   .\.venv\Scripts\python.exe .\can_pipeline\s3_test.py --local --verify-file 00000003/00000004-6B0543C3.MF4
   ```

   The command uses the normal device/session object key, downloads the result, compares the complete SHA-256 digest and retains the local file. A retry at an existing key verifies the stored bytes without replacing that object.

3. Open **S3 monitor** to see the active profile and device ID. The latest manual and automatic attempts show whether S3 accepted the upload and whether the downloaded MF4 matched the local SHA-256. **Last verified MF4** shows the S3 URI, byte count, timestamp and SHA-256 at verification time. The pending count still includes a file retained in the spool after a one-file check. When automatic uploads drain the spool, each file goes to the session in which it was recorded.

For a connectivity-only check use `s3_test.py --local` without a filename. This writes a small object beneath `_test/`. A new successful probe is visible to the hardware and simulator dashboards when the endpoint, region, bucket and prefix match. Older results remain associated with their original profile. After updating the code, restart the logger once and reload the dashboard. Subsequent results appear on its next status refresh.

Both test modes update the chosen profile's `state.json` with the latest S3 session for its device, the highest session across the hardware and simulator profiles when their destinations match, and whether S3 already lists the selected session prefix. Use `--local` to check the simulator's current target. With `session_from_bucket` enabled, startup also lists S3 and picks a number above the matching profiles' remote sessions and the selected profile's local counter. Each profile uploads under its own device ID. The S3 credentials need permission to list both matching device prefixes as well as upload and read the test object.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| WSL reports access denied, fails to start or cannot find Python | Run the two WSL checks in step 1 in your Windows session. Native mode runs the same QA procedure. |
| The source or dashboard port is in use | Check `Get-NetTCPConnection -State Listen -LocalPort 19390,8081`. Stop the previous local run from its own terminal. The hardware dashboard normally uses port 8080. |
| WSL starts but Windows input stays disconnected | Run `Test-NetConnection 127.0.0.1 -Port 19390` from Windows. Check WSL localhost forwarding, or stop the run and use native mode. |
| A value reverts or a fault appears repeatedly | The latest applied target for a signal wins. Recurring faults increment the observed count each cycle. Use **Send once** for a single event. |
| A one-shot command reports no live receiver | Wait for the TCP input to connect before sending a fault. |

## Simulation coverage

The baseline in `simulator_trace.csv` contains six complete cycles from the logger's COM3 capture `00000020-6B041225.MF4` in session 13. It repeats 372 frames across 22 CAN IDs over 7.277519 seconds, retaining byte lengths, payloads, order and observed inter-frame delays. New MF4s use the current receive time and reflect host scheduling. Recorded counters repeat with the trace. Live controls change selected J1939 fields or generate DM1 frames independently.

The sketch header expressly restricts porting its source to another device. The PC producer uses captured output and the logger's signal definitions. Its scope is application-level J1939 QA. Physical CAN arbitration, bus errors, RS-485/J1708 traffic and firmware commands outside the controls above require a separate test setup.
