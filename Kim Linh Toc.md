# QUICKNITCH / 金翎速
## Startup Concept Document — Personal AI Companion Drone

*"The first AI that doesn't just talk to you — it flies with you."*

---

# 1. PRODUCT VISION

## Vấn đề Quicknitch giải quyết

Trợ lý AI hiện tại bị giam cầm trong màn hình. Dù Siri, ChatGPT hay Gemini thông minh đến đâu, chúng vẫn đòi hỏi người dùng phải nhìn xuống điện thoại — một hành động phá vỡ dòng chảy của cuộc sống thực.

Quicknitch giải quyết **nghịch lý cơ bản của AI cá nhân**: AI càng mạnh, người dùng càng phải dừng lại để dùng nó.

**Các pain points cụ thể:**

- Điện thoại là rào cản vật lý giữa người dùng và thế giới
- Smartwatch quá nhỏ để hiển thị context đầy đủ
- Smart glasses vẫn chưa được xã hội chấp nhận rộng rãi
- Không có thiết bị nào có thể quan sát môi trường xung quanh người dùng từ góc nhìn độc lập

Quicknitch là câu trả lời: **một AI có thân xác — nhưng thân xác đó biết bay.**

## Vì sao mọi người muốn một AI companion biết bay?

**Góc độ cảm xúc & văn hóa:**
Kể từ thời R2-D2, WALL-E, đến Golden Snitch của Harry Potter — con người luôn mơ về một người bạn đồng hành nhỏ bé, thông minh, trung thành. Quicknitch không phải là gadget. Nó là *nhân vật*. Người dùng đặt tên cho nó, tùy chỉnh giọng nói, màu đèn LED, thậm chí "tính cách" của nó.

**Góc độ thực dụng:**
- Bay lên để quan sát toàn cảnh khi người dùng cần định hướng
- Ghi lại khoảnh khắc từ góc nhìn thứ ba mà không cần selfie stick
- Đọc thông tin, dịch ngôn ngữ, nhận diện vật thể theo thời gian thực
- Là "mắt" phụ khi người dùng bận tay (nấu ăn, lái xe đạp, tập gym)

**So sánh định vị:**

| Sản phẩm       | Hình thức     | Tương tác                   |
| -------------- | ------------- | --------------------------- |
| AirPods        | Tai nghe      | Thụ động, âm thanh          |
| Apple Watch    | Đồng hồ       | Chủ động, màn hình nhỏ      |
| Meta Ray-Ban   | Kính          | Thụ động, camera cố định    |
| **Quicknitch** | **Bay tự do** | **Chủ động, không gian 3D** |

---

# 2. HARDWARE ARCHITECTURE

## Triết lý thiết kế: "Minimum Viable Drone"

Giống như Apple loại bỏ headphone jack để đơn giản hóa, Quicknitch loại bỏ mọi thứ không cần thiết. Không GPS module lớn. Không gimbal 3 trục. Không cảm biến LiDAR đắt tiền.

**Chỉ những gì tối cần thiết để bay, nghe, nhìn và nói.**

## Thông số mục tiêu (Target Specs — Gen 1)

| Thông số      | Mục tiêu                                |
| ------------- | --------------------------------------- |
| Trọng lượng   | < 35g                                   |
| Kích thước    | ~80mm đường kính (cỡ lòng bàn tay)      |
| Thời gian bay | 12–18 phút / lần sạc                    |
| Thời gian sạc | 25 phút (dock tích hợp)                 |
| Tiếng ồn      | < 55 dB ở khoảng cách 1m                |
| Kết nối       | Bluetooth 5.3 + Wi-Fi 6                 |
| Camera        | 12MP, f/2.4, wide-angle, video 4K/30fps |
| Microphone    | 3-mic array beamforming                 |

## Cấu trúc cơ học

**Khung (Frame):**
- Vật liệu: Carbon fiber reinforced polymer (CFRP) + polycarbonate shroud
- Thiết kế: 4 cánh quạt được bảo vệ bởi guard vòng tròn tích hợp — vừa giảm tiếng ồn, vừa tăng an toàn khi gần người
- Rotor guard cũng đóng vai trò khung cấu trúc, giảm số lượng linh kiện

**Hệ thống động lực:**
- 4× Brushless DC motor siêu nhỏ (đường kính 11–13mm), tương tự motor trong DJI Mini series nhưng được tối ưu hóa lại
- ESC (Electronic Speed Controller) tích hợp trực tiếp vào PCB chính — loại bỏ dây kết nối rời
- Cánh quạt 65mm, pitch tối ưu cho hiệu suất/tiếng ồn

**Pin:**
- LiPo 1S hoặc 2S, ~500–700mAh
- Charging dock dạng viên nang (tương tự AirPods case) với pin dự phòng 2000mAh cho 3–4 lần sạc đầy
- Wireless charging Qi tích hợp trong dock

## Cảm biến Onboard

**Visual System:**
- 1× camera chính 12MP (quan sát & AI vision)
- 1× camera downward-facing (optical flow cho hovering ổn định trong nhà — không cần GPS)
- Không có gimbal cơ học → dùng Electronic Image Stabilization (EIS) để giảm trọng lượng

**Audio System:**
- 3-microphone array (beamforming) để lọc tiếng ồn rotor
- 1× miniature speaker (0.5W) cho phản hồi âm thanh

**Navigation & Stability:**
- IMU 6-DOF (accelerometer + gyroscope)
- Barometer (ổn định độ cao)
- Optical flow sensor (hovering trong nhà không cần GPS)
- ToF (Time-of-Flight) sensor mini (tránh vật cản gần)

**Kết nối:**
- Bluetooth 5.3 (giao tiếp chính với điện thoại)
- Wi-Fi 6 2.4/5GHz (stream video, cloud AI)
- UWB chip (ultra-wideband) cho precision tracking với điện thoại người dùng

## PCB & Chip Architecture

```
┌─────────────────────────────────────────┐
│           MAIN FLIGHT CONTROLLER        │
│  STM32H7 hoặc NXP i.MX RT series       │
│  (flight control + sensor fusion)       │
├─────────────────────────────────────────┤
│           AI EDGE PROCESSOR             │
│  Qualcomm QCC743 hoặc                   │
│  Espressif ESP32-S3 (cho gen đầu)       │
│  (wake word detection, basic NLP)       │
├─────────────────────────────────────────┤
│           CONNECTIVITY MODULE           │
│  BT 5.3 + Wi-Fi 6 combo chip           │
│  (Nordic nRF7002 hoặc tương đương)     │
├─────────────────────────────────────────┤
│           CAMERA ISP                    │
│  Sony IMX377 hoặc OmniVision OV12895   │
└─────────────────────────────────────────┘
```

---

# 3. AI ARCHITECTURE

## Triết lý: "Thin Edge, Rich Cloud"

Quicknitch không cố gắng là một supercomputer biết bay. Phần lớn "trí thông minh" sống trên cloud. Thiết bị chỉ xử lý những gì bắt buộc phải xử lý locally để đảm bảo latency và privacy cơ bản.

## Edge AI (On-device)

**Chạy locally trên chip:**

- **Wake word detection**: "Hey Nitch" — luôn lắng nghe, tiêu thụ < 1mW
- **Basic flight control**: PID loop, sensor fusion, obstacle avoidance phản xạ
- **Face/person tracking**: Lightweight model để theo dõi chủ nhân (MobileNet-based)
- **Audio pre-processing**: Noise cancellation, beamforming, voice activity detection (VAD)
- **Privacy filter**: Detect & blur khuôn mặt người lạ trước khi gửi lên cloud (tuỳ chọn)

## Cloud AI Stack

```
┌──────────────────────────────────────────────────┐
│                  USER'S SMARTPHONE               │
│         (Local relay & UI + BLE gateway)         │
└──────────────────┬───────────────────────────────┘
                   │ Wi-Fi / 5G
┌──────────────────▼───────────────────────────────┐
│              QUICKNITCH CLOUD PLATFORM           │
├──────────────────────────────────────────────────┤
│  Speech-to-Text   │  Whisper-based ASR           │
│  Conversational   │  LLM (GPT-4o / Claude API /  │
│  AI Engine        │  Qwen cho thị trường TQ)     │
│  Computer Vision  │  Object detection, OCR,      │
│                   │  Scene understanding         │
│  User Profile     │  Personalization engine,     │
│  Engine           │  long-term memory            │
│  Navigation API   │  Waypoint planning,          │
│                   │  geofencing rules            │
└──────────────────────────────────────────────────┘
```

## Conversational AI Flow

```
Người dùng nói: "Nitch, căn phòng này có gì lạ không?"
        ↓
[EDGE] Wake word detected → Audio captured → VAD confirmed
        ↓
[EDGE→CLOUD] Audio stream gửi lên qua Wi-Fi
        ↓
[CLOUD] ASR → Text: "căn phòng này có gì lạ không?"
        ↓
[CLOUD] Vision API phân tích frame camera hiện tại
        ↓
[CLOUD] LLM tổng hợp: "Tôi thấy có một chiếc cốc nước đang sôi 
         trên bếp mà bạn có vẻ chưa để ý..."
        ↓
[CLOUD→EDGE] TTS audio stream
        ↓
[DEVICE] Speaker phát response
Tổng latency mục tiêu: < 800ms
```

## Navigation AI

- **Indoor**: Optical flow + IMU + ToF → SLAM đơn giản, không cần map trước
- **Outdoor**: GPS từ smartphone của người dùng (chia sẻ qua BLE) + barometer
- **Follow me mode**: UWB tracking với điện thoại → độ chính xác ±10cm
- **Autonomous patrol**: Người dùng vẽ đường bay trên app, Nitch tự bay theo

---

# 4. USER EXPERIENCE

## Thiết kế tương tác: "Zero-friction AI"

Quicknitch không có màn hình. Không có nút bấm vật lý (ngoại trừ 1 nút nguồn). Toàn bộ tương tác qua:
- **Giọng nói** (primary)
- **App smartphone** (secondary)
- **Đèn LED RGB** (feedback trạng thái)
- **Âm thanh** (earcon, chime, voice)

## Ngôn ngữ đèn LED

| Màu / Pattern    | Ý nghĩa               |
| ---------------- | --------------------- |
| Trắng pulse chậm | Đang hover, chờ lệnh  |
| Xanh dương solid | Đang lắng nghe        |
| Tím xoay         | Đang xử lý / thinking |
| Xanh lá chớp     | Phản hồi sẵn sàng     |
| Đỏ chớp nhanh    | Pin yếu, về dock      |
| Vàng xoay        | Đang sạc trong dock   |

## Cá nhân hóa & Danh tính

Mỗi Quicknitch có một **personality profile** do người dùng tùy chỉnh:

- **Tên riêng** (mặc định "Nitch", có thể đổi)
- **Giọng nói**: chọn từ thư viện (trầm/cao, nam/nữ/trung tính, ngôn ngữ)
- **Tính cách AI**: Nghiêm túc / Hài hước / Quan tâm / Chuyên nghiệp
- **Màu LED chủ đạo**: tuỳ chọn trong app
- **Thói quen học được**: Nitch nhớ lịch trình, sở thích, tên người thân của bạn

## Các kịch bản sử dụng hàng ngày

**🌅 Buổi sáng — Personal Morning Show**
> Nitch tự động rời dock lúc 7:00, bay đến đầu giường, nói: "Chào buổi sáng An! Hôm nay Hà Nội 22°C, có cuộc họp lúc 10h, và cà phê của bạn trên bếp sắp nguội rồi đó." Sau đó bay vào bếp để "kiểm tra" theo nghĩa bóng.

**📚 Học tập — Flying Tutor**
> Sinh viên đọc sách, hỏi: "Nitch, từ 'photosynthesis' nghĩa là gì?" Nitch hover cạnh trang sách, camera nhận diện từ, giải thích bằng tiếng Việt kèm hình minh họa hiện trên app.

**🏃 Tập thể dục — Sports Companion**
> Nitch bay theo người chạy bộ ở khoảng cách 2m, tự động ghi lại video từ góc đẹp, đọc số liệu tim mạch từ smartwatch, nhắc uống nước sau mỗi 20 phút.

**📸 Sáng tạo nội dung — Autonomous Cameraman**
> Creator nói "Nitch, quay tôi từ góc 45 độ bên trái, giữ khoảng cách 1.5m." Nitch tự tính toán vị trí, duy trì góc quay trong khi creator di chuyển tự do.

**🔒 Bảo mật nhà — Mini Sentinel**
> Khi không có người ở nhà, Nitch tuần tra theo lịch đã lập trình, phát hiện chuyển động bất thường và gửi notification về điện thoại kèm video clip.

**👴 Chăm sóc người cao tuổi — Care Companion**
> Nitch nhận diện nếu người cao tuổi không di chuyển quá 2 tiếng trong nhà, tự động kiểm tra và nếu cần, gọi điện cho người thân.

---

# 5. MANUFACTURING STRATEGY

## Triết lý: "AirPods Manufacturing Playbook"

Apple đã chứng minh rằng thiết bị siêu nhỏ, giá đại chúng, và chất lượng cao có thể đồng thời tồn tại — nhờ thiết kế tối ưu cho sản xuất hàng loạt từ ngày đầu.

## Bill of Materials (BOM) — Mục tiêu Gen 1

| Component                               | Chi phí mục tiêu (USD) |
| --------------------------------------- | ---------------------- |
| Frame + shroud (CFRP + PC)              | $3.50                  |
| 4× Brushless motor + ESC                | $4.00                  |
| Main PCB (MCU + AI chip + connectivity) | $6.00                  |
| Camera module (12MP)                    | $4.50                  |
| Battery (LiPo 600mAh)                   | $2.00                  |
| Microphone array (3 mics)               | $1.50                  |
| Speaker + ToF + IMU + Barometer         | $2.00                  |
| Assembly + QC                           | $3.00                  |
| Packaging + dock                        | $3.50                  |
| **Tổng BOM**                            | **~$30**               |

**Mục tiêu giá bán (MSRP):** $149 (global) / ¥999 (TQ)
**Gross margin phần cứng:** ~45–55% sau tối ưu hóa

## Chiến lược sản xuất

**Đối tác sản xuất:**
- ODM tại Thâm Quyến / Đông Quản (hệ sinh thái sẵn có cho micro drone)
- Tham khảo chuỗi cung ứng của DJI Mini và Crazyflie
- Plastic injection molding tại địa phương → giảm logistics cost

**Thiết kế cho sản xuất (DFM):**
- Số lượng screw tối thiểu (target: < 6 screw toàn bộ thiết bị)
- Snap-fit design thay dây kết nối nơi có thể
- PCB 4-layer single-board thay vì multiple board → giảm connector, giảm lỗi lắp ráp
- Rotor guard kiêm luôn chức năng chassis → zero redundant parts

**Supply chain:**
- Battery: CATL hoặc ATL (Dongguan)
- Motor: T-Motor hoặc EMAX
- Camera: OmniVision, Sony Semiconductor
- MCU/AI chip: Qualcomm, Espressif, hoặc HiSilicon (TQ market)

**Logistics lợi thế cạnh tranh:**
- Trọng lượng < 35g → chi phí airfreight cực thấp
- Kích thước nhỏ → 500+ units/thùng pallet
- Không chứa liquid, không lithium ion lớn → customs clearance nhanh hơn drone thông thường

---

# 6. CLOUD PLATFORM

## Kiến trúc nền tảng

```
┌─────────────────────────────────────────────────────┐
│                QUICKNITCH CLOUD                     │
├──────────────┬──────────────┬───────────────────────┤
│  AI Services │  Device Mgmt │  Data & Privacy       │
├──────────────┼──────────────┼───────────────────────┤
│ - ASR/TTS    │ - OTA update │ - E2E encryption      │
│ - LLM Engine │ - Fleet mgmt │ - On-device video     │
│ - Vision API │ - Geo-fence  │   (không lưu raw)     │
│ - Memory DB  │ - Telemetry  │ - GDPR / PIPL ready   │
│ - Persona    │ - Analytics  │ - Data residency       │
└──────────────┴──────────────┴───────────────────────┘
```

## AI Services Layer

**Personalization Engine — "Nitch Memory":**
- Long-term memory lưu trữ preferences, tên người thân, lịch trình thói quen
- Sử dụng vector database (Pinecone / Weaviate) cho semantic retrieval
- Người dùng có thể xem và xóa bất kỳ memory nào trong app

**LLM Backend:**
- Global: GPT-4o API hoặc Claude API (với fallback tier thấp hơn cho free users)
- Trung Quốc (金翎速): Qwen (Alibaba) hoặc Ernie Bot (Baidu) để compliance
- Fine-tuning layer riêng cho Quicknitch persona trên đầu base model

**Vision AI:**
- Scene description (mô tả môi trường xung quanh)
- Object recognition (nhận diện đồ vật, thực phẩm, văn bản)
- Face recognition — chỉ cho chủ nhân đã đăng ký (opt-in, lưu on-device)
- Emotion recognition (optional, opt-in)

## Privacy & Security Architecture

**Nguyên tắc "Privacy by Design":**

1. **Video không bao giờ được lưu raw trên cloud** — chỉ processed insights
2. **Wake word xử lý 100% on-device** — không stream âm thanh liên tục lên cloud
3. **Face recognition của người lạ**: blur mặc định, nhận dạng chỉ cho người dùng đã opt-in
4. **End-to-end encryption** cho tất cả communication device ↔ cloud
5. **Data residency**: server TQ cho 金翎速, server EU/US cho Quicknitch global

---

# 7. BUSINESS MODEL

## Revenue Streams

### Hardware Revenue (One-time)
- **Quicknitch Gen 1**: $149 / ¥999
- **Quicknitch Pro** (camera tốt hơn, pin lớn hơn): $249 / ¥1,699
- **Dock & Accessories**: charging dock $29, skin/color kit $19, replacement propellers $9

### Subscription — "Nitch Premium"

| Tier      | Giá          | Tính năng                                                |
| --------- | ------------ | -------------------------------------------------------- |
| Free      | $0           | Basic voice commands, 5 min cloud AI/ngày                |
| Essential | $4.99/tháng  | Unlimited AI, basic memory, 1GB video cloud              |
| Premium   | $9.99/tháng  | Advanced persona, long-term memory, 10GB, family sharing |
| Creator   | $19.99/tháng | 4K cloud backup, advanced editing AI, API access         |

### B2B & Ecosystem Revenue
- **Developer API**: Developers trả phí để build skills/integrations cho Nitch
- **Enterprise**: Phiên bản dành cho bảo mật nhà/văn phòng, y tế (chăm sóc người cao tuổi)
- **Content partnerships**: Tích hợp với Spotify, YouTube, language learning apps

### Unit Economics Projection (Year 3)

| Metric                     | Target   |
| -------------------------- | -------- |
| Hardware units shipped     | 2M/năm   |
| Attach rate (subscription) | 35%      |
| ARPU (subscriber)          | $8/tháng |
| Hardware revenue           | $250M    |
| Subscription revenue       | $67M     |
| Total ARR                  | ~$320M   |

---

# 8. TECHNICAL CHALLENGES

## Challenge 1: Giới hạn pin ⚡

**Vấn đề:** 12–18 phút flight time là không đủ cho nhiều use case.

**Giải pháp:**
- Thiết kế **"Perch & Listen" mode**: Nitch đậu trên vai/bàn, tắt motor, vẫn lắng nghe và trả lời — tiêu thụ điện như một smart speaker thông thường
- Dock tiện lợi: đặt ở nhiều điểm trong nhà, Nitch tự tìm đường về dock khi pin thấp
- Gen 2: khám phá wireless charging khi hover (resonant coupling, hiệu suất thấp nhưng dùng cho perch mode)

## Challenge 2: Tiếng ồn 🔊

**Vấn đề:** Rotor tạo tiếng ồn 55–65 dB, gây khó chịu trong môi trường yên tĩnh.

**Giải pháp:**
- Cánh quạt profile tối ưu góc tấn (blade pitch optimization)
- Rotor guard hình dạng acoustic-optimized (giảm turbulence)
- **"Whisper mode"**: giảm tốc độ rotor, giới hạn độ cao, tiếng ồn < 45 dB — dùng trong phòng ngủ, thư viện
- Active Noise Compensation trong microphone array để lọc tiếng rotor ra khỏi voice input

## Challenge 3: Độ ổn định bay trong nhà 🏠

**Vấn đề:** Không có GPS trong nhà, gió điều hòa, bề mặt phản quang làm nhiễu optical flow.

**Giải pháp:**
- Kết hợp optical flow + IMU + barometer với sensor fusion algorithm (EKF)
- UWB anchor với điện thoại người dùng làm reference point
- "Safe hovering zone" học từ environment: Nitch tự học layout căn phòng sau vài ngày sử dụng

## Challenge 4: An toàn khi gần người 🛡️

**Vấn đề:** Cánh quạt quay nhanh có thể gây thương tích, đặc biệt với trẻ em.

**Giải pháp:**
- **Enclosed rotor design bắt buộc**: cánh quạt hoàn toàn nằm trong guard — không thể tiếp xúc da người
- Force sensor: phát hiện va chạm → dừng motor trong < 20ms
- **Proximity safety zone**: không bay trong vòng 30cm của khuôn mặt người (lidar ToF enforce)
- Certification mục tiêu: UL, CE, FCC, SRRC (TQ)

---

# 9. REGULATORY & PRIVACY CONSIDERATIONS

## Quy định không phận (Airspace)

Đây là thách thức lớn nhất về mặt regulatory.

| Khu vực           | Quy định hiện tại                                                                                      | Tác động với Quicknitch                         |
| ----------------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------------------- |
| Mỹ (FAA)          | Drone < 250g không cần đăng ký nếu dùng recreational trong nhà                                         | ✅ Indoor use clear; outdoor cần nghiên cứu thêm |
| EU (EASA)         | Drone < 250g: Open Category, ít ràng buộc nếu không có camera... nhưng có camera → cần thêm compliance | ⚠️ Camera compliance phức tạp                    |
| Trung Quốc (CAAC) | Drone < 250g được miễn đăng ký, nhưng cần SRRC certification cho wireless                              | ✅ Thuận lợi cho 金翎速                          |
| Việt Nam          | Drone nhỏ trong nhà chưa có quy định rõ ràng                                                           | ⚠️ Cần lobby chính sách                          |

**Chiến lược regulatory:**
- **Phase 1 Launch: Indoor Only** — tránh toàn bộ airspace regulation, đơn giản hóa go-to-market
- Xây dựng safety record và brand trust trong 2–3 năm
- **Phase 2**: Outdoor mode với geofencing chặt (chỉ bay trong sân nhà, < 10m độ cao)

## Quyền riêng tư & Giám sát

Quicknitch có camera, microphone, và bay quanh người — đây là mối lo ngại lớn nhất của người tiêu dùng và cơ quan quản lý.

**Cam kết công khai (Privacy Pledge):**
- Camera indicator LED bắt buộc: đèn đỏ luôn sáng khi camera đang record
- "No Stranger Face Storage" policy: khuôn mặt người lạ không bao giờ được upload lên cloud
- Annual transparency report về dữ liệu người dùng
- Third-party security audit hàng năm (công bố kết quả)
- Nút hardware kill switch: nhấn giữ 3 giây → tắt camera + microphone hoàn toàn, đèn tắt xác nhận

---

# 10. THỊ TRƯỜNG TRUNG QUỐC — 金翎速 (JīnLíng Sù)

## Phân tích tên thương hiệu

**金翎速** — Jīn Líng Sù

- **金 (Jīn)**: Vàng — sang trọng, quý giá, thành công
- **翎 (Líng)**: Lông vũ (đặc biệt là lông đuôi chim — thường gắn với sự linh hoạt, vẻ đẹp thanh thoát)
- **速 (Sù)**: Nhanh, tốc độ, phản ứng tức thời

Cái tên gợi lên hình ảnh **một chiếc lông vũ vàng bay nhanh** — hoàn toàn phù hợp với Golden Snitch inspiration, nhưng được Hán hóa một cách tự nhiên và có chiều sâu văn hóa. Không phải dịch trực tiếp mà là *tái sáng tạo*.

## Định vị thị trường TQ

**Cơ hội:**

Trung Quốc là thị trường drone lớn nhất thế giới (DJI chiếm ~70% thị phần drone toàn cầu, xuất phát từ Thâm Quyến). Nhưng DJI tập trung vào professional/prosumer — không có ai làm **consumer AI companion drone** cho đại chúng.

**Đối tượng mục tiêu tại TQ:**
- Gen Z và Millennials tại các tier-1/2 cities (Bắc Kinh, Thượng Hải, Thành Đô, Hàng Châu)
- "Digital native" quen với AI assistant (Xiao Ai, Xiao Du, Tmall Genie)
- KOL/influencer cần thiết bị quay nội dung sáng tạo
- Phụ huynh muốn thiết bị học tập thú vị cho con cái

## Chiến lược khác biệt hóa cho 金翎速

**AI Engine: Made for China**

| Global (Quicknitch) | China (金翎速)                  |
| ------------------- | ------------------------------- |
| GPT-4o / Claude     | 通义千问 (Qwen) / 文心一言      |
| Google Maps         | 高德地图 / 百度地图             |
| YouTube integration | 哔哩哔哩 (Bilibili) integration |
| Spotify             | 网易云音乐 / QQ音乐             |
| English-first       | 普通话 + 粤语 + 方言支持        |

**Platform Integration:**
- 微信 (WeChat): Nitch có thể đọc/tóm tắt tin nhắn WeChat (với permission)
- 支付宝 (Alipay): Nhắc nhở thanh toán, theo dõi chi tiêu
- 淘宝/京东: "Nitch, sản phẩm này có tốt không?" — scan và so sánh giá tức thì
- 小红书 (Xiaohongshu): Xuất nội dung video trực tiếp lên platform

## Go-to-Market tại Trung Quốc

**Kênh phân phối:**
- Tmall flagship store (primary e-commerce)
- JD.com (secondary)
- Offline: pop-up tại Ingress/COSCO/retail malls tại tier-1 cities
- Tham khảo model của Xiaomi: bán online first, buzz trước launch

**Marketing strategy:**
- Seed với top KOL trên 抖音 (Douyin) và 小红书
- Campaign: "你的金翎速叫什么名字?" (金翎速 của bạn tên gì?) — viral name customization
- Partnership với IP nổi tiếng: anime, game (ví dụ: Genshin Impact limited edition skin)
- Student ambassador program tại các trường đại học top

**Pricing tại TQ:**

| SKU                 | Giá       | Phân khúc              |
| ------------------- | --------- | ---------------------- |
| 金翎速 Lite         | ¥799      | Sinh viên, entry-level |
| 金翎速 Standard     | ¥999      | Mainstream             |
| 金翎速 Pro          | ¥1,699    | Enthusiast             |
| 订阅 (Subscription) | ¥18/tháng | AI Premium             |

## Regulatory TQ cho 金翎速

- SRRC (无线电型号核准): bắt buộc cho Wi-Fi/BT device
- CCC certification: thiết bị điện tử tiêu dùng
- Network Security Review: do có camera + cloud AI — cần chuẩn bị kỹ
- Data localization: tất cả dữ liệu người dùng TQ lưu trên server trong nước (Alibaba Cloud hoặc Huawei Cloud)
- Drone registration: < 250g miễn đăng ký theo quy định CAAC hiện tại

---

# 11. LONG-TERM VISION

## Năm 1–2: "The Companion"
Ra mắt indoor-only, focus vào AI companion experience. Build community. Prove product-market fit. Target: 500K units shipped.

## Năm 3–4: "The Platform"
Mở developer SDK. Third-party "Skills" ecosystem tương tự Alexa Skills. Outdoor mode ra mắt tại các thị trường có regulation rõ ràng. Target: 5M units shipped, 30% subscription attach rate.

## Năm 5–7: "The Network"
Quicknitch biết nhận ra người quen của bạn (với permission). Nhiều Nitch trong cùng gia đình có thể giao tiếp với nhau. B2B mở rộng: hospital, eldercare, education. Target: 20M units, IPO hoặc strategic acquisition.

## Tầm nhìn 2035

> Trong một thế giới mà mỗi người đều có một 金翎速/Quicknitch, AI không còn là thứ bạn *dùng* — nó là thứ *sống cùng bạn*. Không còn "mở app". Không còn "hỏi Google". Chỉ cần nói, và người bạn vàng nhỏ bé của bạn — đang lơ lửng cạnh vai — sẽ trả lời.

Đây không phải tương lai của smartphone. Đây là tương lai của **presence** — sự hiện diện của AI trong không gian vật lý của con người.

---

*Quicknitch / 金翎速 — The AI that flies with you.*
*"Not smarter than you. Just always there for you."*