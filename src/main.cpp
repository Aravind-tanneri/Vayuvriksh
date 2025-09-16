#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// Network credentials for Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";
WebServer server(80);

// --- System Configuration ---
const float RESERVOIR_VOLUME_L = 20.0; 

// --- Sensor Pin Definitions ---
const int pH_SimPin = 34;
const int ec_sim_pin = 35; 
const int LDR_pin = 32;

// --- Thresholds for sensor values ---
const float PH_MIN = 5.5;
const float PH_MAX = 6.5;
const float EC_MIN = 800.0;
const float EC_MAX = 1600.0;

// MODIFIED: Simplified and corrected pin definitions to match your description
const int mistingRelayPin  = 16; // The one and only water pump (Left Side LED)
const int ledRelayPin      = 17; // The one and only grow light (Right Side LED)
const int flushingRelayPin = 16; // This now points to the SAME pin as the misting pump

// --- Relay Logic ---
// Set to match your hardware. If relays turn on with a LOW signal, swap these.
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// --- State Variables ---
bool isMistingActive = false;
bool isFlushingActive = false;
bool isFlushingPrecedence = false;
bool system_halted = false; 
String ph_status = "ok";
String ec_status = "ok";
String ph_dose_msg = "";
String ec_dose_msg = "";
String misting_status_msg = "Pump is OFF";
String next_misting_msg = "Calculating...";
String last_misting_msg = "N/A";
String flushing_status_msg = "Pump is OFF";
String next_flushing_msg = "Calculating...";
String last_flushing_msg = "N/A";

// --- Time Variables ---
unsigned long previousMistingMillis = 0;
unsigned long previousFlushingMillis = 0;
unsigned long previousSensorMillis = 0;
const long sensorInterval = 500;

// --- Cycle Durations (milliseconds) ---
const long mistingInterval = 7 * 60 * 1000;
const long mistingDuration = 30 * 1000;
const long flushingInterval = 7 * 24 * 60 * 60 * 1000;
const long flushingDuration = 5 * 60 * 1000;

// --- Light Threshold ---
const int lightThreshold = 300;

// --- Global sensor variables ---
float pH_value = 0.0;
float ec_value = 0.0;
float tds_value = 0.0;
float lux_value = 0.0;

// CORRECTED: HTML_CONTENT with restored JavaScript for charts
const char HTML_CONTENT[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Vayuvriksh Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #f2e8cf; margin: 0; padding: 15px; box-sizing: border-box; }
    h1 { font-size: 2.2em; color: #31572c; margin-bottom: 25px; }
    h2 { color: #333; margin-top: 0; }
    p { font-size: 1.1em; color: #31572c; margin-top: 5px; font-weight: 500;}
    .matrix-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 20px; max-width: 1400px; margin: 0 auto; }
    .grid-item { background: #a7c957; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.15); width: 100%; box-sizing: border-box; display: flex; flex-direction: column; justify-content: center; }
    .grid-item.chart { background: #fff; }
    .grid-item.button-container { display: flex; flex-direction: column; justify-content: space-around; gap: 10px; background: none; box-shadow: none; padding: 0; }
    .sensor-value, .status-value { font-weight: bold; color: #3b562bff; }
    .alert { color: #8a0b0b; background-color: #fdd; border: 1px solid #d9534f; padding: 10px; margin-top: 10px; border-radius: 5px; font-weight: bold; text-align: left;}
    button { background-color: #31572c; color: white; padding: 12px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 1em; font-weight: bold; width: 100%; transition: background-color: 0.2s;}
    button:hover { background-color: #4a753c; }
    button:disabled { background-color: #cccccc; cursor: not-allowed; }
    button.resume-button { background-color: #31572c; }
    button.resume-button:hover { background-color: #4a753c; }
    button.stop-button { background-color: #c92a2a; }
    button.stop-button:hover { background-color: #a61e1e; }
    @media (max-width: 1024px) { .matrix-grid { grid-template-columns: 1fr 1fr; } }
    @media (max-width: 768px) { .matrix-grid { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <h1>Vayuvriksh Dashboard</h1>
  <div class="matrix-grid">
    <div class="grid-item"><h2>pH Level</h2><p><span id="ph" class="sensor-value">--</span></p><div id="ph-alert" class="alert" style="display:none;"></div></div>
    <div class="grid-item"><h2>EC (µS/cm)</h2><p><span id="ec" class="sensor-value">--</span></p><div id="ec-alert" class="alert" style="display:none;"></div></div>
    <div class="grid-item"><h2>Light Level (lux)</h2><p><span id="light" class="sensor-value">--</span></p></div>
    <div class="grid-item"><h2>Misting Status</h2><p>Status: <span id="mistingStatus" class="status-value">--</span></p><p>Next In: <span id="nextMisting" class="status-value">--</span></p></div>
    <div class="grid-item button-container">
      <button id="flushButton" onclick="triggerFlush()">Manual Flush</button>
      <button id="resumeButton" class="resume-button" onclick="triggerResume()">Resume System</button>
      <button id="stopButton" class="stop-button" onclick="triggerStop()">Force Stop All</button>
    </div>
    <div class="grid-item"><h2>Flushing Status</h2><p>Status: <span id="flushingStatus" class="status-value">--</span></p><p>Next In: <span id="nextFlushing" class="status-value">--</span></p></div>
    <div class="grid-item chart"><canvas id="phChart"></canvas></div>
    <div class="grid-item chart"><canvas id="ecChart"></canvas></div>
    <div class="grid-item chart"><canvas id="lightChart"></canvas></div>
  </div>
  
  <script>
    let phChart, ecChart, lightChart;
    const MAX_DATA_POINTS = 20;

    function triggerFlush() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/flush", true); xhr.send(); }
    function triggerStop() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/stop", true); xhr.send(); }
    function triggerResume() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/resume", true); xhr.send(); }

    function createChart(ctx, label, color) { return new Chart(ctx, { type: 'line', data: { labels: [], datasets: [{ label: label, data: [], borderColor: color, backgroundColor: color + '33', borderWidth: 2.5, fill: true, tension: 0.4 }] }, options: { responsive: true, maintainAspectRatio: true, scales: { x: { display: true, title: { display: false } }, y: { display: true, title: { display: true, text: label } } }, plugins: { legend: { display: false } } } }); }
    
    window.onload = function() { 
      phChart = createChart(document.getElementById('phChart').getContext('2d'), 'pH', '#31572c'); 
      ecChart = createChart(document.getElementById('ecChart').getContext('2d'), 'EC (µS/cm)', '#8a5a44'); 
      lightChart = createChart(document.getElementById('lightChart').getContext('2d'), 'Light (lux)', '#fca311'); 
    };
    
    function addDataToChart(chart, data) { 
      const timestamp = new Date().toLocaleTimeString(); 
      chart.data.labels.push(timestamp); 
      chart.data.datasets[0].data.push(data); 
      if (chart.data.labels.length > MAX_DATA_POINTS) { 
        chart.data.labels.shift(); 
        chart.data.datasets[0].data.shift(); 
      } 
      chart.update(); 
    }

    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          var data = JSON.parse(this.responseText);
          document.getElementById("mistingStatus").textContent = data.misting_status_msg;
          document.getElementById("nextMisting").textContent = data.next_misting_msg;
          document.getElementById("flushingStatus").textContent = data.flushing_status_msg;
          document.getElementById("nextFlushing").textContent = data.next_flushing_msg;
          document.getElementById("ph").textContent = parseFloat(data.pH).toFixed(2);
          document.getElementById("ec").textContent = parseFloat(data.EC).toFixed(1);
          document.getElementById("light").textContent = parseFloat(data.light).toFixed(1);
          
          addDataToChart(phChart, data.pH);
          addDataToChart(ecChart, data.EC);
          addDataToChart(lightChart, data.light);
          
          var phAlertDiv = document.getElementById("ph-alert");
          if (data.ph_status !== "ok") { phAlertDiv.textContent = data.ph_dose_msg; phAlertDiv.style.display = "block"; } else { phAlertDiv.style.display = "none"; }
          var ecAlertDiv = document.getElementById("ec-alert");
          if (data.ec_status !== "ok") { ecAlertDiv.textContent = data.ec_dose_msg; ecAlertDiv.style.display = "block"; } else { ecAlertDiv.style.display = "none"; }
          
          var flushBtn = document.getElementById("flushButton");
          var resumeBtn = document.getElementById("resumeButton");
          var stopBtn = document.getElementById("stopButton");

          flushBtn.disabled = data.is_flushing || data.system_halted;
          resumeBtn.disabled = !data.system_halted;
          stopBtn.disabled = !data.is_misting && !data.is_flushing;
        }
      };
      xhr.open("GET", "/readings", true);
      xhr.send();
    }, 2000);
  </script>
</body>
</html>
)rawliteral";


// --- Function Declarations ---
String formatMillis(unsigned long millis_val); String formatSecondsToMinutes(unsigned long seconds_val);
void updateCycleStatusMessages(); void handleRoot(); void handleReadings(); void handleManualFlush();
void handleForceStop(); void handleResumeSystem(); void readSensors(); void checkNutrientLevels(); void controlLeds();
void manageMisting(); void manageFlushing(); void updateRelays();
String formatMillis(unsigned long millis_val); 
String formatSecondsToMinutes(unsigned long seconds_val);
String formatMillisDays(unsigned long millis_val); // NEW: Add this line
void updateCycleStatusMessages(); 
// --- Server Handlers ---
void handleRoot() { server.send(200, "text/html", HTML_CONTENT); }
void handleReadings() {
  String jsonResponse = "{";
  jsonResponse += "\"pH\":" + String(pH_value, 2) + ",";
  jsonResponse += "\"ph_status\":\"" + ph_status + "\",";
  jsonResponse += "\"ph_dose_msg\":\"" + ph_dose_msg + "\",";
  jsonResponse += "\"EC\":" + String(ec_value, 1) + ",";
  jsonResponse += "\"ec_status\":\"" + ec_status + "\",";
  jsonResponse += "\"ec_dose_msg\":\"" + ec_dose_msg + "\",";
  jsonResponse += "\"light\":" + String(lux_value, 1) + ",";
  jsonResponse += "\"misting_status_msg\":\"" + misting_status_msg + "\",";
  jsonResponse += "\"last_misting_msg\":\"" + last_misting_msg + "\",";
  jsonResponse += "\"next_misting_msg\":\"" + next_misting_msg + "\",";
  jsonResponse += "\"is_flushing\":" + String(isFlushingActive ? "true" : "false") + ",";
  jsonResponse += "\"is_misting\":" + String(isMistingActive ? "true" : "false") + ",";
  jsonResponse += "\"system_halted\":" + String(system_halted ? "true" : "false") + ",";
  jsonResponse += "\"flushing_status_msg\":\"" + flushing_status_msg + "\",";
  jsonResponse += "\"last_flushing_msg\":\"" + last_flushing_msg + "\",";
  jsonResponse += "\"next_flushing_msg\":\"" + next_flushing_msg + "\"";
  jsonResponse += "}";
  server.send(200, "application/json", jsonResponse);
}
// NEW: Helper function to convert milliseconds to a DD:HH:MM:SS string
String formatMillisDays(unsigned long millis_val) {
  unsigned long total_seconds = millis_val / 1000;
  int days = total_seconds / 86400;
  total_seconds %= 86400;
  int hours = total_seconds / 3600;
  total_seconds %= 3600;
  int minutes = total_seconds / 60;
  int seconds = total_seconds % 60;

  char time_str[20];
  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d:%02d", days, hours, minutes, seconds);
  return String(time_str);
}
void handleManualFlush() {
  if (system_halted) { 
    server.send(403, "text/plain", "Forbidden: System is halted. Press Resume first."); 
    return; 
  }
  if (!isFlushingActive && !isMistingActive) {
    isFlushingActive = true; isFlushingPrecedence = true; previousFlushingMillis = millis();
    Serial.println("Manual flush initiated by user.");
    server.send(200, "text/plain", "Flush Initiated");
  } else {
    Serial.println("Manual flush request ignored: a cycle is already active.");
    server.send(409, "text/plain", "Conflict: A cycle is already active.");
  }
}
void handleForceStop() {
  system_halted = true; 
  isMistingActive = false; isFlushingActive = false; isFlushingPrecedence = false;
  digitalWrite(ledRelayPin, RELAY_OFF); 
  digitalWrite(mistingRelayPin, RELAY_OFF); 
  digitalWrite(flushingRelayPin, RELAY_OFF);
  Serial.println("FORCE STOP initiated by user. All systems halted.");
  server.send(200, "text/plain", "All systems halted");
}
void handleResumeSystem() {
  system_halted = false;
  Serial.println("System resumed by user.");
  server.send(200, "text/plain", "System Resumed");
}

// --- Main Setup and Loop ---
void setup() {
  Serial.begin(115200);
  // MODIFIED: Initialize the correct pins
  pinMode(ledRelayPin, OUTPUT); 
  pinMode(mistingRelayPin, OUTPUT); 
  pinMode(flushingRelayPin, OUTPUT);
  digitalWrite(ledRelayPin, RELAY_OFF); 
  digitalWrite(mistingRelayPin, RELAY_OFF); 
  digitalWrite(flushingRelayPin, RELAY_OFF);
  previousMistingMillis = millis();
  previousFlushingMillis = millis();
  Serial.print("Connecting to "); Serial.println(ssid); WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected."); Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  // MODIFIED: Removed /resume route
  server.on("/", HTTP_GET, handleRoot); 
  server.on("/readings", HTTP_GET, handleReadings); 
  server.on("/flush", HTTP_GET, handleManualFlush); 
  server.on("/stop", HTTP_GET, handleForceStop);
  server.on("/resume", HTTP_GET, handleResumeSystem); // Ensure this line exists
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient(); readSensors(); checkNutrientLevels(); updateCycleStatusMessages(); 
  controlLeds(); manageFlushing(); manageMisting(); updateRelays();
}

// --- Function Definitions ---
String formatMillis(unsigned long m) {
  unsigned long seconds = m / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  seconds %= 60;
  minutes %= 60;
  char b[12]; 
  snprintf(b,sizeof(b),"%02lu:%02lu:%02lu", hours, minutes, seconds); 
  return String(b);
}
String formatSecondsToMinutes(unsigned long s) {
  unsigned long minutes = s / 60;
  unsigned long seconds = s % 60;
  char b[20]; 
  snprintf(b,sizeof(b),"%lu m %lu s", minutes, seconds); 
  return String(b);
}
// MODIFIED: This function now uses the new DD:HH:MM:SS format for the flush timer
void updateCycleStatusMessages() {
    unsigned long currentMillis = millis();
    
    // --- Misting Cycle (uses HH:MM:SS) ---
    if (previousMistingMillis > 0) { last_misting_msg = formatMillis(previousMistingMillis); }
    if (isMistingActive) { 
      unsigned long time_left_ms = (previousMistingMillis + mistingDuration) - currentMillis;
      if (time_left_ms > mistingDuration) time_left_ms = 0;
      misting_status_msg = "Pump ON, " + formatSecondsToMinutes(time_left_ms / 1000) + " left";
      next_misting_msg = "Cycle in progress";
    } else { 
      misting_status_msg = "Pump is OFF"; 
      unsigned long time_to_next_ms = (previousMistingMillis + mistingInterval) - currentMillis;
      if (time_to_next_ms > mistingInterval) time_to_next_ms = mistingInterval;
      next_misting_msg = formatMillis(time_to_next_ms); 
    }

    // --- Flushing Cycle (uses DD:HH:MM:SS) ---
    if (previousFlushingMillis > 0) { last_flushing_msg = formatMillis(previousFlushingMillis); }
    if (isFlushingActive) { 
      unsigned long time_left_ms = (previousFlushingMillis + flushingDuration) - currentMillis;
      if (time_left_ms > flushingDuration) time_left_ms = 0;
      flushing_status_msg = "Pump ON, " + formatSecondsToMinutes(time_left_ms / 1000) + " left";
      next_flushing_msg = "Cycle in progress";
    } else { 
      flushing_status_msg = "Pump is OFF"; 
      unsigned long time_to_next_ms = (previousFlushingMillis + flushingInterval) - currentMillis;
      if (time_to_next_ms > flushingInterval) time_to_next_ms = flushingInterval;
      // This is the only line that changed in this function
      next_flushing_msg = formatMillisDays(time_to_next_ms); 
    }
}
void readSensors() { 
  unsigned long currentMillis = millis();
  if (currentMillis - previousSensorMillis >= sensorInterval) {
    previousSensorMillis = currentMillis; pH_value = (float)analogRead(pH_SimPin)/4095.0*14.0; ec_value = (float)analogRead(ec_sim_pin)/4095.0*3000.0;
    tds_value = ec_value * 0.64; lux_value = (float)(4095 - analogRead(LDR_pin))/4095.0*1000.0;
  }
}
void checkNutrientLevels() {
  char buffer[128];
  if (pH_value < PH_MIN) { ph_status = "low"; float ph_target = (PH_MIN + PH_MAX) / 2.0; float ph_diff = ph_target - pH_value; float dose_ml = (ph_diff / 0.2) * (RESERVOIR_VOLUME_L / 10.0); snprintf(buffer, sizeof(buffer), "ALERT: pH is too low! Add %.1fml of Vriddhi (pH Up).", dose_ml); ph_dose_msg = buffer;
  } else if (pH_value > PH_MAX) { ph_status = "high"; float ph_target = (PH_MIN + PH_MAX) / 2.0; float ph_diff = pH_value - ph_target; float dose_ml = (ph_diff / 0.2) * (RESERVOIR_VOLUME_L / 10.0); snprintf(buffer, sizeof(buffer), "ALERT: pH is too high! Add %.1fml of Saman (pH Down).", dose_ml); ph_dose_msg = buffer;
  } else { ph_status = "ok"; ph_dose_msg = ""; }
  if (ec_value < EC_MIN) { ec_status = "low"; float ec_target = (EC_MIN + EC_MAX) / 2.0; float ec_deficiency = ec_target - ec_value; float dose_ml = (ec_deficiency / 50.0) * (RESERVOIR_VOLUME_L / 10.0); snprintf(buffer, sizeof(buffer), "ALERT: EC is too low! Add %.1fml each of Jeevan (A) and Shakti (B).", dose_ml); ec_dose_msg = buffer;
  } else if (ec_value > EC_MAX) { ec_status = "high"; ec_dose_msg = "ALERT: EC is too high! Dilute with fresh water.";
  } else { ec_status = "ok"; ec_dose_msg = ""; }
}
void controlLeds() {
  if (system_halted) { return; }
  digitalWrite(ledRelayPin, (lux_value < lightThreshold) ? RELAY_ON : RELAY_OFF);
}
void manageFlushing() {
  if (system_halted) { isFlushingActive = false; return; }
  unsigned long currentMillis = millis();
  if (!isFlushingActive && (currentMillis - previousFlushingMillis >= flushingInterval)) { isFlushingActive = true; isFlushingPrecedence = true; previousFlushingMillis = currentMillis; Serial.println("Automatic flush activated."); }
  if (isFlushingActive && (currentMillis - previousFlushingMillis >= flushingDuration)) { isFlushingActive = false; isFlushingPrecedence = false; Serial.println("Flushing Cycle Complete."); }
}
void manageMisting() {
  if (system_halted) { isMistingActive = false; return; }
  unsigned long currentMillis = millis();
  if (!isFlushingActive && (currentMillis - previousMistingMillis >= mistingInterval) && !isFlushingPrecedence) { isMistingActive = true; previousMistingMillis = currentMillis; Serial.println("Misting pump activated."); }
  if (isMistingActive && (currentMillis - previousMistingMillis >= mistingDuration)) { isMistingActive = false; Serial.println("Misting Cycle Complete."); }
}
// MODIFIED: Simplified to control one pump based on two conditions
void updateRelays() {
  bool pump_on = isMistingActive || isFlushingActive;
  digitalWrite(mistingRelayPin, pump_on ? RELAY_ON : RELAY_OFF);
  // flushingRelayPin is the same pin, so no need to write to it separately
}