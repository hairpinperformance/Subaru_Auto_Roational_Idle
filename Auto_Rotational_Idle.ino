// === Pin assignments ===
const int neutralPin   = A7;  // Analog read - HIGH = in neutral (via threshold)
const int clutchPin    = 12;  // LOW = released
const int feedbackPin  = 8;   // HIGH = defogger ON
const int defrostOut   = 6;   // Arduino drives defogger switch

// === Timing configuration ===
const unsigned long pressDurationMs   = 1000;  // 1-second simulated press
const unsigned long settleDelayMs     = 100;   // allow feedback to settle
const unsigned long feedbackStableMs  = 100;   // feedback must be stable
const unsigned long deactivateWindowMs = 4000; // 4-second window for 3 ON triggers
const unsigned long manualLockoutMs   = 3000;  // 3-second ignore period after deactivation
const unsigned long neutralHoldMs     = 3000;  // must stay in neutral this long before ON trigger
const unsigned long clutchHoldMs      = 1500;  // must stay released this long before ON trigger

// === Analog threshold for neutral detection ===
const int neutralThreshold = 512; // ~2.5V for 5V Arduino (adjust if needed)

// --- State variables ---
bool systemActive = false;
bool isPressing   = false;
unsigned long pressStartMs       = 0;
unsigned long feedbackChangeTime = 0;
bool lastFeedback = false;

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
  pinMode(feedbackPin, INPUT_PULLUP);
  pinMode(defrostOut, OUTPUT);
  digitalWrite(defrostOut, LOW);

  Serial.begin(9600);
  Serial.println("Defogger control initialized - analog neutral input on A7");
}

void loop() {
  unsigned long now = millis();

  // --- Read inputs ---
  int neutralValue = analogRead(neutralPin);
  bool neutralEngaged  = (neutralValue > neutralThreshold);
  bool clutchReleased  = (digitalRead(clutchPin) == LOW);
  bool feedbackActive  = (digitalRead(feedbackPin) == HIGH); // TRUE = defogger ON

  // --- Detect manual defogger press to start system ---
  if (!lastManualFeedback && feedbackActive) {
    if (now > ignoreManualUntil) {
      if (!systemActive) {
        systemActive = true;
        Serial.println("System ACTIVATED by manual defogger press");
      }
    } else {
      Serial.println("Manual press ignored (in lockout window)");
    }
  }
  lastManualFeedback = feedbackActive;

  // --- Debounce feedback ---
  if (feedbackActive != lastFeedback) {
    feedbackChangeTime = now;
    lastFeedback = feedbackActive;
  }
  bool feedbackStable = (now - feedbackChangeTime) >= feedbackStableMs;

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

  if (systemActive && !isPressing && feedbackStable) {
    if (!feedbackActive && neutralHeldLongEnough && clutchHeldLongEnough) {
      triggerSwitch = true;
      isTriggerOn = true;
      Serial.println("Trigger ON defogger (after 1.5s neutral + clutch hold)");
    } else if (feedbackActive && (!neutralEngaged || !clutchReleased)) {
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

      // Prevent immediate reactivation
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

  // --- End press ---
  if (isPressing && (now - pressStartMs >= pressDurationMs)) {
    digitalWrite(defrostOut, LOW);
    isPressing = false;
    delay(settleDelayMs);
    feedbackActive = (digitalRead(feedbackPin) == HIGH);
    Serial.print("Feedback now: ");
    Serial.println(feedbackActive ? "ON" : "OFF");
  }

  // --- Debug print ---
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    Serial.print("NeutralVal:"); Serial.print(neutralValue);
    Serial.print(" NeutralEngaged:"); Serial.print(neutralEngaged);
    Serial.print(" ClutchReleased:"); Serial.print(clutchReleased);
    Serial.print(" ClutchHeld:"); Serial.print(clutchHeldLongEnough);
    Serial.print(" FeedbackActive:"); Serial.print(feedbackActive);
    Serial.print(" SystemActive:"); Serial.print(systemActive);
    Serial.print(" NeutralHeld:"); Serial.print(neutralHeldLongEnough);
    Serial.print(" ON_Count:"); Serial.println(triggerOnCount);
    lastPrint = now;
  }

  delay(10);
}
