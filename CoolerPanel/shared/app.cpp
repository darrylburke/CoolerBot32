#include "app.h"
#include "cooler_state.h"
#include "mqtt_router.h"
#include "history.h"
#include "history_msg.h"
#include "commands.h"
#include "alarm.h"
#include "panel_config.h"
#include <cstring>

// Cold-boot default: neither Wi-Fi nor MQTT has connected yet. Both the sim
// (sim/mqtt_mosq.cpp) and the device set this explicitly once a connection
// attempt starts, so the pessimistic default only matters for the brief
// window before that first call.
static link_state_t g_link_state = LINK_WIFI_DOWN;

static CoolerState g_state;
CoolerState& cooler_state() { return g_state; }

static History g_hist;
History& panel_history() { return g_hist; }

static Commands g_cmds;
Commands& panel_commands() { return g_cmds; }

static Alarms g_alarms;
Alarms& panel_alarms() { return g_alarms; }

// Panel-local config (Task 16). Deliberately separate from CoolerState/
// DeviceConfig -- these values are never published to the cooler. The
// platform main() is responsible for overwriting this with the persisted
// value (panel_cfg_load()) before ui_init() runs; absent that, ui_init()
// applies the struct defaults, which is always a safe fallback.
static PanelConfig g_pcfg;
PanelConfig& panel_config() { return g_pcfg; }

// 7 days at 30 s. Falls back to 24 h if the big allocation fails.
void app_init_history() {
    if (!g_hist.init(20160)) g_hist.init(2880);
}

// Task 9 (Commands) still owns outbound /cmd publishing; this just routes
// inbound /data and /availability messages into the shared CoolerState, and
// on a successful /data route also samples into history (time-gated at 30 s
// inside maybe_append -- see history.h -- so the on-change publishes /data
// also sends don't over-sample).
extern "C" void app_on_mqtt_message(const char* topic, const uint8_t* payload, size_t len) {
    // /history (Node-RED, after every /data) is not cooler state: it only
    // fills minutes of the trend this panel did not see itself.
    if (router_is_leaf(topic, "history")) {
        HistoryMsg m;
        if (!history_msg_parse((const char*)payload, len, platform_epoch_utc(), m)) return;
        std::vector<Sample> add;
        history_missing(g_hist, m, add);
        if (!add.empty()) g_hist.merge_in(add.data(), add.size());
        return;
    }
    if (!route_message(g_state, topic, (const char*)payload, len, platform_epoch_utc()))
        return;

    // route_message() returns true for BOTH /data and /availability -- but
    // only /data carries a new reading. /availability is retained and gets
    // redelivered on every reconnect (a WiFi blip, a broker restart), and
    // cooler_parse_data() sets g_state.valid = true permanently on the
    // first successful parse and never clears it. Without this leaf check,
    // every such /availability replay would insert a history sample
    // stamped "now" but carrying whatever temp/humidity happened to still
    // be sitting in g_state from the last real /data -- a measurement that
    // never actually occurred. route_message() already validated the
    // prefix (it returned true), so re-deriving the leaf here is safe.
    const char* prefix = router_prefix();
    size_t pn = std::strlen(prefix);
    const char* leaf = topic + pn + 1;
    if (std::strcmp(leaf, "data") != 0) return;

    // Commands::on_state() is documented to be fed "every fresh /data" --
    // it uses the payload to clear pending entries and to learn the live
    // minofftime for clamping. /availability carries no config/state
    // fields at all (route_message() only flips g_state.online for it), so
    // feeding it here would just replay stale values with no benefit.
    g_cmds.on_state(g_state);
    g_alarms.update(g_state, platform_epoch_utc());

    // The relay state rides along so the trend can colour the bars where the
    // controller was requesting cooling. Only a real box reading is sampled:
    // while the SHT30 is down the controller publishes "temp":null, and the
    // trend must show that as a gap, not as a flat line at the last value.
    if (g_state.valid && g_state.temp_valid)
        g_hist.maybe_append(platform_epoch_utc(), g_state.temp, g_state.humidity, 30,
                            (uint8_t)(g_state.relay ? 1 : 0));
}

extern "C" void app_set_link_state(link_state_t s) { g_link_state = s; }
extern "C" link_state_t app_link_state(void) { return g_link_state; }
