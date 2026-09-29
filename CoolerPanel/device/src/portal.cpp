#include "portal.h"
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <lvgl.h>
#include "config_nvs.h"
#include "net_wifi.h"
#include "theme.h"

static DNSServer s_dns;
static WebServer s_http(80);
static DeviceConfig s_current;
static std::vector<std::string> s_ssids;
static std::string s_ap_ssid, s_ap_pass;

// Password fields are never echoed back to the browser; this sentinel means
// "keep the stored value".
static const char* kMask = "********";

static std::string html_escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out += c;
        }
    }
    return out;
}

static std::string form_page(const std::string& error) {
    std::string ssid_opts;
    bool current_listed = false;
    for (auto& s : s_ssids) {
        std::string sel;
        if (s == s_current.wifi_ssid) { sel = " selected"; current_listed = true; }
        ssid_opts += "<option" + sel + ">" + html_escape(s) + "</option>";
    }
    std::string h;
    h += "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>";
    h += "<title>CoolerPanel Setup</title><style>";
    h += "body{font-family:sans-serif;background:#16161D;color:#eee;margin:0;padding:16px}";
    h += "h1{font-size:20px}fieldset{border:1px solid #444;border-radius:8px;margin:0 0 14px;padding:10px}";
    h += "legend{padding:0 6px;color:#aaa}label{display:block;margin:8px 0 2px;font-size:13px;color:#bbb}";
    h += "input,select{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #555;background:#222;color:#eee}";
    h += ".row{display:flex;gap:10px}.row>div{flex:1}";
    h += "button{padding:10px 18px;border-radius:6px;border:0;font-size:15px;cursor:pointer}";
    h += ".save{background:#3a7bd5;color:#fff}.reset{background:#803333;color:#fff;float:right}";
    h += ".err{background:#803333;padding:10px;border-radius:6px;margin-bottom:12px}";
    h += "</style></head><body><h1>CoolerPanel Setup</h1>";
    if (!error.empty()) h += "<div class=err>" + html_escape(error) + "</div>";
    h += "<form method=POST action=/save>";
    h += "<fieldset><legend>Wi-Fi</legend>";
    h += "<label>Network</label><select name=ssid_sel onchange=\"document.getElementsByName('ssid')[0].value=this.value\">";
    if (!current_listed && !s_current.wifi_ssid.empty())
        h += "<option selected>" + html_escape(s_current.wifi_ssid) + "</option>";
    h += ssid_opts + "</select>";
    h += "<label>SSID (or type manually)</label><input name=ssid value=\"" + html_escape(s_current.wifi_ssid) + "\">";
    h += "<label>Password</label><input name=wpass type=password value=\"" + std::string(s_current.wifi_pass.empty() ? "" : kMask) + "\">";
    h += "</fieldset><fieldset><legend>MQTT</legend>";
    h += "<div class=row><div><label>Host</label><input name=host value=\"" + html_escape(s_current.mqtt_host) + "\"></div>";
    h += "<div><label>Port</label><input name=port type=number value=\"" + std::to_string(s_current.mqtt_port) + "\"></div></div>";
    h += "<div class=row><div><label>Username</label><input name=muser value=\"" + html_escape(s_current.mqtt_user) + "\"></div>";
    h += "<div><label>Password</label><input name=mpass type=password value=\"" + std::string(s_current.mqtt_pass.empty() ? "" : kMask) + "\"></div></div>";
    h += "<label>Topic prefix (the controller's tree, e.g. cooler)</label><input name=base value=\"" + html_escape(s_current.mqtt_base) + "\">";
    h += "</fieldset>";
    h += "<button class=save type=submit>Save &amp; Restart</button>";
    h += "</form><form method=POST action=/reset onsubmit=\"return confirm('Erase all settings?')\">";
    h += "<button class=reset type=submit>Factory Reset</button></form>";
    h += "</body></html>";
    return h;
}

static void send_form(const std::string& error = "") {
    s_http.send(200, "text/html", form_page(error).c_str());
}

static void on_save() {
    DeviceConfig c = s_current;
    c.wifi_ssid = s_http.arg("ssid").c_str();
    c.mqtt_host = s_http.arg("host").c_str();
    c.mqtt_port = (uint16_t)s_http.arg("port").toInt();
    c.mqtt_user = s_http.arg("muser").c_str();
    c.mqtt_base = s_http.arg("base").c_str();
    String wp = s_http.arg("wpass"), mp = s_http.arg("mpass");
    if (wp != kMask) c.wifi_pass = wp.c_str();   // sentinel = keep stored
    if (mp != kMask) c.mqtt_pass = mp.c_str();
    ConfigError v = validate_config(c);
    if (!v.ok) { s_current = c; send_form(v.message); return; }
    s_http.send(200, "text/html",
        "<html><body style='font-family:sans-serif;background:#16161D;color:#eee;padding:24px'>"
        "<h2>Saved &mdash; restarting&hellip;</h2>"
        "<p>The device is rebooting and will connect to your network.</p></body></html>");
    delay(1500);
    config_request_save(c);   // stages in RTC RAM + reboots (never returns)
}

static void on_reset() {
    s_http.send(200, "text/html",
        "<html><body style='font-family:sans-serif;background:#16161D;color:#eee;padding:24px'>"
        "<h2>Settings erased &mdash; restarting&hellip;</h2></body></html>");
    delay(1500);
    config_request_clear();   // never returns
}

extern void setup_build(lv_obj_t*, const char*, const char*);

void portal_run(const DeviceConfig& current, const std::string& ap_pass) {
    s_current = current;
    s_ap_pass = ap_pass;

    char suffix[8];
    snprintf(suffix, sizeof suffix, "%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
    s_ap_ssid = std::string("CoolerPanel-Setup-") + suffix;

    Serial.printf("portal: scanning networks...\n");
    s_ssids = wifi_scan();
    Serial.printf("portal: %u networks; starting AP '%s'\n",
                  (unsigned)s_ssids.size(), s_ap_ssid.c_str());

    WiFi.mode(WIFI_AP);
    WiFi.softAP(s_ap_ssid.c_str(), s_ap_pass.c_str());
    delay(100);
    s_dns.start(53, "*", WiFi.softAPIP());

    s_http.on("/", HTTP_GET, [] { send_form(); });
    s_http.on("/save", HTTP_POST, on_save);
    s_http.on("/reset", HTTP_POST, on_reset);
    // OS captive-portal probes: answer with the form so the sheet pops.
    s_http.onNotFound([] { send_form(); });
    s_http.begin();

    setup_build(lv_screen_active(), s_ap_ssid.c_str(), s_ap_pass.c_str());
    Serial.printf("portal: up at http://%s/\n", WiFi.softAPIP().toString().c_str());

    // The portal is NOT a permanent state. main.cpp's watchdog reboots us here
    // after a sustained WiFi outage, and that outage is usually the router
    // being down rather than the credentials being wrong. Looping forever would
    // turn a transient outage into an indefinite monitoring outage: the panel
    // would sit showing a provisioning page nobody is looking at while the
    // cooler goes unwatched -- strictly worse than the NO DATA alarm it
    // replaced. So give up after a while and reboot back into the normal app,
    // which will retry the stored credentials.
    //
    // Any interaction resets the clock: someone actually provisioning the
    // panel must never have it reboot out from under them mid-form. on_save()
    // reboots on its own, so a completed provisioning never reaches the
    // timeout either way.
    const uint32_t kPortalIdleRebootMs = 20u * 60u * 1000u;
    uint32_t last_activity = millis();
    while (true) {
        s_dns.processNextRequest();
        s_http.handleClient();
        lv_timer_handler();
        if (WiFi.softAPgetStationNum() > 0) last_activity = millis();
        if (millis() - last_activity > kPortalIdleRebootMs) {
            Serial.println("portal: idle timeout, rebooting to retry stored config");
            delay(50);
            ESP.restart();
        }
        delay(2);
    }
}
