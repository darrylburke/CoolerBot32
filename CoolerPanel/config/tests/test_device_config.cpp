#include <doctest/doctest.h>
#include "device_config.h"

TEST_CASE("json round-trip preserves all fields") {
    DeviceConfig c;
    c.wifi_ssid = "home"; c.wifi_pass = "secret";
    c.mqtt_host = "BROKER"; c.mqtt_port = 8883;
    c.mqtt_user = "llmmon"; c.mqtt_pass = "pw";
    c.mqtt_base = "llmmon";
    c.configured = true;
    DeviceConfig r;
    REQUIRE(config_from_json(config_to_json(c), r));
    CHECK(r.wifi_ssid == "home");
    CHECK(r.wifi_pass == "secret");
    CHECK(r.mqtt_host == "BROKER");
    CHECK(r.mqtt_port == 8883);
    CHECK(r.mqtt_user == "llmmon");
    CHECK(r.mqtt_base == "llmmon");
    CHECK(r.configured == true);
}

TEST_CASE("from_json tolerates the seed file shape without configured") {
    // matches repo llmmon_config.json (no configured)
    const char* seed = R"({"wifi":{"ssid":"x","password":"y"},
      "mqtt":{"host":"h","port":1883,"username":"u","password":"p","base_topic":"t"}})";
    DeviceConfig r;
    REQUIRE(config_from_json(seed, r));
    CHECK(r.mqtt_host == "h");
    CHECK(r.mqtt_port == 1883);
    CHECK(r.mqtt_base == "t");
    CHECK(r.configured == false);   // absent → default
}

TEST_CASE("a config saved by the old TLS build still loads") {
    const char* legacy = R"({"wifi":{"ssid":"s","password":"p"},
      "mqtt":{"host":"h","port":8883,"username":"u","password":"pw",
              "tls":true,"base_topic":"t","ca_pem":"-----BEGIN CERTIFICATE-----"}})";
    DeviceConfig r;
    REQUIRE(config_from_json(legacy, r));
    CHECK(r.mqtt_host == "h");
    CHECK(r.mqtt_base == "t");
}

TEST_CASE("from_json returns false on malformed input") {
    DeviceConfig r;
    CHECK_FALSE(config_from_json("{not json", r));
}

TEST_CASE("from_json keeps non-zero defaults when keys are absent") {
    DeviceConfig r;   // fresh struct: port=8883, base="cooler" (cooler defaults)
    REQUIRE(config_from_json(R"({"wifi":{"ssid":"s"}})", r));  // no "mqtt" object at all
    CHECK(r.mqtt_port == 8883);
    CHECK(r.mqtt_base == "cooler");
}

TEST_CASE("a fresh DeviceConfig points at the cooler's broker with the panel login") {
    DeviceConfig c;
    CHECK(c.mqtt_host == COOLER_DEFAULT_MQTT_HOST);
    CHECK(c.mqtt_port == 8883);               // TLS
    CHECK(c.mqtt_user == COOLER_DEFAULT_MQTT_USER);      // not the controller's login
    CHECK(c.mqtt_base == "cooler");
    CHECK_FALSE(c.configured);   // still needs WiFi before it is usable
}

TEST_CASE("validate: missing wifi ssid or host fails") {
    DeviceConfig c; c.mqtt_host="h";
    CHECK_FALSE(validate_config(c).ok);         // no ssid
    c.wifi_ssid="s"; c.mqtt_host="";
    CHECK_FALSE(validate_config(c).ok);         // no host
    c.mqtt_host="h";
    CHECK(validate_config(c).ok);
}
TEST_CASE("validate: port range") {
    DeviceConfig c; c.wifi_ssid="s"; c.mqtt_host="h"; c.mqtt_port=0;
    CHECK_FALSE(validate_config(c).ok);
}
TEST_CASE("boot mode truth table") {
    using BM = BootMode;
    CHECK(decide_boot_mode(/*touch*/true,  /*cfg*/true,  /*wifi*/true)  == BM::Portal); // manual
    CHECK(decide_boot_mode(false, /*cfg*/false, false) == BM::Portal);                  // first boot
    CHECK(decide_boot_mode(false, true,  /*wifi*/false) == BM::Portal);                 // wifi failed
    CHECK(decide_boot_mode(false, true,  true)          == BM::Normal);                 // normal
}
