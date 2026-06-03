#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Arduino.h>
#include <esp_sleep.h>

// ============================================================
// 2-CHANNEL VOLTMETER FOR ESP32
// ============================================================
// 
// SUGGESTED IMPROVEMENTS:
// 1. Add an external ADC (e.g., ADS1115) for higher accuracy and more channels
// 2. Add SD card logging to record voltage data over time
// 3. Add MQTT support to publish readings to a home automation server
// 4. Add a configuration page to save WiFi credentials and settings to SPIFFS
// 5. Add averaging filter to smooth noisy readings (e.g., moving average of last 10 samples)
// 6. Add calibration routine to compensate for resistor tolerance
// 7. Add trigger mode to capture events above/below threshold
// 8. Add battery voltage monitoring for portable operation
// 9. Add a secondary AP mode with a physical button to reset settings
// 10. Use ESP-NOW for low-power wireless sensor networks
//
// ============================================================

// ========== HARDWARE CONFIGURATION ==========
const int ADC_PIN1 = 34;        // Channel 1 (GPIO34)
const int ADC_PIN2 = 35;        // Channel 2 (GPIO35)
const float V_REF = 3.3;        // ESP32 ADC reference voltage
const int ADC_MAX = 4095;        // 12-bit ADC resolution
const float DIVIDER_FACTOR = 6.3636;  // For 0-21V range (R1=10k, R2=1.8k)
// Formula: DIVIDER_FACTOR = (R1 + R2) / R2 = (10000 + 1800) / 1800 = 6.555
// Adjusted slightly for 21V max: 21V / 3.3V = 6.3636

// ========== DEEP SLEEP CONFIGURATION ==========
const unsigned long SLEEP_TIMEOUT_MS = 30000;  // 30 seconds of zero volts before sleep
const float WAKE_VOLTAGE_THRESHOLD = 1.0;      // Wake if either channel > 1V
const unsigned long CHECK_INTERVAL_MS = 1000;  // Check voltage every second in sleep

// ========== NETWORK CONFIGURATION ==========
const char* AP_SSID = "ESP32_Voltmeter";
WebServer server(80);
WebSocketsServer webSocket(81);

// ========== VARIABLE UPDATE RATE ==========
uint32_t updateIntervalUs = 10000;  // 10ms = 100Hz default
uint32_t lastRead = 0;
float currentRate = 100.0;

// ========== DEEP SLEEP TIMER ==========
RTC_DATA_ATTR unsigned long lastActiveTime = 0;  // Persists across sleep
RTC_DATA_ATTR int bootCount = 0;                 // Debug counter

// ========== FAST ADC READ (2 Channels, 15kΩ Input) ==========
inline void readBothChannels(float& v1, float& v2) {
    v1 = analogRead(ADC_PIN1) * (V_REF / ADC_MAX) * DIVIDER_FACTOR;
    v2 = analogRead(ADC_PIN2) * (V_REF / ADC_MAX) * DIVIDER_FACTOR;
}

// ========== CHECK IF SHOULD ENTER DEEP SLEEP ==========
bool shouldEnterSleep(float v1, float v2) {
    if (v1 < 0.1 && v2 < 0.1) {  // Both channels near zero
        if (lastActiveTime == 0) {
            lastActiveTime = millis();
        }
        return (millis() - lastActiveTime) >= SLEEP_TIMEOUT_MS;
    } else {
        // Reset timer if any voltage is present
        lastActiveTime = 0;
        return false;
    }
}

// ========== WAKE FROM DEEP SLEEP CHECK ==========
bool shouldWakeFromSleep() {
    // Configure ADC for wake check
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(ADC_PIN1, ADC_11db);
    analogSetPinAttenuation(ADC_PIN2, ADC_11db);
    
    // Read both channels
    float v1 = analogRead(ADC_PIN1) * (V_REF / ADC_MAX) * DIVIDER_FACTOR;
    float v2 = analogRead(ADC_PIN2) * (V_REF / ADC_MAX) * DIVIDER_FACTOR;
    
    Serial.printf("Wake check: CH1=%.2fV, CH2=%.2fV\n", v1, v2);
    
    // Wake if either channel exceeds threshold
    return (v1 > WAKE_VOLTAGE_THRESHOLD) || (v2 > WAKE_VOLTAGE_THRESHOLD);
}

// ========== ENTER DEEP SLEEP ==========
void goToDeepSleep() {
    Serial.println("Entering deep sleep...");
    Serial.flush();
    
    // Configure wake up timer (check every second)
    esp_sleep_enable_timer_wakeup(CHECK_INTERVAL_MS * 1000);
    
    // Go to sleep
    esp_deep_sleep_start();
}

// ========== WEB PAGE (Embedded) ==========
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>2-Channel Voltmeter</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: monospace; background: black; color: lime; height: 100vh; display: flex; flex-direction: column; padding: 8px; }
        .value { display: flex; justify-content: space-between; gap: 10px; margin-bottom: 8px; }
        .value div { background: #111; padding: 4px 10px; font-size: 18px; flex: 1; text-align: left; }
        .ch1 { color: lime; }
        .ch2 { color: cyan; }
        .graph-container { overflow-x: auto; overflow-y: hidden; white-space: nowrap; background: #111; border: 1px solid #333; cursor: grab; -webkit-overflow-scrolling: touch; flex: 1; min-height: 0; }
        .graph-container:active { cursor: grabbing; }
        canvas { display: block; height: 100%; width: auto; background: #111; }
        .controls { text-align: center; margin-top: 8px; padding-top: 8px; border-top: 1px solid #333; white-space: nowrap; overflow-x: auto; }
        button { background: black; color: lime; border: 1px solid lime; margin: 0 4px; padding: 6px 10px; font-family: monospace; font-size: 14px; cursor: pointer; display: inline-block; }
        button:hover { background: #1a1a1a; }
        #followToggle { width: 70px; }
        .rate-control { display: inline-flex; align-items: center; gap: 6px; background: #111; padding: 4px 8px; border-radius: 4px; margin: 0 4px; }
        .rate-control button { font-size: 18px; font-weight: bold; padding: 3px 10px; margin: 0; }
        .rate-control span { font-size: 14px; min-width: 50px; text-align: center; cursor: pointer; background: #1a1a1a; padding: 3px 6px; border-radius: 3px; }
        .rate-control span:hover { background: #2a2a2a; }
        .rate-input { display: none; width: 60px; background: black; color: lime; border: 1px solid lime; font-family: monospace; font-size: 14px; text-align: center; padding: 3px 6px; }
        .toggle-follow { background: #0a3a0a !important; }
        .toggle-pause { background: #3a0a0a !important; color: #f88 !important; }
        .scroll-hint { text-align: center; font-size: 9px; color: #333; margin-bottom: 4px; }
    </style>
</head>
<body>
    <div class="value">
        <div>CH1: <span id="v1" class="ch1">0.00</span> V</div>
        <div>CH2: <span id="v2" class="ch2">0.00</span> V</div>
    </div>
    <div class="scroll-hint">drag to scroll | pause to freeze, follow to resume</div>
    <div class="graph-container" id="graphContainer"><canvas id="graph"></canvas></div>
    <div class="controls">
        <button id="clearBtn" onclick="clearGraph()">CLR (0)</button>
        <button id="followToggle" onclick="toggleFollow()" class="toggle-follow">pause</button>
        <div class="rate-control">
            <button onclick="changeRate(0.5)">-</button>
            <span id="rateDisplay" onclick="showRateInput()">100</span>
            <input type="number" id="rateInput" class="rate-input" min="0.01" max="500" step="any" onblur="hideRateInput()" onkeypress="handleRateKeypress(event)">
            <button onclick="changeRate(2)">+</button>
        </div>
    </div>
    <script>
        let data1 = [], data2 = [];
        let canvas = document.getElementById('graph');
        let ctx = canvas.getContext('2d');
        let container = document.getElementById('graphContainer');
        let currentRate = 100;
        let targetIntervalMs = 1000 / currentRate;
        let followMode = true;
        let intervalId = null;
        const PIXELS_PER_POINT = 1;
        const MAX_POINTS = 2000;
        const NICE_RATES = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500];
        
        function snapToNice(rate) {
            if (rate <= 0) return 0.01;
            if (rate >= 500) return 500;
            let closest = NICE_RATES[0];
            let minDiff = Math.abs(rate - closest);
            for (let i = 1; i < NICE_RATES.length; i++) {
                let diff = Math.abs(rate - NICE_RATES[i]);
                if (diff < minDiff) { minDiff = diff; closest = NICE_RATES[i]; }
            }
            return closest;
        }
        
        function showRateInput() {
            let span = document.getElementById('rateDisplay');
            let input = document.getElementById('rateInput');
            span.style.display = 'none';
            input.style.display = 'inline-block';
            input.value = currentRate;
            input.focus();
            input.select();
        }
        
        function hideRateInput() {
            let span = document.getElementById('rateDisplay');
            let input = document.getElementById('rateInput');
            span.style.display = 'inline-block';
            input.style.display = 'none';
        }
        
        function handleRateKeypress(event) {
            if (event.key === 'Enter') {
                let input = document.getElementById('rateInput');
                let newRate = parseFloat(input.value);
                if (!isNaN(newRate) && newRate >= 0.01 && newRate <= 500) {
                    setRateExact(newRate);
                }
                hideRateInput();
            }
        }
        
        function setRateExact(newRate) {
            currentRate = newRate;
            targetIntervalMs = 1000 / currentRate;
            let displayValue = currentRate < 0.1 ? currentRate.toFixed(3) : currentRate.toFixed(2);
            document.getElementById('rateDisplay').innerHTML = displayValue;
            if (intervalId) { clearInterval(intervalId); intervalId = null; }
            startDataGenerator();
        }
        
        function setRateSnapped(newRate) { setRateExact(snapToNice(newRate)); }
        
        function updateClearButton() { document.getElementById('clearBtn').innerHTML = `CLR (${data1.length})`; }
        
        function updateModeButton() {
            let btn = document.getElementById('followToggle');
            if (followMode) {
                btn.innerHTML = 'pause';
                btn.classList.remove('toggle-pause');
                btn.classList.add('toggle-follow');
            } else {
                btn.innerHTML = 'follow';
                btn.classList.remove('toggle-follow');
                btn.classList.add('toggle-pause');
            }
        }
        
        function toggleFollow() {
            followMode = !followMode;
            updateModeButton();
            if (followMode) {
                let targetWidth = Math.max(data1.length, container.clientWidth);
                canvas.width = targetWidth;
                canvas.height = container.clientHeight;
                drawFullGraph();
                scrollToEnd();
            }
        }
        
        function drawGrid() {
            let canvasHeight = canvas.height;
            for (let v = 0; v <= 21; v += 3) {
                let y = canvasHeight - (v / 21) * canvasHeight;
                ctx.beginPath();
                ctx.strokeStyle = '#333';
                ctx.lineWidth = 0.5;
                ctx.moveTo(0, y);
                ctx.lineTo(canvas.width, y);
                ctx.stroke();
                ctx.fillStyle = '#555';
                ctx.font = '8px monospace';
                ctx.fillText(v + 'V', 3, y - 2);
            }
        }
        
        function drawFullGraph() {
            let canvasHeight = canvas.height;
            ctx.fillStyle = '#111';
            ctx.fillRect(0, 0, canvas.width, canvasHeight);
            drawGrid();
            if (data2.length >= 2) {
                ctx.beginPath();
                ctx.strokeStyle = 'cyan';
                ctx.lineWidth = 1.5;
                for (let i = 0; i < data2.length; i++) {
                    let x = i * PIXELS_PER_POINT;
                    let y = canvasHeight - (data2[i] / 21) * canvasHeight;
                    y = Math.min(Math.max(y, 0), canvasHeight);
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                }
                ctx.stroke();
            }
            if (data1.length >= 2) {
                ctx.beginPath();
                ctx.strokeStyle = 'lime';
                ctx.lineWidth = 1.5;
                for (let i = 0; i < data1.length; i++) {
                    let x = i * PIXELS_PER_POINT;
                    let y = canvasHeight - (data1[i] / 21) * canvasHeight;
                    y = Math.min(Math.max(y, 0), canvasHeight);
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                }
                ctx.stroke();
            }
        }
        
        function scrollToEnd() {
            let maxScroll = canvas.width - container.clientWidth;
            if (maxScroll > 0) container.scrollLeft = maxScroll;
        }
        
        function addPoints(v1, v2) {
            data1.push(v1);
            data2.push(v2);
            if (data1.length > MAX_POINTS) { data1.shift(); data2.shift(); }
            document.getElementById('v1').innerHTML = v1.toFixed(2);
            document.getElementById('v2').innerHTML = v2.toFixed(2);
            updateClearButton();
            if (followMode) {
                let targetWidth = Math.max(data1.length, container.clientWidth);
                if (canvas.width !== targetWidth) {
                    canvas.width = targetWidth;
                    canvas.height = container.clientHeight;
                }
                drawFullGraph();
                scrollToEnd();
            }
        }
        
        function clearGraph() {
            data1 = []; data2 = [];
            canvas.width = container.clientWidth;
            canvas.height = container.clientHeight;
            drawFullGraph();
            document.getElementById('v1').innerHTML = '0.00';
            document.getElementById('v2').innerHTML = '0.00';
            updateClearButton();
            container.scrollLeft = 0;
        }
        
        function changeRate(factor) {
            let currentIndex = -1;
            for (let i = 0; i < NICE_RATES.length; i++) {
                if (Math.abs(NICE_RATES[i] - currentRate) < 0.0001) { currentIndex = i; break; }
            }
            if (currentIndex !== -1) {
                let newIndex = currentIndex + (factor > 1 ? 1 : -1);
                if (newIndex >= 0 && newIndex < NICE_RATES.length) setRateExact(NICE_RATES[newIndex]);
            } else {
                let multiplied = currentRate * factor;
                if (multiplied < 0.01) multiplied = 0.01;
                if (multiplied > 500) multiplied = 500;
                setRateSnapped(multiplied);
            }
        }
        
        let time = 0;
        function generateTestData() {
            let v1 = 10 + 8 * Math.sin(time * 0.5);
            let v2 = 5 + 12 * Math.sin(time * 0.3 + 2);
            time += 0.05;
            return [v1, v2];
        }
        
        function generateAndAdd() {
            let [v1, v2] = generateTestData();
            addPoints(v1, v2);
        }
        
        function startDataGenerator() {
            if (intervalId) clearInterval(intervalId);
            intervalId = setInterval(generateAndAdd, targetIntervalMs);
        }
        
        function handleResize() {
            if (followMode) {
                canvas.height = container.clientHeight;
                canvas.width = Math.max(data1.length, container.clientWidth);
                drawFullGraph();
                scrollToEnd();
            } else {
                canvas.height = container.clientHeight;
                drawFullGraph();
            }
        }
        
        const resizeObserver = new ResizeObserver(() => handleResize());
        resizeObserver.observe(container);
        window.addEventListener('resize', handleResize);
        startDataGenerator();
        setTimeout(() => { handleResize(); updateModeButton(); updateClearButton(); }, 100);
    </script>
</body>
</html>
)rawliteral";

// ========== WEBSOCKET EVENT HANDLER ==========
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String msg = String((char*)payload);
        if (msg.indexOf("rate") > 0) {
            int colonIndex = msg.indexOf(":");
            if (colonIndex > 0) {
                float newRate = msg.substring(colonIndex + 1).toFloat();
                if (newRate >= 0.01 && newRate <= 500) {
                    currentRate = newRate;
                    updateIntervalUs = 1000000.0 / currentRate;
                    Serial.printf("Rate changed to %.3f Hz\n", currentRate);
                    lastRead = micros();
                }
            }
        }
    }
}

// ========== WEB SERVER HANDLER ==========
void handleRoot() {
    server.send(200, "text/html", index_html);
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    delay(100);
    
    bootCount++;
    Serial.printf("\nBoot #%d\n", bootCount);
    
    // Check if we woke from deep sleep
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woke from deep sleep (timer)");
        // Check if voltage is present - if not, go back to sleep
        if (!shouldWakeFromSleep()) {
            Serial.println("No voltage detected, going back to sleep...");
            delay(100);
            goToDeepSleep();
        }
        Serial.println("Voltage detected, staying awake");
    }
    
    // Configure ADC
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(ADC_PIN1, ADC_11db);
    analogSetPinAttenuation(ADC_PIN2, ADC_11db);
    
    // Start Access Point
    WiFi.softAP(AP_SSID);
    Serial.println("\nESP32 Voltmeter Ready");
    Serial.printf("AP: %s\n", AP_SSID);
    Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("2 Channels | 0-21V Range | Deep sleep after 30s of zero volts\n");
    
    // Setup web server
    server.on("/", handleRoot);
    server.begin();
    
    // Setup WebSockets
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    
    Serial.println("Connect and open browser");
}

// ========== MAIN LOOP ==========
void loop() {
    server.handleClient();
    webSocket.loop();
    
    uint32_t now = micros();
    if (now - lastRead >= updateIntervalUs) {
        lastRead = now;
        
        float v1, v2;
        readBothChannels(v1, v2);
        
        // Check for deep sleep condition
        if (shouldEnterSleep(v1, v2)) {
            Serial.println("Both channels at zero for 30 seconds - entering deep sleep");
            Serial.flush();
            delay(100);
            goToDeepSleep();
        }
        
        // Send as JSON array
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "[%.3f,%.3f]", v1, v2);
        webSocket.broadcastTXT(buffer);
    }
}
