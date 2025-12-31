# PZEM Power Monitor with ESP32
*Smart Power Monitoring & Load Classification System (PZEM-004T + ESP32)


This project is a real-time smart power monitoring system designed to classify electrical loads and detect anomalies. Built on the ESP32 platform using FreeRTOS, it leverages a dual-core architecture to separate critical sensor acquisition tasks from network communication.

The system implements an Intelligent Deep Sleep mechanism to conserve energy, waking up fully only when significant load changes are detected. It utilizes a Random Forest Machine Learning model to classify connected devices (e.g., Laptop, Solder, Printer) with 99.8% accuracy based on power profiles.

**System Architecture

The system follows a distributed IoT architecture:

Edge Layer (ESP32): Reads sensor data via Modbus RTU, handles local safety (relay cutoff), and manages power states.

Communication Layer: Transmits JSON payloads via MQTT (HiveMQ) and handles OTA updates via HTTPS.

Middleware (Node-RED): Orchestrates data flow, triggering the ML API and formatting data for storage.

Backend (Flask): Python-based API running the Random Forest inference engine.

Storage & Visuals: Prometheus (Time-Series DB) stores metrics, visualized on Grafana.

graph LR
    PZEM[PZEM-004T] -->|UART| ESP32
    ESP32 -->|MQTT| Broker[HiveMQ]
    Broker -->|Sub| NodeRED
    NodeRED <-->|HTTP| Flask[ML Model]
    NodeRED -->|Expose| Prometheus
    Prometheus -->|Query| Grafana


**Hardware Components

Microcontroller: ESP32 Development Board (Dual Core).

Sensor: PZEM-004T V3.0 (100A) - Measures Voltage, Current, Power, Energy, Frequency, PF.

Actuator: 1-Channel Relay Module (Active Low) for overload protection.

Power Supply: 5V DC for ESP32 and Sensor logic.

** Software Components

Firmware: ESP-IDF (C Language) with FreeRTOS.

Middleware: Node-RED (Flow-based programming).

Machine Learning: Python (Scikit-learn, Flask).

Database: Prometheus (Time-Series).

Visualization: Grafana.

*** Key Features

1. RTOS Implementation

Tasks are pinned to specific CPU cores to prevent blocking:

Core 1 (Sensor Logic): High-priority loop for reading PZEM registers and handling safety relays.

Core 0 (Network): Handles WiFi stack, MQTT publishing, and OTA downloads.

2. Smart Deep Sleep & Energy Efficiency

The device does not stay awake continuously. It uses a Wake Stub logic:

Wake (every 5s): Quick sensor check (~100ms).

Decision: If load change < 1W, go back to sleep immediately (WiFi OFF).

Active Mode: If load changes significantly, wake up fully, connect WiFi, and transmit data.

Heartbeat: Wakes up fully every 60 sleep cycles (approx. 5 mins) to check for OTA updates.

3. Anomaly Detection

Distinguishes between a device turning on and a grid surge:

Load On: Power Increases ($P \uparrow$), Voltage Sags ($V \downarrow$).

Grid Surge: Voltage Spikes ($V \uparrow > 5V$), Power Increases.

4. Over-The-Air (OTA) Updates

Supports remote firmware updates from a Private GitHub Repository.

Uses update.json manifest to check versions.

Implements Bearer Token authentication for JSON and Basic Auth workaround for binary downloads.



** Dataset

The model was trained on 2,452 samples of real-world electrical data.

Input Features: Active Power (P), Energy (E).

Classes: Nothing, Laptop, Solder, Printer, 3D Printer, and combinations.