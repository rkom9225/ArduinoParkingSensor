#include <cmath>

//#define SERIAL_DBG
#define PT2745 TRUE
#define TM1637 TRUE

#ifdef TM1637
#include <TM1637Display.h>
#endif

// Pin assignment
#define SRF05_ECHO 2
#define SRF05_TRIG 5
#define GRN_LED_PIN 7
#define RED_LED_PIN 12
#ifdef TM1637
#define DISP_CLK 3
#define DISP_DIO 4
#endif

const int MIN_SAFE_PARK_DIST = 10;
const int MAX_SAFE_PARK_DIST = 20;
const int MAX_VALID_DIST = 100;

#ifdef PT2745  // Support for PT2745 (or similar) passive piezo buzzer
#define BUZZER_PIN 8

typedef struct PT2745 {
  long interval = 500;
  long duration = interval * 0.5;
  long lastBuzz = millis();
  int frequency = 450;  // Hz

  void stop() {
    noTone(BUZZER_PIN);
  }

  void soundAtInterval() {
    if (millis() - lastBuzz >= duration)
      noTone(BUZZER_PIN);
    if (millis() - lastBuzz >= interval) {
      noTone(BUZZER_PIN);
      tone(BUZZER_PIN, frequency);
      lastBuzz = millis();
    }
  }

  void setInterval(int distance) {
    interval = min(30 * distance, 1000);
    duration = min(interval * 0.5, 200);  // 50% duty cycle or 200 ms, whichever is lower
  }

} Buzzer;

Buzzer buzzer;
#endif

//Moving average filter for storing last n distances
#define FILTER_SIZE 50

typedef struct Filter {
  unsigned int currentIndex = 0;
  unsigned int filled = 0;
  int values[FILTER_SIZE]{ 0 };

  void addValue(const int newValue) {
    if (newValue == 0)
      return;

    values[currentIndex++] = newValue;

    if (filled < FILTER_SIZE)
      filled += 1;

    if (currentIndex == FILTER_SIZE)
      currentIndex = 0;
  }

  int getAverage() {
    int sum = 0;

    if (filled == 0)
      return 0;

    for (int i = 0; i < filled; i++) {
      if (values[i] != 0)
        sum += values[i];
    }

    return std::floor((float)sum / filled);
  }
} Filter;

Filter filter;

typedef struct HYSRF05 {
  const float durationFactor = 0.0343f;  // Speed of sound in cm/µs
  volatile long echoStartTime = 0;
  volatile long echoPulseDuration = 0;
  unsigned long lastTriggerTime = 0;
  float lastDistance = 0.0f;
  bool measuringEchoPin = false;
  bool triggerPulseSent = false;

  float getDistance() {
    if (!measuringEchoPin && echoPulseDuration != 0 && echoPulseDuration < 30000) {
      lastDistance = (echoPulseDuration * durationFactor) / 2.0f;  //Divided by two due to round-trip time
      Serial.print("Pulse duration: ");
      Serial.print(echoPulseDuration);
      Serial.println();
    }
    return std::round(lastDistance);
  }

  void read() {
    triggerHigh();
    triggerLow();
  }

  void triggerHigh() {
    if (!measuringEchoPin && (micros() - lastTriggerTime > 60000)) {
      digitalWrite(SRF05_TRIG, HIGH);
      lastTriggerTime = micros();
      triggerPulseSent = true;
    }
  }

  void triggerLow() {
    if (triggerPulseSent && (micros() - lastTriggerTime > 10)) {
      digitalWrite(SRF05_TRIG, LOW);
      triggerPulseSent = false;
      measuringEchoPin = true;
    }
  }
} HYSRF05;

HYSRF05 sensor;

void echoISR() {
  if (digitalRead(SRF05_ECHO) == HIGH) {
    sensor.echoStartTime = micros();
  } else {
    sensor.echoPulseDuration = micros() - sensor.echoStartTime;
    sensor.measuringEchoPin = false;
  }
}

int mockSensorDistance() {
  if (Serial.available() > 0) {
    const int newDistance = Serial.parseInt();

    return newDistance;
  }
}

// Generic variables
int lastReportedDistance = 999;
long lastBlink = millis();
long blinkInterval = 500;
long blinkDuration = blinkInterval * 0.5;

void clearLED(int pin) {
  signalLED(pin, false);
}

#ifdef TM1637  // Support for TM1637 7-segment LCD display
TM1637Display display = TM1637Display(DISP_CLK, DISP_DIO);
const uint8_t allON[] = { 0xff, 0xff, 0xff, 0xff };

long tm1637BlinkInterval = 2000;
long tm1637BlinkDuration = tm1637BlinkInterval * 0.5;
long tm1637lastBlink = millis();

const uint8_t FAR[] = {
  SEG_A | SEG_E | SEG_F | SEG_G,
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G
};

const uint8_t OK[] = {
  SEG_G,
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
  SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
  SEG_G
};

void blinkDisplay() {
  if (tm1637TimeSinceLastBlink() >= tm1637BlinkDuration) {
    display.clear();
  }

  if (tm1637TimeSinceLastBlink() >= tm1637BlinkInterval) {
    display.setSegments(OK, 4, 0);
    tm1637lastBlink = millis();
  }
}

void initDisplay() {
  display.setBrightness(5);
  display.setSegments(FAR, 3, 1);
}

long tm1637TimeSinceLastBlink() {
  return millis() - tm1637lastBlink;
}
#endif

void signalLED(int ledPin, bool on) {
  (on) ? digitalWrite(ledPin, HIGH) : digitalWrite(ledPin, LOW);
  if (on) lastBlink = millis();
}

void updateInterval(int distance) {
  blinkInterval = min(30 * distance, 1000);
  blinkDuration = min(blinkInterval * 0.5, 200);  // 50% duty cycle or 200 ms, whichever is lower
#ifdef PT2745
  buzzer.setInterval(distance);
#endif
}

void intervalBlinkLED() {
  if (timeSinceLastBlink() >= blinkDuration) {
    signalLED(RED_LED_PIN, false);
  }

  if (timeSinceLastBlink() >= blinkInterval) {
    signalLED(RED_LED_PIN, true);
  }
}

long timeSinceLastBlink() {
  return millis() - lastBlink;
}

bool checkIdeal(int distance) {
  // Check if we're in the ideal parking range
  if (distance <= MAX_SAFE_PARK_DIST && distance >= MIN_SAFE_PARK_DIST)
    return true;
  return false;
}

bool checkOutOfRange(int distance) {
  if (distance > MAX_VALID_DIST)
    return true;
  return false;
}

void clearAllSignals() {
  clearLED(GRN_LED_PIN);
  clearLED(RED_LED_PIN);
#ifdef TM1637
  display.clear();
#endif
#ifdef PT2745
  buzzer.stop();
#endif
}

void updateSignals(bool distanceChanged, int newDistance) {
  if (checkOutOfRange(newDistance)) {
    if (distanceChanged) {
      clearAllSignals();
#ifdef TM1637
      display.setSegments(FAR, 3, 1);
#endif
      lastReportedDistance = newDistance;
    }
    return;  // We are still too far away to need any assistance
  }

  if (checkIdeal(newDistance)) {
    clearLED(RED_LED_PIN);
    buzzer.stop();
    signalLED(GRN_LED_PIN, true);
#ifdef TM1637
    blinkDisplay();
#endif
    lastReportedDistance = newDistance;
  } else {
    clearLED(GRN_LED_PIN);
    intervalBlinkLED();
    buzzer.soundAtInterval();
    if (distanceChanged) {
#ifdef TM1637
      display.clear();
      display.showNumberDec(newDistance);
#endif
      updateInterval(newDistance);
      lastReportedDistance = newDistance;
    }
#ifdef PT2745
#endif
  }
}

void setup() {
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GRN_LED_PIN, OUTPUT);
  pinMode(SRF05_TRIG, OUTPUT);
  pinMode(SRF05_ECHO, INPUT);
  attachInterrupt(digitalPinToInterrupt(SRF05_ECHO), echoISR, CHANGE);
#ifdef PT2745
  pinMode(BUZZER_PIN, OUTPUT);
#endif
#ifdef TM1637
  pinMode(DISP_CLK, OUTPUT);
  pinMode(DISP_DIO, OUTPUT);
  initDisplay();
#endif
}

void loop() {
  sensor.read();
  filter.addValue(sensor.getDistance());

#ifdef SERIAL_DBG
  int newDistance = mockSensorDistance();
#else
  int newDistance = filter.getAverage();
#endif
  bool distanceChanged = (newDistance != lastReportedDistance);

  updateSignals(distanceChanged, newDistance);
}
