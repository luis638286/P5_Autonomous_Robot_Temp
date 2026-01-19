# Robot Telemetry API – Quick Guide

This API allows a robot to send telemetry data to a backend service and allows a frontend to retrieve the latest data.

---

## Base URL

https://p5-autonomous-robot-temp.onrender.com


---

## Authentication

All **POST** requests must include the following headers:

Content-Type: application/json
X-API-Key: Liquid-Team


**GET** requests do **not** require authentication.

---

## Endpoints

---

### 1️⃣ Send Telemetry Data  
**POST** `/telemetry`

Used by the **robot** to send its latest sensor and position data to the backend.

#### Request Body (JSON)
```json
{
  "temperature": 23.6,
  "position": {
    "x": 10.5,
    "y": 4.2
  }
}

Success Response (201)

{
  "status": "success",
  "message": "Data received"
}

2️⃣ Get Latest Telemetry Data

**GET** `/telemetry/latest`

Used by the frontend to retrieve the most recent telemetry sent by the robot.
Success Response (200)

{
  "temperature": 23.6,
  "position": {
    "x": 10.5,
    "y": 4.2
  }
}

No Data Available (404)

{
  "message": "No data received yet"
}