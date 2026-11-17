// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 SvenPausH
// Dieses Programm ist freie Software: Sie können es unter den Bedingungen der GNU General Public License,
// Version 3, wie von der Free Software Foundation veröffentlicht, weitergeben und/oder ändern.
// Es wird ohne jede Gewährleistung bereitgestellt. Siehe Datei LICENSE.
// Pool-Dosiersteuerung: LilyGo T-Relay (ESP32) + W2839 (TuyaMCU mitlesen)
// WLAN-Setup per AP, Weboberfläche, Dosierlogik mit Sicherungen, MQTT, InfluxDB 1.8/2.x
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "web_ui.h"

// ===================== Hardware =====================
static const int  RELAY_PIN[2] = {21, 19};   // Pumpe 1 (pH-Minus), Pumpe 2 (Chlor)
static const int  UART_RX_PIN  = 26;         // vom W2839 (CB3S Pin 15)
static const int  FLOW_PIN     = 32;         // Durchflussschalter gegen GND
static const int  LED_PIN      = 25;         // rote LED auf der Platine
static const int  CONFIG_PIN   = 27;         // optional: beim Start gegen GND = Setup-AP

// ===================== Einstellungen =====================
struct Settings {
  // Dosierlogik
  bool  dosingEnabled = false;   // false = Testmodus
  int   flowDelayMin  = 10;
  bool  flowInvert    = false;
  float pulseVal      = 10.0f;   // Dosierdauer pro Impuls, Einheit siehe pulseMin
  bool  pulseMin      = false;   // false = Sekunden, true = Minuten
  int   mixMin        = 15;
  int   filterN       = 5;
  // pH (pH-Minus)
  bool  phOn = true;
  float phTarget = 7.2f, phStart = 0.2f, phStop = 0.05f;
  // Redox (Chlor)
  bool  orpOn = true;
  float orpTarget = 700.0f, orpStart = 30.0f, orpStop = 10.0f;
  // Pumpen / Behälter
  float mlPerMin[2] = {60.0f, 60.0f};
  float tankMl[2]   = {5000.0f, 5000.0f};
  // Sicherungen
  bool  sfFlow = true;
  bool  sfData = true;   int dataTimeoutMin = 3;
  bool  sfPlaus = true;
  bool  sfMaxDoses = true; int maxDoses = 6;
  bool  sfMaxRun = true;   int maxRunMin = 30;
  bool  sfOne = true;
  bool  sfTank = true;
  bool  sfPhMin = true;  float phAbsMin = 6.8f;
  // Uhrzeit / LED
  char  ntpServer[48] = "pool.ntp.org";
  char  tzString[48]  = "CET-1CEST,M3.5.0,M10.5.0/3";   // Europa/Berlin
  bool  ledOn = true;
  bool  ledInvert = false;
  // W2839 / Messwerte
  int   dpTemp = 8, dpPh = 106, dpOrp = 131;
  bool  debugFrames = false;
  // WLAN
  char  wifiSsid[33] = "";
  char  wifiPass[65] = "";
  bool  roamOn = true;
  int   roamRssi = -75;
  // Gerät / Zugang
  char  devName[24] = "pool-dosierung";
  bool  authEnabled = true;
  char  adminPass[32] = "admin";
  // MQTT
  bool  mqttOn = false;
  char  mqttHost[64] = "";
  int   mqttPort = 1883;
  char  mqttUser[32] = "";
  char  mqttPass[32] = "";
  char  mqttBase[48] = "pool";
  int   mqttInterval = 10;
  // InfluxDB
  bool  inflOn = false;
  int   inflVer = 2;
  char  inflUrl[96] = "http://192.168.1.10:8086";
  char  inflDb[32] = "pool";
  char  inflOrg[32] = "";
  char  inflBucket[32] = "pool";
  char  inflUser[32] = "";
  char  inflPass[48] = "";
  char  inflToken[128] = "";
  char  inflMeas[32] = "pool";
  int   inflInterval = 30;
} S;

struct Param { char t; const char *key; const char *label; void *ptr; size_t len; const char *hint; };
#define fH(l)          {'H', nullptr, l, nullptr, 0, nullptr}
#define fB(k,l,v,h)     {'b', k, l, &(v), 0, h}
#define fI(k,l,v,h)     {'i', k, l, &(v), 0, h}
#define fF(k,l,v,h)     {'f', k, l, &(v), 0, h}
#define fS(k,l,v,h)     {'s', k, l, (v), sizeof(v), h}
#define fP(k,l,v,h)     {'p', k, l, (v), sizeof(v), h}

static const Param PARAMS[] = {
  fH("Dosierlogik"),
  fB("dosEn",   "Dosierung aktiv", S.dosingEnabled, "Aus = Testmodus: es wird nur angezeigt, was dosiert würde."),
  fI("flowDly", "Dosierfreigabe nach Filterstart (min)", S.flowDelayMin, "Wartezeit, bis die Sonden aktuelle Werte liefern."),
  fB("flowInv", "Durchflussschalter invertieren", S.flowInvert, "Nur ändern, wenn Filter=läuft/steht vertauscht angezeigt wird."),
  fF("pulseV",  "Dosierdauer pro Impuls", S.pulseVal, "Pumpe läuft so lange, dann folgt die Mischzeit. Einheit: siehe nächste Zeile."),
  fB("pulseMin","Dosierdauer in Minuten angeben", S.pulseMin, "Aus = Sekunden (kleine Pools), an = Minuten (große Pools, langsame Pumpen)."),
  fI("mix",     "Mischzeit zwischen Impulsen (min)", S.mixMin, "Mindestpause pro Pumpe, damit sich der Wert einstellen kann."),
  fI("filtN",   "Mittelwert über N Messwerte", S.filterN, "Glättet schwankende Sonden. 1 = aus."),

  fH("pH-Regelung (pH-Minus, Pumpe 1)"),
  fB("phOn",    "pH-Regelung aktiv", S.phOn, ""),
  fF("phTgt",   "Zielwert pH", S.phTarget, ""),
  fF("phStart", "Start: Zielwert + Delta", S.phStart, "Dosieren beginnt, wenn pH über Zielwert + Delta steigt."),
  fF("phStop",  "Stopp: Zielwert − Delta", S.phStop, "Dosieren endet, sobald pH Zielwert − Delta erreicht."),

  fH("Redox-Regelung (Chlor, Pumpe 2)"),
  fB("orpOn",   "Redox-Regelung aktiv", S.orpOn, ""),
  fF("orpTgt",  "Zielwert Redox (mV)", S.orpTarget, ""),
  fF("orpStart","Start: Zielwert − Delta (mV)", S.orpStart, "Dosieren beginnt, wenn Redox unter Zielwert − Delta fällt."),
  fF("orpStop", "Stopp: Zielwert + Delta (mV)", S.orpStop, "Dosieren endet, sobald Redox Zielwert + Delta erreicht."),

  fH("Pumpen und Behälter"),
  fF("ml1",     "Pumpe 1: Förderleistung (ml/min)", S.mlPerMin[0], "Am besten mit Messbecher ausmessen (Test-Funktion unten)."),
  fF("ml2",     "Pumpe 2: Förderleistung (ml/min)", S.mlPerMin[1], ""),
  fF("tank1",   "Behälter 1: Volumen (ml)", S.tankMl[0], ""),
  fF("tank2",   "Behälter 2: Volumen (ml)", S.tankMl[1], ""),

  fH("Sicherungen"),
  fB("sfFlow",  "Dosieren nur bei Durchfluss", S.sfFlow, "Filterpumpe muss laufen. Dringend empfohlen."),
  fB("sfData",  "Sperre bei veralteten Messdaten", S.sfData, ""),
  fI("dataTmo", "… nach (min) ohne Messwerte", S.dataTimeoutMin, ""),
  fB("sfPlaus", "Sperre bei unplausiblen Messwerten", S.sfPlaus, "pH 3–10, Redox 0–1200 mV."),
  fB("sfMaxD",  "Max. Dosierungen pro Tag", S.sfMaxDoses, "Gilt je Pumpe."),
  fI("maxD",    "… Anzahl", S.maxDoses, ""),
  fB("sfMaxR",  "Max. Laufzeit pro Tag", S.sfMaxRun, "Gilt je Pumpe."),
  fI("maxR",    "… Minuten", S.maxRunMin, ""),
  fB("sfOne",   "Immer nur eine Pumpe gleichzeitig", S.sfOne, ""),
  fB("sfTank",  "Sperre bei leerem Behälter", S.sfTank, "Rechnerisch aus Laufzeit und Förderleistung."),
  fB("sfPhMin", "Keine pH-Minus-Dosierung unter pH-Grenzwert", S.sfPhMin, ""),
  fF("phAbs",   "… pH-Grenzwert", S.phAbsMin, ""),

  fH("Uhrzeit (NTP)"),
  fS("ntp",     "NTP-Server", S.ntpServer, "Hostname oder IP, z. B. der NTP-Server im eigenen Netz. Die per DHCP gemeldete Adresse (Option 42) wird nicht ausgewertet."),
  fS("tz",      "Zeitzone (POSIX-Format)", S.tzString, "Europa/Berlin: CET-1CEST,M3.5.0,M10.5.0/3"),

  fH("Status-LED"),
  fB("ledOn",   "Status-LED aktiv", S.ledOn, "Blitz = Messdaten kommen an, Doppelblitz = WLAN fehlt, schnell blinkend = keine Messdaten, Dauerlicht = Pumpe läuft, langsames Blinken = Setup-AP."),
  fB("ledInv",  "LED-Logik invertieren", S.ledInvert, "Nur ändern, wenn die LED genau andersherum leuchtet."),

  fH("Messwerte der W2839"),
  fI("dpTemp",  "Datapoint-ID Temperatur", S.dpTemp, "Standard 8 (Wert ÷ 10 = °C)."),
  fI("dpPh",    "Datapoint-ID pH", S.dpPh, "Standard 106 (Wert ÷ 100)."),
  fI("dpOrp",   "Datapoint-ID Redox", S.dpOrp, "Standard 131 (Wert = mV)."),
  fB("dbgFr",   "Debug: empfangene Daten im Serial Monitor", S.debugFrames, "Zeigt alle Frames und Datapoints, um die IDs anderer Geräte zu finden."),

  fH("WLAN"),
  fS("wSsid",   "WLAN-Name (SSID)", S.wifiSsid, "Nach dem Speichern startet das Gerät neu und verbindet sich mit diesem Netzwerk."),
  fP("wPass",   "WLAN-Passwort", S.wifiPass, "Leer lassen = unverändert."),
  fB("roamOn",  "Zum stärksten Access Point wechseln", S.roamOn, "Alle APs mit derselben SSID und demselben Passwort (nur 2,4 GHz)."),
  fI("roamRssi","… Wechsel prüfen, wenn Signal schlechter als (dBm)", S.roamRssi, "Typisch -75. Je kleiner der Wert, desto später wird gewechselt."),

  fH("Gerät und Zugang"),
  fS("dev",     "Gerätename", S.devName, "Auch Hostname und MQTT-Client-ID."),
  fB("auth",    "Passwortschutz für Einstellungen", S.authEnabled, "Benutzername: admin"),
  fP("apass",   "Passwort", S.adminPass, "Leer lassen = unverändert. Gilt auch für das WLAN-Update (OTA)."),

  fH("MQTT"),
  fB("mqOn",    "MQTT aktiv", S.mqttOn, ""),
  fS("mqHost",  "Server", S.mqttHost, ""),
  fI("mqPort",  "Port", S.mqttPort, ""),
  fS("mqUser",  "Benutzer", S.mqttUser, "Optional"),
  fP("mqPass",  "Passwort", S.mqttPass, "Leer lassen = unverändert."),
  fS("mqBase",  "Basis-Topic", S.mqttBase, "z. B. pool → pool/ph, pool/orp, pool/temperature …"),
  fI("mqInt",   "Intervall (s)", S.mqttInterval, ""),

  fH("InfluxDB"),
  fB("inOn",    "InfluxDB aktiv", S.inflOn, ""),
  fI("inVer",   "Version (1 = 1.8, 2 = 2.x)", S.inflVer, "1: Datenbank + Benutzer/Passwort. 2: Organisation + Bucket + Token."),
  fS("inUrl",   "URL", S.inflUrl, "z. B. http://192.168.1.10:8086"),
  fS("inDb",    "Datenbank (nur v1)", S.inflDb, ""),
  fS("inUser",  "Benutzer (nur v1)", S.inflUser, "Optional"),
  fP("inPass",  "Passwort (nur v1)", S.inflPass, "Leer lassen = unverändert."),
  fS("inOrg",   "Organisation (nur v2)", S.inflOrg, ""),
  fS("inBkt",   "Bucket (nur v2)", S.inflBucket, ""),
  fP("inTok",   "API-Token (nur v2)", S.inflToken, "Leer lassen = unverändert."),
  fS("inMeas",  "Measurement", S.inflMeas, ""),
  fI("inInt",   "Intervall (s)", S.inflInterval, ""),
};
static const size_t NPARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);

// ===================== Laufzeit-Zustand =====================
struct PumpState {
  bool on = false, manual = false, active = false;
  uint32_t startMs = 0, endMs = 0, manualMs = 0;
  float remMl = 0, totalRunSec = 0, dayRunSec = 0;
  uint16_t dayDoses = 0;
  char state[72] = "Start";
} P[2];

float temperature = NAN, ph = NAN, orp = NAN, phF = NAN, orpF = NAN;
uint32_t lastSensorMs = 0;
bool flowRaw = false, flowStable = false;
uint32_t flowChangeMs = 0, flowSinceMs = 0, dayStartMs = 0;
long dayId = 0;

Preferences cfg, st, net;
WebServer server(80);
DNSServer dns;
bool apMode = false, apFallback = false, gotIp = false, otaStarted = false, drdPending = false;
bool everConnected = false;
uint32_t lastWifiOkMs = 0;
uint32_t restartAtMs = 0;
String apOptions;
char mqttStatus[28] = "aus", inflStatus[28] = "aus";

// ===================== Hilfsfunktionen =====================
static void mmss(char *b, size_t n, long s) { if (s < 0) s = 0; snprintf(b, n, "%ld:%02ld", s / 60, s % 60); }

static String fstr(float v) {
  String s = String(v, 3);
  while (s.indexOf('.') >= 0 && (s.endsWith("0") || s.endsWith("."))) { bool dot = s.endsWith("."); s.remove(s.length() - 1); if (dot) break; }
  return s;
}
static String esc(const char *s) {
  String o;
  for (; *s; s++) { if (*s == '&') o += "&amp;"; else if (*s == '<') o += "&lt;"; else if (*s == '>') o += "&gt;"; else if (*s == '"') o += "&quot;"; else o += *s; }
  return o;
}
static String urlEnc(const char *s) {
  String o; char b[4];
  for (; *s; s++) {
    if (isalnum((unsigned char)*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') o += *s;
    else { snprintf(b, sizeof b, "%%%02X", (unsigned char)*s); o += b; }
  }
  return o;
}

static void applyTime() {
  configTzTime(S.tzString[0] ? S.tzString : "CET-1CEST,M3.5.0,M10.5.0/3", S.ntpServer[0] ? S.ntpServer : "pool.ntp.org");
}

static uint32_t pulseMs() { return (uint32_t)(S.pulseVal * (S.pulseMin ? 60000.0f : 1000.0f)); }

void sanitize() {
  S.flowDelayMin = constrain(S.flowDelayMin, 0, 240);
  { float lo = S.pulseMin ? 1.0f / 60.0f : 1.0f, hi = S.pulseMin ? 120.0f : 7200.0f;   // 1 s ... 2 h
    S.pulseVal = constrain(S.pulseVal, lo, hi); }
  S.dpTemp = constrain(S.dpTemp, 1, 255); S.dpPh = constrain(S.dpPh, 1, 255); S.dpOrp = constrain(S.dpOrp, 1, 255);
  S.mixMin = constrain(S.mixMin, 0, 480);
  S.filterN = constrain(S.filterN, 1, 60);
  S.dataTimeoutMin = constrain(S.dataTimeoutMin, 1, 120);
  S.maxDoses = constrain(S.maxDoses, 1, 200);
  S.maxRunMin = constrain(S.maxRunMin, 1, 1440);
  S.mqttPort = constrain(S.mqttPort, 1, 65535);
  S.mqttInterval = constrain(S.mqttInterval, 1, 3600);
  S.inflInterval = constrain(S.inflInterval, 1, 3600);
  S.inflVer = (S.inflVer == 1) ? 1 : 2;
  S.roamRssi = constrain(S.roamRssi, -95, -40);
  for (int i = 0; i < 2; i++) {
    if (S.mlPerMin[i] < 0.1f) S.mlPerMin[i] = 0.1f;
    if (S.tankMl[i] < 1) S.tankMl[i] = 1;
    if (P[i].remMl > S.tankMl[i]) P[i].remMl = S.tankMl[i];
  }
  if (!S.devName[0]) strlcpy(S.devName, "pool-dosierung", sizeof S.devName);
  if (!S.mqttBase[0]) strlcpy(S.mqttBase, "pool", sizeof S.mqttBase);
}

void loadSettings() {
  cfg.begin("cfg", false);
  for (size_t i = 0; i < NPARAMS; i++) {
    const Param &p = PARAMS[i];
    switch (p.t) {
      case 'b': *(bool *)p.ptr = cfg.getBool(p.key, *(bool *)p.ptr); break;
      case 'i': *(int *)p.ptr = cfg.getInt(p.key, *(int *)p.ptr); break;
      case 'f': *(float *)p.ptr = cfg.getFloat(p.key, *(float *)p.ptr); break;
      case 's': case 'p': { String v = cfg.getString(p.key, (const char *)p.ptr); strlcpy((char *)p.ptr, v.c_str(), p.len); break; }
    }
  }
}
void saveSettings() {
  for (size_t i = 0; i < NPARAMS; i++) {
    const Param &p = PARAMS[i];
    switch (p.t) {
      case 'b': cfg.putBool(p.key, *(bool *)p.ptr); break;
      case 'i': cfg.putInt(p.key, *(int *)p.ptr); break;
      case 'f': cfg.putFloat(p.key, *(float *)p.ptr); break;
      case 's': case 'p': cfg.putString(p.key, (const char *)p.ptr); break;
    }
  }
}

// ===================== Zähler / Behälter (dauerhaft gespeichert) =====================
void savePump(int i) {
  char k[12];
  snprintf(k, sizeof k, "rem%d", i);  st.putFloat(k, P[i].remMl);
  snprintf(k, sizeof k, "run%d", i);  st.putFloat(k, P[i].totalRunSec);
  snprintf(k, sizeof k, "dRun%d", i); st.putFloat(k, P[i].dayRunSec);
  snprintf(k, sizeof k, "dN%d", i);   st.putUShort(k, P[i].dayDoses);
}
void loadState() {
  st.begin("st", false);
  char k[12];
  for (int i = 0; i < 2; i++) {
    snprintf(k, sizeof k, "rem%d", i);  P[i].remMl = st.getFloat(k, S.tankMl[i]);
    snprintf(k, sizeof k, "run%d", i);  P[i].totalRunSec = st.getFloat(k, 0);
    snprintf(k, sizeof k, "dRun%d", i); P[i].dayRunSec = st.getFloat(k, 0);
    snprintf(k, sizeof k, "dN%d", i);   P[i].dayDoses = st.getUShort(k, 0);
  }
  dayId = st.getLong("day", 0);
}
long currentDayId() {
  time_t now = time(nullptr);
  if (now < 1700000000) return 0;               // Uhrzeit noch nicht per NTP gesetzt
  struct tm t; localtime_r(&now, &t);
  return (long)(t.tm_year + 1900) * 1000 + t.tm_yday + 1;
}
void resetDaily() {
  for (int i = 0; i < 2; i++) { P[i].dayRunSec = 0; P[i].dayDoses = 0; savePump(i); }
}
void dayCheck() {
  long id = currentDayId();
  if (id != 0) {
    if (id != dayId) { resetDaily(); dayId = id; st.putLong("day", id); }
  } else if (millis() - dayStartMs > 86400000UL) { resetDaily(); dayStartMs = millis(); }
}

// ===================== Pumpen =====================
void pumpOff(int i) {
  digitalWrite(RELAY_PIN[i], LOW);
  PumpState &p = P[i];
  if (!p.on) return;
  uint32_t el = millis() - p.startMs;
  p.on = false;
  if (!p.manual) p.endMs = millis();
  p.remMl -= S.mlPerMin[i] * el / 60000.0f;
  if (p.remMl < 0) p.remMl = 0;
  p.totalRunSec += el / 1000.0f;
  p.dayRunSec += el / 1000.0f;
  savePump(i);
  Serial.printf(">> Pumpe %d AUS (%.1f s)\n", i + 1, el / 1000.0f);
}
void pumpOn(int i, uint32_t manualMs) {
  PumpState &p = P[i];
  p.on = true; p.manual = manualMs > 0; p.manualMs = manualMs; p.startMs = millis();
  if (!p.manual) p.dayDoses++;
  savePump(i);
  digitalWrite(RELAY_PIN[i], HIGH);
  Serial.printf(">> Pumpe %d AN%s\n", i + 1, p.manual ? " (manuell)" : "");
}

// ===================== W2839 (TuyaMCU) mitlesen =====================
uint8_t buf[256]; size_t bufN = 0; uint32_t lastByteMs = 0;

static void ema(float &f, float v) { float a = 1.0f / max(1, S.filterN); f = isnan(f) ? v : f + a * (v - f); }

void decodeFrame(const uint8_t *f, size_t len) {
  if (len < 7 || f[0] != 0x55 || f[1] != 0xAA) return;
  uint16_t dlen = (f[4] << 8) | f[5];
  if (len != (size_t)(7 + dlen)) return;
  uint8_t sum = 0;
  for (size_t i = 0; i < len - 1; i++) sum += f[i];
  if (sum != f[len - 1] || f[3] != 0x07) return;
  size_t i = 6;
  while (i + 4 <= 6 + dlen) {
    uint8_t id = f[i], type = f[i + 1];
    uint16_t l = (f[i + 2] << 8) | f[i + 3];
    i += 4;
    if (i + l > 6 + dlen) break;
    if (type == 2 && l == 4) {
      int32_t v = (int32_t)(((uint32_t)f[i] << 24) | (f[i + 1] << 16) | (f[i + 2] << 8) | f[i + 3]);
      if (id == S.dpTemp) temperature = v / 10.0f;
      if (id == S.dpPh)  { ph = v / 100.0f; ema(phF, ph);   lastSensorMs = millis(); }
      if (id == S.dpOrp) { orp = (float)v;  ema(orpF, orp); lastSensorMs = millis(); }
      if (S.debugFrames) Serial.printf("DP %u  Wert %ld\n", id, (long)v);
    }
    i += l;
  }
}
void readUart() {
  while (Serial2.available() && bufN < sizeof(buf)) { buf[bufN++] = Serial2.read(); lastByteMs = millis(); }
  if (bufN > 0 && millis() - lastByteMs > 30) {
    if (S.debugFrames) { for (size_t i = 0; i < bufN; i++) Serial.printf("%02X ", buf[i]); Serial.println(); }
    decodeFrame(buf, bufN); bufN = 0;
  }
}

// ===================== Dosierlogik =====================
void controlStep() {
  uint32_t now = millis();

  // Durchflussschalter: AUS sofort, EIN erst nach 1,5 s stabil
  bool raw = ((digitalRead(FLOW_PIN) == LOW) != S.flowInvert);
  if (raw != flowRaw) { flowRaw = raw; flowChangeMs = now; }
  if (!flowRaw) flowStable = false;
  else if (!flowStable && now - flowChangeMs > 1500) { flowStable = true; flowSinceMs = now; }

  dayCheck();

  bool haveVals  = !isnan(phF) && !isnan(orpF);
  bool fresh     = lastSensorMs != 0 && (now - lastSensorMs < (uint32_t)S.dataTimeoutMin * 60000UL);
  bool dataFresh = !S.sfData || fresh;
  bool plaus     = !S.sfPlaus || (ph > 3 && ph < 10 && orp > 0 && orp < 1200);
  bool dataOk    = haveVals && dataFresh && plaus;
  bool flowOk    = !S.sfFlow || flowStable;
  bool delayOk   = true;
  if (S.sfFlow) delayOk = flowStable && (now - flowSinceMs >= (uint32_t)S.flowDelayMin * 60000UL);

  // laufende Pumpen prüfen
  for (int i = 0; i < 2; i++) {
    PumpState &p = P[i];
    if (!p.on) continue;
    uint32_t el = now - p.startMs;
    if (p.manual) { if (el >= p.manualMs) pumpOff(i); }
    else if (!S.dosingEnabled || !flowOk || !dataOk || el >= pulseMs()) pumpOff(i);
  }

  // Hysterese
  if (haveVals) {
    if (S.phOn)  { if (phF > S.phTarget + S.phStart) P[0].active = true; else if (phF <= S.phTarget - S.phStop) P[0].active = false; }
    else P[0].active = false;
    if (S.orpOn) { if (orpF < S.orpTarget - S.orpStart) P[1].active = true; else if (orpF >= S.orpTarget + S.orpStop) P[1].active = false; }
    else P[1].active = false;
  }

  // Start-Entscheidung (Pumpe 1 = pH hat Vorrang)
  uint32_t mixMs = (uint32_t)S.mixMin * 60000UL;
  for (int i = 0; i < 2; i++) {
    PumpState &p = P[i];
    if (p.on) { snprintf(p.state, sizeof p.state, "%s", p.manual ? "Manuell an" : "Dosiert gerade"); continue; }
    const char *why = nullptr; char tmp[72];
    bool ctl = (i == 0) ? S.phOn : S.orpOn;
    if (!ctl) why = "Regelung aus";
    else if (!haveVals) why = "Warte auf Messwerte";
    else if (!dataFresh) why = "Gesperrt: Messdaten veraltet";
    else if (!plaus) why = "Gesperrt: Messwert unplausibel";
    else if (!flowOk) why = "Gesperrt: kein Durchfluss";
    else if (!delayOk) {
      char m[12]; long left = flowStable ? (long)S.flowDelayMin * 60 - (long)((now - flowSinceMs) / 1000) : (long)S.flowDelayMin * 60;
      mmss(m, sizeof m, left); snprintf(tmp, sizeof tmp, "Wartet auf Sonden (%s)", m); why = tmp;
    }
    else if (S.sfTank && p.remMl <= 0) why = "Gesperrt: Behälter leer";
    else if (S.sfMaxDoses && p.dayDoses >= S.maxDoses) why = "Gesperrt: Tageslimit Dosierungen";
    else if (S.sfMaxRun && p.dayRunSec >= S.maxRunMin * 60.0f) why = "Gesperrt: Tageslimit Laufzeit";
    else if (i == 0 && S.sfPhMin && ph < S.phAbsMin) why = "Gesperrt: pH unter Grenzwert";
    else if (!p.active) why = "Im Zielbereich";
    else if (p.endMs != 0 && now - p.endMs < mixMs) {
      char m[12]; mmss(m, sizeof m, (long)((mixMs - (now - p.endMs)) / 1000));
      snprintf(tmp, sizeof tmp, "Mischzeit (%s)", m); why = tmp;
    }
    else if (S.sfOne && (P[0].on || P[1].on)) why = "Wartet (andere Pumpe läuft)";
    if (why) { snprintf(p.state, sizeof p.state, "%s", why); continue; }
    if (!S.dosingEnabled) { snprintf(p.state, sizeof p.state, "Testmodus: würde dosieren"); continue; }
    pumpOn(i, 0);
    snprintf(p.state, sizeof p.state, "Dosiert gerade");
  }
}

// ===================== Status-LED =====================
// Normal: kurzer Blitz bei jedem Messwert der W2839, Doppelblitz wenn das WLAN fehlt.
void ledStep() {
  bool on = false;
  uint32_t now = millis();
  if (!S.ledOn) on = false;
  else if (apMode) on = ((now / 1000) % 2) == 0;                          // Setup-AP: langsames Blinken
  else if (P[0].on || P[1].on) on = true;                                 // Pumpe läuft: Dauerlicht
  else if (lastSensorMs == 0 || now - lastSensorMs > 60000UL) on = ((now / 200) % 2) == 0;   // keine Messdaten: schnell blinken
  else {
    uint32_t since = now - lastSensorMs;
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    on = since < 100 || (!wifiOk && since >= 250 && since < 350);
  }
  digitalWrite(LED_PIN, on != S.ledInvert ? HIGH : LOW);
}

// ===================== Status-JSON =====================
static void jnum(String &j, const char *k, float v, int d) {
  j += '"'; j += k; j += "\":";
  if (isnan(v)) j += "null"; else j += String(v, d);
  j += ',';
}
String statusJson() {
  uint32_t now = millis();
  String j; j.reserve(1200);
  j += '{';
  jnum(j, "temp", temperature, 1); jnum(j, "ph", ph, 2); jnum(j, "orp", orp, 0);
  jnum(j, "phF", phF, 2); jnum(j, "orpF", orpF, 0);
  j += "\"flow\":"; j += flowStable ? 1 : 0; j += ',';
  long left = 0;
  if (flowStable && S.sfFlow) { left = (long)S.flowDelayMin * 60 - (long)((now - flowSinceMs) / 1000); if (left < 0) left = 0; }
  jnum(j, "delayLeft", left, 0);
  jnum(j, "dataAge", lastSensorMs ? (now - lastSensorMs) / 1000.0f : NAN, 0);
  j += "\"dosing\":"; j += S.dosingEnabled ? 1 : 0; j += ',';
  j += "\"p\":[";
  for (int i = 0; i < 2; i++) {
    PumpState &p = P[i];
    j += '{';
    j += "\"on\":"; j += p.on ? 1 : 0; j += ',';
    j += "\"state\":\""; j += p.state; j += "\",";
    float elapsed = p.on ? (now - p.startMs) / 1000.0f : 0.0f;          // laufende Dosierung live mitzählen
    float rem = p.remMl - S.mlPerMin[i] * elapsed / 60.0f; if (rem < 0) rem = 0;
    jnum(j, "tankPct", 100.0f * rem / S.tankMl[i], 1);
    jnum(j, "tankMl", rem, 0);
    jnum(j, "runSec", p.totalRunSec + elapsed, 0);
    jnum(j, "dayRunSec", p.dayRunSec + elapsed, 0);
    jnum(j, "mlPulse", S.mlPerMin[i] * pulseMs() / 60000.0f, 1);
    j += "\"dayDoses\":"; j += p.dayDoses;
    j += '}';
    if (i == 0) j += ',';
  }
  j += "],";
  {
    time_t t = time(nullptr); char tb[24] = "";
    if (t > 1700000000) { struct tm lt; localtime_r(&t, &lt); strftime(tb, sizeof tb, "%d.%m.%Y %H:%M:%S", &lt); }
    j += "\"time\":\""; j += tb; j += "\",";
    j += "\"heap\":"; j += ESP.getFreeHeap(); j += ',';
  }
  j += "\"mode\":\""; j += apMode ? "ap" : "sta"; j += "\",";
  j += "\"ip\":\""; j += (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString(); j += "\",";
  j += "\"rssi\":"; j += WiFi.RSSI(); j += ',';
  j += "\"uptime\":"; j += now / 1000; j += ',';
  j += "\"mqtt\":\""; j += mqttStatus; j += "\",";
  j += "\"influx\":\""; j += inflStatus; j += "\"}";
  return j;
}

// ===================== MQTT + InfluxDB (eigener Task, blockiert die Regelung nicht) =====================
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);

void mqttPub(const char *name, const String &v) {
  char t[112]; snprintf(t, sizeof t, "%s/%s", S.mqttBase, name);
  mqtt.publish(t, v.c_str());
}
void mqttPublishAll() {
  mqttPub("temperature", isnan(temperature) ? String("") : String(temperature, 1));
  mqttPub("ph", isnan(ph) ? String("") : String(ph, 2));
  mqttPub("orp", isnan(orp) ? String("") : String(orp, 0));
  mqttPub("flow", String(flowStable ? 1 : 0));
  for (int i = 0; i < 2; i++) {
    String n = String("pump") + (i + 1);
    mqttPub((n + "/relay").c_str(), String(P[i].on ? 1 : 0));
    mqttPub((n + "/tank_percent").c_str(), String(100.0f * P[i].remMl / S.tankMl[i], 1));
    mqttPub((n + "/runtime_s").c_str(), String(P[i].totalRunSec, 0));
    mqttPub((n + "/state").c_str(), String(P[i].state));
  }
  mqttPub("state", statusJson());
}
void influxSend() {
  if (!S.inflUrl[0]) { strlcpy(inflStatus, "keine URL", sizeof inflStatus); return; }
  String line; line.reserve(400);
  line += S.inflMeas; line += ",device="; line += S.devName; line += ' ';
  bool first = true;
  auto addF = [&](const char *k, float v, int d) { if (isnan(v)) return; if (!first) line += ','; first = false; line += k; line += '='; line += String(v, d); };
  auto addI = [&](const char *k, long v) { if (!first) line += ','; first = false; line += k; line += '='; line += v; line += 'i'; };
  addF("temperature", temperature, 1); addF("ph", ph, 2); addF("orp", orp, 0);
  addI("flow", flowStable ? 1 : 0);
  addI("relay1", P[0].on ? 1 : 0); addI("relay2", P[1].on ? 1 : 0);
  addF("tank1_percent", 100.0f * P[0].remMl / S.tankMl[0], 1);
  addF("tank2_percent", 100.0f * P[1].remMl / S.tankMl[1], 1);
  addF("pump1_runtime_s", P[0].totalRunSec, 0); addF("pump2_runtime_s", P[1].totalRunSec, 0);

  String url = S.inflUrl;
  while (url.endsWith("/")) url.remove(url.length() - 1);
  if (S.inflVer == 1) {
    url += "/write?db=" + urlEnc(S.inflDb);
    if (S.inflUser[0]) url += "&u=" + urlEnc(S.inflUser) + "&p=" + urlEnc(S.inflPass);
  } else {
    url += "/api/v2/write?org=" + urlEnc(S.inflOrg) + "&bucket=" + urlEnc(S.inflBucket);
  }
  HTTPClient http; WiFiClient plain; WiFiClientSecure sec;
  bool https = url.startsWith("https");
  if (https) { sec.setInsecure(); http.begin(sec, url); } else http.begin(plain, url);   // HTTPS ohne Zertifikatsprüfung
  http.setTimeout(3000);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  if (S.inflVer == 2 && S.inflToken[0]) http.addHeader("Authorization", String("Token ") + S.inflToken);
  int code = http.POST(line);
  http.end();
  if (code == 204 || code == 200) strlcpy(inflStatus, "OK", sizeof inflStatus);
  else snprintf(inflStatus, sizeof inflStatus, "Fehler (%d)", code);
}
void netTask(void *) {
  uint32_t lastPub = 0, lastInfl = 0, lastConn = 0;
  mqtt.setBufferSize(1536);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (apMode || WiFi.status() != WL_CONNECTED) { if (mqtt.connected()) mqtt.disconnect(); continue; }
    uint32_t now = millis();
    // MQTT
    if (S.mqttOn && S.mqttHost[0]) {
      if (!mqtt.connected()) {
        if (now - lastConn > 10000 || lastConn == 0) {
          lastConn = now;
          mqtt.setServer(S.mqttHost, S.mqttPort);
          char will[112]; snprintf(will, sizeof will, "%s/status", S.mqttBase);
          bool ok = S.mqttUser[0] ? mqtt.connect(S.devName, S.mqttUser, S.mqttPass, will, 0, true, "offline")
                                  : mqtt.connect(S.devName, nullptr, nullptr, will, 0, true, "offline");
          if (ok) { mqtt.publish(will, "online", true); strlcpy(mqttStatus, "verbunden", sizeof mqttStatus); }
          else snprintf(mqttStatus, sizeof mqttStatus, "Fehler (%d)", mqtt.state());
        }
      } else {
        mqtt.loop();
        if (now - lastPub >= (uint32_t)S.mqttInterval * 1000UL) { lastPub = now; mqttPublishAll(); }
      }
    } else { if (mqtt.connected()) mqtt.disconnect(); strlcpy(mqttStatus, "aus", sizeof mqttStatus); }
    // InfluxDB
    if (S.inflOn) {
      if (now - lastInfl >= (uint32_t)S.inflInterval * 1000UL) { lastInfl = now; influxSend(); }
    } else strlcpy(inflStatus, "aus", sizeof inflStatus);
  }
}

// ===================== WLAN / Setup-AP =====================
void scanNetworks() {
  int n = WiFi.scanNetworks();
  apOptions = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    apOptions += "<option value=\"" + esc(s.c_str()) + "\">" + esc(s.c_str()) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
}
void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP_STA);
  char ssid[40]; snprintf(ssid, sizeof ssid, "Pool-Dosierung-%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
  WiFi.softAP(ssid);
  dns.start(53, "*", WiFi.softAPIP());
  scanNetworks();
  Serial.printf("Setup-AP: %s, IP %s\n", ssid, WiFi.softAPIP().toString().c_str());
}
void wifiBegin() {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);          // alle Kanäle prüfen ...
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);      // ... und den stärksten AP mit dieser SSID nehmen
  WiFi.begin(S.wifiSsid, S.wifiPass);
}
void startSTA() {
  WiFi.setHostname(S.devName);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiBegin();
  Serial.printf("Verbinde mit '%s' ...\n", S.wifiSsid);
}
void stopAP() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apMode = false; apFallback = false;
  Serial.println("Setup-AP beendet");
}
// Verbindung überwachen: neu verbinden, bei dauerhaftem Fehlschlag Setup-AP als Notausgang
void wifiWatch() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOkMs = now; everConnected = true;
    if (apFallback) stopAP();
    return;
  }
  if (apMode && !apFallback) return;                  // reiner Setup-Modus ohne WLAN-Daten
  static uint32_t lastRetry = 0;
  if (now - lastWifiOkMs > 60000UL && now - lastRetry > 60000UL) {
    lastRetry = now;
    Serial.println("WLAN getrennt, neuer Verbindungsversuch");
    WiFi.disconnect(false);
    wifiBegin();
  }
  if (!apMode && !everConnected && now > 300000UL) { apFallback = true; startAP(); }   // falsche Zugangsdaten?
}
// Roaming: bei schwachem Signal nach einem deutlich besseren AP derselben SSID suchen
void roamStep() {
  static uint32_t tLast = 0;
  static bool scanning = false;
  if (apMode || !S.roamOn || WiFi.status() != WL_CONNECTED) { if (scanning) { WiFi.scanDelete(); scanning = false; } return; }
  uint32_t now = millis();
  if (!scanning) {
    if (now - tLast > 60000UL && WiFi.RSSI() < S.roamRssi) { tLast = now; WiFi.scanNetworks(true); scanning = true; }
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanning = false;
  if (n <= 0) { WiFi.scanDelete(); return; }
  int cur = WiFi.RSSI(), bestRssi = cur + 8;           // mindestens 8 dB besser
  bool better = false;
  uint8_t *curBssid = WiFi.BSSID();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != String(S.wifiSsid)) continue;
    if (curBssid && memcmp(WiFi.BSSID(i), curBssid, 6) == 0) continue;
    if (WiFi.RSSI(i) > bestRssi) { bestRssi = WiFi.RSSI(i); better = true; }
  }
  WiFi.scanDelete();
  if (better) {
    Serial.printf("Roaming: aktuell %d dBm, besserer AP mit %d dBm gefunden\n", cur, bestRssi);
    WiFi.disconnect(false);
    wifiBegin();                                        // wählt den stärksten AP
  }
}

// ===================== Webserver =====================
bool needAuth() {
  if (S.authEnabled && S.adminPass[0] && !server.authenticate("admin", S.adminPass)) { server.requestAuthentication(); return true; }
  return false;
}
void beginChunked() { server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "text/html", ""); }

void apPage() {
  beginChunked();
  server.sendContent_P(HTML_HEAD);
  String o = "<header><h1>WLAN einrichten</h1></header><main><div class=\"card\"><form method=\"post\" action=\"/wifi\">";
  o += "<div class=\"row\"><span>Netzwerk</span><select name=\"ssid\" style=\"width:60%\">" + apOptions + "</select></div>";
  o += "<div class=\"row\"><span>oder SSID manuell</span><input type=\"text\" name=\"ssidm\"></div>";
  o += "<div class=\"row\"><span>Passwort</span><input type=\"password\" name=\"pass\"></div>";
  o += "<div class=\"save\"><button type=\"submit\">Speichern und verbinden</button> <a href=\"/scan\">Neu suchen</a></div></form></div></main></body></html>";
  server.sendContent(o);
  server.sendContent("");
}
void handleRoot() {
  if (apMode) { apPage(); return; }
  beginChunked();
  server.sendContent_P(HTML_HEAD);
  server.sendContent_P(HTML_MAIN);
  server.sendContent("");
}
void handleStatus() { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", statusJson()); }

void renderField(const Param &p, String &o) {
  if (p.t == 'H') { o += "<h2>"; o += p.label; o += "</h2>"; return; }
  o += "<label class=\"row\"><span>"; o += p.label;
  if (p.hint && p.hint[0]) { o += "<br><span class=\"hint\">"; o += p.hint; o += "</span>"; }
  o += "</span>";
  switch (p.t) {
    case 'b': o += "<input type=\"checkbox\" name=\""; o += p.key; o += "\""; if (*(bool *)p.ptr) o += " checked"; o += ">"; break;
    case 'i': o += "<input type=\"number\" step=\"1\" name=\""; o += p.key; o += "\" value=\""; o += String(*(int *)p.ptr); o += "\">"; break;
    case 'f': o += "<input type=\"number\" step=\"any\" name=\""; o += p.key; o += "\" value=\""; o += fstr(*(float *)p.ptr); o += "\">"; break;
    case 's': o += "<input type=\"text\" maxlength=\""; o += String((int)p.len - 1); o += "\" name=\""; o += p.key; o += "\" value=\""; o += esc((const char *)p.ptr); o += "\">"; break;
    case 'p': o += "<input type=\"password\" maxlength=\""; o += String((int)p.len - 1); o += "\" name=\""; o += p.key; o += "\" placeholder=\"unverändert\">"; break;
  }
  o += "</label>";
}
void handleSettings() {
  if (apMode) { server.sendHeader("Location", "/"); server.send(302); return; }
  if (needAuth()) return;
  beginChunked();
  server.sendContent_P(HTML_HEAD);
  server.sendContent_P(HTML_SET_TOP);
  for (size_t i = 0; i < NPARAMS; i++) { String o; renderField(PARAMS[i], o); server.sendContent(o); }
  server.sendContent_P(HTML_SET_BOTTOM);
  server.sendContent("");
}
void handleSave() {
  if (needAuth()) return;
  char oldSsid[sizeof S.wifiSsid], oldPass[sizeof S.wifiPass];
  strlcpy(oldSsid, S.wifiSsid, sizeof oldSsid); strlcpy(oldPass, S.wifiPass, sizeof oldPass);
  for (size_t i = 0; i < NPARAMS; i++) {
    const Param &p = PARAMS[i];
    switch (p.t) {
      case 'b': *(bool *)p.ptr = server.hasArg(p.key); break;
      case 'i': if (server.hasArg(p.key)) *(int *)p.ptr = server.arg(p.key).toInt(); break;
      case 'f': if (server.hasArg(p.key)) { String v = server.arg(p.key); v.replace(',', '.'); *(float *)p.ptr = v.toFloat(); } break;
      case 's': if (server.hasArg(p.key)) strlcpy((char *)p.ptr, server.arg(p.key).c_str(), p.len); break;
      case 'p': if (server.hasArg(p.key) && server.arg(p.key).length() > 0) strlcpy((char *)p.ptr, server.arg(p.key).c_str(), p.len); break;
    }
  }
  sanitize();
  saveSettings();
  applyTime();
  for (int i = 0; i < 2; i++) savePump(i);
  if (strcmp(oldSsid, S.wifiSsid) != 0 || strcmp(oldPass, S.wifiPass) != 0) restartAtMs = millis() + 2000;   // neue WLAN-Daten -> Neustart
  server.sendHeader("Location", "/settings?saved=1");
  server.send(303);
}
void handleAction() {
  if (needAuth()) return;
  String a = server.arg("a");
  int i = constrain(server.arg("i").toInt(), 0, 1);
  float v = server.arg("v").toFloat();
  if (a == "refill") { P[i].remMl = S.tankMl[i]; savePump(i); }
  else if (a == "resetrun") { P[i].totalRunSec = 0; savePump(i); }
  else if (a == "resetday") { P[i].dayRunSec = 0; P[i].dayDoses = 0; savePump(i); }
  else if (a == "prime") { if (!P[i].on) pumpOn(i, (uint32_t)constrain(v, 1.0f, 120.0f) * 1000UL); }
  else if (a == "stop") { pumpOff(0); pumpOff(1); }
  else if (a == "reboot") { net.putBool("drd", false); restartAtMs = millis() + 1000; }
  else if (a == "wifireset") {
    S.wifiSsid[0] = 0; S.wifiPass[0] = 0;
    cfg.putString("wSsid", ""); cfg.putString("wPass", "");
    net.remove("ssid"); net.remove("pass"); net.putBool("drd", false); restartAtMs = millis() + 1000;
  }
  server.send(200, "text/plain", "ok");
}
void handleWifi() {
  String ssid = server.arg("ssidm"); if (ssid.length() == 0) ssid = server.arg("ssid");
  if (ssid.length() == 0) { server.sendHeader("Location", "/"); server.send(302); return; }
  strlcpy(S.wifiSsid, ssid.c_str(), sizeof S.wifiSsid);
  strlcpy(S.wifiPass, server.arg("pass").c_str(), sizeof S.wifiPass);
  cfg.putString("wSsid", S.wifiSsid);
  cfg.putString("wPass", S.wifiPass);
  net.putBool("drd", false);
  server.send(200, "text/html; charset=utf-8", "<meta name=viewport content='width=device-width,initial-scale=1'><body style='font-family:sans-serif;padding:20px'><h2>Gespeichert</h2><p>Das Gerät startet neu und verbindet sich mit dem WLAN. Danach ist die Oberfläche unter der neuen IP-Adresse (siehe Router) oder unter <b>http://" + String(S.devName) + ".local</b> erreichbar.</p>");
  restartAtMs = millis() + 1500;
}
void handleNotFound() {
  if (apMode) { server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/"); server.send(302); }
  else server.send(404, "text/plain", "Nicht gefunden");
}

// ===================== setup / loop =====================
void onWifiEvent(WiFiEvent_t e, WiFiEventInfo_t info) { if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true; }

void setup() {
  for (int i = 0; i < 2; i++) { pinMode(RELAY_PIN[i], OUTPUT); digitalWrite(RELAY_PIN[i], LOW); }
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  loadSettings();
  loadState();
  sanitize();
  dayStartMs = millis();
  Serial2.setRxBufferSize(1024);
  Serial2.begin(9600, SERIAL_8N1, UART_RX_PIN, -1);

  // Setup-Modus: Reset-Taste innerhalb von 4 s nach dem Einschalten (bzw. 2x kurz hintereinander)
  net.begin("net", false);
  bool forcePortal = net.getBool("drd", false);
  net.putBool("drd", true);
  drdPending = true;
  delay(50);
  if (digitalRead(CONFIG_PIN) == LOW) forcePortal = true;

  if (!S.wifiSsid[0]) {                                   // Übernahme aus früherer Version
    String o = net.getString("ssid", "");
    if (o.length() > 0) {
      strlcpy(S.wifiSsid, o.c_str(), sizeof S.wifiSsid);
      strlcpy(S.wifiPass, net.getString("pass", "").c_str(), sizeof S.wifiPass);
      cfg.putString("wSsid", S.wifiSsid); cfg.putString("wPass", S.wifiPass);
    }
  }
  if (!S.wifiSsid[0] || forcePortal) startAP();
  else startSTA();

  WiFi.onEvent(onWifiEvent);
  applyTime();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, []() { if (apMode) scanNetworks(); server.sendHeader("Location", "/"); server.send(302); });
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/action", HTTP_POST, handleAction);
  server.onNotFound(handleNotFound);
  server.begin();

  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
  Serial.println("Dosiersteuerung gestartet");
}

void loop() {
  readUart();
  static uint32_t tCtl = 0;
  if (millis() - tCtl >= 250) { tCtl = millis(); controlStep(); }
  if (apMode) dns.processNextRequest();
  server.handleClient();
  static uint32_t tLed = 0;
  if (millis() - tLed >= 20) { tLed = millis(); ledStep(); }
  static uint32_t tWifi = 0;
  if (millis() - tWifi >= 1000) { tWifi = millis(); wifiWatch(); roamStep(); }
  if (otaStarted) ArduinoOTA.handle();

  if (gotIp) {
    gotIp = false;
    Serial.printf("WLAN verbunden, IP %s\n", WiFi.localIP().toString().c_str());
    MDNS.begin(S.devName);
    if (!otaStarted) {
      ArduinoOTA.setHostname(S.devName);
      if (S.authEnabled && S.adminPass[0]) ArduinoOTA.setPassword(S.adminPass);
      ArduinoOTA.onStart([]() { pumpOff(0); pumpOff(1); });
      ArduinoOTA.begin();
      otaStarted = true;
    }
  }
  if (drdPending && millis() > 4000) { net.putBool("drd", false); drdPending = false; }
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) { pumpOff(0); pumpOff(1); ESP.restart(); }

  static uint32_t tPrint = 0;
  if (millis() - tPrint > 10000) {
    tPrint = millis();
    Serial.printf("T=%.1f pH=%.2f ORP=%.0f Filter=%s | P1: %s | P2: %s\n", temperature, ph, orp, flowStable ? "laeuft" : "steht", P[0].state, P[1].state);
  }
}
