# Tapo Desktop Client

**Deutsch** · [English](README.en.md)

Ein schlanker Desktop-Viewer für **TP-Link-Tapo**-Kameras unter Linux, mit geringer
Verzögerung. Er kann die Kamera **schwenken und neigen**, findet Kameras **automatisch im
Netzwerk** und speichert Fotos und Aufnahmen lokal.

Das Programm ist **eine einzige Datei**: Es muss nichts installiert werden, und es gibt keine
Abhängigkeiten. Es läuft auf praktisch jedem aktuellen 64-Bit-Linux-Desktop, zum Beispiel
Ubuntu 20.04+, Zorin OS 16+, Linux Mint 20+, Debian 11+ und Fedora 32+.

## Schritt für Schritt: So kommst du zum Kamerabild

Du brauchst nur dein Handy mit der Tapo-App und deinen Linux-Computer. Beide müssen im
**selben WLAN bzw. Heimnetz** sein wie die Kamera.

### Schritt 1: In der Tapo-App ein Kamerakonto anlegen

Tapo-Kameras lassen andere Programme erst nach einer eigenen Anmeldung zu, dem
**Kamerakonto**. Das ist **nicht** dein normales TP-Link-Konto, mit dem du dich in der App
anmeldest.

1. Öffne die **Tapo-App** auf dem Handy.
2. Tippe auf deine **Kamera**, damit das Livebild erscheint.
3. Tippe oben rechts auf das **Zahnrad** (Einstellungen).
4. Tippe auf **Erweiterte Einstellungen**.
5. Tippe auf **Kamerakonto**.
6. Denk dir einen **Benutzernamen** und ein **Passwort** aus und trage beides ein. Das
   Passwort muss je nach App-Version mindestens 6 bis 8 Zeichen lang sein.
7. Tippe auf **Speichern** und **notiere dir beides**, denn du brauchst es gleich.

Hast du mehrere Kameras, wiederholst du das für jede Kamera. Am einfachsten nimmst du
überall dieselben Zugangsdaten.

### Schritt 2: Programm herunterladen

1. Öffne die Seite [**Releases**](https://github.com/m3rtl/tapo-desktop-client-linux/releases).
2. Lade beim neuesten Eintrag die Datei **`tapo-desktop-x86_64-linux`** herunter.
3. Lege die Datei an einen Ort deiner Wahl, zum Beispiel in einen neuen Ordner
   „Tapo“ in deinem persönlichen Ordner.

### Schritt 3: Programm startbar machen (nur einmal nötig)

1. Klicke im Dateimanager mit der **rechten Maustaste** auf die Datei und wähle
   **Eigenschaften**.
2. Schalte **„Als Programm ausführen“** ein. Je nach System steht das auf der Registerkarte
   **Zugriffsrechte**.
3. Schließe das Fenster.

Alternativ geht es im Terminal: `chmod +x tapo-desktop-x86_64-linux`

### Schritt 4: Starten und Kamera hinzufügen

1. **Doppelklicke** auf die Datei. Es öffnet sich ein Fenster, und das Fenster **„Kameras“**
   sucht sofort in deinem Netzwerk.
2. Nach ein paar Sekunden erscheint deine Kamera in der Liste, zum Beispiel „C200“. Klicke
   daneben auf **„Hinzufügen“**.
3. Trage den **Benutzer** und das **Passwort** aus Schritt 1 ein. Den Namen kannst du nach
   Belieben ändern, zum Beispiel „Wohnzimmer“.
4. Klicke auf **„Anmeldung testen“**. Bei grünem **„Anmeldung OK“** passt alles.
5. Klicke auf **„Speichern“**. Nach wenigen Sekunden siehst du das Livebild.

Beim nächsten Start musst du nichts mehr eingeben, denn das Programm merkt sich deine
Kameras.

### Schritt 5: Bedienen

- **Kamera bewegen:** Halte die **Pfeiltasten** gedrückt oder benutze das Steuerkreuz unten
  rechts im Bild. Mit gedrückter **Umschalttaste** geht es schneller.
- **Vollbild:** Doppelklick ins Bild oder Taste **F**. Mit **Esc** geht es zurück.
- **Foto:** Taste **P** oder Knopf „Foto“.
- **Aufnahme:** Taste **V** oder Knopf „Aufnahme“. Fotos und Videos landen in
  `Videos/Tapo Desktop` in deinem persönlichen Ordner.
- **Ton:** Taste **M**.
- Wird die Leiste oben ausgeblendet, bewege kurz die Maus, dann erscheint sie wieder.
- **F1** zeigt alle Tastenkürzel.

### Optional: Ins Startmenü aufnehmen

Öffne ein Terminal in dem Ordner, in dem die Datei liegt, und gib ein:

```sh
./tapo-desktop-x86_64-linux --install-desktop
```

Danach findest du „Tapo Desktop“ im Startmenü. Verschiebst du die Datei später, führe den
Befehl erneut aus.

### Wenn etwas nicht klappt

| Problem | Lösung |
|---|---|
| „Keine Kameras gefunden“ | Computer und Kamera müssen im selben Netz sein, also kein Gäste-WLAN. Alternativ: „Kamera von Hand hinzufügen“ und die IP-Adresse eintragen. Du findest sie in der Tapo-App unter Kamera > Zahnrad > Geräteinfo. |
| „Anmeldung fehlgeschlagen“ | Benutzer und Passwort des **Kamerakontos** verwenden (Schritt 1), nicht die TP-Link-Anmeldung. Nach mehreren Fehlversuchen sperrt die Kamera kurz, warte dann ein paar Minuten. |
| Bild ruckelt oder bleibt hängen | Taste **H** wechselt auf die kleinere SD-Qualität. Das Programm wechselt auch selbst, wenn HD wiederholt ausfällt. |
| Pfeiltasten bewegen falsch herum | In den **Einstellungen** „Richtungen umkehren (Deckenmontage)“ einschalten. |
| Englische Oberfläche gewünscht | **Einstellungen > Sprache > English**. |

Tipp: Stelle im Router ein, dass die Kamera immer dieselbe IP-Adresse bekommt. Die
Einstellung heißt je nach Router „DHCP-Reservierung“ oder „immer die gleiche IP-Adresse
zuweisen“.

## Funktionen

- **Livebild über RTSP** mit minimaler Pufferung. Die CPU dekodiert mit FFmpeg, die GPU
  skaliert über SDL2/OpenGL.
- **Wechsel zwischen HD und SD** (`stream1` / `stream2`). Fällt ein Stream wiederholt aus,
  wechselt das Programm automatisch auf den anderen. Hängt der Stream, verbindet es sich
  selbst neu.
- **Schwenken und Neigen** über ONVIF, zum Beispiel bei C200, C210, C220 und C500: Tasten
  oder Steuerkreuz, Umschalttaste für volle Geschwindigkeit. In der Tapo-App gespeicherte
  **Positionen** rufst du mit den Tasten 1–8 auf.
- **Automatische Suche** per ONVIF WS-Discovery. Ist Multicast blockiert, wird zusätzlich
  das lokale /24-Netz nach dem ONVIF-Port durchsucht.
- **Mehrere Kameras**, lokal gespeichert.
- **Fotos** (JPEG) und **Aufnahmen** (MKV). Die Aufnahme kopiert den Stream ohne
  Neukodierung und braucht deshalb kaum CPU.
- Kameraton (G.711), stummschaltbar.
- Vollbild, automatisch ausblendende Bedienelemente und ein portabler Modus für USB-Sticks.
- Oberfläche auf **Deutsch** (Standard) und **Englisch**.

## Tastenkürzel

| Taste | Aktion |
|---|---|
| Pfeiltasten / WASD | schwenken / neigen, solange gedrückt (Umschalt = schnell) |
| Leertaste | Schwenken stoppen |
| 1 – 8 | gespeicherte Position anfahren |
| H / Tab | zwischen HD und SD wechseln |
| M | Ton an / aus |
| P | Foto |
| V | Aufnahme starten / beenden |
| F / F11 / Doppelklick | Vollbild |
| Bild↑ / Bild↓ | vorherige / nächste Kamera |
| R | neu verbinden |
| F1 | Hilfe |
| Q | beenden |

## Kommandozeile

```
--config DATEI     diese Konfigurationsdatei verwenden
--portable         Konfiguration neben der Programmdatei speichern (tapo-desktop.ini)
--camera N         mit Kamera N starten (1 = erste)
--fullscreen       im Vollbild starten
--lang de|en       Sprache der Oberfläche (Standard: Deutsch)
--install-desktop  Menüeintrag für den aktuellen Benutzer anlegen
--verbose          FFmpeg-Diagnose ausgeben
```

## Wo werden Daten gespeichert?

| Was | Wo |
|---|---|
| Kameras und Einstellungen | `~/.config/tapo-desktop/config.ini` (Dateirechte 0600) |
| Portabler Modus | `tapo-desktop.ini` neben der Programmdatei. Wird automatisch verwendet, wenn die Datei existiert. |
| Fotos und Aufnahmen | `~/Videos/Tapo Desktop/` (änderbar) |

Die Kamera-Passwörter stehen **im Klartext** in dieser Datei, lesbar nur für deinen
Benutzer. Das Programm verbindet sich ausschließlich mit den Kameras, die du hinzufügst.
Es gibt keine Cloud, keine Telemetrie und keine Update-Abfrage.

**Portabler Modus**, zum Beispiel für einen USB-Stick oder einen zweiten Rechner: Lege die
Programmdatei und eine `tapo-desktop.ini` in denselben Ordner. Dann liest und speichert das
Programm seine Einstellungen dort. Ein Beispiel findest du in
[config.example.ini](config.example.ini).

## Selbst bauen

Die Release-Datei wird mit [Zig](https://ziglang.org) als C/C++-Cross-Compiler gegen
glibc 2.31 gebaut. FFmpeg (nur RTSP, H.264/H.265, G.711, MJPEG und Matroska), SDL2 und
Dear ImGui sind statisch gelinkt. X11, OpenGL, PulseAudio/PipeWire und ALSA lädt SDL erst zur
Laufzeit; sie sind auf jedem Desktop vorhanden.

```sh
./build.sh          # lädt alle Quellen, prüft ihre Prüfsummen, Ergebnis: dist/tapo-desktop
```

Voraussetzungen: ein Debian- oder Ubuntu-basiertes System (für `apt-get download` der
Header-Pakete), `curl`, `cmake`, `make` und ein C-Compiler für nasm. Der erste Build dauert
ein paar Minuten. Releases baut GitHub Actions automatisch bei jedem Tag `v*`.

**Neue Sprache hinzufügen:** in `src/i18n.cpp` eine Übersetzungstabelle anlegen und die
Sprache in `languages()` eintragen.

## Grenzen

- Nur Linux x86-64. Wayland-Sitzungen funktionieren über XWayland.
- Die Wiedergabe von der SD-Karte der Kamera wird nicht unterstützt, weil Tapo dafür ein
  eigenes Protokoll verwendet.
- Nur Zugriff im lokalen Netz, keine Tapo-Cloud.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Die Fremdkomponenten stehen in
[THIRD_PARTY.md](THIRD_PARTY.md). FFmpeg steht unter LGPL-2.1: Mit `build.sh` lässt sich das
Programm mit jeder beliebigen FFmpeg-Version neu bauen.

*Kein Produkt von TP-Link und nicht von TP-Link unterstützt. „Tapo“ ist eine Marke von
TP-Link.*
