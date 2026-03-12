# Quicknitch — Technical Product Design Document

---

## Product Vision

Quicknitch is a palm-sized autonomous flying AI companion — the AirPods of personal AI. It follows you, observes your environment, answers questions through voice, and connects to powerful cloud AI systems. It is designed for extreme miniaturization, ultra-low manufacturing cost, and global mass-market consumer adoption.

---

## 1. Hardware Architecture

**Physical Form Factor**

Quicknitch targets a 68mm tip-to-tip propeller span, a 32mm body diameter, and a total mass of 28–34g including battery. The body is a flattened oblate sphere — inspired by the Golden Snitch — with two counter-rotating coaxial micro-rotors for vertical thrust and four tilt-actuated lateral micro-rotors for full 6-DOF maneuverability. Propellers are shrouded inside ducted rings molded into the chassis, eliminating exposed blades entirely.

**Propulsion**

Eight coreless brushed DC micro-motors (6mm × 15mm) drive 40mm, 3-blade injection-molded polycarbonate props at ~16,000 KV. Total thrust at full throttle: ~90g, giving a 2.5× thrust-to-weight ratio. Motor drivers are DRV8837 dual H-bridge ICs. Hover draw is approximately 3.5W at 50% throttle; full throttle peaks at 6.5W.

**Compute & Sensors**

Primary SoC: Espressif ESP32-S3 (dual-core Xtensa LX7 @ 240MHz, 8MB PSRAM, integrated Wi-Fi 6 + BLE 5.0). Secondary AI accelerator: Himax WE-I Plus with always-on camera and hardware CNN inference at under 2mW. IMU: ICM-42688-P at 32kHz ODR. Altitude hold via BMP388 barometer. Indoor positioning via PMW3901 optical flow sensor. Obstacle avoidance via four VL53L4CX ToF proximity sensors (front, rear, left, right).

**Camera & Audio**

Main camera: OV2640 2MP with JPEG hardware compression. Always-on micro-cam: HM01B0 on the Himax SoC for wake-triggered vision at under 2mW. Microphone array: two MEMS microphones (SPH0645LM4H) spaced 22mm apart for beamforming and motor-wash noise rejection.

**Battery & Charging**

250mAh 1S LiPo (3.7V, ~5.5g). Managed by BQ25180 buck-boost IC. Expected flight time: 8 minutes at hover, 5 minutes under active AI streaming. The AirPods-style charging case holds 5× capacity (1250mAh), charges via USB-C PD, and refuels Quicknitch through pogo-pin contacts in 35 minutes. The case recharges fully in 90 minutes.

**Power Budget Summary**

| Subsystem                | Idle  | Active   |
| ------------------------ | ----- | -------- |
| Motors (hover)           | —     | 3,500mW  |
| ESP32-S3 + Wi-Fi         | 20mW  | 400mW    |
| Himax AI vision          | 2mW   | 80mW     |
| Camera + streaming       | —     | 200mW    |
| Sensors (IMU, ToF, flow) | 5mW   | 30mW     |
| **Total**                | ~27mW | ~4,210mW |

**Heat & Vibration**

Motors are the primary heat source. Thermal vias in the PCB and copper pours route heat to the outer chassis shell. Operating temperature range: 0–40°C. Vibration isolation between motor mounts and the main PCB is achieved using silicone grommets and soft-mount motor standoffs, protecting the IMU from propeller harmonics.

---

## 2. Flight and Navigation System

**Stabilization**

The flight controller runs a cascaded PID loop at 1kHz on the ESP32-S3. Outer loop: position/velocity control. Inner loop: attitude and rate control. Motor mixing handles the coaxial + lateral rotor geometry. The ICM-42688-P IMU feeds attitude estimation via a Madgwick AHRS filter at 500Hz.

**Indoor Navigation & Positioning**

GPS-free by default. Position is estimated by fusing optical flow (PMW3901) with barometric altitude (BMP388) and IMU dead-reckoning. This gives stable hover and slow translation indoors. For richer indoor localization, the always-on downward camera runs a lightweight monocular visual odometry pipeline, providing centimeter-scale drift correction over 3–5 meters.

**User Following**

The main camera runs a lightweight MobileNetV2-based person detector (quantized to INT8, running on the Himax CNN accelerator). The detected bounding box feeds a proportional controller that keeps the user centered in frame at a configurable follow distance (default: 1.0–1.5m). The UWB module (optional SKU: Qorvo DWM3001C) provides sub-10cm ranging to the paired smartphone as a reliable fallback tether when vision fails (cluttered scene, low light).

**Orbit and Hover Modes**

Preset behavioral modes include: hover-and-listen (stationary, mic active), orbit (circular flight around user at fixed radius), follow (track user movement), perch (land on a flat surface to save battery), and return-to-case (autonomous dock). Mode transitions are triggered by voice command or smartphone app.

**Obstacle Avoidance**

The four ToF sensors provide 60cm range bubbles on each cardinal axis. Any sensor reading below a configurable threshold (default: 25cm) triggers an emergency vector redirect. At slow flight speeds (under 0.8 m/s), this is sufficient for typical indoor environments. Future hardware revisions can add a fisheye stereo camera for full 3D depth avoidance.

---

## 3. AI System Architecture

**On-Device AI (Edge)**

The Himax WE-I Plus handles all always-on, low-power inference tasks: wake-word detection ("Hey Nitch") using a depthwise-separable CNN trained on the user's voice, simple gesture detection (wave, point, thumbs-up) from the micro-cam, and basic safety scene detection (detecting that the device is indoors, near glass, or above a staircase). These models are quantized to INT8 and run entirely on-device, consuming under 5mW combined.

The ESP32-S3 handles mid-complexity tasks when awake: face recognition for user identification, optical flow math, motor mixing, PID execution, and Wi-Fi/BLE stack management.

**Cloud AI Integration**

Once a wake word fires, audio is captured, VAD (voice activity detection) segments the utterance, and the PCM stream is compressed to Opus codec at 16kHz, 32kbps. This stream is sent over Wi-Fi to the CloudAI backend via WebSocket.

The backend pipeline is:
1. **Speech-to-Text** — Whisper (OpenAI) or equivalent, ~200ms latency
2. **LLM Reasoning** — GPT-4o or Claude 3.5 Sonnet with a personalized system prompt containing user memory context
3. **Text-to-Speech** — ElevenLabs or Kokoro (open-source) generating a natural voice
4. **Response Streaming** — Audio streamed back to Quicknitch in 100ms chunks, played through the onboard piezo speaker or a paired Bluetooth earbud

Total round-trip latency target: under 1.2 seconds from end of speech to first audio byte.

**Edge + Cloud AI Split**

| Task                       | Where                |
| -------------------------- | -------------------- |
| Wake word                  | On-device (Himax)    |
| Gesture detection          | On-device (Himax)    |
| Face recognition           | On-device (ESP32-S3) |
| Flight stabilization       | On-device (ESP32-S3) |
| Obstacle avoidance         | On-device (ESP32-S3) |
| Speech-to-Text             | Cloud                |
| LLM reasoning              | Cloud                |
| Text-to-Speech             | Cloud                |
| Long-term memory retrieval | Cloud                |
| Video understanding        | Cloud (on demand)    |

---

## 4. Communication Architecture

**Smartphone Link**

Primary link: Wi-Fi 6 (2.4GHz) to the user's phone acting as a local access point, or directly to a home router. BLE 5.0 is used for low-power command and telemetry when not streaming (mode changes, battery status, pairing). UWB (optional) provides sub-10cm ranging for precise user location as a follow-mode supplement.

**Cloud Link**

Quicknitch routes all cloud traffic through the paired smartphone (acting as a relay) or directly via the home Wi-Fi router. A custom MQTT-over-TLS channel handles telemetry and command. A WebSocket channel over HTTPS handles real-time audio streaming. Video clips are uploaded asynchronously to cloud storage via chunked HTTP POST.

**Compression Strategy**

Audio: Opus at 32kbps (real-time, ~4KB/s). Video stream to cloud: H.264 at 480p, 15fps, ~400kbps. Video is only streamed on explicit user request ("what do you see?") to conserve battery and bandwidth. Still frames (JPEG ~50KB) are uploaded for visual question answering.

**Offline Fallback**

When Wi-Fi is unavailable, Quicknitch falls back to BLE tethering to the smartphone, which relays cloud traffic over cellular. If both are unavailable, the device enters offline mode: on-device wake word, limited local responses ("I'm offline, please reconnect"), and safe hover or perch behavior.

---

## 5. Manufacturing Strategy

**PCB Design**

A single 4-layer flex-rigid PCB integrates the ESP32-S3 module, Himax WE-I Plus, IMU, barometer, motor drivers, power management IC, and all passive components. The PCB is shaped to the interior contour of the shell, maximizing volume efficiency. LGA and QFN packages are used throughout to minimize footprint. The camera and optical flow sensor are connected via short ZIF flex cables.

**Body & Shell**

The shell is two-piece injection-molded ABS/PC blend — upper and lower halves that snap together and are ultrasonically welded. Total plastic mass: ~8g. Tooling cost: ~$25,000 per mold. At 100K units/year, tooling is fully amortized within the first production run. Motor ducts are integrated into the lower shell as a single part.

**Assembly**

Automated SMT reflow for PCB population. Manual or semi-automated motor press-fit into duct housings. Battery hot-glued and pogo-pin connected. Camera module ribbon-cable inserted. Shell halves ultrasonically welded. Final calibration (IMU offset, camera focus, motor balance) performed on automated test jigs at 30 seconds per unit.

**Component Sourcing at Scale**

| Component         | Source Strategy                                         |
| ----------------- | ------------------------------------------------------- |
| ESP32-S3          | Espressif — high-volume pricing ~$2.10/unit             |
| Himax WE-I Plus   | Himax — ~$1.80/unit                                     |
| Micro-motors (×8) | Chinese OEM (e.g., BETAFPV supply chain) — ~$0.30/motor |
| LiPo battery      | Shenzhen battery manufacturer — ~$1.50/cell             |
| ToF sensors (×4)  | STMicro VL53L4CX — ~$0.70/unit                          |
| OV2640 camera     | OmniVision — ~$1.20/unit                                |
| Shell (2 halves)  | Chinese injection molder — ~$0.90/unit at 500K          |

**Estimated BOM at 1M Units: ~$28–34**

Target retail: $129–149. Charging case adds ~$8 BOM. Target case retail: included in box.

---

## 6. Personalization System

**User Recognition**

On first setup, Quicknitch captures 10 face embeddings from multiple angles, stored as a 128-dimensional FaceNet vector on-device and backed up (encrypted) to the user's cloud profile. On subsequent sessions, the on-device face recognizer identifies the owner within 1–2 seconds of visual contact.

**Voice Fingerprinting**

The wake word model is fine-tuned on 30 seconds of the user's voice during onboarding, improving false-accept rejection for household members. Voice identity verification runs as a secondary filter on all commands.

**Personal Memory**

All conversations are stored as timestamped transcripts in the user's cloud profile. Before each LLM call, a retrieval-augmented generation (RAG) pipeline fetches the 5 most contextually relevant prior exchanges and injects them into the system prompt. Over time, Quicknitch builds a model of user preferences, habits, schedule patterns, and frequently asked topics. This memory is fully portable — downloadable and deletable by the user.

**Behavioral Customization**

Users configure follow distance, response voice, personality tone, hover height, and privacy zones (geofenced rooms where camera is disabled) through the companion app. A "Focus Mode" disables all proactive interruptions except safety alerts.

---

## 7. Ecosystem and Software Platform

**Companion App**

iOS and Android app built in React Native. Features: live camera feed, flight mode selector, conversation history, memory browser, device settings, firmware OTA updates, and charging case battery status. The app also acts as the Wi-Fi relay bridge when Quicknitch is outside home network range.

**Cloud Device Management**

Fleet management backend (for enterprise / family accounts) built on AWS IoT Core + DynamoDB. Handles: device registration, OTA firmware delivery, telemetry logging, crash reporting, and remote diagnostics. All data encrypted at rest (AES-256) and in transit (TLS 1.3).

**Developer API**

A REST + WebSocket API allows third-party developers to:
- Subscribe to real-time sensor streams (camera, audio, IMU)
- Issue flight commands (go to position, orbit, hover)
- Trigger custom TTS responses
- Register custom wake phrases
- Access conversation transcripts (with user permission)

Rate-limited tiers: free (hobbyist), $29/month (developer), $299/month (commercial).

**AI Skill Marketplace**

Users can install "Skills" — small cloud-side prompt + API integrations that give Quicknitch new capabilities. Examples: Smart Home Controller (connects to Home Assistant), Personal Trainer (fitness coaching with rep counting via camera), Language Tutor, Meeting Transcriber. Revenue split: 70% developer / 30% Quicknitch platform.

---

## 8. Safety and Regulatory Design

**Physical Safety**

Fully shrouded propellers in molded duct rings — fingers and hair cannot contact blades. Maximum flight speed capped at 1.5 m/s in consumer mode. Automatic motor cutoff on freefall detection (>0.3g negative Z for >200ms). All sharp edges removed from the shell per IEC 62368-1 consumer electronics safety standard.

**Geofencing & Privacy Zones**

Users define 3D privacy zones via the app (e.g., "no flying in the bedroom"). The device stores these as polygon exclusion zones in flash. Camera recording is automatically disabled when inside a privacy zone. A physical LED ring (visible from all angles) illuminates red when the camera is active — non-defeatable in firmware, controlled by hardware logic.

**Regulatory Compliance**

At 28–34g, Quicknitch falls below the 250g threshold that triggers drone registration in the United States (FAA), European Union (EASA), and most other jurisdictions. The device is classified as a toy/consumer electronics device, not an unmanned aircraft system. Wi-Fi and BLE modules are FCC Part 15 / CE certified using pre-certified ESP32-S3 modules, dramatically reducing certification cost and timeline.

**Data Privacy**

All video and audio processed on-device is discarded unless the user explicitly triggers a save. Cloud transcripts are stored under GDPR/CCPA-compliant data handling. No data is sold to third parties. On-device face embeddings are never transmitted unless user opts into cloud backup with explicit consent.

---

## 9. Business Model

**Hardware**

Quicknitch device + charging case: $149 MSRP. BOM + manufacturing: ~$42 fully loaded. Gross margin: ~72%. Channel: direct-to-consumer (quicknitch.com), Amazon, Apple retail, Best Buy. First-year target: 500K units = ~$74M revenue.

**AI Subscription**

Quicknitch Free: 50 AI queries/day, basic memory (30 days). Quicknitch Plus: $9.99/month — unlimited queries, 2-year memory, priority latency, video understanding, skill marketplace access. Quicknitch Family: $14.99/month — up to 4 devices. Target: 40% subscription attach rate at 12 months post-purchase. At 2M active devices and 40% attach: ~$96M ARR.

**Developer & Enterprise**

API access tiers as described above. Enterprise licensing for custom-branded Quicknitch hardware (hospitality, retail, education). White-label manufacturing program for OEM partners.

**Skill Marketplace**

Platform 30% take rate on all paid skill subscriptions. Projected at 2M users with average $3/month skill spend: ~$21.6M ARR gross platform revenue.

---

## 10. Long-Term Vision

Today, the smartphone is the primary personal computing surface. It demands your eyes, your hands, and your full attention. Quicknitch proposes a fundamentally different computing paradigm: ambient, spatial, and always-present without being always-demanding.

In the near term (2–5 years), Quicknitch becomes the AI interface for the home — the device you talk to while cooking, that watches your baby while you're in the next room, that reads you messages while you exercise, that documents your life without requiring you to hold anything.

In the medium term (5–10 years), as battery energy density improves (solid-state cells at 500+ Wh/kg), miniaturized LiDAR becomes cost-viable, and on-device LLM inference reaches the capability of today's cloud models, Quicknitch evolves into a fully autonomous AI agent. It navigates complex indoor and outdoor environments, proactively manages your schedule, interfaces with AR glasses as a spatial AI engine, and coordinates with other Quicknitch devices in multi-agent swarm behaviors.

In the long term, a world where every person owns a personal flying AI companion is a world where the computing interface has finally escaped the rectangle. The phone required the internet. The earbud required wireless audio. Quicknitch requires the convergence of micro-robotics, edge AI, and large language models — three technologies that are all arriving simultaneously, right now.

The device category Quicknitch creates — the **Personal Flying AI Interface** — becomes the third great consumer computing platform after the PC and the smartphone. It is embodied, spatial, voice-first, and always with you. Not in your pocket. In your world.

---

*Quicknitch Technical Design Document — Revision 1.0*