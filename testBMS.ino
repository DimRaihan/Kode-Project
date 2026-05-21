#include <NimBLEDevice.h>

const char* TARGET_ADDR = "20:25:03:32:08:0a";

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* dataChar = nullptr;

uint8_t requestSettings[] = {
  0xAA, 0x55, 0x90, 0xEB, 0x96, 0x00, 0x79, 0x62, 0x96, 0xED,
  0xE3, 0xD0, 0x82, 0xA1, 0x9B, 0x5B, 0x3C, 0x9C, 0x4B, 0x5D
};

uint8_t requestDeviceInfo[] = {
  0xAA, 0x55, 0x90, 0xEB, 0x97, 0x00, 0xDF, 0x52, 0x88, 0x67,
  0x9D, 0x0A, 0x09, 0x6B, 0x9A, 0xF6, 0x70, 0x9A, 0x17, 0xFD
};

uint16_t readUInt16LE(uint8_t* data, int index) {
  return (uint16_t)data[index] | ((uint16_t)data[index + 1] << 8);
}

void parseLiveFrame(uint8_t* data, size_t len) {
  if (len < 20) return;

  if (!(data[0] == 0x55 && data[1] == 0xAA && data[2] == 0xEB &&
        data[3] == 0x90 && data[4] == 0x02)) {
    return;
  }

  uint8_t frameCounter = data[5];

  float cell[6];
  float packVoltage = 0.0;

  for (int i = 0; i < 6; i++) {
    uint16_t mv = readUInt16LE(data, 6 + (i * 2));
    cell[i] = mv / 1000.0;
    packVoltage += cell[i];
  }

  Serial.println();
  Serial.println("===== JK BMS LIVE DATA =====");

  Serial.print("Frame Counter : ");
  Serial.println(frameCounter);

  Serial.print("Pack Voltage  : ");
  Serial.print(packVoltage, 3);
  Serial.println(" V");

  for (int i = 0; i < 6; i++) {
    Serial.print("Cell ");
    Serial.print(i + 1);
    Serial.print("        : ");
    Serial.print(cell[i], 3);
    Serial.println(" V");
  }

  float minCell = cell[0];
  float maxCell = cell[0];
  int minIndex = 0;
  int maxIndex = 0;

  for (int i = 1; i < 6; i++) {
    if (cell[i] < minCell) {
      minCell = cell[i];
      minIndex = i;
    }
    if (cell[i] > maxCell) {
      maxCell = cell[i];
      maxIndex = i;
    }
  }

  Serial.print("Min Cell      : Cell ");
  Serial.print(minIndex + 1);
  Serial.print(" = ");
  Serial.print(minCell, 3);
  Serial.println(" V");

  Serial.print("Max Cell      : Cell ");
  Serial.print(maxIndex + 1);
  Serial.print(" = ");
  Serial.print(maxCell, 3);
  Serial.println(" V");

  Serial.print("Cell Diff     : ");
  Serial.print((maxCell - minCell) * 1000.0, 0);
  Serial.println(" mV");

  Serial.println("============================");
}

void notifyCallback(
  NimBLERemoteCharacteristic* ch,
  uint8_t* data,
  size_t len,
  bool isNotify
) {
  parseLiveFrame(data, len);
}

bool connectToBMS() {
  Serial.println("Scanning...");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  NimBLEScanResults results = scan->getResults(15000);

  const NimBLEAdvertisedDevice* target = nullptr;

  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    String addr = dev->getAddress().toString().c_str();

    if (addr.equalsIgnoreCase(TARGET_ADDR)) {
      target = dev;
      Serial.println("BMS found.");
      break;
    }
  }

  if (target == nullptr) {
    Serial.println("BMS not found.");
    scan->clearResults();
    return false;
  }

  client = NimBLEDevice::createClient();

  Serial.println("Connecting...");
  if (!client->connect(target)) {
    Serial.println("Connect failed.");
    scan->clearResults();
    return false;
  }

  Serial.println("Connected.");

  NimBLERemoteService* service = client->getService("ffe0");
  if (!service) {
    Serial.println("Service FFE0 not found.");
    client->disconnect();
    return false;
  }

  dataChar = service->getCharacteristic("ffe1");
  if (!dataChar) {
    Serial.println("Characteristic FFE1 not found.");
    client->disconnect();
    return false;
  }

  if (!dataChar->subscribe(true, notifyCallback)) {
    Serial.println("Subscribe failed.");
    client->disconnect();
    return false;
  }

  Serial.println("Subscribed to BMS notify.");
  scan->clearResults();
  return true;
}

void sendRequest(uint8_t* frame, size_t len) {
  if (!client || !client->isConnected() || !dataChar) return;
  dataChar->writeValue(frame, len, true);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("JK BMS BLE Live Data Reader");

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  if (connectToBMS()) {
    delay(1000);

    sendRequest(requestSettings, sizeof(requestSettings));
    delay(2000);

    sendRequest(requestDeviceInfo, sizeof(requestDeviceInfo));
  }
}

void loop() {
  static unsigned long lastWake = 0;

  if (client && client->isConnected()) {
    if (millis() - lastWake > 30000) {
      lastWake = millis();

      sendRequest(requestSettings, sizeof(requestSettings));
      delay(500);
      sendRequest(requestDeviceInfo, sizeof(requestDeviceInfo));
    }
  } else {
    Serial.println("Disconnected. Restart ESP32.");
    delay(5000);
  }
}