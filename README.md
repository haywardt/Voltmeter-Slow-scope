# ESP32 2-Channel Voltmeter (0-21V)

A dual-channel, web-based voltmeter for ESP32 that streams live voltage data via WebSockets with a real-time scrolling graph. Perfect for bench testing, data logging, or monitoring two voltage sources simultaneously.

## Features

- **2 independent channels** – 0-21V DC measurement range
- **Adjustable update rate** – 0.01 Hz to 500 Hz (fractional Hz supported)
- **Live web interface** – Real-time graphs with 3V grid lines
- **WebSocket streaming** – Low overhead, efficient data push
- **500-point scrolling history** per channel (auto-prunes oldest)
- **Configurable input impedance** – 15kΩ default (3.6mA total draw)
- **Access Point mode** – No router needed, direct connection

## Hardware Requirements

### Components
- ESP32 development board
- 4x resistors:
  - 2x 10kΩ (R1 for each channel)
  - 2x 1.8kΩ (R2 for each channel)
- Breadboard and jumper wires (optional)

### Pin Connections

| Channel | ESP32 Pin | Resistor Divider |
|---------|-----------|------------------|
| CH1     | GPIO34    | 10kΩ + 1.8kΩ to GND |
| CH2     | GPIO35    | 10kΩ + 1.8kΩ to GND |

### Circuit Description

**Channel 1:** Connect Vin (0-21V) to one end of a 10kΩ resistor. Connect the other end of the 10kΩ resistor to both GPIO34 and one end of a 1.8kΩ resistor. Connect the other end of the 1.8kΩ resistor to GND.

**Channel 2:** Same as Channel 1, but using GPIO35 instead of GPIO34.

## Installation

### 1. Install Required Libraries
Open Arduino IDE → Tools → Manage Libraries → Install:
- WebSockets by Markus Sattler (version 2.3.5 or newer)
- WiFi (built-in)
- WebServer (built-in)

### 2. Upload Code to ESP32
- Select your ESP32 board in Tools → Board
- Select correct COM port
- Click Upload

### 3. Connect and Use
1. Power the ESP32 via USB
2. Connect to Wi-Fi network `ESP32_Voltmeter` (no password)
3. Open browser to `http://192.168.4.1`
4. Apply 0-21V to GPIO34 and/or GPIO35

## Web Interface

### Controls
- **Clear All** – Resets both graphs
- **Rate [Hz]** – Set update frequency (0.01 to 500 Hz)
- **Apply** – Confirm new rate

### Display
- **CH1/CH2 values** – Live voltage readings (3 decimal places)
- **Graphs** – 0-21V vertical scale with 3V grid lines
- **Sample counter** – Total points shown per channel

### Graph Features
- Lime green trace = Channel 1
- Cyan trace = Channel 2
- Auto-scrolling (new points on right, old points scroll left)
- 500-point buffer per channel

## Performance

| Parameter | Value |
|-----------|-------|
| Update rate | 0.01 - 500 Hz |
| ADC resolution | 12-bit (0-4095) |
| Voltage accuracy | +-3% (typical, with 1% resistors) |
| Input impedance | 11.8kΩ per channel |
| Current draw (divider) | 1.78 mA per channel @21V |
| Total draw (divider) | 3.6 mA @21V |
| WebSocket latency | Less than 1 ms typical |

## Troubleshooting

### No Wi-Fi network appears
- Check ESP32 LED (should be on/flashing)
- Reset ESP32 (press EN button)
- Verify code uploaded successfully (check Serial Monitor)

### Web page loads but no data
- Check voltage divider connections
- Verify GPIO34/35 are not shorted to GND or Vcc
- Open browser console (F12) for WebSocket errors
- Try refreshing the page

### Voltage readings are inaccurate
- Verify resistor values (measure with multimeter)
- Calibrate divider factor using actual resistor measurements
- Add 10nF capacitor from GPIO to GND for noisy signals

## Customization

### Change Voltage Range
Update the DIVIDER_FACTOR constant in code based on your resistor values:
`DIVIDER_FACTOR = (R1 + R2) / R2`

### Add More Channels
- Use additional ADC1 pins: GPIO32, GPIO33, GPIO36, GPIO39
- Add divider circuit for each channel
- Extend the read function for all channels

### Battery-Powered Operation
Use high-impedance divider:
- R1 = 1MΩ, R2 = 180kΩ
- Add 10nF capacitor from GPIO to GND
- Current draw drops to 18µA per channel @21V

## Technical Details

### ADC Configuration
- Resolution: 12-bit (0-4095)
- Attenuation: ADC_11db (0-3.6V range)
- Sampling: Sequential (non-simultaneous)

### Divider Math
- Divider ratio: R2 / (R1 + R2) = 1.8k / 11.8k = 0.1525
- Input voltage = ADC_reading * (3.3V / 4095) * ((R1 + R2) / R2)

### Timing Loop
- Uses micros() for precise intervals
- Integer arithmetic for interval calculation
- Non-blocking, handles WebSocket traffic

## License

MIT License – Free for personal and commercial use.

## Version History

- v1.0 – Initial release
  - 2 channels, 0-21V range
  - WebSocket streaming
  - Adjustable 0.01-500 Hz update rate
  - 3V grid lines
