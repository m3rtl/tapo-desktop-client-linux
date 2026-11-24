// Tapo Desktop Client – translations
//
// The English text in the source code is the key. To add a language, add an entry to
// languages() and a table like kGerman below.
#include "i18n.h"

#include <atomic>
#include <string_view>
#include <unordered_map>

namespace {

using Table = std::unordered_map<std::string_view, const char*>;

const Table kGerman = {
    // toolbar
    {"No camera", "Keine Kamera"},
    {"Camera (PgUp/PgDn)", "Kamera (Bild↑/Bild↓)"},
    {"Manage cameras...", "Kameras verwalten …"},
    {"Stream quality: HD = stream1, SD = stream2 (H)", "Bildqualität: HD = stream1, SD = stream2 (H)"},
    {"Sound on", "Ton an"},
    {"Sound off", "Ton aus"},
    {"Mute / unmute (M)", "Ton an/aus (M)"},
    {"Snapshot", "Foto"},
    {"Save a JPEG of the current picture (P)", "Aktuelles Bild als JPEG speichern (P)"},
    {"Record", "Aufnahme"},
    {"Stop recording", "Aufnahme beenden"},
    {"Record to a local MKV file without re-encoding (V)", "Lokal als MKV aufnehmen, ohne Neukodierung (V)"},
    {"Pan/Tilt", "Schwenken"},
    {"Show the pan/tilt pad – or use the arrow keys / WASD (Shift = fast)",
     "Steuerkreuz anzeigen – oder Pfeiltasten / WASD benutzen (Umschalt = schnell)"},
    {"Cameras", "Kameras"},
    {"Settings", "Einstellungen"},
    {"Fullscreen", "Vollbild"},
    {"Window", "Fenster"},
    {"F / F11 / double-click", "F / F11 / Doppelklick"},

    // status / toasts
    {"No camera configured yet.\nClick \"Cameras\" to scan your network.",
     "Noch keine Kamera eingerichtet.\nKlicke auf „Kameras“, um dein Netzwerk zu durchsuchen."},
    {"No picture yet", "Noch kein Bild"},
    {"Snapshot saved: ", "Foto gespeichert: "},
    {"Snapshot failed: ", "Foto fehlgeschlagen: "},
    {"Recording to ", "Aufnahme läuft nach "},
    {"Recording stopped", "Aufnahme beendet"},
    {"Could not save ", "Konnte nicht speichern: "},
    {"Connecting…", "Verbinde …"},
    {"Connecting to ", "Verbinde mit "},
    {"login failed – check the camera account", "Anmeldung fehlgeschlagen – Kamerakonto prüfen"},
    {"no answer from camera", "keine Antwort von der Kamera"},
    {"no supported video stream", "kein unterstützter Videostream"},
    {"video decoder: ", "Video-Decoder: "},
    {"stream stalled", "Stream hängt"},
    {"stream ended", "Stream beendet"},
    {" – switching to ", " – wechsle zu "},
    {" – retrying in ", " – neuer Versuch in "},
    {"Recording: cannot create ", "Aufnahme: kann Ordner nicht anlegen: "},
    {"Recording failed: cannot write ", "Aufnahme fehlgeschlagen: kann nicht schreiben: "},
    {"Recording stopped: ", "Aufnahme abgebrochen: "},
    {"JPEG encoder missing", "JPEG-Encoder fehlt"},
    {"out of memory", "zu wenig Speicher"},
    {"unsupported pixel format", "nicht unterstütztes Pixelformat"},
    {"cannot open JPEG encoder", "JPEG-Encoder lässt sich nicht öffnen"},
    {"JPEG encoding failed", "JPEG-Kodierung fehlgeschlagen"},
    {"cannot write ", "kann nicht schreiben: "},
    {"write error", "Schreibfehler"},

    // pan / tilt (ONVIF)
    {"Pan/tilt not available", "Schwenken nicht verfügbar"},
    {"Pan/tilt: connecting…", "Schwenken: verbinde …"},
    {"Pan/tilt: ", "Schwenken: "},
    {"Pan/tilt: no media profile found", "Schwenken: kein Medienprofil gefunden"},
    {"This camera has no pan/tilt", "Diese Kamera kann nicht schwenken"},
    {"camera not reachable", "Kamera nicht erreichbar"},
    {"login failed – check the camera account (user/password)",
     "Anmeldung fehlgeschlagen – Kamerakonto prüfen (Benutzer/Passwort)"},
    {"camera answered with HTTP ", "Kamera antwortete mit HTTP "},
    {"Login OK", "Anmeldung OK"},
    {" – pan/tilt supported", " – Schwenken unterstützt"},
    {" – no pan/tilt", " – ohne Schwenken"},
    {"Preset ", "Position "},
    {"Stop", "Stopp"},

    // camera manager
    {"Saved cameras", "Gespeicherte Kameras"},
    {"None yet – scan your network or add a camera manually.",
     "Noch keine – Netzwerk durchsuchen oder Kamera von Hand hinzufügen."},
    {"Name", "Name"},
    {"Address", "Adresse"},
    {"Show", "Anzeigen"},
    {"Edit", "Bearbeiten"},
    {"Remove", "Entfernen"},
    {"Really remove?", "Wirklich entfernen?"},
    {"Add camera manually", "Kamera von Hand hinzufügen"},
    {"Find cameras in your network", "Kameras im Netzwerk finden"},
    {"Scan network", "Netzwerk durchsuchen"},
    {"Scanning...", "Suche läuft …"},
    {"ONVIF discovery + port scan, takes a few seconds", "ONVIF-Suche + Portscan, dauert ein paar Sekunden"},
    {"No cameras found. Make sure the camera is in the same network as this computer and that a Camera Account "
     "is set up in the Tapo app.",
     "Keine Kameras gefunden. Die Kamera muss im selben Netzwerk wie dieser Computer sein, und in der Tapo-App "
     "muss ein Kamerakonto eingerichtet sein."},
    {"Model", "Modell"},
    {"ONVIF camera", "ONVIF-Kamera"},
    {"saved", "gespeichert"},
    {"Add", "Hinzufügen"},
    {"Camera ", "Kamera "},
    {"Tapo cameras need a Camera Account for access from other apps: Tapo app > camera > Settings > Advanced "
     "Settings > Camera Account. Use those credentials here – not your TP-Link ID.",
     "Für den Zugriff aus anderen Programmen brauchen Tapo-Kameras ein Kamerakonto: Tapo-App > Kamera > "
     "Einstellungen > Erweiterte Einstellungen > Kamerakonto. Diese Zugangsdaten hier eintragen – nicht die "
     "TP-Link-ID."},

    // camera dialog
    {"Edit camera", "Kamera bearbeiten"},
    {"Add camera", "Kamera hinzufügen"},
    {"IP address / host", "IP-Adresse / Hostname"},
    {"User", "Benutzer"},
    {"Password", "Passwort"},
    {"Camera Account from the Tapo app (Advanced Settings)", "Kamerakonto aus der Tapo-App (Erweiterte Einstellungen)"},
    {"Advanced", "Erweitert"},
    {"RTSP port", "RTSP-Port"},
    {"ONVIF port", "ONVIF-Port"},
    {"HD stream path", "HD-Streampfad"},
    {"SD stream path", "SD-Streampfad"},
    {"Test login", "Anmeldung testen"},
    {"Testing...", "Teste …"},
    {"Save", "Speichern"},
    {"Cancel", "Abbrechen"},

    // settings
    {"Language", "Sprache"},
    {"Stream", "Stream"},
    {"Start in HD", "Start in HD"},
    {"Start in SD", "Start in SD"},
    {"TCP (robust)", "TCP (robust)"},
    {"UDP (lowest latency, needs good Wi-Fi)", "UDP (geringste Verzögerung, braucht gutes WLAN)"},
    {"Decoder threads", "Decoder-Threads"},
    {"1 = lowest latency. 2 or more spreads the work on slow CPUs (adds ~1 frame delay).",
     "1 = geringste Verzögerung. Ab 2 wird die Arbeit auf langsamen CPUs verteilt (ca. 1 Bild mehr Verzögerung)."},
    {"Switch HD/SD after errors", "HD/SD wechseln nach Fehlern"},
    {"If a stream fails this many times in a row, switch to the other one (0 = never).",
     "Schlägt ein Stream so oft hintereinander fehl, auf den anderen wechseln (0 = nie)."},
    {"Connect timeout (s)", "Verbindungs-Timeout (s)"},
    {"Stall timeout (s)", "Hänger-Timeout (s)"},
    {"Sound on at start", "Ton beim Start an"},
    {"Speed", "Geschwindigkeit"},
    {"Invert directions (ceiling mount)", "Richtungen umkehren (Deckenmontage)"},
    {"Local storage", "Lokaler Speicher"},
    {"Snapshots & recordings", "Fotos & Aufnahmen"},
    {"Config file: ", "Konfigurationsdatei: "},
    {" (portable)", " (portabel)"},

    // help
    {"Help", "Hilfe"},
    {"Arrow keys / WASD", "Pfeiltasten / WASD"},
    {"pan / tilt while held (Shift = fast)", "schwenken / neigen, solange gedrückt (Umschalt = schnell)"},
    {"Space", "Leertaste"},
    {"stop pan / tilt", "Schwenken stoppen"},
    {"go to preset (set up in the Tapo app)", "gespeicherte Position anfahren (in der Tapo-App angelegt)"},
    {"switch HD / SD stream", "zwischen HD und SD wechseln"},
    {"sound on / off", "Ton an / aus"},
    {"snapshot (JPEG)", "Foto (JPEG)"},
    {"start / stop recording (MKV)", "Aufnahme starten / beenden (MKV)"},
    {"F / F11 / double-click", "F / F11 / Doppelklick"},
    {"fullscreen", "Vollbild"},
    {"PgUp / PgDn", "Bild↑ / Bild↓"},
    {"previous / next camera", "vorherige / nächste Kamera"},
    {"reconnect", "neu verbinden"},
    {"close dialog / leave fullscreen", "Dialog schließen / Vollbild verlassen"},
    {"Q / Ctrl+Q", "Q / Strg+Q"},
    {"quit", "beenden"},
    {"Not affiliated with TP-Link. Tapo is a trademark of TP-Link.",
     "Kein Produkt von TP-Link. Tapo ist eine Marke von TP-Link."},

    // command line
    {"Menu entry installed: ", "Menüeintrag angelegt: "},
    {"unknown option: ", "unbekannte Option: "},
    {"live view and pan/tilt control for Tapo cameras", "Livebild und Schwenksteuerung für Tapo-Kameras"},
    {"Usage: tapo-desktop [options]\n"
     "  --config FILE      use this configuration file\n"
     "  --portable         keep the configuration next to the executable (tapo-desktop.ini)\n"
     "  --camera N         start with camera number N (1 = first)\n"
     "  --fullscreen       start in fullscreen\n"
     "  --lang de|en       user interface language (default: German)\n"
     "  --install-desktop  add a menu entry for the current user\n"
     "  --verbose          print FFmpeg diagnostics\n"
     "  --version, --help\n\n"
     "Configuration is stored in ~/.config/tapo-desktop/config.ini (or tapo-desktop.ini\n"
     "next to the executable in portable mode). Press F1 in the app for keyboard shortcuts.\n",
     "Aufruf: tapo-desktop [Optionen]\n"
     "  --config DATEI     diese Konfigurationsdatei verwenden\n"
     "  --portable         Konfiguration neben der Programmdatei speichern (tapo-desktop.ini)\n"
     "  --camera N         mit Kamera Nummer N starten (1 = erste)\n"
     "  --fullscreen       im Vollbild starten\n"
     "  --lang de|en       Sprache der Oberfläche (Standard: Deutsch)\n"
     "  --install-desktop  Menüeintrag für den aktuellen Benutzer anlegen\n"
     "  --verbose          FFmpeg-Diagnose ausgeben\n"
     "  --version, --help\n\n"
     "Die Konfiguration liegt in ~/.config/tapo-desktop/config.ini (im portablen Modus in\n"
     "tapo-desktop.ini neben der Programmdatei). F1 in der App zeigt die Tastenkürzel.\n"},
};

std::atomic<int> g_lang{0};  // index into languages()

}  // namespace

const std::vector<Language>& languages() {
    static const std::vector<Language> l = {{"de", "Deutsch"}, {"en", "English"}};
    return l;
}

void set_language(const std::string& code) {
    const auto& l = languages();
    for (size_t i = 0; i < l.size(); i++)
        if (code == l[i].code) {
            g_lang = (int)i;
            return;
        }
    g_lang = 0;
}

std::string language() { return languages()[g_lang].code; }

const char* tr(const char* en) {
    if (g_lang != 0) return en;  // English = source language
    auto it = kGerman.find(en);
    return it == kGerman.end() ? en : it->second;
}
