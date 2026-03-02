#include <NimBLEDevice.h>
#include <MFRC522.h>
#include <SPI.h>
#include <FastLED.h>

// Pin Definitions
#define REED_PIN      1    // GPIO1: Magnetic Reed Switch
#define LED_POWER_PIN 10   // GPIO10: LED Power Gate
#define LED_DATA_PIN  4    // GPIO4: SK6812MINI Data
#define RC522_RST     2    // GPIO2: RFID Reset
#define RC522_SS      7    // GPIO7: RFID Chip Select
#define RC522_IRQ     3    // GPIO3: RFID Interrupt for Wake-up

const float BELT_LENGTH = 2.8; 
MFRC522 mfrc522(RC522_SS, RC522_RST);
CRGB leds[1];
NimBLECharacteristic* pTreadmillData;
bool deviceConnected = false;
bool workoutInProgress = false;
volatile int pulseCount = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastPulseTime = 0;

// BLE 
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        leds[0] = CRGB::Green; FastLED.show(); // Connected Blink
        delay(500);
        leds[0] = CRGB::Black; FastLED.show();
    };
    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
    }
};

// --- Interrupt Service Routine ---
void IRAM_ATTR handlePulse() {
    unsigned long now = millis();
    if (now - lastPulseTime > 50) { // Software Debounce
        pulseCount++;
        lastPulseTime = now;
        workoutInProgress = true; // Lock session upon first movement
    }
}

void setup() {
    // 1. Gated LED Initialization
    pinMode(LED_POWER_PIN, OUTPUT);
    digitalWrite(LED_POWER_PIN, HIGH);
    FastLED.addLeds<SK6812, LED_DATA_PIN, GRB>(leds, 1);

    // 2. Hardware Setup
    pinMode(REED_PIN, INPUT_PULLUP);
    attachInterrupt(digitalRead(REED_PIN), handlePulse, FALLING);
    SPI.begin(4, 5, 6, 7); 
    mfrc522.PCD_Init();

    // 3. BLE FTMS Initialization
    NimBLEDevice::init("pi.runner");
    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    NimBLEService *pService = pServer->createService("1826"); // Fitness Machine
    pTreadmillData = pService->createCharacteristic("2ACD", NIMBLE_PROPERTY::NOTIFY); // Treadmill Data
    
    pService->start();
    pServer->getAdvertising()->start();
}

void loop() {
    // White Blink if searching for phone
    if (!deviceConnected) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastBlinkTime > 1000) {
            leds[0] = (leds[0] == CRGB::Black) ? CRGB::White : CRGB::Black;
            FastLED.show();
            lastBlinkTime = currentMillis;
        }
    }

    // Broadcast Data (Every 1s if connected)
    if (deviceConnected && workoutInProgress) {
        updateFTMS(); 
        delay(1000);
    }

    // Session End (Phone Tap)
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        if (workoutInProgress) {
            terminateAndSleep();
        }
    }
}

void updateFTMS() {
   
    uint16_t speed = (pulseCount > 0) ? 500 : 0; 
    uint32_t distance = pulseCount * BELT_LENGTH;
    
    uint8_t data[12] = {0x04, 0x00, (uint8_t)(speed & 0xFF), (uint8_t)(speed >> 8)};
    data[4] = (uint8_t)(distance & 0xFF);
    data[5] = (uint8_t)((distance >> 8) & 0xFF);
    data[6] = (uint8_t)((distance >> 16) & 0xFF);
    
    pTreadmillData->setValue(data, 12);
    pTreadmillData->notify();
}

void terminateAndSleep() {
    leds[0] = CRGB::Red; FastLED.show(); // End of Session Blink
    delay(500);
    leds[0] = CRGB::Black; FastLED.show();
    
    digitalWrite(LED_POWER_PIN, LOW); // Kill LED power
    esp_deep_sleep_enable_gpio_wakeup(1 << RC522_IRQ, ESP_GPIO_WAKEUP_LOW_LEVEL);
    esp_deep_sleep_start();
}
