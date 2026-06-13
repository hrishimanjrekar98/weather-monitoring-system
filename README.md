# 🌦️ Real-Time Weather & Air Quality Monitoring System

A professional, end-to-end IoT solution using the **ESP32-S3** to monitor environmental conditions and air quality, with a live web-based analytics dashboard.

## 🚀 Live Dashboard
Check the real-time data here: [https://hrishimanjrekar98.github.io/weather-monitoring-system/](https://hrishimanjrekar98.github.io/weather-monitoring-system/)

---

## 🛠️ System Features
- **Hardware Integration:** Utilizes ESP32-S3 with DHT22 (Temperature/Humidity) and MQ135 (Air Quality) sensors.
- **RGB Status LED:** On-board status indication with custom brightness control (10% intensity).
    - 🟢 **Blinking Green:** System Nominal / Data Sending.
    - 🔵 **Blinking Blue:** Wi-Fi/MQTT Connection in progress.
    - 🟡 **Solid Yellow:** Warning - Elevated Gas Levels.
    - 🔴 **Blinking Red:** Critical - Hazardous Gas or Sensor Error.
- **MQTT Connectivity:** Real-time data transmission via HiveMQ Cloud broker.
- **Advanced Web Dashboard:** 
    - **Industry Standard Charts:** Gradient area fills and interactive crosshair tooltips.
    - **Data Historian:** Continuous logging of every MQTT message with separated Date and Time stamps.
    - **Export Capability:** One-click CSV download for historical data analysis.
- **Source Control:** Managed with Git using a robust `main` and `development` branching strategy.

---

## 🏗️ Technical Stack
- **Firmware:** ESP-IDF (C), FreeRTOS.
- **Sensors:** DHT22, MQ135.
- **Cloud:** HiveMQ MQTT Broker (WSS).
- **Web Frontend:** HTML5, CSS3 (Industrial Dark Theme), JavaScript (Vanilla).
- **Analytics:** Chart.js for high-resolution telemetry visualization.

---

## 📂 Project Structure
- `/main`: ESP32 source code and logic.
- `/managed_components`: ESP-IDF drivers for MQTT and Sensors.
- `index.html`: The live monitoring dashboard.
- `README.md`: Documentation (you are here).

---

## 🔧 Installation & Setup
1. **Clone the repo:**
   ```bash
   git clone https://github.com/hrishimanjrekar98/weather-monitoring-system.git
   ```
2. **Build & Flash (ESP-IDF):**
   ```bash
   idf.py build
   idf.py -p [PORT] flash monitor
   ```
3. **Open Dashboard:** Simply open `index.html` in your browser or visit the GitHub Pages link.

---

## 👨‍💻 Author
**Hrishikesh Manjrekar**
[GitHub Profile](https://github.com/hrishimanjrekar98)
