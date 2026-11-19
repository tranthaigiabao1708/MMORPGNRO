# Step 2 Network Performance, Bandwidth & WAN Latency Report (`InfiniteUniverse` — UE `5.8.4`)

---

## 1. Hardware & Multi-Process Test Topology

| Parameter | Verified Value |
| :--- | :--- |
| **OS** | Windows 11 (`26H2` Build `26300`) |
| **CPU / GPU / RAM** | Intel Core `i5-14600KF` (`14C / 20T`) · NVIDIA GeForce `RTX 5060 Ti` (`16 GB` VRAM, D3D12 SM6) · `32 GB` RAM |
| **Process 1 (Authority)** | **Headless Dedicated Server (`NM_DedicatedServer / NetMode=1`)** — `UnrealEditor.exe ... -server -NullRHI -port=7777` |
| **Process 2 (`Client 1`)** | **Packaged Cooked `Win64 Development` (`NM_Client / NetMode=3`)** — `127.0.0.1:7777` (`960×540` Windowed D3D12, `-NoVSync`) |
| **Process 3 (`Client 2`)** | **Packaged Cooked `Win64 Development` (`NM_Client / NetMode=3`)** — `127.0.0.1:7777` (`960×540` Windowed D3D12, `-NoVSync`) |
| **Active World Load** | `/Game/Maps/Prototype/Lvl_Greybox_Prototype` (`106` Server actors / `94` Replicated Client actors: `2` Live Cel-Toon Players, `4` Replicated AI Raiders, `36m` Martial PvP Arena, `67m` Sky Spire, Projectiles & `UPoseableMeshComponent` Afterimages) |

> **Important Note on Concurrent GPU Load:** Both D3D12 Client windows AND the Headless Dedicated Server ran **simultaneously on the same physical machine**, sharing CPU cores and GPU frame-time while still exceeding `124–132 Avg FPS` (`66–69` 1% Low FPS) per Client window!

---

## 2. Run #1 — Localhost / LAN Multi-Process Benchmark (`3` Concurrent Processes)

Measured across the full `27.5s` 4-Stage Multiplayer Loop (**Co-op Friendly-Fire Block → 2-Player 3D Flight Formation (`FSavedMove`) → Co-op Enemy AI Raid & Respawn → West Martial Arena 1v1 PvP & Mirage Perfect Evade**):

| Process / Metric | Gatekeeper Target | Measured Result | Verdict |
| :--- | :---: | :---: | :---: |
| **Dedicated Server Tick Rate (`Avg / 1% Low`)** | `>= 30.0 Hz` | **`33.9 Hz Avg` / `31.2 Hz` 1% Low** | **`PASS`** |
| **Dedicated Server Physical RAM (`Start → End`)** | Stable / No leak | **`1,418.0 MB → 1,510.4 MB`** (flat steady-state `1,506→1,510 MB`) | **`PASS`** |
| **Dedicated Server Total Bandwidth (`2 Clients`)** | `< 30.0 KB/s` (`< 15/client`) | **`9.79 KB/s In` / `5.32 KB/s Out`** (`~4.90 In / 2.66 Out` per client) | **`PASS`** |
| **Client 1 Frame Rate (`Avg / 1% Low FPS`)** | `>= 60.0 FPS` | **`132.2 Avg FPS` / `69.2` 1% Low FPS** (`7.561 ms`) | **`PASS`** |
| **Client 2 Frame Rate (`Avg / 1% Low FPS`)** | `>= 60.0 FPS` | **`124.1 Avg FPS` / `66.3` 1% Low FPS** (`8.056 ms`) | **`PASS`** |
| **Client Per-Connection UDP Bandwidth (`In / Out`)** | `< 15.0 KB/s` | **`2.73 KB/s In` / `5.04 KB/s Out`** (`7.77 KB/s` combined!) | **`PASS`** |
| **Client Physical RAM (`Client 1 / Client 2 End`)** | Stable / No leak | **`1,113.2 MB` / `1,105.5 MB`** | **`PASS`** |

---

## 3. Run #2 — Harsh WAN Latency, Jitter & Packet Loss Stress Test (`~110–130 ms RTT, 2% Loss`)

Injected live packet-layer degradation on **both** Server (`Net PktLag=40, Net PktLagVariance=8, Net PktLoss=1`) and Clients (`Net PktLag=40, Net PktLagVariance=7, Net PktLoss=1`), producing **`108.8 ms – 129.8 ms` real round-trip ping** with `±15 ms` jitter and `2%` bidirectional UDP packet loss:

| Process / Metric | Gatekeeper Target | Measured Result under WAN Lag & Loss | Verdict |
| :--- | :---: | :---: | :---: |
| **Measured Live Client RTT (`PingMs`)** | `>= 80 ms` WAN test | **`108.8 ms – 129.8 ms` RTT** | **`PASS`** |
| **Dedicated Server Tick Rate (`Avg / 1% Low`)** | `>= 30.0 Hz` | **`40.6 Hz Avg` / `31.2 Hz` 1% Low** | **`PASS`** |
| **Client 1 Frame Rate (`Avg / 1% Low FPS`)** | `>= 60.0 FPS` | **`128.9 Avg FPS` / `69.3` 1% Low FPS** | **`PASS`** |
| **Client 2 Frame Rate (`Avg / 1% Low FPS`)** | `>= 60.0 FPS` | **`125.5 Avg FPS` / `67.3` 1% Low FPS** | **`PASS`** |
| **Client UDP Bandwidth under Retransmits** | `< 15.0 KB/s` | **`3.21 KB/s In` / `5.24 KB/s Out`** (`8.45 KB/s` combined) | **`PASS`** |
| **State Machine Recovery (`Attacking/Dodging/Flying/Dead`)** | `0` stuck states | **`0` deadlocks** (`3` AI Raiders + `2` PvP Knockouts & Respawns completed cleanly) | **`PASS`** |
| **Crashes / Unexpected Disconnects** | `0` | **`0`** (`ExitCode 0` on Server, Client 1, Client 2) | **`PASS`** |

---

## 4. Per-Second Live Multi-Process Telemetry Excerpt (`Step2_Client1Profile.csv` & `Step2_Client1Profile_WANLag.csv`)

| Sec | Mode | Client 1 FPS | Frame (`ms`) | RAM (`MB`) | Online Players | Living AI | Net In (`KB/s`) | Net Out (`KB/s`) | Ping (`ms`) | Active Multiplayer Phase |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| `2` | LAN | `138.9` | `7.198` | `1049.3` | `2` | `4` | `1.58` | `5.83` | `22.9` | Co-op Spawn & Friendly-Fire Guard |
| `8` | LAN | `158.8` | `6.295` | `1131.6` | `2` | `4` | `2.76` | `5.30` | `18.7` | 2-Player 3D Flight Formation (`FSavedMove`) |
| `14` | LAN | `103.8` | `9.631` | `1119.7` | `2` | `3` | `4.01` | `5.15` | `21.6` | Co-op Raid Enemy Defeated (`3/4` Alive) |
| `22` | LAN | `129.4` | `7.727` | `1103.1` | `2` | `4` | `2.51` | `7.32` | `22.3` | West Martial Arena 1v1 PvP & Mirage Evade |
| `20` | WAN | `170.1` | `5.879` | `1145.0` | `2` | `4` | `4.53` | `4.93` | `109.0` | WAN `~115ms` RTT + `2%` Loss Arena PvP |
| `25` | WAN | `124.6` | `8.028` | `1123.3` | `2` | `4` | `2.14` | `4.84` | `126.9` | WAN `~127ms` RTT Respawn & Sync Recovery |

---

## 5. Reproducible Multi-Process Launch Commands

```powershell
# 1. Interactive 1 Dedicated Server + 2 Side-by-Side Windowed Clients (Manual Playtest):
powershell -ExecutionPolicy Bypass -File "c:\MMORPGNRO\InfiniteUniverse\Tools\LaunchMultiplayerTest.ps1"

# 2. Automated 3-Process Benchmark (LAN):
powershell -ExecutionPolicy Bypass -File "c:\MMORPGNRO\InfiniteUniverse\Tools\LaunchMultiplayerTest.ps1" -AutomatedDemoCapture

# 3. Automated 3-Process WAN Lag / Jitter / 2% Packet Loss Stress Test:
powershell -ExecutionPolicy Bypass -File "c:\MMORPGNRO\InfiniteUniverse\Tools\LaunchMultiplayerTest.ps1" -AutomatedDemoCapture -SimulateWANLag
```
