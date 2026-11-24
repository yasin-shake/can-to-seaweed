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

At first start, the launcher creates `can_pipeline/local_run/config.json` with device ID `EC000001` and uploads disabled. This local profile has its own state, logs and spool. Subsequent starts reuse it. For hardware capture, start `pipeline.py` with its original configuration.

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
