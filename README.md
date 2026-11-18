# Pool-Dosiersteuerung mit ESP32

Automatische Dosierung von **pH-Minus** und **Chlor** für den Pool. Ein ESP32 (LilyGo T-Relay) liest die Messwerte
eines **Tuya-Messgeräts W2839** (pH, Redox, Temperatur) mit, schaltet bis zu zwei **Dosierpumpen** und überwacht
dabei den **Durchflussschalter** der Filteranlage. Alles ist über eine Weboberfläche einstellbar, die Daten gehen
per **MQTT** und **InfluxDB** (1.8 und 2.x) an dein Monitoring.

> **Sicherheitshinweis:** Das Projekt schaltet Netzspannung (230 V) und dosiert Chemikalien in dein Badewasser.
> Aufbau nur mit Fachkenntnis, die Netzseite im berührungssicheren Gehäuse und abgesichert. Im Zweifel eine
> Elektrofachkraft hinzuziehen. Die Software wird ohne Gewähr bereitgestellt, die Nutzung erfolgt auf eigene
> Verantwortung. Kontrolliere Wasserwerte und Dosiermengen regelmäßig selbst.

## Funktionen

- Messwerte der W2839 **passiv mitlesen** (TuyaMCU-Protokoll), die Tuya-Cloud-Anbindung des Geräts bleibt erhalten
- Regelung für **pH (pH-Minus)** und **Redox (Chlor)** mit je zwei Delta-Werten (Hysterese) und Glättung der Messwerte
- **Impulsbetrieb** mit einstellbarer Dosierdauer (**Sekunden oder Minuten**) und Mischzeit
- Freigabe erst, wenn der **Durchflussschalter** meldet, dass die Filterpumpe läuft, plus einstellbare Wartezeit
- Einzeln abschaltbare **Sicherungen** (Tageslimits, Datenüberwachung, Behälter leer, …)
- **Laufzeitzähler** und berechneter **Behälterfüllstand** je Pumpe, dauerhaft gespeichert
- Weboberfläche mit Live-Werten, Einstellungen mit Passwortschutz, Wartungsfunktionen
- **WLAN-Einrichtung** über Setup-AP, Roaming zwischen Access Points, Update per WLAN (OTA)
- **MQTT** und **InfluxDB 1.8 / 2.x**
- **Status-LED** auf der Platine und **NTP-Zeit** mit konfigurierbarem Server und Zeitzone

## Hardware

| Teil | Hinweis |
|---|---|
| LilyGo **T-Relay 4 Channel (ESP32)** | 4 Relais, Versorgung 12–24 V DC. Genutzt werden 2 Relais |
| **Tuya W2839** | pH/Redox/Temperatur-Messgerät mit CB3S-Funkmodul |
| 2 Dosierpumpen | Getestet mit SEKO-Schlauchpumpen (0,6–4 l/h, 230 V, ca. 3,5 W) |
| Durchflussschalter | Potentialfreier Kontakt (Schließer: geschlossen bei Durchfluss) |
| Netzteil 12–24 V DC | z. B. 19-V-Notebooknetzteil |
| USB-Seriell-Adapter | z. B. LilyGo T-U2T, nur zum ersten Flashen |

### Pinbelegung (T-Relay ESP32)

| Funktion | Pin | Bemerkung |
|---|---|---|
| Relais 1 (Pumpe 1, pH-Minus) | IO21 | auf der Platine |
| Relais 2 (Pumpe 2, Chlor) | IO19 | auf der Platine |
| Datenleitung von der W2839 | IO26 | nur Empfang (RX) |
| Durchflussschalter | IO32 | gegen GND, interner Pull-up |
| Setup-Taster (optional) | IO27 | beim Start gegen GND = Setup-AP |

Pins ändern: Konstanten am Anfang von `src/main.cpp` (`RELAY_PIN`, `UART_RX_PIN`, `FLOW_PIN`, `CONFIG_PIN`).

### Verdrahtung

```
 W2839 (CB3S Pin 15 = RX1/P10) ──────────► IO26   (ESP32 empfängt nur, TX bleibt frei)
 W2839 GND ──────────────────────────────► GND
 Durchflussschalter ── Kontakt 1 ────────► IO32
                    └─ Kontakt 2 ────────► GND

 230 V:  L ──[Sicherung T 0,5–1 A]── COM (Relais 1) ── NO ── Pumpe 1 ── N
         L ──[Sicherung T 0,5–1 A]── COM (Relais 2) ── NO ── Pumpe 2 ── N
         PE an die Pumpen (Schutzleiter durchschleifen)
```

Hinweise:

- Die Relais schalten die **Phase (L)**. N und PE gehen direkt zur Pumpe.
- Netzseite im **berührungssicheren Gehäuse** mit Zugentlastung, Niederspannung (Sensorleitungen) räumlich getrennt führen.
- Beim Löten an die W2839 muss das Gerät **spannungsfrei** sein. Das Mitlesen verändert nichts am Gerät.
- Der Durchflussschalter muss für Kleinspannung geeignet sein. Ein Schalter, der Netzspannung führt, gehört nicht an einen GPIO.

## Installation

1. [VS Code](https://code.visualstudio.com/) und die Erweiterung **PlatformIO IDE** installieren.
2. Projektordner öffnen. Die Bibliothek PubSubClient wird automatisch geladen.
3. T-U2T-Adapter anschließen (TX↔RX gekreuzt, GND, **IO0 gegen GND** beim Einschalten für den Download-Modus), dann **Upload** in PlatformIO.
4. Nach dem ersten Flashen sind Updates per WLAN möglich (PlatformIO: `upload_protocol = espota`, `upload_port = <Gerätename>.local`).
   Das OTA-Passwort entspricht dem Einstellungs-Passwort (falls aktiv).

Projektstruktur:

```
platformio.ini
src/main.cpp     Firmware (Regelung, Webserver, MQTT, Influx)
src/web_ui.h     HTML/CSS/JavaScript der Oberfläche
```

## Erster Start und WLAN

1. Ohne gespeicherte WLAN-Daten spannt das Board den Access Point **„Pool-Dosierung-XXXX“** auf (offen, ohne Passwort).
2. Mit Handy oder Rechner verbinden. Die Setup-Seite öffnet sich meist automatisch, sonst `http://192.168.4.1` aufrufen.
3. Netzwerk auswählen oder SSID eintragen, Passwort eingeben, speichern. Das Gerät startet neu und verbindet sich.
4. Die Oberfläche ist danach unter der IP aus dem Router oder unter `http://<Gerätename>.local` erreichbar
   (Standard: `http://pool-dosierung.local`).

**Setup-AP erneut öffnen:**

- Strom einschalten und **innerhalb von 4 Sekunden die Reset-Taste drücken** (oder Reset zweimal kurz hintereinander).
  Der Reset-Taster ist der EN-Taster des ESP32, deshalb geht „gedrückt halten beim Einschalten“ nicht.
- Oder IO27 beim Start gegen GND legen.
- Oder in den Einstellungen unter Wartung „WLAN-Daten löschen“.
- Verbindet sich das Gerät nach dem Start 5 Minuten lang nicht, öffnet es den Setup-AP automatisch. Kommt das WLAN zurück, schaltet er sich wieder ab.

**Roaming:** Das Board wählt beim Verbinden den stärksten AP mit der eingetragenen SSID. Fällt der AP aus, verbindet es sich neu.
Bei schwachem Signal (Standard schlechter als −75 dBm) sucht es alle 60 Sekunden nach einem um mindestens 8 dB besseren AP derselben SSID.
Voraussetzung: gleiche SSID und gleiches Passwort an allen APs, nur 2,4 GHz. Der Wechsel kostet einige Sekunden Verbindungsunterbrechung.
Die Dosierlogik läuft unabhängig vom WLAN weiter.

## Weboberfläche

- **Startseite** (ohne Passwort): Temperatur, pH, Redox, Durchfluss, Dosierfreigabe-Countdown, Pumpenstatus mit Begründung, Behälterstand, Zähler.
  Die Werte werden jede Sekunde per Abfrage von `/api/status` aktualisiert (kein Neuladen, kein WebSocket).
- **Einstellungen** (`/settings`, Benutzer `admin`, Standardpasswort `admin` – bitte ändern): alle Parameter und Wartungsfunktionen.
  Das Passwort wird per HTTP Basic Auth unverschlüsselt übertragen, daher nur im vertrauenswürdigen Heimnetz nutzen.

## Dosierlogik

Jede Pumpe wird alle 250 ms bewertet. Ein Impuls startet nur, wenn **alle** Bedingungen erfüllt sind:

1. Dosierung aktiv (sonst Testmodus)
2. Regelung für den Wert aktiv und Messwerte vorhanden, frisch und plausibel
3. Durchflussschalter meldet „läuft“ und die **Wartezeit nach Filterstart** ist abgelaufen
4. Behälter nicht leer, Tageslimits nicht erreicht, bei pH-Minus der pH nicht unter dem Grenzwert
5. Der Wert hat die **Startschwelle** überschritten und noch nicht die **Stoppschwelle** erreicht (Hysterese)
6. Die **Mischzeit** seit dem letzten Impuls dieser Pumpe ist vorbei
7. Keine andere Pumpe läuft (wenn „immer nur eine Pumpe“ aktiv ist)

Der pH-Minus-Pumpe (Pumpe 1) wird Vorrang gegeben, weil die Chlorwirkung vom pH abhängt.
Während eines Impulses wird sofort abgeschaltet, wenn der Durchfluss ausfällt, die Messdaten ausbleiben, die Regelung oder die Dosierung deaktiviert wird.
Mit der Option **„Impuls vorzeitig beenden, wenn der Zielbereich erreicht ist“** (Standard an) endet ein laufender Impuls außerdem, sobald der Regelwert die Stoppschwelle
erreicht. Das ist vor allem bei langen Impulsen (Minuten) wichtig. Danach beginnt wie üblich die Mischzeit.
Die Länge eines Impulses wird zusätzlich auf die **verbleibende Tageslaufzeit** (wenn das Tageslimit aktiv ist) und den **rechnerischen Behälterrest**
(wenn die Behältersperre aktiv ist) begrenzt. So kann ein einzelner Impuls diese Grenzen nicht überschreiten.
Bleibt weniger als 1 Sekunde übrig, wird nicht mehr gestartet.

### Hysterese

```
pH (steigt im Badebetrieb, Pumpe 1 = pH-Minus)

  Start-Schwelle  = Zielwert + Delta Start   ── ab hier wird dosiert
  Zielwert
  Stopp-Schwelle  = Zielwert − Delta Stopp   ── hier endet das Dosieren

Redox (sinkt mit der Zeit, Pumpe 2 = Chlor)

  Stopp-Schwelle  = Zielwert + Delta Stopp   ── hier endet das Dosieren
  Zielwert
  Start-Schwelle  = Zielwert − Delta Start   ── ab hier wird dosiert
```

Zwischen den Schwellen bleibt der letzte Zustand erhalten. Die Delta-Werte fangen das Rauschen der Sonden ab.
Zusätzlich glättet ein Mittelwert über N Messwerte (Standard 5) die Regelwerte. Auf der Startseite stehen Rohwert und Regelwert.

### Dosierdauer: Sekunden oder Minuten

Die Dosierdauer pro Impuls gilt je Impuls, danach beginnt die Mischzeit. Mit der Option **„Dosierdauer in Minuten angeben“** wird der Wert als Minuten gelesen.
Zulässig sind 1 Sekunde bis 2 Stunden.
Die Menge pro Impuls berechnet sich aus Förderleistung × Dosierdauer und steht auf der Startseite bei jeder Pumpe.

Beispiele (nur zur Orientierung, bitte an das eigene Becken anpassen):

| Becken | Pumpe | Dosierdauer | Mischzeit | Menge je Impuls |
|---|---|---|---|---|
| kleiner Pool, schnelle Pumpe | 60 ml/min | 10 s | 15 min | 10 ml |
| großer Pool (ca. 30 m³), langsame Pumpe | 10 ml/min | 10 min | 30–60 min | 100 ml |

Die Förderleistung der Schlauchpumpe hängt von der eingestellten Drehzahl ab. Nach jeder Änderung neu ausmessen
(Test-Funktion unter Wartung, Messbecher) und unter „Pumpen und Behälter“ eintragen.
Bei Pools mit langer Reaktionszeit sind längere Mischzeiten und höhere Tageslimits (Laufzeit pro Tag) nötig.

## Status-LED

Die rote LED (IO25) zeigt den Zustand, ohne dass man die Weboberfläche öffnen muss. Sie lässt sich in den Einstellungen abschalten.

| Anzeige | Bedeutung |
|---|---|
| kurzer Blitz alle paar Sekunden | alles in Ordnung, Messwerte der W2839 kommen an |
| Doppelblitz | Messwerte kommen an, aber das WLAN ist nicht verbunden |
| schnelles Blinken | seit über 60 s keine Messwerte (oder noch kein Messwert seit dem Start) |
| Dauerlicht | eine Pumpe läuft |
| langsames Blinken (1 s an / 1 s aus) | Setup-AP ist aktiv |

Leuchtet die LED genau andersherum (meist an, mit kurzen Aussetzern), die Option „LED-Logik invertieren“ aktivieren.

## Uhrzeit (NTP)

Die Uhrzeit kommt per NTP. Sie wird nur für den **Tageswechsel der Zähler** gebraucht, die Dosierlogik läuft auch ohne.
Standardmäßig wird der **NTP-Server übernommen, den der DHCP-Server meldet** (DHCP-Option 42). Meldet der DHCP-Server keinen,
gilt der eingetragene Rückfall-Server (Standard `pool.ntp.org`, ein Hostname oder eine IP im eigenen Netz geht auch).
Die Zeitzone ist einstellbar (Standard `CET-1CEST,M3.5.0,M10.5.0/3` für Europa/Berlin).
Eine Änderung der NTP-Quelle startet das Gerät neu. Auf der Startseite steht in der Statuszeile die aktuelle Uhrzeit mit dem
verwendeten NTP-Server, daran sieht man, ob die Synchronisation geklappt hat.

## Einstellungen im Überblick

| Gruppe | Einstellung | Standard |
|---|---|---|
| Dosierlogik | Dosierung aktiv (aus = Testmodus) | aus |
| | Dosierfreigabe nach Filterstart | 10 min |
| | Durchflussschalter invertieren | aus |
| | Dosierdauer pro Impuls / Einheit | 10 / Sekunden |
| | Mischzeit zwischen Impulsen | 15 min |
| | Impuls vorzeitig beenden bei Zielwert | an |
| | Mittelwert über N Messwerte | 5 |
| pH | Regelung aktiv / Zielwert / Delta Start / Delta Stopp | an / 7,2 / 0,2 / 0,05 |
| Redox | Regelung aktiv / Zielwert / Delta Start / Delta Stopp | an / 700 mV / 30 / 10 |
| Pumpen | Förderleistung je Pumpe (ml/min) | 60 |
| | Gesamtlaufzeit beim Auffüllen zurücksetzen | aus |
| | Zähler speichern alle (min) | 120 |
| Behälter | Volumen je Behälter (ml) | 5000 |
| Sicherungen | Nur bei Durchfluss dosieren | an |
| | Sperre bei veralteten Messdaten / nach | an / 3 min |
| | Sperre bei unplausiblen Werten (pH 3–10, Redox 0–1200 mV) | an |
| | Max. Dosierungen pro Tag und Pumpe | an / 6 |
| | Max. Laufzeit pro Tag und Pumpe | an / 30 min |
| | Immer nur eine Pumpe gleichzeitig | an |
| | Sperre bei leerem Behälter | an |
| | Kein pH-Minus unter pH-Grenzwert | an / 6,8 |
| Uhrzeit | NTP aus DHCP, NTP-Server (Rückfall), Zeitzone | an / pool.ntp.org / CET-1CEST,M3.5.0,M10.5.0/3 |
| Status-LED | aktiv, Logik invertieren | an / aus |
| Messwerte | Datapoint-IDs (Temperatur, pH, Redox) | 8 / 106 / 131 |
| | Debug-Ausgabe der Rohdaten | aus |
| WLAN | SSID, Passwort, Roaming, Schwelle | – / – / an / −75 dBm |
| Zugang | Gerätename, Passwortschutz, Passwort | pool-dosierung / an / admin |
| MQTT | Server, Port, Benutzer, Passwort, Basis-Topic, Intervall | aus / – / 1883 / – / – / pool / 10 s |
| InfluxDB | Version, URL, Datenbank, Benutzer, Passwort, Organisation, Bucket, Token, Measurement, Intervall | aus / 2 / … / pool / 30 s |

Die Tageszähler werden um Mitternacht (NTP-Zeit, Europa/Berlin) zurückgesetzt. Ohne Internetzeit gilt ersatzweise ein 24-Stunden-Fenster.

### Behälter und Zähler

Der Füllstand wird **rechnerisch** aus Laufzeit × Förderleistung ermittelt, nicht gemessen. Nach dem Auffüllen unter Wartung
„Behälter aufgefüllt“ drücken. Die Gesamtlaufzeit je Pumpe ist ein Zähler, der separat zurückgesetzt werden kann. Mit der Option
„Gesamtlaufzeit beim Auffüllen zurücksetzen“ zählt er stattdessen die Laufzeit je Behälterfüllung.
Die **Tageswerte** (Laufzeit und Dosierungen heute) werden beim Auffüllen bewusst **nicht** zurückgesetzt, weil sie als Tageslimit
vor Überdosierung schützen und nur um Mitternacht (oder manuell unter Wartung) auf 0 gehen.

### Gespeicherte Werte überschreiben (z. B. nach Stromausfall)

Unter Einstellungen → Wartung zeigt jede Pumpe die gespeicherten Werte: **Füllstand (ml)**, **Gesamtlaufzeit (s)**, **Laufzeit heute (s)** und **Dosierungen heute**.
Die Felder lassen sich überschreiben und mit „Werte übernehmen“ speichern. Nach einem Stromausfall kannst du so die letzten Werte aus MQTT/InfluxDB
(`tankN_ml`, `pumpN_runtime_s`, `pumpN_day_runtime_s`, `pumpN_day_doses`) eintragen. Läuft die Pumpe gerade, wird das Überschreiben abgelehnt.
„Behälter aufgefüllt“ setzt den Füllstand weiterhin auf das eingestellte Behältervolumen.

### Testmodus

Mit „Dosierung aktiv“ = aus werden die Pumpen nie geschaltet. Die Startseite zeigt trotzdem pro Pumpe, was der Regler tun würde
(„Testmodus: würde dosieren“). Empfehlung: erst einige Tage im Testmodus beobachten, dann scharf schalten.

## MQTT

Basis-Topic ist einstellbar (Standard `pool`). Veröffentlicht werden:

| Topic | Inhalt |
|---|---|
| `pool/temperature` | °C |
| `pool/ph` | pH |
| `pool/orp` | Redox in mV |
| `pool/flow` | 1 = Filter läuft, 0 = steht |
| `pool/pump1/relay`, `pool/pump2/relay` | 1 = an |
| `pool/pump1/tank_percent`, `…/pump2/tank_percent` | Füllstand in % |
| `pool/pump1/tank_ml`, `…/pump2/tank_ml` | Füllstand in ml |
| `pool/pump1/runtime_s`, `…/pump2/runtime_s` | Gesamtlaufzeit in Sekunden |
| `pool/pump1/day_runtime_s`, `…/pump2/day_runtime_s` | Laufzeit heute in Sekunden |
| `pool/pump1/day_doses`, `…/pump2/day_doses` | Dosierungen heute |
| `pool/pump1/state`, `…/pump2/state` | Statustext |
| `pool/state` | komplettes JSON wie `/api/status` |
| `pool/status` | `online` / `offline` (Last Will, retained) |

Beispiel für Home Assistant (`configuration.yaml`):

```yaml
mqtt:
  sensor:
    - name: "Pool pH"
      state_topic: "pool/ph"
    - name: "Pool Redox"
      state_topic: "pool/orp"
      unit_of_measurement: "mV"
    - name: "Pool Temperatur"
      state_topic: "pool/temperature"
      unit_of_measurement: "°C"
  binary_sensor:
    - name: "Pool Filter läuft"
      state_topic: "pool/flow"
      payload_on: "1"
      payload_off: "0"
```

## InfluxDB

Es wird per HTTP im Line Protocol geschrieben (Tag `device` = Gerätename, Measurement einstellbar).

| Version | Endpunkt | Zugangsdaten |
|---|---|---|
| 1.8 | `/write?db=<Datenbank>` | optional Benutzer und Passwort |
| 2.x | `/api/v2/write?org=<Organisation>&bucket=<Bucket>` | API-Token |

Felder: `temperature`, `ph`, `orp`, `flow`, `relay1`, `relay2` sowie je Pumpe (N = 1, 2) `tankN_percent`, `tankN_ml`,
`pumpN_runtime_s`, `pumpN_day_runtime_s` und `pumpN_day_doses`. Damit sind alle dauerhaft gespeicherten Zählerwerte im Monitoring vorhanden.
Bei `https://` wird das Zertifikat nicht geprüft.

## HTTP-Schnittstelle

`GET /api/status` liefert JSON, z. B.:

```json
{"temp":25.0,"ph":7.18,"orp":702,"phF":7.19,"orpF":701,"flow":1,"delayLeft":0,"dataAge":2,"dosing":1,
 "p":[{"on":0,"state":"Im Zielbereich","tankPct":86.4,"tankMl":4320,"runSec":3600,"dayRunSec":120,"mlPulse":100.0,"dayDoses":2},
      {"on":0,"state":"Im Zielbereich","tankPct":95.0,"tankMl":4750,"runSec":900,"dayRunSec":0,"mlPulse":100.0,"dayDoses":0}],
 "time":"17.11.2026 14:05:12","heap":187000,"mode":"sta","ip":"192.168.1.50","rssi":-61,"uptime":86400,"mqtt":"verbunden","influx":"OK"}
```

Die geschützten Endpunkte (`/settings`, `/save`, `/action`) verlangen Basic Auth, wenn der Passwortschutz aktiv ist.

## Andere Messgeräte oder Firmware-Varianten

Die Firmware liest das **TuyaMCU-Protokoll** (Frames `55 AA …`, 9600 Baud) mit. Auf der W2839 liefern die Datapoints
**8** (Temperatur ÷ 10), **106** (pH ÷ 100) und **131** (Redox in mV). Bei anderen Geräten oder Firmware-Ständen können die IDs abweichen.
So findest du sie:

1. Unter Einstellungen „Debug: empfangene Daten im Serial Monitor“ aktivieren (115200 Baud).
2. Beobachte, welche Datapoint-IDs sich mit den Anzeigewerten des Geräts ändern (z. B. pH-Sonde in Pufferlösung tauchen).
3. IDs unter „Messwerte der W2839“ eintragen. Die Skalierung (÷ 10 bzw. ÷ 100) ist im Code fest (`decodeFrame` in `main.cpp`).

Gelesen wird nur die Leitung vom Mess-Mikrocontroller zum CB3S (CB3S Pin 15 = P10/RX1). Der TX des ESP32 bleibt unbeschaltet,
damit er den Bus nicht stört. Die Pinbelegung des CB3S sollte vor dem Löten mit Multimeter oder Datenblatt geprüft werden.

## Flash-Verschleiß

Einstellungen und Zähler liegen im NVS des ESP32 (mit Wear-Leveling), nicht in einer Datei. Geschrieben wird nur bei Änderungen:

- **Einstellungen:** nur beim Speichern, und nur Werte, die sich geändert haben.
- **Zähler und Behälterstand:** nur bei Änderungen und höchstens alle 2 Stunden (einstellbar, 5–1440 min), sofort nur bei Benutzeraktionen, Tageswechsel, Neustart über die Oberfläche und OTA-Update.
- **Messwerte, Weboberfläche, MQTT, InfluxDB:** keine Flash-Zugriffe.
- **Beim Start:** zwei kleine Schreibzugriffe für die Erkennung des Reset-Doppeldrucks.

Ein Speichervorgang ist kein Löschzyklus: Der NVS hängt kleine Einträge (je 32 Byte) an einen 4-KB-Sektor an und löscht diesen erst, wenn er voll ist,
und verteilt das auf mehrere Sektoren. Mit dem Standardintervall liegt die Last deutlich unter der typischen Lebensdauer von etwa 100.000 Löschzyklen je Sektor.

## Fehlersuche

| Problem | Ursache / Lösung |
|---|---|
| Keine Messwerte, „Messdaten veraltet“ | RX-Leitung an IO26 und GND prüfen. Debug aktivieren, ggf. 115200 Baud testen (im Code `Serial2.begin`) |
| „Filter steht“, obwohl Pumpe läuft | „Durchflussschalter invertieren“ aktivieren oder Schalter prüfen |
| „Wartet auf Sonden“ | Wartezeit nach Filterstart läuft noch |
| „Gesperrt: Tageslimit …“ | Limit für Laufzeit/Dosierungen zu niedrig, in den Einstellungen anpassen oder Tageszähler zurücksetzen |
| „Gesperrt: Behälter leer“ | Nach dem Auffüllen „Behälter aufgefüllt“ drücken |
| Kein Zugriff nach WLAN-Wechsel | Setup-AP öffnen (Reset-Taste, siehe oben) |
| Pumpe läuft beim Booten kurz an | Relais sind beim Start ausgeschaltet. Bei Auffälligkeiten Verdrahtung prüfen |

## Bekannte Einschränkungen

- Behälterstand und Zähler sind berechnet. Sie werden zur Schonung des Flash-Speichers nur bei Änderungen und höchstens im eingestellten Intervall (Standard 2 Stunden) gespeichert,
  sofort bei Benutzeraktionen, Tageswechsel, Neustart über die Oberfläche und WLAN-Update.
  Bei Stromausfall, Druck auf die Reset-Taste oder Absturz gehen die Änderungen seit dem letzten Speichern verloren (inklusive der Tageszähler der Sicherungen).
- Weboberfläche und Passwort laufen über unverschlüsseltes HTTP.
- Das Roaming ist einfach gehalten (Neuverbindung bei schwachem Signal), kein 802.11k/v/r.
- Die Regelung kennt keine Beckenvolumen- oder Wasserchemie-Berechnung. Mengen und Intervalle stellst du selbst ein.

## Lizenz

Dieses Projekt steht unter der **GNU General Public License v3.0** (GPL-3.0). Den vollständigen Text enthält die Datei [`LICENSE`](LICENSE).
Weitergegebene Änderungen müssen ebenfalls unter der GPL-3.0 veröffentlicht werden.

Die Software wird ohne jede Gewährleistung bereitgestellt (siehe Sicherheitshinweis am Anfang).
