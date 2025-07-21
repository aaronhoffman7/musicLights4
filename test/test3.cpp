#include <Arduino.h>

// === MSGEQ7 Pin Configuration ===
#define STROBE_PIN 12      // Digital output to MSGEQ7 STROBE
#define RESET_PIN  13      // Digital output to MSGEQ7 RESET
#define ANALOG_PIN 36      // Analog input from MSGEQ7 OUT (must be ADC-capable)

// === Timing ===
#define READ_DELAY_MICROS 60  // Delay after strobe before reading

// === Globals ===
float bandValues[7];  // Stores the 7 band readings

void setup() {
  Serial.begin(115200);

  pinMode(STROBE_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  digitalWrite(STROBE_PIN, LOW);
  digitalWrite(RESET_PIN, LOW);

  // Startup MSGEQ7
  delay(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(10);
  digitalWrite(RESET_PIN, LOW);
}

void loop() {
  readMSGEQ7();

  // Print band values (CSV format)
  for (int i = 0; i < 7; i++) {
    Serial.print(bandValues[i], 1);  // One decimal place
    if (i < 6) Serial.print(",");
  }
  Serial.println();

  delay(50);  // Adjust for smoother data if needed
}


// === Read all 7 bands into bandValues[] ===
void readMSGEQ7() {
  digitalWrite(RESET_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(RESET_PIN, LOW);

  for (int i = 0; i < 7; i++) {
    digitalWrite(STROBE_PIN, LOW);
    delayMicroseconds(READ_DELAY_MICROS);

    int raw = analogRead(ANALOG_PIN);  // ESP32 ADC: 0–4095
    bandValues[i] = map(raw, 0, 4095, 0, 1023);  // optional: scale to 10-bit like Arduino

    digitalWrite(STROBE_PIN, HIGH);
    delayMicroseconds(READ_DELAY_MICROS);
  }
}
