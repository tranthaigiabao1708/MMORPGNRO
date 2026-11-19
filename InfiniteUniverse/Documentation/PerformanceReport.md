# Step 1 Performance & Stability Report (`InfiniteUniverse` — UE `5.8.4`)

---

## 1. Hardware & Test Configuration

| Parameter | Verified Value |
| :--- | :--- |
| **OS** | Windows 11 (`26H2` Build `26300`) |
| **CPU** | Intel Core `i5-14600KF` (`14` Physical Cores / `20` Logical Threads) |
| **GPU** | NVIDIA GeForce `RTX 5060 Ti` (`16 GB` VRAM, DirectX 12 SM6/SM5) |
| **System RAM** | `32 GB` Physical Memory |
| **Build Target** | Packaged Standalone `Win64 Development` (`Saved/PackagedBuilds/Windows/InfiniteUniverse.exe`) |
| **Test Map** | `/Game/Maps/Prototype/Lvl_Greybox_Prototype` (`104` active actors: `1` Cel-Toon Player, `4` Cel-Toon AI Raiders via `BP_EnemySpawner`, `3` Practice Dummies, `67m` Sky Spire, `5` Luminous Flight Gates, Parkour & Colonnade) |
| **Resolution** | `1920 × 1080` (`Windowed`, `-NoVSync`, Live HUD + `F3` Telemetry Active) |

---

## 2. Live D3D12 Rendered Benchmark Results (`4,770` Frames)

Measured via `-AutoDemoShowcase -ResX=1920 -ResY=1080 -Windowed -NoVSync` executing the complete 7-stage Core Gameplay Loop (Ground Sprint/Jump → 3D Flight Takeoff/Boost/Hover/Land → Ki Blast + PointLight → Enemy Counterattack & Mirage Evade → Melee Combo Kill → Spawner Respawn):

| Metric | Target Threshold | Measured Result | Verdict |
| :--- | :---: | :---: | :---: |
| **Average Frame Rate (`Avg FPS`)** | `>= 60.0 FPS` | **`215.3 FPS`** | `PASS` (`3.58x` target) |
| **1% Low Frame Rate (`1% Low FPS`)** | `>= 45.0 FPS` | **`172.7 FPS`** | `PASS` |
| **Average Frame Time (`Avg Frame ms`)** | `<= 16.67 ms` | **`4.645 ms`** | `PASS` |
| **Pure Gameplay/AI/Anim CPU Tick (`60Hz`)** | `<= 4.00 ms` | **`0.246 ms`** | `PASS` (`FullGameplayLoopAnd60FPSBudgetProfile`) |
| **Process Physical RAM (`Start → Peak → End`)** | Stable / No runaway leak | **`915.1 MB → 1,136.4 MB → 1,115.6 MB`** | `PASS` (Settles cleanly at `~1.11 GB`) |
| **Transient Actor Cleanup (`KiProjectile` / `Afterimage`)** | `0` leaked actors | **`0` leaked** after `3,600`-frame endurance soak | `PASS` (`EnduranceStabilityMemoryAndAttributeDamage`) |
| **Crashes / Hangs / Lost Input** | `0` | **`0`** across `23` automated tests & packaged runs | `PASS` |

---

## 3. Per-Second Live D3D12 Telemetry Log (`1920×1080`)

| Sec | Avg FPS | Avg Frame (`ms`) | Physical RAM (`MB`) | Total Actors | Living AI Raiders | Active Core Loop Phase |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| `1` | `226.8` | `4.409` | `1098.1` | `104` | `4` | Spawn & 360° Camera Orbit |
| `5` | `178.0` | `5.619` | `1123.3` | `104` | `4` | Ground Sprint → 3D Flight Takeoff |
| `8` | `204.4` | `4.891` | `1128.3` | `104` | `4` | 3D Flight Boost (`FOV 100°`) & Hover |
| `12` | `260.9` | `3.832` | `1132.0` | `104` | `4` | Target Lock-On & Luminous `Ki Blast` |
| `15` | `210.1` | `4.759` | `1111.1` | `104` | `4` | Enemy Counterattack & Mirage Evade |
| `18` | `242.5` | `4.124` | `1111.6` | `104` | `3` | Melee Finisher & Raider Defeated (`3/4` Alive) |
| `22` | `252.2` | `3.965` | `1115.5` | `104` | `4` | Spawner Auto-Respawn (`4/4` Alive) |

---

## 4. Reproducible 30-Minute Unattended Burn-In Command

To run a full **30-minute (`1800s`) wall-clock D3D12 endurance burn-in** at any time:
```powershell
Start-Process -Wait "c:\MMORPGNRO\InfiniteUniverse\Saved\PackagedBuilds\Windows\InfiniteUniverse.exe" -ArgumentList "-AutoDemoShowcase -DemoDuration=1800 -ResX=1920 -ResY=1080 -Windowed -NoVSync -unattended -nopause"
```
Output CSV is saved automatically to:
`Saved/PackagedBuilds/Windows/InfiniteUniverse/Saved/Logs/Step1_LiveD3D12Profile.csv`.
