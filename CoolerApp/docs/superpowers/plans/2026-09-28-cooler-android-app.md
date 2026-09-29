# CoolerApp (Android) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A sideloaded Android app that monitors the walk-in cooler and changes its settings over the same MQTT contract (`cooler/data`, `cooler/availability`, `cooler/cmd`) as the CoolerPanel LCD.

**Architecture:** Kotlin + Jetpack Compose, patterned on `~/projects/SPA/spa-android`: a plain-JVM `model/` package (parsing, wording, bounds, health, pending commands, trend) that is fully unit-tested; a `data/` layer (HiveMQ TLS session, repository, Keystore config store, setup probe); a `CoolerController` that folds messages into one `UiState`; and three Compose tabs plus a setup screen. The MQTT connection lives only while the app is in the foreground.

**Tech Stack:** Kotlin (AGP 9.3.1 built-in Kotlin), Jetpack Compose (BOM 2026.08.00, Material 3), HiveMQ MQTT client 1.3.3, kotlinx-serialization-json 1.9.0 (runtime JSON tree only, no compiler plugin), JUnit 4 + kotlinx-coroutines-test, Compose UI test.

**Spec:** `docs/superpowers/specs/2026-09-28-cooler-android-app-design.md`

## Global Constraints

- Repo: `~/Cooler/CoolerApp` (a folder of the Cooler repository). All paths below are relative to it unless absolute.
- Package / applicationId / namespace: `ai.northtrail.cooler`. minSdk 26, compileSdk 37, targetSdk 37, Java/Kotlin toolchain 11.
- Plugins: `com.android.application` 9.3.1, `org.jetbrains.kotlin.plugin.compose` 2.2.10. Gradle wrapper 9.6.1 (copied from spa-android).
- Dependencies exactly: compose-bom 2026.08.00, activity-compose 1.13.0, lifecycle-runtime-compose 2.11.0, lifecycle-viewmodel-compose 2.11.0, `com.hivemq:hivemq-mqtt-client:1.3.3`, `org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0`, junit 4.13.2, kotlinx-coroutines-test 1.9.0, compose ui-test-junit4.
- TLS only. When `app/src/main/assets/broker_ca.pem` exists it is the **only** trusted CA. It is git-ignored. No credentials in the APK or in git.
- Subscribe to exactly `<base>/data` and `<base>/availability` (no wildcard). Publish only to `<base>/cmd`, QoS 1, retain false.
- Defaults: host `mqtt.example.com`, port `8883`, topic base `cooler`.
- Status wording is identical to CoolerPanel `shared/model/status_text.cpp` (spec §6).
- Settings bounds exactly as spec §4. Debounce 400 ms; pending timeout 3 s; controller silent after 5 min (300 000 ms).
- No background service, no WorkManager, no notifications.
- Every commit ends with these trailers (the commit commands below already include them):
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC`

## Review Focus

- **Controller clock not yet synced** (`hist_last_ts` 0 or a 1970 value) → the trend must not seed samples at 1970; it seeds nothing (Task 5 `seedSkippedWhenClockUnsynced`).
- **User taps + then − back to the current value within the debounce** → nothing is sent and no false "Not applied" appears (Task 7 `burstBackToCurrentSendsNothing`).
- **A periodic live `/data` still carrying the old value arrives between send and apply** → the row stays pending; it neither confirms nor reverts early (Task 7 `staleLiveDataKeepsWaiting`).
- **Broker reachable but the ACL (or a wrong topic base) yields no `cooler/data`** → Setup says "Connected, but no cooler data on cooler/data" instead of hanging or saving (Task 8 `noDataWhenBrokerIsSilent`).
- **Topic base changed in Setup** → the old cooler's state is dropped and the new topics are used without restarting the app (Task 7 `resetForgetsOldCoolerAndFollowsNewTopics`).

---

## File structure

```
CoolerApp/
  .gitignore  README.md  settings.gradle.kts  build.gradle.kts  gradle.properties
  gradlew  gradlew.bat  gradle/wrapper/*            (copied from spa-android)
  tools/import_ca.sh                                (CA from ../controller/cooler-v4.yaml)
  app/build.gradle.kts  app/proguard-rules.pro
  app/src/main/AndroidManifest.xml
  app/src/main/res/{values/strings.xml, values/themes.xml, xml/data_extraction_rules.xml, drawable/ic_launcher.xml}
  app/src/main/java/ai/northtrail/cooler/
    CoolerApplication.kt   MainActivity.kt   CoolerViewModel.kt   CoolerController.kt (+ UiState)
    model/  CoolerState.kt  CoolerStateParser.kt  Topics.kt  StatusText.kt  Bounds.kt  Commands.kt
            BrokerConfig.kt  TrendBuffer.kt  Health.kt  PendingCommands.kt  DetailRows.kt
    data/   CoolerTransport.kt  HiveMqSession.kt  MqttRepository.kt  ConfigStore.kt  BrokerProbe.kt
    ui/     Theme.kt  StatusStrip.kt  StatusScreen.kt  TrendChart.kt  SettingsScreen.kt
            DetailScreen.kt  SetupScreen.kt  CoolerApp.kt
  app/src/test/java/ai/northtrail/cooler/
    CoolerControllerTest.kt
    model/  CoolerStateParserTest.kt  TopicsTest.kt  StatusTextTest.kt  BoundsTest.kt  CommandsTest.kt
            BrokerConfigTest.kt  TrendBufferTest.kt  HealthTest.kt  PendingCommandsTest.kt  DetailRowsTest.kt
    data/   FakeBroker.kt  HiveMqSessionTest.kt  BrokerProbeTest.kt
  app/src/androidTest/java/ai/northtrail/cooler/ui/
    StatusScreenTest.kt  SetupScreenTest.kt  SettingsScreenTest.kt  DetailScreenTest.kt
```

Unit tests: `./gradlew testDebugUnitTest` (JVM, no device). UI tests: `./gradlew connectedDebugAndroidTest` (needs a phone on `adb` or an emulator; an existing AVD such as `parkedin_api30` can be started headless with `~/Android/Sdk/emulator/emulator -avd parkedin_api30 -no-window -no-audio &` then `adb wait-for-device`). If no device can be obtained, run `./gradlew assembleDebugAndroidTest` so the UI tests at least compile, and report that they were not run.

---

### Task 1: Project scaffold

**Files:**
- Create: `.gitignore`, `settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`, `local.properties` (ignored), `gradlew`, `gradlew.bat`, `gradle/wrapper/*`
- Create: `app/build.gradle.kts`, `app/proguard-rules.pro`, `app/src/main/AndroidManifest.xml`
- Create: `app/src/main/res/values/strings.xml`, `app/src/main/res/values/themes.xml`, `app/src/main/res/xml/data_extraction_rules.xml`, `app/src/main/res/drawable/ic_launcher.xml`
- Create: `app/src/main/java/ai/northtrail/cooler/MainActivity.kt` (placeholder, replaced in Task 9)
- Create: `tools/import_ca.sh`, `README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a building Android project; `tools/import_ca.sh` writes `app/src/main/assets/broker_ca.pem`.

- [ ] **Step 1: Copy the Gradle wrapper from spa-android**

```bash
cd ~/Cooler/CoolerApp
cp -r ~/projects/SPA/spa-android/gradle ~/projects/SPA/spa-android/gradlew ~/projects/SPA/spa-android/gradlew.bat .
mkdir -p app/src/main/java/ai/northtrail/cooler app/src/main/res/values app/src/main/res/xml app/src/main/res/drawable tools
printf 'sdk.dir=~/Android/Sdk\n' > local.properties
```

- [ ] **Step 2: Write the root build files**

`.gitignore`:
```
.gradle/
build/
/local.properties
.idea/
*.iml
.superpowers/

# The broker's private CA; generated by tools/import_ca.sh. Never committed.
app/src/main/assets/broker_ca.pem
```

`settings.gradle.kts`:
```kotlin
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "CoolerApp"
include(":app")
```

`build.gradle.kts`:
```kotlin
plugins {
    id("com.android.application") version "9.3.1" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.2.10" apply false
}
```

`gradle.properties`:
```
org.gradle.jvmargs=-Xmx3g -Dfile.encoding=UTF-8
org.gradle.caching=true
android.useAndroidX=true
android.nonTransitiveRClass=true
```

- [ ] **Step 3: Write the app module build file and R8 rules**

`app/build.gradle.kts`:
```kotlin
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "ai.northtrail.cooler"
    compileSdk = 37

    defaultConfig {
        applicationId = "ai.northtrail.cooler"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    packaging {
        resources.excludes += setOf(
            "META-INF/INDEX.LIST",
            "META-INF/io.netty.versions.properties",
        )
    }
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(11)
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2026.08.00")
    implementation(composeBom)
    androidTestImplementation(composeBom)

    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.11.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")

    implementation("com.hivemq:hivemq-mqtt-client:1.3.3")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
```

`app/proguard-rules.pro`:
```
-keepclassmembernames class io.netty.** { *; }
-keepclassmembers class org.jctools.** { *; }
```

- [ ] **Step 4: Write the manifest and resources**

`app/src/main/AndroidManifest.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <application
        android:allowBackup="false"
        android:dataExtractionRules="@xml/data_extraction_rules"
        android:fullBackupContent="false"
        android:icon="@drawable/ic_launcher"
        android:label="@string/app_name"
        android:supportsRtl="true"
        android:theme="@style/Theme.Cooler"
        android:usesCleartextTraffic="false">
        <activity
            android:name=".MainActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

`app/src/main/res/values/strings.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">Cooler32</string>
</resources>
```

`app/src/main/res/values/themes.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="Theme.Cooler" parent="android:style/Theme.Material.NoActionBar">
        <item name="android:windowLightStatusBar">false</item>
        <item name="android:navigationBarColor">#0F1720</item>
        <item name="android:statusBarColor">#0F1720</item>
    </style>
</resources>
```

`app/src/main/res/xml/data_extraction_rules.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<data-extraction-rules>
    <cloud-backup>
        <exclude domain="root" path="." />
    </cloud-backup>
    <device-transfer>
        <exclude domain="root" path="." />
    </device-transfer>
</data-extraction-rules>
```

`app/src/main/res/drawable/ic_launcher.xml` (a snowflake on the app background):
```xml
<?xml version="1.0" encoding="utf-8"?>
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="48dp" android:height="48dp"
    android:viewportWidth="48" android:viewportHeight="48">
    <path android:fillColor="#0F1720" android:pathData="M0,0h48v48h-48z" />
    <path android:strokeColor="#78C8FF" android:strokeWidth="3" android:strokeLineCap="round"
        android:pathData="M24,8V40M10.1,16L37.9,32M10.1,32L37.9,16" />
</vector>
```

- [ ] **Step 5: Write a placeholder activity (replaced in Task 9)**

`app/src/main/java/ai/northtrail/cooler/MainActivity.kt`:
```kotlin
package ai.northtrail.cooler

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.Text

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { Text("Cooler32") }
    }
}
```

- [ ] **Step 6: Write the CA import script**

`tools/import_ca.sh`:
```bash
#!/usr/bin/env bash
# Copies the broker's private CA (LLMMon Private CA) out of the controller's
# ESPHome config (mqtt: certificate_authority) into the app's assets, where
# MqttRepository makes it the only trusted CA. The output is git-ignored.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
YAML="${1:-$HERE/../controller/cooler-v4.yaml}"
OUT="$HERE/app/src/main/assets/broker_ca.pem"
mkdir -p "$(dirname "$OUT")"
awk '/certificate_authority: \|-?$/ { grab = 1; next }
     grab && /^    / { sub(/^    /, ""); print; next }
     grab { exit }' "$YAML" > "$OUT.tmp"
if ! grep -q -- '-----BEGIN CERTIFICATE-----' "$OUT.tmp" || ! grep -q -- '-----END CERTIFICATE-----' "$OUT.tmp"; then
    rm -f "$OUT.tmp"
    echo "no certificate_authority block found in $YAML" >&2
    exit 1
fi
mv "$OUT.tmp" "$OUT"
openssl x509 -in "$OUT" -noout -subject
```

Run: `chmod +x tools/import_ca.sh && tools/import_ca.sh`
Expected: prints `subject=CN = LLMMon Private CA` (or `subject=CN=LLMMon Private CA`).

- [ ] **Step 7: Write a short README (expanded in Task 12)**

`README.md`:
````markdown
# CoolerApp

Android app for the walk-in cooler (Cooler32). Monitors and changes settings
over MQTT, the same contract as the CoolerPanel LCD. Design:
`docs/superpowers/specs/2026-09-28-cooler-android-app-design.md`.

## Build

```bash
tools/import_ca.sh          # once: bundles the broker's private CA (git-ignored)
./gradlew testDebugUnitTest
./gradlew installDebug      # to a phone on adb
```
````

- [ ] **Step 8: Build**

Run: `./gradlew assembleDebug`
Expected: `BUILD SUCCESSFUL`. Then `git status --short` must not list `broker_ca.pem` or `local.properties`.

- [ ] **Step 9: Commit**

```bash
git add .gitignore settings.gradle.kts build.gradle.kts gradle.properties gradlew gradlew.bat gradle app tools README.md
git commit -m "build: Android project scaffold and CA import script" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 2: `/data` model, parser and topics

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/CoolerState.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/model/CoolerStateParser.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/model/Topics.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/model/CoolerStateParserTest.kt`, `app/src/test/java/ai/northtrail/cooler/model/TopicsTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `val SETTING_KEYS: List<String>` (the ten setting keys, spec §4 order).
  - `data class CoolerState(temp: Double?, humidity: Double?, finTemp: Double?, finOhms: Double?, finSlope: Double?, mode: String?, overrideSrc: String?, state: String?, relay: Boolean, coolCall: Boolean, defrost: Boolean, compressor: Int?, shtFault: Boolean, finFault: Boolean, noResponse: Boolean, runS: Long, offS: Long, holdS: Long, settings: Map<String, Int>, finCal: Boolean, calActive: Boolean, calPoints: Int, calSpan: Double, histIntervalS: Int?, histLastTs: Long?, tempHist: List<Double>, uptimeS: Long?)` — every parameter has a default (`null`, `false`, `0`, `0.0`, `emptyMap()`, `emptyList()`).
  - `object CoolerStateParser { const val VERSION = 2; fun parse(payload: String): CoolerState? }`
  - `class Topics(base: String = "cooler") { val data: String; val availability: String; val cmd: String; val subscriptions: List<String> }`

- [ ] **Step 1: Write the failing tests**

`app/src/test/java/ai/northtrail/cooler/model/CoolerStateParserTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CoolerStateParserTest {
    // A real payload captured from the controller on 2026-09-28 (history shortened).
    private val full = """
        {"v":2,"temp":9.410621643,"humidity":73.50270844,"fin_temp":21.97052002,"fin_ohms":11456.79102,
         "fin_slope":-0.226509839,"mode":"normal","override_src":"none","state":"idle","relay":0,"cool_call":0,
         "defrost":0,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":0,"off_s":39,"hold_s":0,
         "coolerset":12,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,
         "sampleinterval":3600,"fin_cutoff":1,"fin_recover":3,"settle":10,"fin_cal":0,"fin_beta":3950,
         "fin_r0":10000,"cal_active":0,"cal_points":0,"cal_span":0,"hist_n":3,"hist_interval_s":3600,
         "hist_last_ts":1790640000,"temp_hist":[21,20,19],"hum_hist":[60,61,62],"uptime_s":5025}
    """.trimIndent()

    @Test
    fun parsesEveryFieldTheAppUses() {
        val s = CoolerStateParser.parse(full)!!
        assertEquals(9.410621643, s.temp!!, 1e-9)
        assertEquals(73.50270844, s.humidity!!, 1e-9)
        assertEquals(21.97052002, s.finTemp!!, 1e-9)
        assertEquals(11456.79102, s.finOhms!!, 1e-6)
        assertEquals(-0.226509839, s.finSlope!!, 1e-9)
        assertEquals("normal", s.mode)
        assertEquals("none", s.overrideSrc)
        assertEquals("idle", s.state)
        assertFalse(s.relay)
        assertFalse(s.coolCall)
        assertFalse(s.defrost)
        assertEquals(0, s.compressor)
        assertFalse(s.shtFault || s.finFault || s.noResponse)
        assertEquals(0L, s.runS)
        assertEquals(39L, s.offS)
        assertEquals(0L, s.holdS)
        assertEquals(SETTING_KEYS.toSet(), s.settings.keys)
        assertEquals(12, s.settings["coolerset"])
        assertEquals(3600, s.settings["sampleinterval"])
        assertEquals(1, s.settings["fin_cutoff"])
        assertFalse(s.finCal)
        assertFalse(s.calActive)
        assertEquals(3600, s.histIntervalS)
        assertEquals(1_790_640_000L, s.histLastTs)
        assertEquals(listOf(21.0, 20.0, 19.0), s.tempHist)
        assertEquals(5025L, s.uptimeS)
    }

    @Test
    fun nullsBecomeUnknown() {
        val s = CoolerStateParser.parse(
            """{"v":2,"temp":null,"humidity":null,"fin_temp":null,"compressor":null,"state":"cooling","relay":1}""",
        )!!
        assertNull(s.temp)
        assertNull(s.humidity)
        assertNull(s.finTemp)
        assertNull(s.compressor)
        assertEquals("cooling", s.state)
        assertTrue(s.relay)
        assertTrue(s.settings.isEmpty())
        assertTrue(s.tempHist.isEmpty())
    }

    @Test
    fun aBareVersionIsAnEmptyState() {
        assertEquals(CoolerState(), CoolerStateParser.parse("""{"v":2}"""))
    }

    @Test
    fun otherVersionsAreIgnored() {
        assertNull(CoolerStateParser.parse("""{"v":1,"temp":4.0}"""))
        assertNull(CoolerStateParser.parse("""{"temp":4.0}"""))
    }

    @Test
    fun anythingThatIsNotAJsonObjectIsIgnored() {
        assertNull(CoolerStateParser.parse("hello"))
        assertNull(CoolerStateParser.parse("[1,2]"))
        assertNull(CoolerStateParser.parse(""))
    }

    @Test
    fun wrongTypesAreTreatedAsMissing() {
        val s = CoolerStateParser.parse(
            """{"v":2,"temp":"warm","relay":"yes","coolerset":"x","mode":5,"temp_hist":[1,"a",null,2]}""",
        )!!
        assertNull(s.temp)
        assertFalse(s.relay)
        assertNull(s.settings["coolerset"])
        assertNull(s.mode)
        assertEquals(listOf(1.0, 2.0), s.tempHist)
    }
}
```

`app/src/test/java/ai/northtrail/cooler/model/TopicsTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class TopicsTest {
    @Test
    fun defaultBaseIsCooler() {
        val t = Topics()
        assertEquals("cooler/data", t.data)
        assertEquals("cooler/availability", t.availability)
        assertEquals("cooler/cmd", t.cmd)
    }

    @Test
    fun subscribesToExactlyDataAndAvailability() {
        assertEquals(listOf("barn/data", "barn/availability"), Topics("barn").subscriptions)
    }

    @Test
    fun surroundingSpaceAndTrailingSlashAreDropped() {
        assertEquals("cooler/data", Topics(" cooler/ ").data)
    }
}
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.*'`
Expected: compilation FAILS (`Unresolved reference: CoolerStateParser`, `Topics`).

- [ ] **Step 3: Implement**

`app/src/main/java/ai/northtrail/cooler/model/CoolerState.kt`:
```kotlin
package ai.northtrail.cooler.model

/** The controller's ten settings, as carried in `/data` and accepted on `/cmd` (spec §4). */
val SETTING_KEYS: List<String> = listOf(
    "coolerset", "range", "sampleinterval",
    "fin_cutoff", "fin_recover", "settle",
    "minofftime", "minruntime", "maxrun", "dutypercent",
)

/**
 * One `cooler/data` snapshot. A null means the controller sent null or left the
 * field out; the UI shows it as "--".
 */
data class CoolerState(
    val temp: Double? = null,
    val humidity: Double? = null,
    val finTemp: Double? = null,
    val finOhms: Double? = null,
    /** °C per minute. */
    val finSlope: Double? = null,
    val mode: String? = null,
    val overrideSrc: String? = null,
    val state: String? = null,
    val relay: Boolean = false,
    val coolCall: Boolean = false,
    val defrost: Boolean = false,
    /** 1 running, 0 stopped, null when the fin sensor can't tell. */
    val compressor: Int? = null,
    val shtFault: Boolean = false,
    val finFault: Boolean = false,
    val noResponse: Boolean = false,
    val runS: Long = 0,
    val offS: Long = 0,
    val holdS: Long = 0,
    val settings: Map<String, Int> = emptyMap(),
    val finCal: Boolean = false,
    val calActive: Boolean = false,
    val calPoints: Int = 0,
    val calSpan: Double = 0.0,
    val histIntervalS: Int? = null,
    /** Epoch seconds of the newest `tempHist` sample (controller clock, SNTP). */
    val histLastTs: Long? = null,
    /** Whole degrees, oldest first. */
    val tempHist: List<Double> = emptyList(),
    val uptimeS: Long? = null,
)
```

`app/src/main/java/ai/northtrail/cooler/model/CoolerStateParser.kt`:
```kotlin
package ai.northtrail.cooler.model

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.longOrNull

/** Turns a `cooler/data` payload into a [CoolerState]; null for anything that isn't v2 JSON. */
object CoolerStateParser {
    const val VERSION = 2

    fun parse(payload: String): CoolerState? {
        val o = runCatching { Json.parseToJsonElement(payload) }.getOrNull() as? JsonObject ?: return null
        if (o.int("v") != VERSION) return null
        return CoolerState(
            temp = o.double("temp"),
            humidity = o.double("humidity"),
            finTemp = o.double("fin_temp"),
            finOhms = o.double("fin_ohms"),
            finSlope = o.double("fin_slope"),
            mode = o.text("mode"),
            overrideSrc = o.text("override_src"),
            state = o.text("state"),
            relay = o.flag("relay"),
            coolCall = o.flag("cool_call"),
            defrost = o.flag("defrost"),
            compressor = o.int("compressor"),
            shtFault = o.flag("sht_fault"),
            finFault = o.flag("fin_fault"),
            noResponse = o.flag("no_response"),
            runS = o.long("run_s") ?: 0,
            offS = o.long("off_s") ?: 0,
            holdS = o.long("hold_s") ?: 0,
            settings = SETTING_KEYS.mapNotNull { key -> o.int(key)?.let { key to it } }.toMap(),
            finCal = o.flag("fin_cal"),
            calActive = o.flag("cal_active"),
            calPoints = o.int("cal_points") ?: 0,
            calSpan = o.double("cal_span") ?: 0.0,
            histIntervalS = o.int("hist_interval_s"),
            histLastTs = o.long("hist_last_ts"),
            tempHist = o.doubles("temp_hist"),
            uptimeS = o.long("uptime_s"),
        )
    }

    private fun JsonObject.prim(key: String): JsonPrimitive? =
        (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull }

    private fun JsonObject.double(key: String): Double? = prim(key)?.doubleOrNull
    private fun JsonObject.int(key: String): Int? = prim(key)?.intOrNull
    private fun JsonObject.long(key: String): Long? = prim(key)?.longOrNull
    private fun JsonObject.flag(key: String): Boolean = int(key) == 1
    private fun JsonObject.text(key: String): String? = prim(key)?.takeIf { it.isString }?.content

    private fun JsonObject.doubles(key: String): List<Double> =
        (this[key] as? JsonArray)
            ?.mapNotNull { (it as? JsonPrimitive)?.takeUnless { p -> p is JsonNull }?.doubleOrNull }
            .orEmpty()
}
```

`app/src/main/java/ai/northtrail/cooler/model/Topics.kt`:
```kotlin
package ai.northtrail.cooler.model

/** The cooler's three topics under a base (default `cooler`). */
class Topics(base: String = "cooler") {
    private val b = base.trim().trimEnd('/')

    val data = "$b/data"
    val availability = "$b/availability"
    val cmd = "$b/cmd"

    /** Exact topics only: the broker login may read nothing else. */
    val subscriptions: List<String> = listOf(data, availability)
}
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.*'`
Expected: PASS (9 tests).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/model app/src/test/java/ai/northtrail/cooler/model
git commit -m "feat(model): parse cooler/data v2 and name the cooler topics" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 3: Status wording (port of the panel's `status_text.cpp`)

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/StatusText.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/model/StatusTextTest.kt`

**Interfaces:**
- Consumes: `CoolerState` (Task 2).
- Produces:
  - `data class Chip(val text: String, val alert: Boolean)`
  - `object StatusText { fun dur(secs: Long): String; fun temp(c: Double?): String; fun modeLabel(s: CoolerState?): String; fun stateValue(s: CoolerState?): String; fun stateKey(s: CoolerState?): String; fun compressor(s: CoolerState?): String; fun finCal(s: CoolerState?): String; fun switchChip(s: CoolerState?): Chip; fun chips(s: CoolerState?): List<Chip>; fun setpointLine(s: CoolerState?): String }`

The first eight test groups are CoolerPanel `tests/test_status_text.cpp` ported one-for-one (the panel's `valid=false` / `compressor=-1` become `null` here). `temp`, `chips` and `setpointLine` are app-only.

- [ ] **Step 1: Write the failing test**

`app/src/test/java/ai/northtrail/cooler/model/StatusTextTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StatusTextTest {
    private fun st(mode: String, state: String) = CoolerState(mode = mode, state = state)

    @Test
    fun durPicksSecondsMinutesOrHours() {
        assertEquals("0s", StatusText.dur(0))
        assertEquals("59s", StatusText.dur(59))
        assertEquals("1m", StatusText.dur(60))
        assertEquals("59m", StatusText.dur(3599))
        assertEquals("1h 05m", StatusText.dur(3900))
    }

    @Test
    fun modeLabels() {
        assertEquals("NORMAL", StatusText.modeLabel(st("normal", "idle")))
        assertEquals("OVERRIDE", StatusText.modeLabel(st("override", "idle")))
        assertEquals("FIN PROXY", StatusText.modeLabel(st("fin-proxy", "idle")))
        assertEquals("OVR + FIN PROXY", StatusText.modeLabel(st("override-proxy", "idle")))
        assertEquals("BLIND TIMER", StatusText.modeLabel(st("blind", "idle")))
        assertEquals("turbo", StatusText.modeLabel(st("turbo", "idle")))
        assertEquals("--", StatusText.modeLabel(null))
        assertEquals("--", StatusText.modeLabel(CoolerState()))
    }

    @Test
    fun stateValueUsesTheTimerThatMattersForEachState() {
        assertEquals("Cooling 2m", StatusText.stateValue(st("normal", "cooling").copy(runS = 142)))
        assertEquals("Defrost 1m", StatusText.stateValue(st("normal", "defrost").copy(offS = 75)))
        assertEquals("Waiting 3m", StatusText.stateValue(st("normal", "wait").copy(holdS = 200)))
        assertEquals("Resting 6m", StatusText.stateValue(st("fin-proxy", "rest").copy(holdS = 360)))
        assertEquals("Resting", StatusText.stateValue(st("fin-proxy", "rest").copy(holdS = 0)))
        // Relay open, nothing needed: the AC's continuous fan is still running.
        assertEquals("Fan only 15m", StatusText.stateValue(st("normal", "idle").copy(offS = 900)))
        assertEquals("--", StatusText.stateValue(null))
    }

    @Test
    fun coolingInsideTheMinimumRunSaysSoWithTheTimeLeft() {
        val s = st("normal", "cooling").copy(runS = 142, holdS = 38)
        assertEquals("Cooling • min run 38s", StatusText.stateValue(s))
        assertEquals("Cooling 2m", StatusText.stateValue(s.copy(holdS = 0)))
    }

    @Test
    fun stateKeyIsTheModeWhenNotResting() {
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "cooling").copy(holdS = 38)))
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "cooling")))
        assertEquals("OVERRIDE", StatusText.stateKey(st("override", "wait").copy(holdS = 100)))
    }

    @Test
    fun compressorWording() {
        val s = st("normal", "cooling")
        assertEquals("Running", StatusText.compressor(s.copy(compressor = 1)))
        assertEquals("Starting", StatusText.compressor(s.copy(compressor = 0, relay = true)))
        assertEquals("Stopped", StatusText.compressor(s.copy(compressor = 0, relay = false)))
        assertEquals("--", StatusText.compressor(s.copy(compressor = null)))
        assertEquals("--", StatusText.compressor(null))
    }

    @Test
    fun finCalibrationSummary() {
        val s = st("normal", "idle")
        assertEquals("fin uncalibrated", StatusText.finCal(s))
        assertEquals("fin cal ok", StatusText.finCal(s.copy(finCal = true)))
        assertEquals(
            "calibrating 3 pts 6.2C",
            StatusText.finCal(s.copy(finCal = true, calActive = true, calPoints = 3, calSpan = 6.2)),
        )
        assertEquals("--", StatusText.finCal(null))
    }

    @Test
    fun stateKeyNamesWhatARestIsWaitingOn() {
        assertEquals("FIN PROXY - SETTLE", StatusText.stateKey(st("fin-proxy", "rest").copy(holdS = 360)))
        assertEquals("OVR + FIN PROXY - SETTLE", StatusText.stateKey(st("override-proxy", "rest").copy(holdS = 60)))
        assertEquals("BLIND TIMER - BACKUP DUTY", StatusText.stateKey(st("blind", "rest").copy(finFault = true, holdS = 180)))
        assertEquals("NORMAL - BACKUP DUTY", StatusText.stateKey(st("normal", "rest").copy(finFault = true, holdS = 180)))
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "rest")))
    }

    @Test
    fun switchChipShowsTheOverrideSwitchPositionOrALostLink() {
        val s = st("normal", "idle")
        assertEquals(Chip("SWITCH OFF", false), StatusText.switchChip(s.copy(overrideSrc = "none")))
        assertEquals(Chip("SWITCH ON", true), StatusText.switchChip(s.copy(overrideSrc = "switch")))
        assertEquals(Chip("SWITCH ON", true), StatusText.switchChip(s.copy(overrideSrc = "both")))
        assertEquals(Chip("LINK LOST", true), StatusText.switchChip(s.copy(overrideSrc = "link")))
        assertEquals(Chip("--", false), StatusText.switchChip(CoolerState()))
        assertEquals(Chip("--", false), StatusText.switchChip(null))
    }

    @Test
    fun temperatureHasOneDecimal() {
        assertEquals("4.8", StatusText.temp(4.75))
        assertEquals("9.4", StatusText.temp(9.410621643))
        assertEquals("--", StatusText.temp(null))
    }

    @Test
    fun chipsListTheSwitchThenEveryActiveCondition() {
        assertEquals(listOf(Chip("--", false)), StatusText.chips(null))
        val quiet = st("normal", "idle").copy(overrideSrc = "none")
        assertEquals(listOf(Chip("SWITCH OFF", false)), StatusText.chips(quiet))
        val busy = quiet.copy(defrost = true, shtFault = true, finFault = true, noResponse = true, calActive = true)
        assertEquals(
            listOf(
                Chip("SWITCH OFF", false),
                Chip("DEFROST", true),
                Chip("BOX SENSOR FAULT", true),
                Chip("FIN SENSOR FAULT", true),
                Chip("AC NOT RESPONDING", true),
                Chip("CALIBRATING", false),
            ),
            StatusText.chips(busy),
        )
        assertTrue(StatusText.chips(busy).drop(1).dropLast(1).all { it.alert })
        assertFalse(StatusText.chips(busy).last().alert)
    }

    @Test
    fun setpointLineShowsTheOnAndOffPoints() {
        val s = CoolerState(settings = mapOf("coolerset" to 12, "range" to 2))
        assertEquals("set 12 ±2 · on >14 off <10", StatusText.setpointLine(s))
        assertEquals("--", StatusText.setpointLine(CoolerState(settings = mapOf("coolerset" to 12))))
        assertEquals("--", StatusText.setpointLine(null))
    }
}
```

- [ ] **Step 2: Run the test to see it fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.StatusTextTest'`
Expected: compilation FAILS (`Unresolved reference: StatusText`).

- [ ] **Step 3: Implement**

`app/src/main/java/ai/northtrail/cooler/model/StatusText.kt`:
```kotlin
package ai.northtrail.cooler.model

import java.util.Locale

data class Chip(val text: String, val alert: Boolean)

/**
 * The same wording as the CoolerPanel (shared/model/status_text.cpp). Change one,
 * change both: the app and the wall panel must describe the cooler identically.
 */
object StatusText {
    /** "45s" / "12m" / "1h 05m". */
    fun dur(secs: Long): String = when {
        secs < 60 -> "${secs}s"
        secs < 3_600 -> "${secs / 60}m"
        else -> String.format(Locale.US, "%dh %02dm", secs / 3_600, (secs % 3_600) / 60)
    }

    fun temp(c: Double?): String = if (c == null) "--" else String.format(Locale.US, "%.1f", c)

    fun modeLabel(s: CoolerState?): String = when (val m = s?.mode) {
        null, "" -> "--"
        "normal" -> "NORMAL"
        "override" -> "OVERRIDE"
        "fin-proxy" -> "FIN PROXY"
        "override-proxy" -> "OVR + FIN PROXY"
        "blind" -> "BLIND TIMER"
        else -> m
    }

    fun stateValue(s: CoolerState?): String {
        if (s == null) return "--"
        return when (val st = s.state) {
            null, "" -> "--"
            // Inside the minimum run the relay is held closed: show the time left.
            "cooling" -> if (s.holdS > 0) "Cooling • min run ${dur(s.holdS)}" else "Cooling ${dur(s.runS)}"
            "defrost" -> "Defrost ${dur(s.offS)}"
            "wait" -> "Waiting ${dur(s.holdS)}"
            "rest" -> if (s.holdS > 0) "Resting ${dur(s.holdS)}" else "Resting"
            // Relay open with nothing needed: the AC's continuous fan still moves box air.
            "idle" -> "Fan only ${dur(s.offS)}"
            else -> st
        }
    }

    fun stateKey(s: CoolerState?): String {
        val m = modeLabel(s)
        if (s == null || s.state != "rest") return m
        // A rest is either the timed backup duty (fin sensor failed) or the
        // fin-proxy settle before the coil can be read as box temperature.
        val why = when {
            s.finFault -> "BACKUP DUTY"
            s.mode?.contains("proxy") == true -> "SETTLE"
            else -> null
        }
        return if (why == null) m else "$m - $why"
    }

    fun compressor(s: CoolerState?): String {
        if (s == null) return "--"
        val c = s.compressor ?: return "--"
        return when {
            c == 1 -> "Running"
            s.relay -> "Starting"
            else -> "Stopped"
        }
    }

    fun finCal(s: CoolerState?): String = when {
        s == null -> "--"
        s.calActive -> String.format(Locale.US, "calibrating %d pts %.1fC", s.calPoints, s.calSpan)
        s.finCal -> "fin cal ok"
        else -> "fin uncalibrated"
    }

    fun switchChip(s: CoolerState?): Chip = when (s?.overrideSrc) {
        null, "" -> Chip("--", false)
        "switch", "both" -> Chip("SWITCH ON", true)
        "link" -> Chip("LINK LOST", true)
        else -> Chip("SWITCH OFF", false)
    }

    /** The switch chip first, then one chip per active condition. */
    fun chips(s: CoolerState?): List<Chip> {
        if (s == null) return listOf(switchChip(null))
        return buildList {
            add(switchChip(s))
            if (s.defrost) add(Chip("DEFROST", true))
            if (s.shtFault) add(Chip("BOX SENSOR FAULT", true))
            if (s.finFault) add(Chip("FIN SENSOR FAULT", true))
            if (s.noResponse) add(Chip("AC NOT RESPONDING", true))
            if (s.calActive) add(Chip("CALIBRATING", false))
        }
    }

    /** "set 12 ±2 · on >14 off <10". */
    fun setpointLine(s: CoolerState?): String {
        if (s == null) return "--"
        val set = s.settings["coolerset"] ?: return "--"
        val range = s.settings["range"] ?: return "--"
        return "set $set ±$range · on >${set + range} off <${set - range}"
    }
}
```

- [ ] **Step 4: Run the test to see it pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.StatusTextTest'`
Expected: PASS (12 tests).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/model/StatusText.kt app/src/test/java/ai/northtrail/cooler/model/StatusTextTest.kt
git commit -m "feat(model): status wording ported from the panel" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 4: Settings bounds, command payloads, broker config

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/Bounds.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/model/Commands.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/model/BrokerConfig.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/model/BoundsTest.kt`, `CommandsTest.kt`, `BrokerConfigTest.kt` (same test directory)

**Interfaces:**
- Consumes: `SETTING_KEYS` (Task 2), `Topics` (Task 2).
- Produces:
  - `enum class Widget { STEPPER, PRESET }`
  - `enum class Group(val title: String) { BOX("Box"), COIL("Coil"), TIMING("Timing") }`
  - `data class Bound(val key: String, val label: String, val unit: String, val lo: Int, val hi: Int, val step: Int, val widget: Widget, val group: Group)`
  - `object Bounds { val ALL: List<Bound>; val SAMPLE_PRESETS: List<Pair<Int, String>>; fun find(key: String): Bound?; fun floor(key: String, finCutoff: Int): Int; fun clamp(key: String, value: Int, finCutoff: Int): Int; fun format(key: String, value: Int?): String }`
  - `data class Publish(val topic: String, val payload: String)`
  - `class Commands(topics: Topics) { fun setting(key: String, value: Int): Publish; fun calibrate(start: Boolean): Publish; fun resetFinCal(): Publish }`
  - `data class BrokerConfig(val host: String = DEFAULT_HOST, val port: Int = 8883, val username: String = "", val password: String = "", val base: String = "cooler") { val problems: List<String>; val isUsable: Boolean; companion object { const val DEFAULT_HOST = "mqtt.example.com" } }`

- [ ] **Step 1: Write the failing tests**

`app/src/test/java/ai/northtrail/cooler/model/BoundsTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BoundsTest {
    @Test
    fun coversExactlyTheControllersSettings() {
        assertEquals(SETTING_KEYS, Bounds.ALL.map { it.key })
    }

    @Test
    fun everyStepperClampsToItsRange() {
        val expected = mapOf(
            "coolerset" to (2 to 40), "range" to (0 to 5), "sampleinterval" to (10 to 3600),
            "fin_cutoff" to (-5 to 5), "settle" to (2 to 30), "minofftime" to (0 to 30),
            "minruntime" to (0 to 600), "maxrun" to (1 to 60), "dutypercent" to (5 to 100),
        )
        for ((key, range) in expected) {
            assertEquals(key, range.first, Bounds.clamp(key, -1000, finCutoff = -5))
            assertEquals(key, range.second, Bounds.clamp(key, 1000, finCutoff = -5))
        }
    }

    @Test
    fun iceClearMustStayAboveIceCutoff() {
        assertEquals(4, Bounds.floor("fin_recover", finCutoff = 3))
        assertEquals(4, Bounds.clamp("fin_recover", 2, finCutoff = 3))
        assertEquals(-4, Bounds.floor("fin_recover", finCutoff = -5))
        assertEquals(10, Bounds.clamp("fin_recover", 50, finCutoff = 5))
        assertEquals(2, Bounds.floor("coolerset", finCutoff = 3))
    }

    @Test
    fun stepsMatchThePanel() {
        assertEquals(30, Bounds.find("minruntime")!!.step)
        assertEquals(5, Bounds.find("dutypercent")!!.step)
        assertEquals(Widget.PRESET, Bounds.find("sampleinterval")!!.widget)
    }

    @Test
    fun unknownKeysPassThrough() {
        assertNull(Bounds.find("turbo"))
        assertEquals(99, Bounds.clamp("turbo", 99, finCutoff = 0))
    }

    @Test
    fun valuesAreFormattedWithTheirUnit() {
        assertEquals("4 °C", Bounds.format("coolerset", 4))
        assertEquals("180 s", Bounds.format("minruntime", 180))
        assertEquals("50 %", Bounds.format("dutypercent", 50))
        assertEquals("1 h", Bounds.format("sampleinterval", 3600))
        assertEquals("5 m", Bounds.format("sampleinterval", 300))
        assertEquals("120 s", Bounds.format("sampleinterval", 120))
        assertEquals("--", Bounds.format("coolerset", null))
    }
}
```

`app/src/test/java/ai/northtrail/cooler/model/CommandsTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class CommandsTest {
    private val commands = Commands(Topics())

    @Test
    fun aSettingIsOneKeyOnTheCmdTopic() {
        assertEquals(Publish("cooler/cmd", """{"coolerset":5}"""), commands.setting("coolerset", 5))
        assertEquals(Publish("cooler/cmd", """{"fin_cutoff":-2}"""), commands.setting("fin_cutoff", -2))
    }

    @Test(expected = IllegalArgumentException::class)
    fun unknownSettingsAreRefused() {
        commands.setting("clearhist", 1)
    }

    @Test
    fun calibrationAndReset() {
        assertEquals(Publish("cooler/cmd", """{"calibrate":1}"""), commands.calibrate(start = true))
        assertEquals(Publish("cooler/cmd", """{"calibrate":0}"""), commands.calibrate(start = false))
        assertEquals(Publish("cooler/cmd", """{"fincal_reset":1}"""), commands.resetFinCal())
    }

    @Test
    fun followsTheTopicBase() {
        assertEquals("barn/cmd", Commands(Topics("barn")).setting("range", 2).topic)
    }
}
```

`app/src/test/java/ai/northtrail/cooler/model/BrokerConfigTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrokerConfigTest {
    private val good = BrokerConfig(username = "app-user", password = "secret")

    @Test
    fun defaultsPointAtTheCoolerBroker() {
        val c = BrokerConfig()
        assertEquals("mqtt.example.com", c.host)
        assertEquals(8883, c.port)
        assertEquals("cooler", c.base)
    }

    @Test
    fun usableOnceCredentialsAreFilledIn() {
        assertTrue(good.isUsable)
        assertTrue(good.problems.isEmpty())
    }

    @Test
    fun eachMissingFieldIsReported() {
        assertEquals(listOf("Host is required"), good.copy(host = " ").problems)
        assertEquals(listOf("Username is required"), good.copy(username = "").problems)
        assertEquals(listOf("Password is required"), good.copy(password = "").problems)
        assertFalse(good.copy(port = 0).isUsable)
        assertFalse(good.copy(port = 70000).isUsable)
    }

    @Test
    fun topicBaseMustBeAPlainTopic() {
        for (bad in listOf("", "/cooler", "cooler/#", "cooler/+")) {
            assertEquals(bad, listOf("Topic base must be a plain topic, like cooler"), good.copy(base = bad).problems)
        }
        assertTrue(good.copy(base = "site/cooler").isUsable)
    }
}
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.*'`
Expected: compilation FAILS (`Unresolved reference: Bounds`, `Commands`, `BrokerConfig`).

- [ ] **Step 3: Implement**

`app/src/main/java/ai/northtrail/cooler/model/Bounds.kt`:
```kotlin
package ai.northtrail.cooler.model

enum class Widget { STEPPER, PRESET }

enum class Group(val title: String) { BOX("Box"), COIL("Coil"), TIMING("Timing") }

data class Bound(
    val key: String,
    val label: String,
    val unit: String,
    val lo: Int,
    val hi: Int,
    val step: Int,
    val widget: Widget,
    val group: Group,
)

/**
 * Mirrors CoolerPanel shared/model/bounds.cpp, which mirrors the controller's own
 * clamp_settings(). dutypercent floors at 5 (not the controller's 1) so a step of 5
 * gives 5, 10, 15 ...
 */
object Bounds {
    val SAMPLE_PRESETS: List<Pair<Int, String>> =
        listOf(10 to "10 s", 60 to "1 m", 300 to "5 m", 900 to "15 m", 3600 to "1 h")

    val ALL: List<Bound> = listOf(
        Bound("coolerset", "Set point", "°C", 2, 40, 1, Widget.STEPPER, Group.BOX),
        Bound("range", "Range ±", "°C", 0, 5, 1, Widget.STEPPER, Group.BOX),
        Bound("sampleinterval", "Sample every", "s", 10, 3600, 0, Widget.PRESET, Group.BOX),
        Bound("fin_cutoff", "Ice cutoff", "°C", -5, 5, 1, Widget.STEPPER, Group.COIL),
        Bound("fin_recover", "Ice clear", "°C", -4, 10, 1, Widget.STEPPER, Group.COIL),
        Bound("settle", "Settle", "min", 2, 30, 1, Widget.STEPPER, Group.COIL),
        Bound("minofftime", "Min off", "min", 0, 30, 1, Widget.STEPPER, Group.TIMING),
        Bound("minruntime", "Min run", "s", 0, 600, 30, Widget.STEPPER, Group.TIMING),
        Bound("maxrun", "Max run", "min", 1, 60, 1, Widget.STEPPER, Group.TIMING),
        Bound("dutypercent", "Backup duty", "%", 5, 100, 5, Widget.STEPPER, Group.TIMING),
    )

    fun find(key: String): Bound? = ALL.firstOrNull { it.key == key }

    /** The lowest legal value; "Ice clear" must stay above "Ice cutoff". */
    fun floor(key: String, finCutoff: Int): Int {
        val b = find(key) ?: return Int.MIN_VALUE
        return if (key == "fin_recover") maxOf(b.lo, finCutoff + 1) else b.lo
    }

    fun clamp(key: String, value: Int, finCutoff: Int): Int {
        val b = find(key) ?: return value
        return value.coerceIn(floor(key, finCutoff), b.hi)
    }

    fun format(key: String, value: Int?): String {
        if (value == null) return "--"
        val b = find(key) ?: return value.toString()
        if (b.widget == Widget.PRESET) {
            return SAMPLE_PRESETS.firstOrNull { it.first == value }?.second ?: "$value ${b.unit}"
        }
        return "$value ${b.unit}"
    }
}
```

`app/src/main/java/ai/northtrail/cooler/model/Commands.kt`:
```kotlin
package ai.northtrail.cooler.model

import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

data class Publish(val topic: String, val payload: String)

/** Payloads for `<base>/cmd`. The controller reads each key only if it is an integer. */
class Commands(private val topics: Topics) {
    fun setting(key: String, value: Int): Publish {
        require(Bounds.find(key) != null) { "unknown setting $key" }
        return Publish(topics.cmd, buildJsonObject { put(key, value) }.toString())
    }

    /** 1 starts a fin calibration run (ignored while one is active); 0 aborts it. */
    fun calibrate(start: Boolean): Publish =
        Publish(topics.cmd, buildJsonObject { put("calibrate", if (start) 1 else 0) }.toString())

    fun resetFinCal(): Publish = Publish(topics.cmd, buildJsonObject { put("fincal_reset", 1) }.toString())
}
```

`app/src/main/java/ai/northtrail/cooler/model/BrokerConfig.kt`:
```kotlin
package ai.northtrail.cooler.model

data class BrokerConfig(
    val host: String = DEFAULT_HOST,
    val port: Int = 8883,
    val username: String = "",
    val password: String = "",
    val base: String = "cooler",
) {
    val problems: List<String>
        get() = buildList {
            if (host.isBlank()) add("Host is required")
            if (port !in 1..65535) add("Port must be 1 to 65535")
            if (username.isBlank()) add("Username is required")
            if (password.isEmpty()) add("Password is required")
            val b = base.trim()
            if (b.isEmpty() || b.startsWith("/") || b.any { it == '#' || it == '+' }) {
                add("Topic base must be a plain topic, like cooler")
            }
        }

    val isUsable: Boolean get() = problems.isEmpty()

    companion object {
        const val DEFAULT_HOST = "mqtt.example.com"
    }
}
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.*'`
Expected: PASS (all model tests, 14 new).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/model app/src/test/java/ai/northtrail/cooler/model
git commit -m "feat(model): settings bounds, cmd payloads and broker config" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 5: Trend buffer

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/TrendBuffer.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/model/TrendBufferTest.kt`

**Interfaces:**
- Consumes: `CoolerState` (Task 2).
- Produces:
  - `data class Sample(val epochS: Long, val tempC: Double, val relay: Boolean?)` (`relay` null for samples seeded from the controller's history)
  - `class TrendBuffer { val all: List<Sample>; fun seeded(state: CoolerState): TrendBuffer; fun appended(epochS: Long, tempC: Double?, relay: Boolean): TrendBuffer; fun samples(sinceS: Long): List<Sample>; companion object { const val CAPACITY = 8640; const val MIN_GAP_S = 10L; const val MIN_VALID_EPOCH = 1_600_000_000L } }` — immutable; each call returns a new buffer.

The controller's `temp_hist` is whole degrees, oldest first, the newest at `hist_last_ts`, one every `hist_interval_s` (the controller skips samples while its sensor is invalid, so the dating is approximate, as on the panel). A later Node-RED history source can merge into the same buffer.

- [ ] **Step 1: Write the failing test**

`app/src/test/java/ai/northtrail/cooler/model/TrendBufferTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TrendBufferTest {
    private val last = 1_790_640_000L
    private val history = CoolerState(histIntervalS = 3600, histLastTs = last, tempHist = listOf(21.0, 20.0, 19.0))

    @Test
    fun seedDatesSamplesBackFromTheLastTimestamp() {
        val b = TrendBuffer().seeded(history)
        assertEquals(
            listOf(Sample(last - 7200, 21.0, null), Sample(last - 3600, 20.0, null), Sample(last, 19.0, null)),
            b.all,
        )
    }

    @Test
    fun seedSkippedWhenClockUnsynced() {
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = 0)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = 12_345)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = null)).all.isEmpty())
    }

    @Test
    fun seedSkippedWithoutAnIntervalOrHistory() {
        assertTrue(TrendBuffer().seeded(history.copy(histIntervalS = null)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histIntervalS = 0)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(tempHist = emptyList())).all.isEmpty())
    }

    @Test
    fun reseedingDoesNotDuplicate() {
        assertEquals(3, TrendBuffer().seeded(history).seeded(history).all.size)
    }

    @Test
    fun liveSamplesCloserThanTheMinimumGapAreDropped() {
        val b = TrendBuffer().appended(1_000, 4.0, true).appended(1_005, 4.1, true).appended(1_010, 4.2, false)
        assertEquals(listOf(Sample(1_000, 4.0, true), Sample(1_010, 4.2, false)), b.all)
    }

    @Test
    fun missingTemperatureIsNotASample() {
        assertTrue(TrendBuffer().appended(1_000, null, false).all.isEmpty())
    }

    @Test
    fun samplesOlderThanTheNewestAreDropped() {
        val b = TrendBuffer().appended(2_000, 4.0, false).appended(1_990, 5.0, false)
        assertEquals(listOf(Sample(2_000, 4.0, false)), b.all)
    }

    @Test
    fun capacityKeepsTheNewest() {
        var b = TrendBuffer()
        repeat(TrendBuffer.CAPACITY + 5) { i -> b = b.appended(10_000L + i * 10, 4.0, false) }
        assertEquals(TrendBuffer.CAPACITY, b.all.size)
        assertEquals(10_000L + 5 * 10, b.all.first().epochS)
    }

    @Test
    fun samplesSinceFiltersByTime() {
        val b = TrendBuffer().appended(1_000, 4.0, false).appended(2_000, 5.0, false)
        assertEquals(listOf(Sample(2_000, 5.0, false)), b.samples(sinceS = 1_500))
    }
}
```

- [ ] **Step 2: Run the test to see it fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.TrendBufferTest'`
Expected: compilation FAILS (`Unresolved reference: TrendBuffer`).

- [ ] **Step 3: Implement**

`app/src/main/java/ai/northtrail/cooler/model/TrendBuffer.kt`:
```kotlin
package ai.northtrail.cooler.model

/** One point on the trend. [relay] is null for samples seeded from the controller's history. */
data class Sample(val epochS: Long, val tempC: Double, val relay: Boolean?)

/**
 * Box temperature over time: the controller's short history (seed) plus live
 * `/data` received while the app is open. Immutable.
 */
class TrendBuffer private constructor(val all: List<Sample>) {
    constructor() : this(emptyList())

    fun seeded(state: CoolerState): TrendBuffer {
        val last = state.histLastTs ?: return this
        val interval = state.histIntervalS ?: return this
        // Before SNTP sync the controller stamps 0 (1970): no usable dates.
        if (last < MIN_VALID_EPOCH || interval <= 0 || state.tempHist.isEmpty()) return this
        val n = state.tempHist.size
        val seed = state.tempHist.mapIndexed { i, t -> Sample(last - (n - 1 - i).toLong() * interval, t, null) }
        return merged(seed)
    }

    fun appended(epochS: Long, tempC: Double?, relay: Boolean): TrendBuffer {
        if (tempC == null) return this
        val newest = all.lastOrNull()?.epochS
        // /data also publishes on every change; keep bursts from over-sampling.
        if (newest != null && epochS - newest < MIN_GAP_S) return this
        return merged(listOf(Sample(epochS, tempC, relay)))
    }

    fun samples(sinceS: Long): List<Sample> = all.filter { it.epochS >= sinceS }

    private fun merged(extra: List<Sample>): TrendBuffer {
        val byTime = LinkedHashMap<Long, Sample>()
        for (s in all + extra) byTime[s.epochS] = s
        return TrendBuffer(byTime.values.sortedBy { it.epochS }.takeLast(CAPACITY))
    }

    companion object {
        /** 24 h at the 10 s minimum spacing. */
        const val CAPACITY = 8640
        const val MIN_GAP_S = 10L
        const val MIN_VALID_EPOCH = 1_600_000_000L
    }
}
```

- [ ] **Step 4: Run the test to see it pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.TrendBufferTest'`
Expected: PASS (9 tests).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/model/TrendBuffer.kt app/src/test/java/ai/northtrail/cooler/model/TrendBufferTest.kt
git commit -m "feat(model): trend buffer seeded from the controller's history" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 6: Link health and problem banners

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/Health.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/model/HealthTest.kt`

**Interfaces:**
- Consumes: `CoolerState` (Task 2).
- Produces:
  - `enum class LinkState { DISCONNECTED, CONNECTING, CONNECTED, REJECTED, TLS_FAILED }`
  - `data class LinkStatus(val state: LinkState = LinkState.DISCONNECTED, val detail: String = "", val connectedAtMillis: Long? = null)`
  - `enum class LinkView { CONNECTING, REJECTED, TLS_FAILED, UNREACHABLE, WAITING, CONTROLLER_OFFLINE, SILENT, ONLINE }`
  - `enum class Banner(val text: String) { CONTROLLER_OFFLINE, CONTROLLER_SILENT, AC_NOT_RESPONDING, BOX_SENSOR_FAULT, FIN_SENSOR_FAULT }`
  - `data class Health(val link: LinkView, val label: String, val banners: List<Banner>, val dataAgeMillis: Long?) { val controlsEnabled: Boolean; fun stripText(): String }`
  - `object Liveness { const val SILENT_AFTER_MS = 300_000L; fun evaluate(link: LinkStatus, online: Boolean?, state: CoolerState?, lastLiveDataAtMillis: Long?, nowMillis: Long): Health }`
  - `fun formatAge(millis: Long): String`

Silence is measured from the later of the last live `/data` and the moment the link connected, so opening the app with only the retained copy is not "silent". The age shown is from the last **live** `/data` (a retained copy's age is unknown).

- [ ] **Step 1: Write the failing test**

`app/src/test/java/ai/northtrail/cooler/model/HealthTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HealthTest {
    private val connected = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = 0)
    private val state = CoolerState(mode = "normal", state = "idle")

    private fun eval(
        link: LinkStatus = connected,
        online: Boolean? = true,
        s: CoolerState? = state,
        lastLive: Long? = null,
        now: Long = 1_000,
    ) = Liveness.evaluate(link, online, s, lastLive, now)

    @Test
    fun brokerProblemsComeFirst() {
        assertEquals("Broker refused access", eval(link = LinkStatus(LinkState.REJECTED)).label)
        assertEquals(
            "Can't verify the broker (bad cert)",
            eval(link = LinkStatus(LinkState.TLS_FAILED, "bad cert")).label,
        )
        assertEquals("Connecting…", eval(link = LinkStatus(LinkState.CONNECTING)).label)
        assertEquals("Broker unreachable", eval(link = LinkStatus(LinkState.DISCONNECTED)).label)
        assertFalse(eval(link = LinkStatus(LinkState.DISCONNECTED)).controlsEnabled)
    }

    @Test
    fun connectedWithoutDataIsWaiting() {
        val h = eval(s = null)
        assertEquals(LinkView.WAITING, h.link)
        assertEquals("Waiting for controller", h.label)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun controllerOfflineDisablesControlsAndRaisesABanner() {
        val h = eval(online = false)
        assertEquals(LinkView.CONTROLLER_OFFLINE, h.link)
        assertEquals(listOf(Banner.CONTROLLER_OFFLINE), h.banners)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun retainedDataAloneIsOnlineWithUnknownAge() {
        val h = eval(lastLive = null, now = 5_000)
        assertEquals(LinkView.ONLINE, h.link)
        assertTrue(h.controlsEnabled)
        assertNull(h.dataAgeMillis)
        assertEquals("Online · waiting for update", h.stripText())
    }

    @Test
    fun silentAfterFiveMinutesWithoutLiveData() {
        assertEquals(LinkView.ONLINE, eval(lastLive = 0, now = 300_000).link)
        val h = eval(lastLive = 0, now = 300_001)
        assertEquals(LinkView.SILENT, h.link)
        assertEquals("No data for 5 min", h.label)
        assertEquals(listOf(Banner.CONTROLLER_SILENT), h.banners)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun reconnectingRestartsTheSilenceClock() {
        val h = eval(link = connected.copy(connectedAtMillis = 400_000), lastLive = 0, now = 500_000)
        assertEquals(LinkView.ONLINE, h.link)
    }

    @Test
    fun faultBannersFollowTheStateEvenWhenTheBrokerIsDown() {
        val faulty = state.copy(noResponse = true, shtFault = true, finFault = true)
        assertEquals(
            listOf(Banner.AC_NOT_RESPONDING, Banner.BOX_SENSOR_FAULT, Banner.FIN_SENSOR_FAULT),
            eval(s = faulty).banners,
        )
        assertEquals(
            listOf(Banner.AC_NOT_RESPONDING, Banner.BOX_SENSOR_FAULT, Banner.FIN_SENSOR_FAULT),
            eval(link = LinkStatus(LinkState.DISCONNECTED), s = faulty).banners,
        )
    }

    @Test
    fun stripTextCarriesTheAge() {
        assertEquals("Online · updated 12s ago", eval(lastLive = 988_000, now = 1_000_000).stripText())
        assertEquals(
            "Broker unreachable · last data 14m ago",
            eval(link = LinkStatus(LinkState.DISCONNECTED), lastLive = 0, now = 840_000).stripText(),
        )
        assertEquals("Broker unreachable", eval(link = LinkStatus(LinkState.DISCONNECTED)).stripText())
    }

    @Test
    fun ageFormatting() {
        assertEquals("59s", formatAge(59_999))
        assertEquals("1m", formatAge(60_000))
        assertEquals("2h", formatAge(7_200_000))
    }
}
```

- [ ] **Step 2: Run the test to see it fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.HealthTest'`
Expected: compilation FAILS (`Unresolved reference: Liveness`).

- [ ] **Step 3: Implement**

`app/src/main/java/ai/northtrail/cooler/model/Health.kt`:
```kotlin
package ai.northtrail.cooler.model

enum class LinkState { DISCONNECTED, CONNECTING, CONNECTED, REJECTED, TLS_FAILED }

data class LinkStatus(
    val state: LinkState = LinkState.DISCONNECTED,
    val detail: String = "",
    val connectedAtMillis: Long? = null,
)

enum class LinkView { CONNECTING, REJECTED, TLS_FAILED, UNREACHABLE, WAITING, CONTROLLER_OFFLINE, SILENT, ONLINE }

/** Shown while the app is open. There is no acknowledge: nothing here notifies. */
enum class Banner(val text: String) {
    CONTROLLER_OFFLINE("Controller offline"),
    CONTROLLER_SILENT("Controller silent: no data for 5 min"),
    AC_NOT_RESPONDING("AC not responding"),
    BOX_SENSOR_FAULT("Box sensor fault"),
    FIN_SENSOR_FAULT("Fin sensor fault"),
}

data class Health(
    val link: LinkView,
    val label: String,
    val banners: List<Banner>,
    /** Since the last live /data; null when only a retained copy has been seen. */
    val dataAgeMillis: Long?,
) {
    val controlsEnabled: Boolean get() = link == LinkView.ONLINE

    fun stripText(): String {
        val age = dataAgeMillis?.let(::formatAge)
        return when {
            link == LinkView.ONLINE -> if (age == null) "Online · waiting for update" else "Online · updated $age ago"
            age != null -> "$label · last data $age ago"
            else -> label
        }
    }
}

object Liveness {
    /** The controller publishes every 30 s; the panel calls it silent after 5 min. */
    const val SILENT_AFTER_MS = 300_000L

    fun evaluate(
        link: LinkStatus,
        online: Boolean?,
        state: CoolerState?,
        lastLiveDataAtMillis: Long?,
        nowMillis: Long,
    ): Health {
        val (view, label) = when (link.state) {
            LinkState.REJECTED -> LinkView.REJECTED to "Broker refused access"
            LinkState.TLS_FAILED -> LinkView.TLS_FAILED to "Can't verify the broker (${link.detail})"
            LinkState.CONNECTING -> LinkView.CONNECTING to "Connecting…"
            LinkState.DISCONNECTED -> LinkView.UNREACHABLE to "Broker unreachable"
            LinkState.CONNECTED -> when {
                online == false -> LinkView.CONTROLLER_OFFLINE to "Controller offline"
                state == null -> LinkView.WAITING to "Waiting for controller"
                else -> {
                    // From the later of the last live /data and connecting, so the
                    // retained copy seen on connect doesn't count as silence.
                    val reference = maxOf(lastLiveDataAtMillis ?: 0L, link.connectedAtMillis ?: 0L)
                    if (nowMillis - reference > SILENT_AFTER_MS) LinkView.SILENT to "No data for 5 min"
                    else LinkView.ONLINE to "Online"
                }
            }
        }
        val banners = buildList {
            if (view == LinkView.CONTROLLER_OFFLINE) add(Banner.CONTROLLER_OFFLINE)
            if (view == LinkView.SILENT) add(Banner.CONTROLLER_SILENT)
            if (state?.noResponse == true) add(Banner.AC_NOT_RESPONDING)
            if (state?.shtFault == true) add(Banner.BOX_SENSOR_FAULT)
            if (state?.finFault == true) add(Banner.FIN_SENSOR_FAULT)
        }
        return Health(view, label, banners, lastLiveDataAtMillis?.let { nowMillis - it })
    }
}

fun formatAge(millis: Long): String {
    val s = millis / 1_000
    return when {
        s < 60 -> "${s}s"
        s < 3_600 -> "${s / 60}m"
        else -> "${s / 3_600}h"
    }
}
```

- [ ] **Step 4: Run the test to see it pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.HealthTest'`
Expected: PASS (9 tests).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/model/Health.kt app/src/test/java/ai/northtrail/cooler/model/HealthTest.kt
git commit -m "feat(model): link health and problem banners" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 7: Pending commands and the controller

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/data/CoolerTransport.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/model/PendingCommands.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/CoolerController.kt` (holds `UiState` too)
- Test: `app/src/test/java/ai/northtrail/cooler/model/PendingCommandsTest.kt`, `app/src/test/java/ai/northtrail/cooler/CoolerControllerTest.kt`

**Interfaces:**
- Consumes: `CoolerState`, `CoolerStateParser`, `Topics` (Task 2); `Bounds`, `Commands`, `Publish` (Task 4); `TrendBuffer` (Task 5); `LinkStatus`, `Health`, `Liveness` (Task 6).
- Produces:
  - `data class IncomingMessage(val topic: String, val payload: String, val retained: Boolean)`
  - `interface CoolerTransport { val link: StateFlow<LinkStatus>; val messages: SharedFlow<IncomingMessage>; fun connect(); fun disconnect(); fun publish(publish: Publish): Boolean }`
  - `data class Pending(val value: Int, val sentAtMillis: Long)`; `data class Resolution(val pending: Map<String, Pending>, val confirmed: Set<String>, val timedOut: Set<String>)`; `object PendingCommands { const val TIMEOUT_MS = 3_000L; fun resolve(pending: Map<String, Pending>, state: CoolerState?, stateLiveAtMillis: Long?, nowMillis: Long): Resolution }`
  - `data class UiState(val configured: Boolean, val link: LinkStatus, val online: Boolean?, val state: CoolerState?, val stateLiveAtMillis: Long?, val lastLiveDataAtMillis: Long?, val trend: TrendBuffer, val health: Health, val drafts: Map<String, Int>, val pending: Map<String, Pending>, val message: String?) { fun settingValue(key: String): Int?; fun isPending(key: String): Boolean }`
  - `class CoolerController(transport: CoolerTransport, topics: Topics, scope: CoroutineScope, clock: () -> Long, configured: Boolean, onIgnored: (String) -> Unit = {}) { val ui: StateFlow<UiState>; fun start(); fun reset(newTopics: Topics); fun stepSetting(key: String, direction: Int); fun chooseSetting(key: String, value: Int); fun startCalibration(); fun abortCalibration(); fun resetFinCal(); fun consumeMessage(); fun markConfigured(); companion object { const val DEBOUNCE_MS = 400L } }`

A setting edit becomes a **draft** (shown at once), is published after 400 ms of quiet, then is **pending** until a *live* `/data` reports the same value (confirmed) or 3 s pass (reverted to whatever the controller reports, with "Not applied: <label>"). A draft that ends on the controller's current value, with nothing pending, sends nothing.

- [ ] **Step 1: Write the failing tests**

`app/src/test/java/ai/northtrail/cooler/model/PendingCommandsTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class PendingCommandsTest {
    private val sent = mapOf("coolerset" to Pending(7, sentAtMillis = 1_000))
    private val seven = CoolerState(settings = mapOf("coolerset" to 7))
    private val four = CoolerState(settings = mapOf("coolerset" to 4))

    @Test
    fun liveMatchingValueAfterSendingConfirms() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = 1_500, nowMillis = 1_500)
        assertEquals(setOf("coolerset"), r.confirmed)
        assertEquals(emptyMap<String, Pending>(), r.pending)
    }

    @Test
    fun dataFromBeforeTheSendDoesNotConfirm() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = 900, nowMillis = 1_500)
        assertEquals(sent, r.pending)
    }

    @Test
    fun retainedDataDoesNotConfirm() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = null, nowMillis = 1_500)
        assertEquals(sent, r.pending)
    }

    @Test
    fun aDifferentValueKeepsWaiting() {
        val r = PendingCommands.resolve(sent, four, stateLiveAtMillis = 1_500, nowMillis = 1_500)
        assertEquals(sent, r.pending)
        assertEquals(emptySet<String>(), r.timedOut)
    }

    @Test
    fun timesOutAtThreeSeconds() {
        assertEquals(sent, PendingCommands.resolve(sent, four, 1_500, nowMillis = 3_999).pending)
        val r = PendingCommands.resolve(sent, four, 1_500, nowMillis = 4_000)
        assertEquals(setOf("coolerset"), r.timedOut)
        assertEquals(emptyMap<String, Pending>(), r.pending)
    }
}
```

`app/src/test/java/ai/northtrail/cooler/CoolerControllerTest.kt`:
```kotlin
package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.LinkView
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Sample
import ai.northtrail.cooler.model.Topics
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private class FakeTransport : CoolerTransport {
    override val link = MutableStateFlow(LinkStatus())
    override val messages = MutableSharedFlow<IncomingMessage>(extraBufferCapacity = 64)
    val published = mutableListOf<Publish>()
    var publishSucceeds = true
    override fun connect() {}
    override fun disconnect() {}
    override fun publish(publish: Publish): Boolean {
        if (publishSucceeds) published += publish
        return publishSucceeds
    }
}

private fun payload(coolerset: Int = 4, finCutoff: Int = 1, finRecover: Int = 3, temp: Double = 4.8): String =
    """{"v":2,"temp":$temp,"humidity":70,"fin_temp":2.0,"mode":"normal","override_src":"none",""" +
        """"state":"idle","relay":0,"coolerset":$coolerset,"range":2,"sampleinterval":3600,""" +
        """"fin_cutoff":$finCutoff,"fin_recover":$finRecover,"settle":10,"minofftime":5,""" +
        """"minruntime":180,"maxrun":10,"dutypercent":50}"""

@OptIn(ExperimentalCoroutinesApi::class)
class CoolerControllerTest {
    private val transport = FakeTransport()
    private val topics = Topics()
    private val ignored = mutableListOf<String>()

    private fun TestScope.controller(): CoolerController =
        CoolerController(
            transport = transport,
            topics = topics,
            scope = backgroundScope,
            clock = { testScheduler.currentTime },
            configured = true,
            onIgnored = { ignored += it },
        ).also { it.start(); runCurrent() }

    private fun TestScope.emit(topic: String, body: String, retained: Boolean = false) {
        transport.messages.tryEmit(IncomingMessage(topic, body, retained))
        runCurrent()
    }

    /** Broker connected, controller online, retained /data delivered. */
    private fun TestScope.online(body: String = payload()) {
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
        emit("cooler/availability", "online", retained = true)
        emit("cooler/data", body, retained = true)
    }

    private fun cmd(json: String) = Publish("cooler/cmd", json)

    @Test
    fun retainedDataFillsTheStateButLeavesTheAgeUnknown() = runTest {
        val c = controller()
        online()
        val ui = c.ui.value
        assertEquals(4, ui.state!!.settings["coolerset"])
        assertEquals(LinkView.ONLINE, ui.health.link)
        assertNull(ui.health.dataAgeMillis)
        assertTrue(ui.trend.all.isEmpty())
    }

    @Test
    fun liveDataRecordsItsAgeAndATrendSample() = runTest {
        val c = controller()
        online()
        advanceTimeBy(5_000)
        emit("cooler/data", payload())
        assertEquals(0L, c.ui.value.health.dataAgeMillis)
        assertEquals(listOf(Sample(5, 4.8, false)), c.ui.value.trend.all)
    }

    @Test
    fun aBadPayloadKeepsTheLastGoodState() = runTest {
        val c = controller()
        online()
        emit("cooler/data", """{"v":1}""")
        assertEquals(4, c.ui.value.state!!.settings["coolerset"])
        assertEquals(1, ignored.size)
    }

    @Test
    fun offlineDisablesControlsAndStepsDoNothing() = runTest {
        val c = controller()
        online()
        emit("cooler/availability", "offline", retained = true)
        assertEquals(LinkView.CONTROLLER_OFFLINE, c.ui.value.health.link)
        c.stepSetting("coolerset", +1)
        advanceTimeBy(1_000)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        assertTrue(c.ui.value.drafts.isEmpty())
    }

    @Test
    fun aBurstOfTapsSendsOnePublishAfterTheDebounce() = runTest {
        val c = controller()
        online()
        repeat(3) { c.stepSetting("coolerset", +1) }
        assertEquals(7, c.ui.value.settingValue("coolerset"))
        assertTrue(c.ui.value.isPending("coolerset"))
        advanceTimeBy(399)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        advanceTimeBy(2)
        runCurrent()
        assertEquals(listOf(cmd("""{"coolerset":7}""")), transport.published)
        assertEquals(7, c.ui.value.pending["coolerset"]!!.value)
        assertEquals(7, c.ui.value.settingValue("coolerset"))
    }

    @Test
    fun liveMatchingDataConfirms() = runTest {
        val c = controller()
        online()
        repeat(3) { c.stepSetting("coolerset", +1) }
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 7))
        assertFalse(c.ui.value.isPending("coolerset"))
        assertEquals(7, c.ui.value.settingValue("coolerset"))
        assertNull(c.ui.value.message)
    }

    @Test
    fun retainedMatchingDataDoesNotConfirm() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 5), retained = true)
        assertTrue(c.ui.value.isPending("coolerset"))
    }

    @Test
    fun staleLiveDataKeepsWaiting() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 4)) // periodic publish, sent before the cmd landed
        assertTrue(c.ui.value.isPending("coolerset"))
        assertNull(c.ui.value.message)
        advanceTimeBy(1_000)
        emit("cooler/data", payload(coolerset = 5))
        assertFalse(c.ui.value.isPending("coolerset"))
        assertNull(c.ui.value.message)
    }

    @Test
    fun noConfirmationRevertsAfterThreeSeconds() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(4_000)
        runCurrent()
        assertFalse(c.ui.value.isPending("coolerset"))
        assertEquals(4, c.ui.value.settingValue("coolerset"))
        assertEquals("Not applied: Set point", c.ui.value.message)
    }

    @Test
    fun burstBackToCurrentSendsNothing() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        c.stepSetting("coolerset", -1)
        advanceTimeBy(500)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        assertFalse(c.ui.value.isPending("coolerset"))
        advanceTimeBy(4_000)
        runCurrent()
        assertNull(c.ui.value.message)
    }

    @Test
    fun iceClearCannotGoBelowIceCutoffPlusOne() = runTest {
        val c = controller()
        online(payload(finCutoff = 3, finRecover = 4))
        c.stepSetting("fin_recover", -1)
        advanceTimeBy(500)
        runCurrent()
        assertTrue(transport.published.isEmpty())
    }

    @Test
    fun aPresetIsSentAfterTheDebounce() = runTest {
        val c = controller()
        online()
        c.chooseSetting("sampleinterval", 300)
        advanceTimeBy(500)
        runCurrent()
        assertEquals(listOf(cmd("""{"sampleinterval":300}""")), transport.published)
    }

    @Test
    fun aFailedPublishIsReportedAndNothingIsLeftPending() = runTest {
        val c = controller()
        online()
        transport.publishSucceeds = false
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        runCurrent()
        assertEquals("Couldn't send: controller not reachable", c.ui.value.message)
        assertFalse(c.ui.value.isPending("coolerset"))
        c.consumeMessage()
        assertNull(c.ui.value.message)
    }

    @Test
    fun calibrationActionsPublishImmediately() = runTest {
        val c = controller()
        online()
        c.startCalibration()
        c.abortCalibration()
        c.resetFinCal()
        assertEquals(
            listOf(cmd("""{"calibrate":1}"""), cmd("""{"calibrate":0}"""), cmd("""{"fincal_reset":1}""")),
            transport.published,
        )
    }

    @Test
    fun silentAfterFiveMinutesDisablesControls() = runTest {
        val c = controller()
        online()
        emit("cooler/data", payload())
        advanceTimeBy(301_000)
        runCurrent()
        assertEquals(LinkView.SILENT, c.ui.value.health.link)
        assertFalse(c.ui.value.health.controlsEnabled)
    }

    @Test
    fun resetForgetsOldCoolerAndFollowsNewTopics() = runTest {
        val c = controller()
        online()
        c.reset(Topics("barn"))
        assertNull(c.ui.value.state)
        emit("cooler/data", payload(coolerset = 8))
        assertNull(c.ui.value.state)
        emit("barn/data", payload(coolerset = 9))
        assertEquals(9, c.ui.value.state!!.settings["coolerset"])
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        runCurrent()
        assertEquals(listOf(Publish("barn/cmd", """{"coolerset":10}""")), transport.published)
    }
}
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.CoolerControllerTest' --tests 'ai.northtrail.cooler.model.PendingCommandsTest'`
Expected: compilation FAILS (`Unresolved reference: CoolerTransport`, `CoolerController`, `PendingCommands`).

- [ ] **Step 3: Implement the transport interface and pending commands**

`app/src/main/java/ai/northtrail/cooler/data/CoolerTransport.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Publish
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

data class IncomingMessage(val topic: String, val payload: String, val retained: Boolean)

interface CoolerTransport {
    val link: StateFlow<LinkStatus>
    val messages: SharedFlow<IncomingMessage>
    fun connect()
    fun disconnect()

    /** Returns false if the message could not be handed to a connected client. */
    fun publish(publish: Publish): Boolean
}
```

`app/src/main/java/ai/northtrail/cooler/model/PendingCommands.kt`:
```kotlin
package ai.northtrail.cooler.model

data class Pending(val value: Int, val sentAtMillis: Long)

data class Resolution(
    val pending: Map<String, Pending>,
    val confirmed: Set<String>,
    val timedOut: Set<String>,
)

object PendingCommands {
    const val TIMEOUT_MS = 3_000L

    /**
     * Confirmed only by a live /data received after sending that reports the
     * requested value: a retained copy, or a periodic publish that crossed the
     * command in flight, still carries the old value.
     */
    fun resolve(
        pending: Map<String, Pending>,
        state: CoolerState?,
        stateLiveAtMillis: Long?,
        nowMillis: Long,
    ): Resolution {
        val confirmed = mutableSetOf<String>()
        val timedOut = mutableSetOf<String>()
        val remaining = mutableMapOf<String, Pending>()
        for ((key, p) in pending) {
            val live = stateLiveAtMillis != null && stateLiveAtMillis >= p.sentAtMillis
            when {
                live && state?.settings?.get(key) == p.value -> confirmed += key
                nowMillis - p.sentAtMillis >= TIMEOUT_MS -> timedOut += key
                else -> remaining[key] = p
            }
        }
        return Resolution(remaining, confirmed, timedOut)
    }
}
```

- [ ] **Step 4: Implement the controller**

`app/src/main/java/ai/northtrail/cooler/CoolerController.kt`:
```kotlin
package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.Bounds
import ai.northtrail.cooler.model.Commands
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.CoolerStateParser
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Liveness
import ai.northtrail.cooler.model.Pending
import ai.northtrail.cooler.model.PendingCommands
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Topics
import ai.northtrail.cooler.model.TrendBuffer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class UiState(
    val configured: Boolean,
    val link: LinkStatus = LinkStatus(),
    /** From `<base>/availability`; null until seen. */
    val online: Boolean? = null,
    val state: CoolerState? = null,
    /** When [state] arrived, if it came from a live (non-retained) message. */
    val stateLiveAtMillis: Long? = null,
    val lastLiveDataAtMillis: Long? = null,
    val trend: TrendBuffer = TrendBuffer(),
    val health: Health = Liveness.evaluate(LinkStatus(), null, null, null, 0L),
    /** Edits still inside the debounce. */
    val drafts: Map<String, Int> = emptyMap(),
    /** Sent, waiting for the controller to report them. */
    val pending: Map<String, Pending> = emptyMap(),
    val message: String? = null,
) {
    /** What a setting row shows: the edit in progress, else the value sent, else the controller's. */
    fun settingValue(key: String): Int? = drafts[key] ?: pending[key]?.value ?: state?.settings?.get(key)

    fun isPending(key: String): Boolean = key in drafts || key in pending
}

class CoolerController(
    private val transport: CoolerTransport,
    topics: Topics,
    private val scope: CoroutineScope,
    private val clock: () -> Long,
    configured: Boolean,
    private val onIgnored: (String) -> Unit = {},
) {
    @Volatile private var topics = topics
    @Volatile private var commands = Commands(topics)
    private val state = MutableStateFlow(UiState(configured = configured))
    val ui: StateFlow<UiState> = state.asStateFlow()
    private val draftJobs = mutableMapOf<String, Job>()

    fun start() {
        scope.launch { transport.messages.collect { onMessage(it) } }
        scope.launch { transport.link.collect { l -> update { it.copy(link = l) } } }
        // Re-evaluate silence and command timeouts even when nothing arrives.
        scope.launch {
            while (isActive) {
                delay(1_000)
                update { it }
            }
        }
    }

    /** A new topic base is a different cooler: forget everything seen on the old one. */
    fun reset(newTopics: Topics) {
        topics = newTopics
        commands = Commands(newTopics)
        draftJobs.values.forEach { it.cancel() }
        draftJobs.clear()
        update { UiState(configured = it.configured, link = it.link) }
    }

    fun stepSetting(key: String, direction: Int) {
        val s = state.value
        val bound = Bounds.find(key) ?: return
        val current = s.settingValue(key) ?: return
        if (!s.health.controlsEnabled) return
        val next = Bounds.clamp(key, current + direction * bound.step, s.settingValue("fin_cutoff") ?: 0)
        if (next != current) edit(key, next)
    }

    fun chooseSetting(key: String, value: Int) {
        val s = state.value
        if (!s.health.controlsEnabled || Bounds.find(key) == null) return
        val next = Bounds.clamp(key, value, s.settingValue("fin_cutoff") ?: 0)
        if (next != s.settingValue(key)) edit(key, next)
    }

    fun startCalibration() = act(commands.calibrate(start = true))
    fun abortCalibration() = act(commands.calibrate(start = false))
    fun resetFinCal() = act(commands.resetFinCal())

    fun consumeMessage() = state.update { it.copy(message = null) }
    fun markConfigured() = update { it.copy(configured = true) }

    private fun onMessage(m: IncomingMessage) {
        val t = topics
        val now = clock()
        when (m.topic) {
            t.availability -> update {
                when (m.payload.trim()) {
                    "online" -> it.copy(online = true)
                    "offline" -> it.copy(online = false)
                    else -> it
                }
            }
            t.data -> {
                val parsed = CoolerStateParser.parse(m.payload)
                if (parsed == null) {
                    onIgnored("ignored ${m.topic}: not cooler/data v${CoolerStateParser.VERSION}")
                    return
                }
                update { cur ->
                    var trend = cur.trend
                    if (m.retained || trend.all.isEmpty()) trend = trend.seeded(parsed)
                    if (!m.retained) trend = trend.appended(now / 1_000, parsed.temp, parsed.relay)
                    cur.copy(
                        state = parsed,
                        stateLiveAtMillis = if (m.retained) null else now,
                        lastLiveDataAtMillis = if (m.retained) cur.lastLiveDataAtMillis else now,
                        trend = trend,
                    )
                }
            }
        }
    }

    private fun edit(key: String, value: Int) {
        update { it.copy(drafts = it.drafts + (key to value)) }
        draftJobs[key]?.cancel()
        draftJobs[key] = scope.launch {
            delay(DEBOUNCE_MS)
            val s = state.value
            val v = s.drafts[key] ?: return@launch
            // A burst that ends where it started asks the controller for nothing.
            if (v == s.state?.settings?.get(key) && key !in s.pending) {
                update { it.copy(drafts = it.drafts - key) }
                return@launch
            }
            send(key, v)
        }
    }

    private fun send(key: String, value: Int) {
        val sent = state.value.health.controlsEnabled && transport.publish(commands.setting(key, value))
        update {
            val cleared = it.copy(drafts = it.drafts - key)
            if (sent) cleared.copy(pending = cleared.pending + (key to Pending(value, clock())))
            else cleared.copy(message = NOT_SENT)
        }
    }

    private fun act(publish: Publish) {
        if (!state.value.health.controlsEnabled || !transport.publish(publish)) {
            update { it.copy(message = NOT_SENT) }
        }
    }

    /** Applies [change], then recomputes health and resolves pending commands. */
    private fun update(change: (UiState) -> UiState) {
        state.update { current ->
            val next = change(current)
            val now = clock()
            val r = PendingCommands.resolve(next.pending, next.state, next.stateLiveAtMillis, now)
            next.copy(
                health = Liveness.evaluate(next.link, next.online, next.state, next.lastLiveDataAtMillis, now),
                pending = r.pending,
                message = if (r.timedOut.isEmpty()) next.message
                else "Not applied: " + r.timedOut.joinToString { key -> Bounds.find(key)?.label ?: key },
            )
        }
    }

    companion object {
        const val DEBOUNCE_MS = 400L
        private const val NOT_SENT = "Couldn't send: controller not reachable"
    }
}
```

- [ ] **Step 5: Run the tests to see them pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.CoolerControllerTest' --tests 'ai.northtrail.cooler.model.PendingCommandsTest'`
Expected: PASS (16 + 5 tests). Then run the whole suite: `./gradlew testDebugUnitTest` — all PASS.

- [ ] **Step 6: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler app/src/test/java/ai/northtrail/cooler
git commit -m "feat: controller folds cooler messages into UI state; debounced, confirmed settings" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 8: MQTT session, repository, config store and setup probe

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/data/HiveMqSession.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/data/BrokerProbe.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/data/ConfigStore.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/data/MqttRepository.kt`
- Test: `app/src/test/java/ai/northtrail/cooler/data/FakeBroker.kt`, `HiveMqSessionTest.kt`, `BrokerProbeTest.kt` (same test directory)

**Interfaces:**
- Consumes: `CoolerTransport`, `IncomingMessage` (Task 7); `LinkState`, `LinkStatus` (Task 6); `BrokerConfig`, `Publish` (Task 4); `Topics` (Task 2).
- Produces:
  - `class HiveMqSession(host: String, port: Int, username: String, password: ByteArray, clientId: String, trustManagerFactory: TrustManagerFactory?, subscriptions: List<String>, onLink: (LinkState, String, Long?) -> Unit, onMessage: (IncomingMessage) -> Unit) { val clientState: MqttClientState; fun start(); fun stop(); fun publish(publish: Publish): Boolean }` — plain JVM; `trustManagerFactory == null` means plain TCP (tests only).
  - `internal fun safeMessage(error: Throwable): String`
  - `sealed interface ProbeResult { data object Ok; data object LoginRefused; data class TlsFailed(val reason: String); data object Unreachable; data class NoData(val topic: String) }`, `fun ProbeResult.message(): String?`
  - `object BrokerProbe { const val TIMEOUT_MS = 10_000L; suspend fun run(config: BrokerConfig, trust: TrustManagerFactory?, clientId: String, timeoutMs: Long = TIMEOUT_MS): ProbeResult }`
  - `class ConfigStore(context: Context) { fun load(): BrokerConfig; fun isConfigured(): Boolean; fun save(config: BrokerConfig); fun uiId(): String }`
  - `class MqttRepository(context: Context, configStore: ConfigStore) : CoolerTransport { fun saveAndConnect(config: BrokerConfig); suspend fun probe(config: BrokerConfig): ProbeResult }`

`HiveMqSession` is spa-android's session with two changes: it subscribes to a **list** of exact topics in one SUBSCRIBE, and a failed TLS handshake is reported as `TLS_FAILED` (not `REJECTED`). A SUBACK failure (broker ACL refusing a topic) is reported as `REJECTED`. `ConfigStore` and `MqttRepository` are spa-android's with the cooler's names; they need an Android `Context` and are exercised by the bench check in Task 12.

- [ ] **Step 1: Write the fake broker used by the tests**

`app/src/test/java/ai/northtrail/cooler/data/FakeBroker.kt`:
```kotlin
package ai.northtrail.cooler.data

import java.io.InputStream
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

/**
 * Just enough of an MQTT 3.1.1 broker for tests: answers CONNECT with [connackCode],
 * records SUBSCRIBE topic filters, grants them, and optionally publishes one
 * message (QoS 0) right after the SUBACK.
 */
class FakeBroker(
    private val connackCode: Int = 0,
    private val publishAfterSubscribe: Pair<String, String>? = null,
) : AutoCloseable {
    private val server = ServerSocket(0)
    val port: Int get() = server.localPort
    val accepted = AtomicInteger()
    val subscribedTopics = CopyOnWriteArrayList<String>()

    init {
        thread(isDaemon = true) {
            while (!server.isClosed) {
                val socket = runCatching { server.accept() }.getOrNull() ?: break
                accepted.incrementAndGet()
                thread(isDaemon = true) { runCatching { serve(socket) } }
            }
        }
    }

    private fun serve(socket: Socket) = socket.use { s ->
        val input = s.getInputStream()
        val out = s.getOutputStream()
        readPacket(input) ?: return@use // CONNECT
        out.write(byteArrayOf(0x20, 0x02, 0x00, connackCode.toByte()))
        out.flush()
        if (connackCode != 0) {
            Thread.sleep(300)
            return@use
        }
        while (true) {
            val (type, body) = readPacket(input) ?: break
            when (type) {
                0x80 -> { // SUBSCRIBE: packet id, then (length, topic, qos)*
                    val topics = mutableListOf<String>()
                    var i = 2
                    while (i + 2 <= body.size) {
                        val len = ((body[i].toInt() and 0xFF) shl 8) or (body[i + 1].toInt() and 0xFF)
                        topics += String(body, i + 2, len, Charsets.UTF_8)
                        i += 2 + len + 1
                    }
                    subscribedTopics += topics
                    out.write(byteArrayOf(0x90.toByte(), (2 + topics.size).toByte(), body[0], body[1]))
                    out.write(ByteArray(topics.size) { 0x01 })
                    publishAfterSubscribe?.let { (topic, payload) -> out.write(publishPacket(topic, payload)) }
                }
                0xC0 -> out.write(byteArrayOf(0xD0.toByte(), 0x00)) // PINGREQ -> PINGRESP
                0xE0 -> break // DISCONNECT
            }
            out.flush()
        }
    }

    private fun readPacket(input: InputStream): Pair<Int, ByteArray>? {
        val header = input.read()
        if (header < 0) return null
        var length = 0
        var multiplier = 1
        while (true) {
            val b = input.read()
            if (b < 0) return null
            length += (b and 0x7F) * multiplier
            if (b and 0x80 == 0) break
            multiplier *= 128
        }
        val body = ByteArray(length)
        var off = 0
        while (off < length) {
            val r = input.read(body, off, length - off)
            if (r < 0) return null
            off += r
        }
        return (header and 0xF0) to body
    }

    private fun publishPacket(topic: String, payload: String): ByteArray {
        val t = topic.toByteArray()
        val body = byteArrayOf((t.size shr 8).toByte(), t.size.toByte()) + t + payload.toByteArray()
        return byteArrayOf(0x30) + remainingLength(body.size) + body
    }

    private fun remainingLength(n: Int): ByteArray {
        val out = mutableListOf<Byte>()
        var x = n
        do {
            var b = x % 128
            x /= 128
            if (x > 0) b = b or 0x80
            out += b.toByte()
        } while (x > 0)
        return out.toByteArray()
    }

    override fun close() = server.close()
}

fun waitUntil(timeoutMs: Long = 5_000, condition: () -> Boolean) {
    val end = System.currentTimeMillis() + timeoutMs
    while (!condition() && System.currentTimeMillis() < end) Thread.sleep(20)
}
```

- [ ] **Step 2: Write the failing session and probe tests**

`app/src/test/java/ai/northtrail/cooler/data/HiveMqSessionTest.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.Topics
import com.hivemq.client.mqtt.MqttClientState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.ServerSocket
import java.util.concurrent.CopyOnWriteArrayList

class HiveMqSessionTest {
    private val links = CopyOnWriteArrayList<LinkState>()
    private val messages = CopyOnWriteArrayList<IncomingMessage>()

    private fun session(port: Int) = HiveMqSession(
        host = "127.0.0.1",
        port = port,
        username = "app-user",
        password = "x".toByteArray(),
        clientId = "test-${System.nanoTime()}",
        trustManagerFactory = null,
        subscriptions = Topics().subscriptions,
        onLink = { state, _, _ -> links += state },
        onMessage = { messages += it },
    )

    @Test
    fun subscribesToExactlyTheTwoCoolerTopics() {
        FakeBroker().use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { LinkState.CONNECTED in links }
            s.stop()
            assertTrue("links seen: $links", LinkState.CONNECTED in links)
            assertEquals(listOf("cooler/data", "cooler/availability"), broker.subscribedTopics.toList())
        }
    }

    @Test
    fun deliversMessagesWithTheirTopic() {
        FakeBroker(publishAfterSubscribe = "cooler/data" to """{"v":2}""").use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { messages.isNotEmpty() }
            s.stop()
            assertEquals(IncomingMessage("cooler/data", """{"v":2}""", retained = false), messages.first())
        }
    }

    @Test
    fun rejectedCredentialsReportRejectedAndDoNotRetry() {
        FakeBroker(connackCode = 5).use { broker ->
            val s = session(broker.port)
            s.start()
            Thread.sleep(4_000)
            assertTrue("links seen: $links", LinkState.REJECTED in links)
            assertEquals("connection attempts", 1, broker.accepted.get())
            assertEquals(MqttClientState.DISCONNECTED, s.clientState)
        }
    }

    @Test
    fun stoppedSessionStopsReconnectingWhileTheBrokerIsUnreachable() {
        val closedPort = ServerSocket(0).use { it.localPort }
        val s = session(closedPort)
        s.start()
        Thread.sleep(1_500) // first attempt fails; an automatic reconnect is now scheduled
        s.stop()
        Thread.sleep(5_000) // long enough for that attempt to run
        assertEquals(MqttClientState.DISCONNECTED, s.clientState)
    }
}
```

`app/src/test/java/ai/northtrail/cooler/data/BrokerProbeTest.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.net.ServerSocket

class BrokerProbeTest {
    private fun config(port: Int) =
        BrokerConfig(host = "127.0.0.1", port = port, username = "app-user", password = "x")

    private fun probe(port: Int, timeoutMs: Long = 5_000) = runBlocking {
        BrokerProbe.run(config(port), trust = null, clientId = "probe-${System.nanoTime()}", timeoutMs = timeoutMs)
    }

    @Test
    fun okWhenCoolerDataArrives() {
        FakeBroker(publishAfterSubscribe = "cooler/data" to """{"v":2}""").use {
            assertEquals(ProbeResult.Ok, probe(it.port))
        }
    }

    @Test
    fun loginRefusedOnBadCredentials() {
        FakeBroker(connackCode = 5).use {
            assertEquals(ProbeResult.LoginRefused, probe(it.port))
        }
    }

    @Test
    fun unreachableWhenNothingListens() {
        val closedPort = ServerSocket(0).use { it.localPort }
        assertEquals(ProbeResult.Unreachable, probe(closedPort))
    }

    @Test
    fun noDataWhenBrokerIsSilent() {
        FakeBroker().use {
            assertEquals(ProbeResult.NoData("cooler/data"), probe(it.port, timeoutMs = 1_500))
        }
    }

    @Test
    fun eachResultHasAMessage() {
        assertNull(ProbeResult.Ok.message())
        assertEquals("Login refused: check the username, password and broker ACL", ProbeResult.LoginRefused.message())
        assertEquals("Can't verify the broker (expired)", ProbeResult.TlsFailed("expired").message())
        assertEquals("Can't reach the broker", ProbeResult.Unreachable.message())
        assertEquals("Connected, but no cooler data on cooler/data", ProbeResult.NoData("cooler/data").message())
    }
}
```

- [ ] **Step 3: Run the tests to see them fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.data.*'`
Expected: compilation FAILS (`Unresolved reference: HiveMqSession`, `BrokerProbe`, `ProbeResult`).

- [ ] **Step 4: Implement the session**

`app/src/main/java/ai/northtrail/cooler/data/HiveMqSession.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.Publish
import com.hivemq.client.mqtt.MqttClientState
import com.hivemq.client.mqtt.MqttGlobalPublishFilter
import com.hivemq.client.mqtt.datatypes.MqttQos
import com.hivemq.client.mqtt.mqtt3.Mqtt3AsyncClient
import com.hivemq.client.mqtt.mqtt3.Mqtt3Client
import com.hivemq.client.mqtt.mqtt3.exceptions.Mqtt3ConnAckException
import com.hivemq.client.mqtt.mqtt3.message.connect.connack.Mqtt3ConnAckReturnCode
import com.hivemq.client.mqtt.mqtt3.message.subscribe.Mqtt3Subscribe
import com.hivemq.client.mqtt.mqtt3.message.subscribe.Mqtt3Subscription
import com.hivemq.client.mqtt.mqtt3.message.subscribe.suback.Mqtt3SubAckReturnCode
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLHandshakeException
import javax.net.ssl.TrustManagerFactory

/**
 * One MQTT client lifetime. Plain JVM (no Android types) so its reconnect,
 * subscribe and shutdown behaviour can be tested against a local socket.
 */
class HiveMqSession(
    host: String,
    port: Int,
    private val username: String,
    private val password: ByteArray,
    clientId: String,
    trustManagerFactory: TrustManagerFactory?,
    private val subscriptions: List<String>,
    private val onLink: (LinkState, String, Long?) -> Unit,
    private val onMessage: (IncomingMessage) -> Unit,
) {
    @Volatile private var stopped = false
    private val client: Mqtt3AsyncClient

    init {
        var builder = Mqtt3Client.builder()
            .identifier(clientId)
            .serverHost(host)
            .serverPort(port)
        if (trustManagerFactory != null) {
            builder = builder.sslConfig().trustManagerFactory(trustManagerFactory).applySslConfig()
        }
        client = builder
            .automaticReconnect()
                .initialDelay(1, TimeUnit.SECONDS)
                .maxDelay(60, TimeUnit.SECONDS)
                .applyAutomaticReconnect()
            .addConnectedListener {
                // A stopped session that still managed to (re)connect must hang up.
                if (stopped) client.disconnect() else subscribe()
            }
            .addDisconnectedListener { context ->
                // disconnect() cannot cancel a scheduled automatic reconnect; only the
                // listener can. Without this a stopped session retries forever.
                if (stopped) {
                    context.reconnector.reconnect(false)
                    return@addDisconnectedListener
                }
                val fatal = fatal(context.cause)
                if (fatal != null) {
                    // Wrong password, ACL or certificate: retrying only hammers the broker.
                    stopped = true
                    context.reconnector.reconnect(false)
                    onLink(fatal.first, fatal.second, null)
                } else {
                    onLink(LinkState.DISCONNECTED, "Connection lost | retrying", null)
                }
            }
            .buildAsync()
        client.publishes(MqttGlobalPublishFilter.ALL) { publish ->
            if (stopped) return@publishes
            onMessage(
                IncomingMessage(
                    topic = publish.topic.toString(),
                    payload = publish.payloadAsBytes.toString(Charsets.UTF_8),
                    retained = publish.isRetain,
                ),
            )
        }
    }

    val clientState: MqttClientState get() = client.state

    fun start() {
        onLink(LinkState.CONNECTING, "Connecting with TLS", null)
        client.connectWith()
            .cleanSession(true)
            .keepAlive(30)
            .simpleAuth()
                .username(username)
                .password(password)
                .applySimpleAuth()
            .send()
            .whenComplete { _, error ->
                if (stopped || error == null) return@whenComplete
                val fatal = fatal(error)
                if (fatal != null) onLink(fatal.first, fatal.second, null)
                else onLink(LinkState.DISCONNECTED, "Connect failed: ${safeMessage(error)}", null)
            }
    }

    private fun subscribe() {
        val request = Mqtt3Subscribe.builder()
            .addSubscriptions(
                subscriptions.map { Mqtt3Subscription.builder().topicFilter(it).qos(MqttQos.AT_LEAST_ONCE).build() },
            )
            .build()
        client.subscribe(request).whenComplete { ack, error ->
            if (stopped) return@whenComplete
            when {
                error != null ->
                    onLink(LinkState.DISCONNECTED, "Subscription failed: ${safeMessage(error)}", null)
                ack.returnCodes.any { it == Mqtt3SubAckReturnCode.FAILURE } ->
                    onLink(LinkState.REJECTED, "Broker refused the subscription (check the ACL)", null)
                else ->
                    onLink(LinkState.CONNECTED, "Connected | TLS", System.currentTimeMillis())
            }
        }
    }

    fun stop() {
        stopped = true
        client.disconnect()
    }

    fun publish(publish: Publish): Boolean {
        if (stopped || !client.state.isConnected) return false
        return runCatching {
            client.publishWith()
                .topic(publish.topic)
                .qos(MqttQos.AT_LEAST_ONCE)
                .payload(publish.payload.toByteArray())
                .retain(false)
                .send()
            true
        }.getOrDefault(false)
    }
}

private fun fatal(cause: Throwable): Pair<LinkState, String>? {
    for (e in generateSequence(cause) { it.cause }) {
        if (e is Mqtt3ConnAckException) {
            val code = e.mqttMessage.returnCode
            if (code == Mqtt3ConnAckReturnCode.BAD_USER_NAME_OR_PASSWORD ||
                code == Mqtt3ConnAckReturnCode.NOT_AUTHORIZED
            ) return LinkState.REJECTED to "Broker refused the login"
        }
        if (e is SSLHandshakeException) return LinkState.TLS_FAILED to safeMessage(e)
    }
    return null
}

internal fun safeMessage(error: Throwable): String =
    error.message?.replace(Regex("(?i)(password|username)=[^, ]+"), "$1=[hidden]")
        ?.take(100) ?: error.javaClass.simpleName
```

If `addSubscriptions(Collection)` does not resolve on this HiveMQ version, build the request with one `.addSubscription(Mqtt3Subscription)` call per topic instead (the builder returns `Mqtt3SubscribeBuilder.Complete` after the first); do not fall back to a wildcard.

- [ ] **Step 5: Implement the probe**

`app/src/main/java/ai/northtrail/cooler/data/BrokerProbe.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.Topics
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean
import javax.net.ssl.TrustManagerFactory

sealed interface ProbeResult {
    data object Ok : ProbeResult
    data object LoginRefused : ProbeResult
    data class TlsFailed(val reason: String) : ProbeResult
    data object Unreachable : ProbeResult
    data class NoData(val topic: String) : ProbeResult
}

/** What Setup shows; null means the broker answered with cooler data. */
fun ProbeResult.message(): String? = when (this) {
    ProbeResult.Ok -> null
    ProbeResult.LoginRefused -> "Login refused: check the username, password and broker ACL"
    is ProbeResult.TlsFailed -> "Can't verify the broker ($reason)"
    ProbeResult.Unreachable -> "Can't reach the broker"
    is ProbeResult.NoData -> "Connected, but no cooler data on $topic"
}

/**
 * Setup's "Test & save": connect with the entered values and wait for the
 * retained `<base>/data`, so a wrong password, ACL or topic base is caught
 * before anything is saved.
 */
object BrokerProbe {
    const val TIMEOUT_MS = 10_000L

    suspend fun run(
        config: BrokerConfig,
        trust: TrustManagerFactory?,
        clientId: String,
        timeoutMs: Long = TIMEOUT_MS,
    ): ProbeResult {
        val topics = Topics(config.base)
        val result = CompletableDeferred<ProbeResult>()
        val subscribed = AtomicBoolean(false)
        val session = HiveMqSession(
            host = config.host.trim(),
            port = config.port,
            username = config.username.trim(),
            password = config.password.toByteArray(),
            clientId = clientId,
            trustManagerFactory = trust,
            subscriptions = topics.subscriptions,
            onLink = { state, detail, _ ->
                when (state) {
                    LinkState.CONNECTED -> subscribed.set(true)
                    LinkState.REJECTED -> result.complete(ProbeResult.LoginRefused)
                    LinkState.TLS_FAILED -> result.complete(ProbeResult.TlsFailed(detail))
                    LinkState.DISCONNECTED -> result.complete(ProbeResult.Unreachable)
                    LinkState.CONNECTING -> Unit
                }
            },
            onMessage = { if (it.topic == topics.data) result.complete(ProbeResult.Ok) },
        )
        session.start()
        return try {
            withTimeoutOrNull(timeoutMs) { result.await() }
                ?: if (subscribed.get()) ProbeResult.NoData(topics.data) else ProbeResult.Unreachable
        } finally {
            session.stop()
        }
    }
}
```

- [ ] **Step 6: Run the tests to see them pass**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.data.*'`
Expected: PASS (4 + 5 tests; the session tests take ~15 s because they wait on real sockets).

- [ ] **Step 7: Implement the config store**

`app/src/main/java/ai/northtrail/cooler/data/ConfigStore.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Broker settings in private prefs; the password AES-GCM encrypted with an Android Keystore key. */
class ConfigStore(context: Context) {
    private val preferences = context.getSharedPreferences("cooler_config", Context.MODE_PRIVATE)

    fun load(): BrokerConfig {
        val defaults = BrokerConfig()
        return BrokerConfig(
            host = preferences.getString("host", defaults.host).orEmpty(),
            port = preferences.getInt("port", defaults.port),
            username = preferences.getString("username", defaults.username).orEmpty(),
            password = decryptPassword(),
            base = preferences.getString("base", defaults.base).orEmpty(),
        )
    }

    fun isConfigured(): Boolean = preferences.getBoolean("configured", false) && load().isUsable

    fun save(config: BrokerConfig) {
        require(config.isUsable) { "Broker configuration is incomplete" }
        val (ciphertext, iv) = encrypt(config.password)
        preferences.edit()
            .putString("host", config.host.trim())
            .putInt("port", config.port)
            .putString("username", config.username.trim())
            .putString("base", config.base.trim().trimEnd('/'))
            .putString("password_ciphertext", ciphertext)
            .putString("password_iv", iv)
            .putBoolean("configured", true)
            .apply()
    }

    /** Stable per-install id, so the broker sees one client per phone. */
    fun uiId(): String {
        val existing = preferences.getString("ui_id", null)
        if (!existing.isNullOrBlank()) return existing
        val created = "android-${UUID.randomUUID().toString().replace("-", "").take(12)}"
        preferences.edit().putString("ui_id", created).apply()
        return created
    }

    private fun encrypt(cleartext: String): Pair<String, String> {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        return Base64.encodeToString(cipher.doFinal(cleartext.toByteArray()), Base64.NO_WRAP) to
            Base64.encodeToString(cipher.iv, Base64.NO_WRAP)
    }

    private fun decryptPassword(): String {
        val ciphertext = preferences.getString("password_ciphertext", null) ?: return ""
        val iv = preferences.getString("password_iv", null) ?: return ""
        return runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(128, Base64.decode(iv, Base64.NO_WRAP)))
            String(cipher.doFinal(Base64.decode(ciphertext, Base64.NO_WRAP)))
        }.getOrDefault("")
    }

    private fun secretKey(): SecretKey {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return generator.generateKey()
    }

    private companion object {
        const val KEY_ALIAS = "cooler_mqtt_password"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
```

- [ ] **Step 8: Implement the repository**

`app/src/main/java/ai/northtrail/cooler/data/MqttRepository.kt`:
```kotlin
package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Topics
import android.content.Context
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.security.KeyStore
import java.security.cert.CertificateFactory
import javax.net.ssl.TrustManagerFactory

class MqttRepository(
    private val context: Context,
    private val configStore: ConfigStore,
) : CoolerTransport {
    private val mutableLink = MutableStateFlow(LinkStatus())
    override val link: StateFlow<LinkStatus> = mutableLink.asStateFlow()
    private val mutableMessages = MutableSharedFlow<IncomingMessage>(
        extraBufferCapacity = 256,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    override val messages: SharedFlow<IncomingMessage> = mutableMessages.asSharedFlow()

    @Volatile private var session: HiveMqSession? = null

    @Synchronized
    fun saveAndConnect(config: BrokerConfig) {
        configStore.save(config)
        disconnect()
        connect()
    }

    @Synchronized
    override fun connect() {
        val config = configStore.load()
        if (!config.isUsable) return
        if (session?.clientState?.isConnectedOrReconnect == true) return
        // Never drop a session without stopping it: an abandoned client keeps
        // reconnecting with the same client id and knocks the live one off.
        session?.stop()
        session = null
        val trust = try {
            createTrustManagerFactory()
        } catch (error: Throwable) {
            mutableLink.value = LinkStatus(LinkState.TLS_FAILED, "TLS setup failed: ${safeMessage(error)}")
            return
        }
        lateinit var created: HiveMqSession
        created = HiveMqSession(
            host = config.host,
            port = config.port,
            username = config.username,
            password = config.password.toByteArray(),
            clientId = "cooler-${configStore.uiId()}",
            trustManagerFactory = trust,
            subscriptions = Topics(config.base).subscriptions,
            // A superseded session must never overwrite the current link state.
            onLink = { state, detail, at -> setLinkFrom(created, state, detail, at) },
            onMessage = { if (session === created) mutableMessages.tryEmit(it) },
        )
        session = created
        created.start()
    }

    @Synchronized
    override fun disconnect() {
        session?.stop()
        session = null
        mutableLink.value = LinkStatus(LinkState.DISCONNECTED, "Disconnected")
    }

    override fun publish(publish: Publish): Boolean = session?.publish(publish) ?: false

    /** Setup's "Test & save", with the same trust as the real connection. */
    suspend fun probe(config: BrokerConfig): ProbeResult {
        val trust = try {
            createTrustManagerFactory()
        } catch (error: Throwable) {
            return ProbeResult.TlsFailed(safeMessage(error))
        }
        return BrokerProbe.run(config, trust, clientId = "cooler-probe-${configStore.uiId()}")
    }

    @Synchronized
    private fun setLinkFrom(source: HiveMqSession, state: LinkState, detail: String, connectedAt: Long?) {
        if (session !== source) return
        mutableLink.value = LinkStatus(state, detail, connectedAt)
    }

    /**
     * Trusts only `assets/broker_ca.pem` when it is bundled (the broker's private
     * CA), otherwise the system trust store. TLS is used either way.
     */
    private fun createTrustManagerFactory(): TrustManagerFactory {
        val keyStore = if (BROKER_CA_ASSET in context.assets.list("").orEmpty()) {
            val certificate = context.assets.open(BROKER_CA_ASSET).use { stream ->
                CertificateFactory.getInstance("X.509").generateCertificate(stream)
            }
            KeyStore.getInstance(KeyStore.getDefaultType()).apply {
                load(null)
                setCertificateEntry("broker-ca", certificate)
            }
        } else {
            null
        }
        return TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply {
            init(keyStore)
        }
    }

    private companion object {
        const val BROKER_CA_ASSET = "broker_ca.pem"
    }
}
```

- [ ] **Step 9: Build and run the whole unit suite**

Run: `./gradlew assembleDebug testDebugUnitTest`
Expected: `BUILD SUCCESSFUL`, all tests PASS.

- [ ] **Step 10: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/data app/src/test/java/ai/northtrail/cooler/data
git commit -m "feat(data): TLS MQTT session on the two cooler topics, setup probe, Keystore config" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 9: App shell, Setup and the Status tab

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/CoolerApplication.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/CoolerViewModel.kt`
- Modify: `app/src/main/java/ai/northtrail/cooler/MainActivity.kt` (replace the placeholder)
- Modify: `app/src/main/AndroidManifest.xml` (add `android:name=".CoolerApplication"`)
- Create: `app/src/main/java/ai/northtrail/cooler/ui/Theme.kt`, `StatusStrip.kt`, `TrendChart.kt`, `StatusScreen.kt`, `SetupScreen.kt`, `CoolerApp.kt`
- Test: `app/src/androidTest/java/ai/northtrail/cooler/ui/StatusScreenTest.kt`, `SetupScreenTest.kt`

**Interfaces:**
- Consumes: `CoolerController`, `UiState` (Task 7); `MqttRepository`, `ConfigStore`, `ProbeResult.message()` (Task 8); `StatusText`, `Chip` (Task 3); `Health`, `LinkView`, `Banner` (Task 6); `Sample` (Task 5); `BrokerConfig`, `Topics` (Tasks 2, 4).
- Produces:
  - `class CoolerApplication : Application() { val configStore: ConfigStore; val repository: MqttRepository }`
  - `class CoolerViewModel(application: Application) : AndroidViewModel { val ui: StateFlow<UiState>; val setup: StateFlow<SetupUi>; val brokerConfig: BrokerConfig; fun connect(); fun disconnect(); fun testAndSave(config: BrokerConfig, onSaved: () -> Unit); fun stepSetting(key: String, direction: Int); fun chooseSetting(key: String, value: Int); fun startCalibration(); fun abortCalibration(); fun resetFinCal(); fun consumeMessage() }`
  - `data class SetupUi(val probing: Boolean = false, val error: String? = null)` (in `ui/SetupScreen.kt`)
  - `object CoolerColors`, `@Composable fun CoolerTheme(content)`, `@Composable fun StatusStrip(health: Health, modifier: Modifier = Modifier)`, `@Composable fun Banners(banners: List<Banner>)`, `@Composable fun TrendChart(samples, fromEpochS, toEpochS, setpoint, range, modifier)`, `@Composable fun StatusScreen(ui: UiState, nowEpochS: Long, modifier: Modifier = Modifier)`, `@Composable fun SetupScreen(initial: BrokerConfig, setup: SetupUi, onTestAndSave: (BrokerConfig) -> Unit, onCancel: (() -> Unit)?)`, `@Composable fun CoolerApp(viewModel: CoolerViewModel)`

After this task the app is usable end to end for monitoring: first run shows Setup; Test & save probes, saves and connects; Status shows the cooler. Settings and Detail tabs arrive in Tasks 10 and 11.

- [ ] **Step 1: Write the failing UI tests**

`app/src/androidTest/java/ai/northtrail/cooler/ui/StatusScreenTest.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Banner
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import org.junit.Rule
import org.junit.Test

class StatusScreenTest {
    @get:Rule val rule = createComposeRule()

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 12_000L)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme { StatusScreen(ui, nowEpochS = 1_790_640_000L) }
    }

    @Test
    fun showsTemperatureStateAndSwitch() {
        val s = CoolerState(
            temp = 4.75, humidity = 78.2, finTemp = 1.2, mode = "normal", overrideSrc = "switch",
            state = "cooling", relay = true, holdS = 38, compressor = 0,
            settings = mapOf("coolerset" to 4, "range" to 2),
        )
        show(UiState(configured = true, state = s, health = online))
        rule.onNodeWithText("4.8 °C").assertExists()
        rule.onNodeWithText("78 %RH").assertExists()
        rule.onNodeWithText("set 4 ±2 · on >6 off <2").assertExists()
        rule.onNodeWithText("NORMAL").assertExists()
        rule.onNodeWithText("Cooling • min run 38s").assertExists()
        rule.onNodeWithText("SWITCH ON").assertExists()
        rule.onNodeWithText("Coil 1.2 °C").assertExists()
        rule.onNodeWithText("Compressor Starting").assertExists()
        rule.onNodeWithText("Online · updated 12s ago", substring = true).assertExists()
    }

    @Test
    fun showsBannersAndDashesWithoutData() {
        show(
            UiState(
                configured = true,
                health = Health(LinkView.CONTROLLER_OFFLINE, "Controller offline", listOf(Banner.CONTROLLER_OFFLINE), null),
            ),
        )
        rule.onAllNodesWithText("Controller offline", substring = true).assertCountEquals(2) // strip + banner
        rule.onNodeWithText("-- °C").assertExists()
        rule.onNodeWithText("No history yet").assertExists()
    }
}
```

`app/src/androidTest/java/ai/northtrail/cooler/ui/SetupScreenTest.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.BrokerConfig
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SetupScreenTest {
    @get:Rule val rule = createComposeRule()
    private var saved: BrokerConfig? = null

    private fun show(setup: SetupUi = SetupUi()) = rule.setContent {
        CoolerTheme {
            SetupScreen(BrokerConfig(username = "app-user"), setup, onTestAndSave = { saved = it }, onCancel = null)
        }
    }

    @Test
    fun testAndSaveNeedsAPassword() {
        show()
        rule.onNodeWithText("Test & save").assertIsNotEnabled()
        rule.onNodeWithText("Password").performTextInput("secret")
        rule.onNodeWithText("Test & save").assertIsEnabled().performClick()
        assertEquals(BrokerConfig(username = "app-user", password = "secret"), saved)
    }

    @Test
    fun showsTheProbeError() {
        show(SetupUi(error = "Can't reach the broker"))
        rule.onNodeWithText("Can't reach the broker").assertExists()
    }

    @Test
    fun buttonIsBusyWhileTesting() {
        show(SetupUi(probing = true))
        rule.onNodeWithText("Testing…").assertIsNotEnabled()
    }
}
```

- [ ] **Step 2: Confirm the UI tests fail to compile**

Run: `./gradlew assembleDebugAndroidTest`
Expected: FAILS (`Unresolved reference: CoolerTheme`, `StatusScreen`, `SetupScreen`, `SetupUi`).

- [ ] **Step 3: Application, view model and activity**

`app/src/main/java/ai/northtrail/cooler/CoolerApplication.kt`:
```kotlin
package ai.northtrail.cooler

import ai.northtrail.cooler.data.ConfigStore
import ai.northtrail.cooler.data.MqttRepository
import android.app.Application

class CoolerApplication : Application() {
    lateinit var configStore: ConfigStore
        private set
    lateinit var repository: MqttRepository
        private set

    override fun onCreate() {
        super.onCreate()
        configStore = ConfigStore(this)
        repository = MqttRepository(this, configStore)
    }
}
```

`app/src/main/java/ai/northtrail/cooler/CoolerViewModel.kt`:
```kotlin
package ai.northtrail.cooler

import ai.northtrail.cooler.data.message
import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.Topics
import ai.northtrail.cooler.ui.SetupUi
import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class CoolerViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as CoolerApplication

    private val controller = CoolerController(
        transport = app.repository,
        topics = Topics(app.configStore.load().base),
        scope = viewModelScope,
        clock = System::currentTimeMillis,
        configured = app.configStore.isConfigured(),
        onIgnored = { Log.w(TAG, it) },
    ).also { it.start() }

    val ui: StateFlow<UiState> = controller.ui

    private val setupState = MutableStateFlow(SetupUi())
    val setup: StateFlow<SetupUi> = setupState.asStateFlow()

    /** The saved broker settings, minus the password (it is never shown again). */
    val brokerConfig: BrokerConfig get() = app.configStore.load().copy(password = "")

    fun connect() = app.repository.connect()
    fun disconnect() = app.repository.disconnect()

    /** Probe first; save and connect only if the broker answered with cooler data. */
    fun testAndSave(config: BrokerConfig, onSaved: () -> Unit) {
        if (setupState.value.probing) return
        setupState.value = SetupUi(probing = true)
        viewModelScope.launch {
            val error = app.repository.probe(config).message()
            if (error == null) {
                val newTopics = Topics(config.base)
                if (newTopics.data != Topics(app.configStore.load().base).data) controller.reset(newTopics)
                app.repository.saveAndConnect(config)
                controller.markConfigured()
                onSaved()
            }
            setupState.value = SetupUi(probing = false, error = error)
        }
    }

    fun stepSetting(key: String, direction: Int) = controller.stepSetting(key, direction)
    fun chooseSetting(key: String, value: Int) = controller.chooseSetting(key, value)
    fun startCalibration() = controller.startCalibration()
    fun abortCalibration() = controller.abortCalibration()
    fun resetFinCal() = controller.resetFinCal()
    fun consumeMessage() = controller.consumeMessage()

    private companion object {
        const val TAG = "Cooler"
    }
}
```

`app/src/main/java/ai/northtrail/cooler/MainActivity.kt` (replace the whole file):
```kotlin
package ai.northtrail.cooler

import ai.northtrail.cooler.ui.CoolerApp
import ai.northtrail.cooler.ui.CoolerTheme
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels

class MainActivity : ComponentActivity() {
    private val viewModel: CoolerViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { CoolerTheme { CoolerApp(viewModel) } }
    }

    // Connected only while visible: no background service (spec §7).
    override fun onStart() {
        super.onStart()
        viewModel.connect()
    }

    override fun onStop() {
        viewModel.disconnect()
        super.onStop()
    }
}
```

In `app/src/main/AndroidManifest.xml`, change `<application` to `<application android:name=".CoolerApplication"` (keep every other attribute).

- [ ] **Step 4: Theme, strip and banners**

`app/src/main/java/ai/northtrail/cooler/ui/Theme.kt`:
```kotlin
package ai.northtrail.cooler.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object CoolerColors {
    val Background = Color(0xFF0F1720)
    val Surface = Color(0xFF1A2532)
    val Text = Color(0xFFE6EDF3)
    val Muted = Color(0xFF8B98A5)
    val Accent = Color(0xFF78C8FF)
    val Warn = Color(0xFFFFB74D)
    val WarnBackground = Color(0xFF3A2A10)
    val Ok = Color(0xFF7EE787)
    val OkBackground = Color(0xFF12301F)
    val Bad = Color(0xFFFF8A80)
    val BadBackground = Color(0xFF3A1616)
    val Band = Color(0x3378C8FF)
    val RelayTint = Color(0x2278C8FF)
}

@Composable
fun CoolerTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = CoolerColors.Accent,
            onPrimary = CoolerColors.Background,
            background = CoolerColors.Background,
            surface = CoolerColors.Background,
            surfaceVariant = CoolerColors.Surface,
            onBackground = CoolerColors.Text,
            onSurface = CoolerColors.Text,
        ),
        content = content,
    )
}
```

`app/src/main/java/ai/northtrail/cooler/ui/StatusStrip.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.Banner
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun StatusStrip(health: Health, modifier: Modifier = Modifier) {
    val (fg, bg) = when (health.link) {
        LinkView.ONLINE -> CoolerColors.Ok to CoolerColors.OkBackground
        LinkView.CONNECTING, LinkView.WAITING -> CoolerColors.Muted to CoolerColors.Surface
        else -> CoolerColors.Bad to CoolerColors.BadBackground
    }
    Text(
        "● " + health.stripText(),
        color = fg,
        style = MaterialTheme.typography.labelLarge,
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .background(bg, RoundedCornerShape(12.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
    )
}

@Composable
fun Banners(banners: List<Banner>) {
    banners.forEach { b ->
        Text(
            b.text,
            color = CoolerColors.Bad,
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp)
                .background(CoolerColors.BadBackground, RoundedCornerShape(12.dp))
                .padding(12.dp),
        )
    }
}
```

- [ ] **Step 5: Trend chart**

`app/src/main/java/ai/northtrail/cooler/ui/TrendChart.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.Sample
import ai.northtrail.cooler.model.StatusText
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp

/**
 * Box temperature from [fromEpochS] to [toEpochS]: the setpoint band shaded,
 * spans with the relay closed tinted, the temperature as a line.
 */
@Composable
fun TrendChart(
    samples: List<Sample>,
    fromEpochS: Long,
    toEpochS: Long,
    setpoint: Int?,
    range: Int?,
    modifier: Modifier = Modifier,
) {
    if (samples.size < 2) {
        Box(modifier.background(CoolerColors.Surface, RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
            Text("No history yet", color = CoolerColors.Muted)
        }
        return
    }
    val bandLo = if (setpoint != null && range != null) (setpoint - range).toDouble() else null
    val bandHi = if (setpoint != null && range != null) (setpoint + range).toDouble() else null
    val temps = samples.map { it.tempC }
    val yMin = listOfNotNull(temps.min(), bandLo).min() - 1.0
    val yMax = listOfNotNull(temps.max(), bandHi).max() + 1.0
    val span = (toEpochS - fromEpochS).coerceAtLeast(1).toFloat()

    Column(modifier) {
        Row(Modifier.fillMaxWidth()) {
            Text("max ${StatusText.temp(temps.max())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
            Spacer(Modifier.weight(1f))
            Text("last 24 h", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
        }
        Canvas(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(vertical = 4.dp)
                .background(CoolerColors.Surface, RoundedCornerShape(12.dp)),
        ) {
            fun x(t: Long) = (t - fromEpochS) / span * size.width
            fun y(c: Double) = ((yMax - c) / (yMax - yMin)).toFloat() * size.height

            samples.zipWithNext().forEach { (a, b) ->
                if (a.relay == true) {
                    drawRect(CoolerColors.RelayTint, Offset(x(a.epochS), 0f), Size(x(b.epochS) - x(a.epochS), size.height))
                }
            }
            if (bandLo != null && bandHi != null) {
                drawRect(CoolerColors.Band, Offset(0f, y(bandHi)), Size(size.width, y(bandLo) - y(bandHi)))
            }
            val line = Path()
            samples.forEachIndexed { i, s ->
                if (i == 0) line.moveTo(x(s.epochS), y(s.tempC)) else line.lineTo(x(s.epochS), y(s.tempC))
            }
            drawPath(line, CoolerColors.Accent, style = Stroke(width = 2.dp.toPx()))
        }
        Text("min ${StatusText.temp(temps.min())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
    }
}
```

- [ ] **Step 6: Status screen**

`app/src/main/java/ai/northtrail/cooler/ui/StatusScreen.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Chip
import ai.northtrail.cooler.model.StatusText
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

const val TREND_WINDOW_S = 24 * 3_600L

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun StatusScreen(ui: UiState, nowEpochS: Long, modifier: Modifier = Modifier) {
    val s = ui.state
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        Banners(ui.health.banners)
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Text("${StatusText.temp(s?.temp)} °C", style = MaterialTheme.typography.displayLarge)
            Spacer(Modifier.weight(1f))
            Text(
                s?.humidity?.let { "${it.roundToInt()} %RH" } ?: "-- %RH",
                style = MaterialTheme.typography.titleLarge,
                color = CoolerColors.Muted,
            )
        }
        Text(StatusText.setpointLine(s), color = CoolerColors.Muted, modifier = Modifier.padding(horizontal = 16.dp))
        Column(
            Modifier
                .fillMaxWidth()
                .padding(16.dp)
                .background(CoolerColors.Surface, RoundedCornerShape(16.dp))
                .padding(16.dp),
        ) {
            Text(StatusText.stateKey(s), style = MaterialTheme.typography.labelLarge, color = CoolerColors.Accent)
            Text(StatusText.stateValue(s), style = MaterialTheme.typography.headlineSmall)
        }
        FlowRow(
            Modifier.padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            StatusText.chips(s).forEach { ChipView(it) }
        }
        Row(Modifier.fillMaxWidth().padding(16.dp)) {
            Text("Coil ${StatusText.temp(s?.finTemp)} °C", modifier = Modifier.weight(1f))
            Text("Compressor ${StatusText.compressor(s)}")
        }
        TrendChart(
            samples = ui.trend.samples(nowEpochS - TREND_WINDOW_S),
            fromEpochS = nowEpochS - TREND_WINDOW_S,
            toEpochS = nowEpochS,
            setpoint = s?.settings?.get("coolerset"),
            range = s?.settings?.get("range"),
            modifier = Modifier.fillMaxWidth().height(220.dp).padding(16.dp),
        )
    }
}

@Composable
private fun ChipView(chip: Chip) {
    Text(
        chip.text,
        color = if (chip.alert) CoolerColors.Warn else CoolerColors.Muted,
        style = MaterialTheme.typography.labelMedium,
        modifier = Modifier
            .background(if (chip.alert) CoolerColors.WarnBackground else CoolerColors.Surface, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
    )
}
```

- [ ] **Step 7: Setup screen**

`app/src/main/java/ai/northtrail/cooler/ui/SetupScreen.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.BrokerConfig
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

data class SetupUi(val probing: Boolean = false, val error: String? = null)

@Composable
fun SetupScreen(
    initial: BrokerConfig,
    setup: SetupUi,
    onTestAndSave: (BrokerConfig) -> Unit,
    onCancel: (() -> Unit)?,
) {
    var host by remember { mutableStateOf(initial.host) }
    var port by remember { mutableStateOf(initial.port.toString()) }
    var username by remember { mutableStateOf(initial.username) }
    var password by remember { mutableStateOf("") }
    var base by remember { mutableStateOf(initial.base) }
    val config = BrokerConfig(host, port.toIntOrNull() ?: 0, username, password, base)

    Column(Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(24.dp)) {
        Text("Connect to the cooler", style = MaterialTheme.typography.headlineSmall)
        Text("TLS only; the password is stored encrypted on this phone.", style = MaterialTheme.typography.bodySmall)
        OutlinedTextField(host, { host = it }, label = { Text("Broker host") }, singleLine = true,
            modifier = Modifier.fillMaxWidth().padding(top = 16.dp))
        OutlinedTextField(port, { port = it.filter(Char::isDigit) }, label = { Text("Port") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
        OutlinedTextField(username, { username = it }, label = { Text("Username") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        OutlinedTextField(password, { password = it }, label = { Text("Password") }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
        OutlinedTextField(base, { base = it }, label = { Text("Topic base") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        config.problems.firstOrNull()?.let {
            Text(it, color = CoolerColors.Muted, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
        }
        Button(
            onClick = { onTestAndSave(config) },
            enabled = config.isUsable && !setup.probing,
            modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
        ) {
            if (setup.probing) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                    Text("Testing…")
                }
            } else {
                Text("Test & save")
            }
        }
        setup.error?.let { Text(it, color = CoolerColors.Bad, modifier = Modifier.padding(top = 8.dp)) }
        if (onCancel != null) TextButton(onClick = onCancel, modifier = Modifier.fillMaxWidth()) { Text("Cancel") }
    }
}
```

- [ ] **Step 8: App scaffold (Status only for now)**

`app/src/main/java/ai/northtrail/cooler/ui/CoolerApp.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.CoolerViewModel
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle

@Composable
fun CoolerApp(viewModel: CoolerViewModel) {
    val ui by viewModel.ui.collectAsStateWithLifecycle()
    val setup by viewModel.setup.collectAsStateWithLifecycle()
    var editingBroker by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(ui.message) {
        ui.message?.let {
            snackbar.showSnackbar(it)
            viewModel.consumeMessage()
        }
    }

    if (!ui.configured || editingBroker) {
        SetupScreen(
            initial = viewModel.brokerConfig,
            setup = setup,
            onTestAndSave = { viewModel.testAndSave(it) { editingBroker = false } },
            onCancel = if (ui.configured) ({ editingBroker = false }) else null,
        )
        return
    }

    // ui changes at least once a second (the controller's ticker), so this stays current.
    val nowEpochS = remember(ui) { System.currentTimeMillis() / 1_000 }
    Scaffold(snackbarHost = { SnackbarHost(snackbar) }) { padding ->
        StatusScreen(ui, nowEpochS, Modifier.padding(padding))
    }
}
```

- [ ] **Step 9: Build, run unit tests, run the UI tests**

Run: `tools/import_ca.sh && ./gradlew assembleDebug testDebugUnitTest assembleDebugAndroidTest`
Expected: `BUILD SUCCESSFUL`.

Run (device or emulator attached, see "File structure"; instrumented tests are filtered with a runner argument, not `--tests`):
`./gradlew connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=ai.northtrail.cooler.ui.StatusScreenTest,ai.northtrail.cooler.ui.SetupScreenTest`
Expected: 5 tests PASS. If no device can be obtained, state that the UI tests compiled but were not run.

- [ ] **Step 10: Commit**

```bash
git add app/src/main app/src/androidTest
git commit -m "feat(ui): app shell, setup with broker probe, status tab" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 10: Settings tab

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/ui/SettingsScreen.kt`
- Modify: `app/src/main/java/ai/northtrail/cooler/ui/CoolerApp.kt` (replace the whole file: adds the bottom navigation with Status and Settings)
- Test: `app/src/androidTest/java/ai/northtrail/cooler/ui/SettingsScreenTest.kt`

**Interfaces:**
- Consumes: `UiState.settingValue/isPending` (Task 7); `Bounds`, `Bound`, `Group`, `Widget` (Task 4); `Health` (Task 6); `StatusStrip`, `CoolerColors` (Task 9); `CoolerViewModel.stepSetting/chooseSetting` (Task 9).
- Produces: `@Composable fun SettingsScreen(ui: UiState, onStep: (String, Int) -> Unit, onChoose: (String, Int) -> Unit, modifier: Modifier = Modifier)`. Test tags `value-<key>` (the value text) and `pending-<key>` (spinner while a draft or pending command exists). Buttons have content descriptions `Lower <label>` / `Raise <label>`.

- [ ] **Step 1: Write the failing UI test**

`app/src/androidTest/java/ai/northtrail/cooler/ui/SettingsScreenTest.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import ai.northtrail.cooler.model.Pending
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SettingsScreenTest {
    @get:Rule val rule = createComposeRule()
    private val steps = mutableListOf<Pair<String, Int>>()
    private val choices = mutableListOf<Pair<String, Int>>()

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 1_000L)
    private val settings = mapOf(
        "coolerset" to 4, "range" to 2, "sampleinterval" to 3600, "fin_cutoff" to 3, "fin_recover" to 4,
        "settle" to 10, "minofftime" to 5, "minruntime" to 180, "maxrun" to 10, "dutypercent" to 50,
    )
    private val base = UiState(configured = true, state = CoolerState(settings = settings), health = online)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme { SettingsScreen(ui, onStep = { k, d -> steps += k to d }, onChoose = { k, v -> choices += k to v }) }
    }

    @Test
    fun controlsAreOffUnlessOnline() {
        show(base.copy(health = Health(LinkView.UNREACHABLE, "Broker unreachable", emptyList(), null)))
        rule.onNodeWithContentDescription("Raise Set point").assertIsNotEnabled()
        rule.onNodeWithText("Settings can only be changed while the controller is online.").assertExists()
    }

    @Test
    fun steppersReportTheirKeyAndDirection() {
        show(base)
        rule.onNodeWithContentDescription("Raise Set point").assertIsEnabled().performClick()
        rule.onNodeWithContentDescription("Lower Set point").performClick()
        assertEquals(listOf("coolerset" to 1, "coolerset" to -1), steps)
    }

    @Test
    fun pendingShowsTheSentValueWithASpinner() {
        show(base.copy(pending = mapOf("coolerset" to Pending(7, 0))))
        rule.onNodeWithTag("value-coolerset").assertTextEquals("7 °C")
        rule.onNodeWithTag("pending-coolerset").assertExists()
    }

    @Test
    fun confirmedShowsTheControllersValueWithoutASpinner() {
        show(base.copy(state = CoolerState(settings = settings + ("coolerset" to 7))))
        rule.onNodeWithTag("value-coolerset").assertTextEquals("7 °C")
        rule.onNodeWithTag("pending-coolerset").assertDoesNotExist()
    }

    @Test
    fun iceClearCannotBeLoweredToIceCutoff() {
        show(base)
        rule.onNodeWithContentDescription("Lower Ice clear").performScrollTo().assertIsNotEnabled()
        rule.onNodeWithContentDescription("Raise Ice clear").assertIsEnabled()
    }

    @Test
    fun presetsReportTheChosenInterval() {
        show(base)
        rule.onNodeWithTag("value-sampleinterval").assertTextEquals("1 h")
        rule.onNodeWithText("5 m").performClick()
        assertEquals(listOf("sampleinterval" to 300), choices)
    }
}
```

- [ ] **Step 2: Confirm it fails to compile**

Run: `./gradlew assembleDebugAndroidTest`
Expected: FAILS (`Unresolved reference: SettingsScreen`).

- [ ] **Step 3: Implement the screen**

`app/src/main/java/ai/northtrail/cooler/ui/SettingsScreen.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Bound
import ai.northtrail.cooler.model.Bounds
import ai.northtrail.cooler.model.Group
import ai.northtrail.cooler.model.Widget
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(
    ui: UiState,
    onStep: (String, Int) -> Unit,
    onChoose: (String, Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val enabled = ui.health.controlsEnabled
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        if (!enabled) {
            Text(
                "Settings can only be changed while the controller is online.",
                color = CoolerColors.Muted,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
        Group.entries.forEach { group ->
            Text(
                group.title,
                style = MaterialTheme.typography.titleMedium,
                color = CoolerColors.Accent,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
            )
            Bounds.ALL.filter { it.group == group }.forEach { b ->
                if (b.widget == Widget.PRESET) PresetRow(b, ui, enabled, onChoose) else StepperRow(b, ui, enabled, onStep)
            }
        }
    }
}

@Composable
private fun ValueText(b: Bound, ui: UiState) {
    val pending = ui.isPending(b.key)
    if (pending) CircularProgressIndicator(Modifier.size(16.dp).testTag("pending-${b.key}"), strokeWidth = 2.dp)
    Text(
        Bounds.format(b.key, ui.settingValue(b.key)),
        color = if (pending) CoolerColors.Muted else CoolerColors.Text,
        modifier = Modifier.padding(horizontal = 12.dp).testTag("value-${b.key}"),
    )
}

@Composable
private fun StepperRow(b: Bound, ui: UiState, enabled: Boolean, onStep: (String, Int) -> Unit) {
    val value = ui.settingValue(b.key)
    val floor = Bounds.floor(b.key, ui.settingValue("fin_cutoff") ?: b.lo)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp).heightIn(min = 56.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(b.label, Modifier.weight(1f))
        ValueText(b, ui)
        IconButton(onClick = { onStep(b.key, -1) }, enabled = enabled && value != null && value > floor) {
            Icon(Icons.Filled.Remove, contentDescription = "Lower ${b.label}")
        }
        IconButton(onClick = { onStep(b.key, +1) }, enabled = enabled && value != null && value < b.hi) {
            Icon(Icons.Filled.Add, contentDescription = "Raise ${b.label}")
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PresetRow(b: Bound, ui: UiState, enabled: Boolean, onChoose: (String, Int) -> Unit) {
    val value = ui.settingValue(b.key)
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
        Row(Modifier.heightIn(min = 56.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(b.label, Modifier.weight(1f))
            ValueText(b, ui)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Bounds.SAMPLE_PRESETS.forEach { (seconds, label) ->
                FilterChip(
                    selected = value == seconds,
                    onClick = { onChoose(b.key, seconds) },
                    enabled = enabled,
                    label = { Text(label) },
                )
            }
        }
    }
}
```

- [ ] **Step 4: Add the bottom navigation**

`app/src/main/java/ai/northtrail/cooler/ui/CoolerApp.kt` (replace the whole file):
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.CoolerViewModel
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AcUnit
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.lifecycle.compose.collectAsStateWithLifecycle

private data class Tab(val label: String, val icon: ImageVector)

private val tabs = listOf(Tab("Status", Icons.Filled.AcUnit), Tab("Settings", Icons.Filled.Tune))

@Composable
fun CoolerApp(viewModel: CoolerViewModel) {
    val ui by viewModel.ui.collectAsStateWithLifecycle()
    val setup by viewModel.setup.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var editingBroker by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(ui.message) {
        ui.message?.let {
            snackbar.showSnackbar(it)
            viewModel.consumeMessage()
        }
    }

    if (!ui.configured || editingBroker) {
        SetupScreen(
            initial = viewModel.brokerConfig,
            setup = setup,
            onTestAndSave = { viewModel.testAndSave(it) { editingBroker = false } },
            onCancel = if (ui.configured) ({ editingBroker = false }) else null,
        )
        return
    }

    // ui changes at least once a second (the controller's ticker), so this stays current.
    val nowEpochS = remember(ui) { System.currentTimeMillis() / 1_000 }
    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, t ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Icon(t.icon, contentDescription = null) },
                        label = { Text(t.label) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.padding(padding)) {
            when (tab) {
                0 -> StatusScreen(ui, nowEpochS)
                else -> SettingsScreen(ui, viewModel::stepSetting, viewModel::chooseSetting)
            }
        }
    }
}
```

- [ ] **Step 5: Build and run the UI tests**

Run: `./gradlew assembleDebug assembleDebugAndroidTest`
Expected: `BUILD SUCCESSFUL`.

Run (device attached): `./gradlew connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=ai.northtrail.cooler.ui.SettingsScreenTest`
Expected: 6 tests PASS. If no device can be obtained, state that the UI tests compiled but were not run.

- [ ] **Step 6: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler/ui app/src/androidTest
git commit -m "feat(ui): settings tab with debounced steppers and sample presets" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 11: Detail tab (read-only details, fin calibration, broker settings)

**Files:**
- Create: `app/src/main/java/ai/northtrail/cooler/model/DetailRows.kt`
- Create: `app/src/main/java/ai/northtrail/cooler/ui/DetailScreen.kt`
- Modify: `app/src/main/java/ai/northtrail/cooler/ui/CoolerApp.kt` (tabs list and `when` gain Detail)
- Test: `app/src/test/java/ai/northtrail/cooler/model/DetailRowsTest.kt`, `app/src/androidTest/java/ai/northtrail/cooler/ui/DetailScreenTest.kt`

**Interfaces:**
- Consumes: `CoolerState` (Task 2), `StatusText` (Task 3), `Health` (Task 6), `UiState` (Task 7), `StatusStrip`, `CoolerColors` (Task 9), `CoolerViewModel.startCalibration/abortCalibration/resetFinCal/brokerConfig` (Task 9).
- Produces:
  - `object DetailRows { fun of(s: CoolerState?, health: Health): List<Pair<String, String>> }`
  - `@Composable fun DetailScreen(ui: UiState, brokerHost: String, onStartCal: () -> Unit, onAbortCal: () -> Unit, onResetCal: () -> Unit, onBrokerSettings: () -> Unit, modifier: Modifier = Modifier)`

- [ ] **Step 1: Write the failing tests**

`app/src/test/java/ai/northtrail/cooler/model/DetailRowsTest.kt`:
```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DetailRowsTest {
    private val health = Health(LinkView.ONLINE, "Online", emptyList(), 0L)

    @Test
    fun withoutDataOnlyTheLinkIsShown() {
        assertEquals(listOf("Link" to "Online"), DetailRows.of(null, health))
    }

    @Test
    fun everyFieldIsRendered() {
        val s = CoolerState(
            mode = "override", overrideSrc = "switch", state = "cooling", relay = true, coolCall = true,
            runS = 142, offS = 0, holdS = 38, compressor = 1, finTemp = 1.25, finOhms = 11456.79,
            finSlope = -0.2265, shtFault = false, finFault = true, noResponse = true, uptimeS = 5025,
        )
        assertEquals(
            listOf(
                "Link" to "Online",
                "Mode" to "OVERRIDE",
                "Override source" to "switch",
                "State" to "Cooling • min run 38s",
                "Relay" to "Closed (cooling)",
                "Cooling call" to "Yes",
                "Run" to "2m",
                "Off" to "0s",
                "Hold" to "38s",
                "Compressor" to "Running",
                "Coil" to "1.3 °C",
                "Fin resistance" to "11457 Ω",
                "Fin slope" to "-0.23 °C/min",
                "Box sensor" to "OK",
                "Fin sensor" to "Fault",
                "AC response" to "Not responding",
                "Uptime" to "1h 23m",
            ),
            DetailRows.of(s, health),
        )
    }

    @Test
    fun missingNumbersAreDashes() {
        val rows = DetailRows.of(CoolerState(), health).toMap()
        assertEquals("--", rows["Fin resistance"])
        assertEquals("--", rows["Fin slope"])
        assertEquals("--", rows["Uptime"])
        assertEquals("-- °C", rows["Coil"])
        assertEquals("--", rows["Override source"])
    }
}
```

`app/src/androidTest/java/ai/northtrail/cooler/ui/DetailScreenTest.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class DetailScreenTest {
    @get:Rule val rule = createComposeRule()
    private var starts = 0
    private var aborts = 0
    private var resets = 0
    private var brokerClicks = 0

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 0L)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme {
            DetailScreen(
                ui, brokerHost = "mqtt.example.com",
                onStartCal = { starts++ }, onAbortCal = { aborts++ }, onResetCal = { resets++ },
                onBrokerSettings = { brokerClicks++ },
            )
        }
    }

    @Test
    fun resetNeedsConfirmation() {
        show(UiState(configured = true, state = CoolerState(finCal = true), health = online))
        rule.onNodeWithText("Reset calibration").performScrollTo().performClick()
        assertEquals(0, resets)
        rule.onNodeWithText("Reset fin calibration?").assertExists()
        rule.onNodeWithText("Reset").performClick()
        assertEquals(1, resets)
    }

    @Test
    fun startOrAbortFollowsTheCalibrationRun() {
        show(UiState(configured = true, state = CoolerState(calActive = true, calPoints = 3, calSpan = 6.2), health = online))
        rule.onNodeWithText("calibrating 3 pts 6.2C").performScrollTo().assertExists()
        rule.onNodeWithText("Abort calibration").performScrollTo().performClick()
        assertEquals(1, aborts)
        assertEquals(0, starts)
    }

    @Test
    fun calibrationIsOffUnlessOnline() {
        show(
            UiState(
                configured = true,
                state = CoolerState(),
                health = Health(LinkView.CONTROLLER_OFFLINE, "Controller offline", emptyList(), null),
            ),
        )
        rule.onNodeWithText("Start calibration").performScrollTo().assertIsNotEnabled()
        rule.onNodeWithText("Broker settings").performScrollTo().performClick()
        assertEquals(1, brokerClicks)
    }
}
```

- [ ] **Step 2: Run to see them fail**

Run: `./gradlew testDebugUnitTest --tests 'ai.northtrail.cooler.model.DetailRowsTest'` and `./gradlew assembleDebugAndroidTest`
Expected: both FAIL to compile (`Unresolved reference: DetailRows`, `DetailScreen`).

- [ ] **Step 3: Implement the rows**

`app/src/main/java/ai/northtrail/cooler/model/DetailRows.kt`:
```kotlin
package ai.northtrail.cooler.model

import java.util.Locale
import kotlin.math.roundToLong

/** The Detail tab's read-only table, label to value. */
object DetailRows {
    fun of(s: CoolerState?, health: Health): List<Pair<String, String>> {
        val link = "Link" to health.label
        if (s == null) return listOf(link)
        return listOf(
            link,
            "Mode" to StatusText.modeLabel(s),
            "Override source" to (s.overrideSrc ?: "--"),
            "State" to StatusText.stateValue(s),
            "Relay" to if (s.relay) "Closed (cooling)" else "Open",
            "Cooling call" to if (s.coolCall) "Yes" else "No",
            "Run" to StatusText.dur(s.runS),
            "Off" to StatusText.dur(s.offS),
            "Hold" to StatusText.dur(s.holdS),
            "Compressor" to StatusText.compressor(s),
            "Coil" to "${StatusText.temp(s.finTemp)} °C",
            "Fin resistance" to (s.finOhms?.let { "${it.roundToLong()} Ω" } ?: "--"),
            "Fin slope" to (s.finSlope?.let { String.format(Locale.US, "%.2f °C/min", it) } ?: "--"),
            "Box sensor" to if (s.shtFault) "Fault" else "OK",
            "Fin sensor" to if (s.finFault) "Fault" else "OK",
            "AC response" to if (s.noResponse) "Not responding" else "OK",
            "Uptime" to (s.uptimeS?.let(StatusText::dur) ?: "--"),
        )
    }
}
```

- [ ] **Step 4: Implement the screen**

`app/src/main/java/ai/northtrail/cooler/ui/DetailScreen.kt`:
```kotlin
package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.DetailRows
import ai.northtrail.cooler.model.StatusText
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun DetailScreen(
    ui: UiState,
    brokerHost: String,
    onStartCal: () -> Unit,
    onAbortCal: () -> Unit,
    onResetCal: () -> Unit,
    onBrokerSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var confirmReset by remember { mutableStateOf(false) }
    val s = ui.state
    val enabled = ui.health.controlsEnabled

    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        DetailRows.of(s, ui.health).forEach { (label, value) ->
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)) {
                Text(label, color = CoolerColors.Muted, modifier = Modifier.weight(1f))
                Text(value)
            }
        }

        Heading("Fin calibration")
        Text(StatusText.finCal(s), modifier = Modifier.padding(horizontal = 16.dp))
        Row(Modifier.padding(16.dp)) {
            if (s?.calActive == true) {
                OutlinedButton(onClick = onAbortCal, enabled = enabled) { Text("Abort calibration") }
            } else {
                Button(onClick = onStartCal, enabled = enabled) { Text("Start calibration") }
            }
            Spacer(Modifier.width(8.dp))
            OutlinedButton(onClick = { confirmReset = true }, enabled = enabled) { Text("Reset calibration") }
        }

        Heading("Broker")
        Text(brokerHost, color = CoolerColors.Muted, modifier = Modifier.padding(horizontal = 16.dp))
        OutlinedButton(onClick = onBrokerSettings, modifier = Modifier.padding(16.dp)) { Text("Broker settings") }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Reset fin calibration?") },
            text = { Text("The controller forgets the calibration it collected and goes back to the default thermistor curve.") },
            confirmButton = {
                TextButton(onClick = { confirmReset = false; onResetCal() }) { Text("Reset") }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun Heading(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleMedium,
        color = CoolerColors.Accent,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
    )
}
```

- [ ] **Step 5: Add the Detail tab**

In `app/src/main/java/ai/northtrail/cooler/ui/CoolerApp.kt`:

Add the import `import androidx.compose.material.icons.filled.Info`.

Replace
```kotlin
private val tabs = listOf(Tab("Status", Icons.Filled.AcUnit), Tab("Settings", Icons.Filled.Tune))
```
with
```kotlin
private val tabs = listOf(
    Tab("Status", Icons.Filled.AcUnit),
    Tab("Settings", Icons.Filled.Tune),
    Tab("Detail", Icons.Filled.Info),
)
```

Replace
```kotlin
                0 -> StatusScreen(ui, nowEpochS)
                else -> SettingsScreen(ui, viewModel::stepSetting, viewModel::chooseSetting)
```
with
```kotlin
                0 -> StatusScreen(ui, nowEpochS)
                1 -> SettingsScreen(ui, viewModel::stepSetting, viewModel::chooseSetting)
                else -> DetailScreen(
                    ui = ui,
                    brokerHost = viewModel.brokerConfig.host,
                    onStartCal = viewModel::startCalibration,
                    onAbortCal = viewModel::abortCalibration,
                    onResetCal = viewModel::resetFinCal,
                    onBrokerSettings = { editingBroker = true },
                )
```

- [ ] **Step 6: Run the tests**

Run: `./gradlew testDebugUnitTest assembleDebug assembleDebugAndroidTest`
Expected: `BUILD SUCCESSFUL`, all unit tests PASS (3 new).

Run (device attached): `./gradlew connectedDebugAndroidTest`
Expected: all 14 UI tests PASS (Status 2, Setup 3, Settings 6, Detail 3). If no device can be obtained, state that the UI tests compiled but were not run.

- [ ] **Step 7: Commit**

```bash
git add app/src/main/java/ai/northtrail/cooler app/src/test/java/ai/northtrail/cooler/model/DetailRowsTest.kt app/src/androidTest
git commit -m "feat(ui): detail tab with fin calibration and broker settings" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
```

---

### Task 12: Broker account, README and bench check

**Files:**
- Modify: `~/Cooler/controller/secrets.yaml` (git-ignored; append two keys)
- Modify on the broker host: `/etc/mosquitto/passwd`, `/etc/mosquitto/acl` (backed up first)
- Modify: `README.md`

**Interfaces:**
- Consumes: the finished app (Tasks 1–11); `ssh <broker-host>` (the user authorised it for broker users); the live controller on `cooler/*`.
- Produces: broker user `app-user` (read `cooler/data`, `cooler/availability`; write `cooler/cmd`); `app_mqtt_username` / `app_mqtt_password` in `controller/secrets.yaml`; an installed, bench-checked APK.

Never print the password in chat, commit it, or put it on a command line that ends up in local shell history; the commands below keep it in a variable and send it over ssh's stdin.

- [ ] **Step 1: Generate and store the app's password (once)**

```bash
cd ~/Cooler/controller
git check-ignore -q secrets.yaml && echo "secrets.yaml is ignored"
grep -q '^app_mqtt_username:' secrets.yaml || {
  P=$(openssl rand -base64 24 | tr -d '/+=' | cut -c1-24)
  printf 'app_mqtt_username: app-user\napp_mqtt_password: "%s"\n' "$P" >> secrets.yaml
}
grep -c '^app_mqtt_' secrets.yaml
```
Expected: `secrets.yaml is ignored`, then `2`.

- [ ] **Step 2: Add the user and ACL on the broker (backups first)**

```bash
cd ~/Cooler/controller
P=$(sed -n 's/^app_mqtt_password: "\(.*\)"$/\1/p' secrets.yaml)
printf '%s\n' "$P" | ssh <broker-host> '
  set -e
  read -r P
  TS=$(date -u +%Y%m%dT%H%M%SZ)
  sudo cp -a /etc/mosquitto/passwd /etc/mosquitto/passwd.bak-$TS
  sudo cp -a /etc/mosquitto/acl /etc/mosquitto/acl.bak-$TS
  sudo mosquitto_passwd -b /etc/mosquitto/passwd app-user "$P"
  grep -q "^user app-user$" /etc/mosquitto/acl || printf "\nuser app-user\ntopic read cooler/data\ntopic read cooler/availability\ntopic write cooler/cmd\n" | sudo tee -a /etc/mosquitto/acl >/dev/null
  sudo systemctl reload mosquitto
  systemctl is-active mosquitto
  echo "backups: *.bak-$TS"
'
```
Expected: `active` and the backup suffix. (Reload, not restart: live sessions stay up.)

- [ ] **Step 3: Verify the account against the live broker**

```bash
cd ~/Cooler
P=$(sed -n 's/^app_mqtt_password: "\(.*\)"$/\1/p' controller/secrets.yaml)
CA=CoolerApp/app/src/main/assets/broker_ca.pem
timeout 40 mosquitto_sub -h mqtt.example.com -p 8883 --cafile "$CA" -u app-user -P "$P" -t cooler/data -C 1 > /tmp/app-user-check.json
python3 -c "import json; d=json.load(open('/tmp/app-user-check.json')); print('v', d['v'], 'mode', d['mode'], 'temp', d['temp'])"
rm -f /tmp/app-user-check.json
```
Expected: `v 2 mode ... temp ...` (the retained `/data`, read as `app-user`).

- [ ] **Step 4: Write the full README**

`README.md` (replace the whole file):
````markdown
# CoolerApp

Android app for the walk-in cooler (Cooler32). It monitors the cooler and
changes its settings over MQTT, speaking the same contract as the CoolerPanel
wall LCD. Design: `docs/superpowers/specs/2026-09-28-cooler-android-app-design.md`.

- **Status**: box temperature and humidity, run state (same wording as the
  panel), override switch, defrost / fault chips, coil temperature, compressor,
  and a trend of the controller's recent history plus live data.
- **Settings**: the controller's ten settings with the panel's limits. Changes
  are sent after a short pause and confirmed by the controller.
- **Detail**: every status field, fin calibration (start, abort, reset) and the
  broker settings.

The app connects only while it is open. It does not notify; alerts belong in
Node-RED, fed from the retained `cooler/data` and `cooler/availability`.

## Build and install

```bash
tools/import_ca.sh           # bundles the broker's private CA (git-ignored)
./gradlew testDebugUnitTest  # JVM unit tests
./gradlew installDebug       # to a phone on adb
./gradlew connectedDebugAndroidTest   # UI tests, phone or emulator attached
```

## First run

Enter the broker (default `mqtt.example.com`, port 8883), username
`app-user` and its password (`app_mqtt_password` in the git-ignored
`../controller/secrets.yaml`), and topic base `cooler`, then **Test & save**.
The app saves only after it has received `cooler/data`. The password is
encrypted with an Android Keystore key and never shown again.

## Broker account

`app-user` on the broker's mosquitto may read `cooler/data` and
`cooler/availability` and write `cooler/cmd`, nothing else.
````

- [ ] **Step 5: Install and bench-check against the live controller**

Run: `adb devices` (a phone must be listed), then `./gradlew installDebug`.

On the phone, check each and note the result:
1. First launch shows Setup. Enter the `app-user` login; **Test & save** succeeds within a few seconds and Status fills in immediately (retained data), with the strip reading `Online · waiting for update`, then `Online · updated Ns ago` within 30 s.
2. Status matches the panel: temperature, state line, switch chip.
3. Settings: note the current **Set point**, press **+** once. The row shows a spinner, then the new value; the panel shows the same value within a few seconds. Press **−** to restore it.
4. Detail: **Start calibration** → the calibration line changes to `calibrating 0 pts 0.0C` within a few seconds; **Abort calibration** → back to the previous text. Do not press **Reset calibration** (it would discard the controller's fin calibration).
5. Wrong password in Setup (Detail → Broker settings, type a wrong password, Test & save) → `Login refused: check the username, password and broker ACL`, and the saved settings still work after Cancel.
6. Put the app in the background and bring it back: it reconnects and updates.
7. Only if the user agrees to a controller power cycle: unplug the controller. Within about 45 s (the broker's keep-alive timeout) the strip reads `Controller offline · last data …` with a red banner and the Settings controls grey out; plug it back in and it returns to `Online` within a minute.

- [ ] **Step 6: Run everything one last time and commit**

Run: `./gradlew testDebugUnitTest assembleDebug`
Expected: `BUILD SUCCESSFUL`, all unit tests PASS.

```bash
git add README.md
git commit -m "docs: README with build, first run and broker account" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" \
  --trailer "Claude-Session: https://claude.ai/code/session_01T35HiEsJvTcUCFqWzYsnhC"
git status --short   # must be clean; broker_ca.pem must not appear
```
