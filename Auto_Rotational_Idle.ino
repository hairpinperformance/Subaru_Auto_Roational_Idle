// === Pin assignments ===
const int neutralPin   = A7;  // Analog read - HIGH = in neutral
const int clutchPin    = 12;  // LOW = released
const int feedbackPin  = A3;  // Analog read - HIGH = defogger ON
const int defrostOut   = 6;   // Arduino drives defogger switch

// === Timing configuration ===
const unsigned long pressDurationMs   = 1000;  // 1-second simulated press
const unsigned long settleDelayMs     = 100;   // allow feedback to settle
const unsigned long deactivateWindowMs = 4000; // 4-second window for 3 ON triggers
const unsigned long manualLockoutMs   = 3000;  // 3-second ignore period after deactivation
const unsigned long neutralHoldMs     = 3000;  // must stay in neutral this long before ON trigger
const unsigned long clutchHoldMs      = 1500;  // must stay released this long before ON trigger

// === Analog thresholds ===
const int neutralThreshold  = 512; // ~2.5V threshold for neutral detection
const int feedbackHighThreshold = 900;  // above this = definitely ON
const int feedbackLowThreshold  = 100;  // below this = definitely OFF
const unsigned long feedbackOffDelayMs = 1000;  // must stay low 1s before OFF

// --- State variables ---
bool systemActive = false;
bool isPressing   = false;
unsigned long pressStartMs       = 0;
unsigned long feedbackLowStart   = 0;
bool feedbackState = false;  // smoothed/filtered ON/OFF
bool lastFeedbackState = false;

// --- Manual activate/deactivate tracking ---
bool lastManualFeedback = false;
unsigned long ignoreManualUntil = 0;

// --- Auto-deactivate (3 ON triggers in 4s) ---
int triggerOnCount = 0;
unsigned long firstTriggerOnTime = 0;

// --- Neutral hold tracking ---
bool wasNeutral = false;
unsigned long neutralStartMs = 0;

// --- Clutch hold tracking ---
bool wasClutchReleased = false;
unsigned long clutchStartMs = 0;

void setup() {
  pinMode(clutchPin, INPUT_PULLUP);
  pinMode(defrostOut, OUTPUT);
  digitalWrite(defrostOut, LOW);

  Serial.begin(9600);
  Serial.println("Defogger control initialized");
}

// === Feedback update function ===
void updateFeedbackState() {
  int val = analogRead(feedbackPin);
  unsigned long now = millis();

  // If currently ON, look for a low condition
  if (feedbackState) {
    if (val < feedbackLowThreshold) {
      if (feedbackLowStart == 0) {
        feedbackLowStart = now;
        Serial.print("Feedback low detected - starting OFF timer (v=");
        Serial.print(val);
        Serial.println(")");
      } else if (now - feedbackLowStart >= feedbackOffDelayMs) {
        feedbackState = false;
        feedbackLowStart = 0;
        Serial.println("Feedback OFF confirmed (stable low)");
      }
    } else {
      if (feedbackLowStart > 0) {
        Serial.println("Feedback spike -> cancel OFF timer, keep ON");
        feedbackLowStart = 0;
      }
    }
  } else {
    if (val > feedbackHighThreshold) {
      feedbackState = true;
      Serial.println("Feedback ON detected (stable high)");
    }
  }

  // Optional 1s debug print
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    Serial.print("FeedbackRaw:"); Serial.print(val);
    Serial.print(" FeedbackState:"); Serial.println(feedbackState ? "ON" : "OFF");
    lastPrint = now;
  }
}

// === Main loop ===
void loop() {
  unsigned long now = millis();

  // --- Update feedback state ---
  updateFeedbackState();

  // --- Read other inputs ---
  int neutralValue = analogRead(neutralPin);
  bool neutralEngaged  = (neutralValue > neutralThreshold);
  bool clutchReleased  = (digitalRead(clutchPin) == LOW);

  // --- Detect manual defogger press to start system ---
  if (!lastManualFeedback && feedbackState) {
    if (now > ignoreManualUntil) {
      if (!systemActive) {
        systemActive = true;
        Serial.println("System ACTIVATED by manual defogger press");
      }
    } else {
      Serial.println("Manual press ignored (in lockout window)");
    }
  }
  lastManualFeedback = feedbackState;

  // --- Track neutral hold time ---
  if (neutralEngaged) {
    if (!wasNeutral) {
      wasNeutral = true;
      neutralStartMs = now;
    }
  } else {
    wasNeutral = false;
    neutralStartMs = 0;
  }
  bool neutralHeldLongEnough = (wasNeutral && (now - neutralStartMs >= neutralHoldMs));

  // --- Track clutch hold time ---
  if (clutchReleased) {
    if (!wasClutchReleased) {
      wasClutchReleased = true;
      clutchStartMs = now;
    }
  } else {
    wasClutchReleased = false;
    clutchStartMs = 0;
  }
  bool clutchHeldLongEnough = (wasClutchReleased && (now - clutchStartMs >= clutchHoldMs));

  // --- Determine if we need to trigger a switch press ---
  bool triggerSwitch = false;
  bool isTriggerOn = false;

  if (systemActive && !isPressing) {
    if (!feedbackState && neutralHeldLongEnough && clutchHeldLongEnough) {
      triggerSwitch = true;
      isTriggerOn = true;
      Serial.println("Trigger ON defogger (after neutral + clutch hold)");
    } else if (feedbackState && (!neutralEngaged || !clutchReleased)) {
      triggerSwitch = true;
      isTriggerOn = false;
      Serial.println("Trigger OFF defogger");
    }
  }

  // --- Track ON triggers for auto-deactivation ---
  if (isTriggerOn) {
    if (firstTriggerOnTime == 0 || (now - firstTriggerOnTime > deactivateWindowMs)) {
      firstTriggerOnTime = now;
      triggerOnCount = 1;
    } else {
      triggerOnCount++;
    }

    if (triggerOnCount >= 3) {
      systemActive = false;
      Serial.println("System DEACTIVATED - too many ON triggers within 4s");
      ignoreManualUntil = now + manualLockoutMs;
      triggerOnCount = 0;
      firstTriggerOnTime = 0;
    }
  }

  // --- Trigger switch press ---
  if (!isPressing && triggerSwitch && systemActive) {
    digitalWrite(defrostOut, HIGH);
    isPressing = true;
    pressStartMs = now;
  }

  // --- End simulated press ---
  if (isPressing && (now - pressStartMs >= pressDurationMs)) {
    digitalWrite(defrostOut, LOW);
    isPressing = false;
    delay(settleDelayMs);
    Serial.print("Feedback now: ");
    Serial.println(feedbackState ? "ON" : "OFF");
  }

  // --- Debug print ---
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    Serial.print("NeutralVal:"); Serial.print(neutralValue);
    Serial.print(" NeutralEngaged:"); Serial.print(neutralEngaged);
    Serial.print(" ClutchReleased:"); Serial.print(clutchReleased);
    Serial.print(" FeedbackState:"); Serial.print(feedbackState ? "ON" : "OFF");
    Serial.print(" SystemActive:"); Serial.print(systemActive);
    Serial.print(" NeutralHeld:"); Serial.print(neutralHeldLongEnough);
    Serial.print(" ON_Count:"); Serial.println(triggerOnCount);
    lastPrint = now;
  }

  delay(10);
}
