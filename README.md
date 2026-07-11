# Smart Environment Management System (SEMS)

SEMS (Smart Environment Management System) is an ESP32-based IoT solution designed to automate and monitor lecture hall environments. The system performs real-time occupancy detection, climate control, lighting automation, air quality monitoring, and emergency detection while operating independently in offline mode or remotely through Node-RED using MQTT.

---

## Features

- Autonomous Lecture Hall Automation
- ESP32-Based Embedded Controller
- Online & Offline Operation Modes
- Intelligent Occupancy Detection
- Adaptive Lighting Control
- Temperature-Based Climate Control
- Humidity Monitoring
- Smoke & Gas Detection
- Motion Detection
- LCD Status Display
- MQTT Communication
- Node-RED Dashboard
- Wi-Fi Connectivity
- Self-Diagnostic Startup
- Periodic Hardware Health Check
- Manual Remote Control
- Emergency Alarm System
- Energy Saving Automation

---

## System Workflow

### Startup

1. ESP32 initializes all connected sensors.
2. Hardware self-diagnostic begins.
3. LCD displays sensor status.
4. Green LED indicates successful startup.
5. Yellow LED reports hardware faults.

### Environment Monitoring

- Detects occupancy using Ultrasonic and PIR sensors.
- Controls lighting according to occupancy level.
- Controls fan and cooling based on temperature.
- Regulates ventilation according to humidity.
- Continuously monitors smoke and gas levels.

### Emergency Handling

- Detects gas leaks and fire conditions.
- Activates buzzer and warning LEDs.
- Displays emergency messages.
- Continues operating autonomously if network connectivity is lost.

---

## Hardware Components

- ESP32
- DHT22 Temperature & Humidity Sensor
- MQ-2 Gas & Smoke Sensor
- HC-SR501 PIR Motion Sensor
- HC-SR04 Ultrasonic Sensor
- 16×2 I2C LCD
- Active Buzzer
- Green LED
- Yellow LED
- Red LED
- Blue LED (AC Indicator)
- White LEDs (Lighting & Fan Indicators)
- Breadboard & Jumper Wires

---

## Software & Technologies

### Embedded Development

- Arduino IDE
- C++

### IoT

- ESP32
- MQTT

### Dashboard

- Node-RED

### Communication

- Wi-Fi
- MQTT Broker

---

## Project Structure

```
SEMS/
│
├── Arduino/
│   └── Project-I0T-(SEMS).ino
├── Node-RED/
│   └── Project-I0T-(SEMS)-Node-RED.json
├── Documentation/
├── README.md
└── LICENSE
```

---

## System Capabilities

- Smart Lecture Hall Automation
- Occupancy Counting
- Adaptive Lighting
- Climate Automation
- Fire Detection
- Gas Leak Detection
- Fault Detection
- Manual Override
- Remote Monitoring
- Offline Automation
- Energy Optimization

---

## Future Improvements

- Mobile Application
- Cloud Dashboard
- AI-Based Occupancy Prediction
- Face Recognition Attendance
- Voice Assistant Integration
- Firebase Integration
- Data Analytics
- Historical Sensor Logs
- Email & SMS Alerts
- Smart Scheduling

---

## Applications

- Smart Classrooms
- Universities
- Smart Buildings
- Office Automation
- Energy Management
- IoT Education
- Embedded Systems Projects

---

## Author

**Mohamed AlBry**

Faculty of Computer Science & Artificial Intelligence

---

## License

This project is licensed under the MIT License.
