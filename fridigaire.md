> **Note:** this is the original design conversation that led to the CN3 thermistor-swap idea, kept for background. Values here (18 °C / 27 °C, 22 °C setpoint, generic resistor formulas) were proposals; the as-built network (35 kΩ open, 13.6 kΩ closed, AC at its minimum setpoint) is in [RULES-v4.md](RULES-v4.md) and [V4_Wiring/index.html](V4_Wiring/index.html).

**Yes—your approach is sound in principle.** You can measure the detached thermistor at two temperatures, substitute equivalent resistances at CN3 to request or stop cooling, and then use the original thermistor as a temperature sensor for your ESP32. Reading a thermistor through a resistor-divider circuit is a standard microcontroller interface. **The part that still needs validation is how your exact A/C firmware responds to those substituted values.** :chatgpt-content-reference{index="0"}

I would make one important improvement: **use one relay and a two-resistor network that never leaves CN3 disconnected during normal switching.**

## 1. Your measurement plan works

With the **A/C unplugged and the thermistor completely disconnected from its board**, record its resistance at a stable warm temperature and again in the fridge.

Measure the actual temperature beside the probe with a separate thermometer; do not assume the fridge’s setting equals the temperature at the sensor. Leave the probe in place until both readings stabilize, and keep it dry rather than immersing an unverified probe in water.

For an **NTC thermistor**, you should find:

| Measurement | Expected resistance | Intended simulated condition |
|---|---|---|
| Warm room | Lower resistance, `R_WARM` | Room is warm: request cooling |
| Cold location | Higher resistance, `R_COLD` | Room is cool: stop requesting cooling |

That decreasing-resistance-with-increasing-temperature behaviour is what makes the proposed circuit work. **Use your measured values—not an assumed “10 kΩ” sensor specification.** :chatgpt-content-reference{index="1"}

### The simulated “cold” temperature does not have to be 4°C

A 4°C measurement is useful for calibrating your ESP32’s actual room-temperature reading. However, the A/C only needs to see a temperature **below its own thermostat setting** to stop requesting cooling.

For an initial test, I would consider simulated temperatures around **18°C for off and 27°C for on**, with the A/C set to **22°C**. Those are proposed test points, not manufacturer-prescribed settings. They avoid presenting an unusually cold room reading; Frigidaire specifies a normal indoor operating range of **16–32°C**. :chatgpt-content-reference{index="2"}

**Your ESP32 can still regulate the actual room to 4°C.** The A/C’s displayed setpoint would simply be the dividing point between your two simulated temperatures. That does not make 4°C operation a manufacturer-approved use.

## 2. Use one normally open relay, not two independently switched resistors

Here is the circuit arrangement I would prototype:

```text
                    A/C SIDE ONLY

CN3 pin A ----+--------[ R_COLD ]---------+---- CN3 pin B
              |                         |
              +--[ R_ADD ]--o/ o---------+
                         Relay COM / NO

The relay coil/control input is driven separately by the ESP32.
Neither CN3 pin connects to ESP32 power or ground.
```

`R_COLD` is permanently connected across CN3. Closing the relay connects `R_ADD` **in parallel** with it, reducing the total resistance to `R_WARM`.

Calculate the additional resistor as:

\[
\boxed{R_{\text{ADD}}=
\frac{R_{\text{COLD}}\times R_{\text{WARM}}}
{R_{\text{COLD}}-R_{\text{WARM}}}}
\]

This follows directly from the parallel-resistance equation.

| Relay condition | Resistance presented to CN3 | Intended request |
|---|---|---|
| De-energized/open | `R_COLD` | Stop cooling |
| Energized/closed | `R_COLD ‖ R_ADD = R_WARM` | Start/continue cooling |

**The additional resistor is not simply your measured warm resistance.** Its value must account for the cold resistor that remains connected.

This arrangement avoids the brief open circuit produced by a conventional *break-before-make* changeover relay. It also makes a non-latching relay’s de-energized state your “stop cooling” request. Break-before-make contacts explicitly open the previous connection before establishing the next one. :chatgpt-content-reference{index="3"}

Use a relay suitable for **low-current signal switching**, with adequate electrical isolation. A “10 A” contact rating alone does not establish suitability for a tiny sensor signal; Omron specifically distinguishes contacts suitable for these small loads. :chatgpt-content-reference{index="4"}

## 3. Yes, reuse the original thermistor on the ESP32

Once **both thermistor wires are disconnected from the Frigidaire**, it can become part of a completely separate measurement circuit:

```text
ESP32 3.3 V
     |
  [ R_FIXED ]
     |
     +---------- ADC input
     |
[ Original thermistor ]
     |
ESP32 ground
```

This is the standard voltage-divider approach. Choose `R_FIXED` after measuring the probe; a value near the thermistor’s resistance around your intended measurement temperature is a reasonable starting point. :chatgpt-content-reference{index="5"}

For that orientation:

\[
R_{\text{THERMISTOR}} =
R_{\text{FIXED}}
\frac{V_{\text{ADC}}}{V_{\text{SUPPLY}}-V_{\text{ADC}}}
\]

Then convert resistance into temperature using a calibrated thermistor model or lookup table. **Do not linearly interpolate resistance versus temperature over the whole range, or assume the probe has a B3950 coefficient.** Thermistor conversion requires its resistance/temperature characteristics. :chatgpt-content-reference{index="6"}

Your warm and fridge measurements provide two points for an approximate beta-model calibration. I would also check the completed ESP32 measurement against a reference thermometer near the intended **4°C operating temperature**.

For the ESP32’s built-in ADC, configure the appropriate input range and use calibrated voltage readings. Espressif documents that the measurable range depends on the chip and attenuation setting; `analogReadMilliVolts()` provides a calibrated millivolt result. Do not assume raw ADC full scale equals exactly 3.3 V. :chatgpt-content-reference{index="7"}

## 4. Keep the fin sensor and safety logic separate

**Reusing the original room thermistor does not eliminate the need for your separate fin sensor.** CoolBot uses room temperature to determine cooling demand and fin temperature to interrupt that demand before icing. The fan continues running during that interruption. :chatgpt-content-reference{index="8"}

For your implementation, I would require:

- **No cooling request on startup, invalid sensor readings, or a controller fault.** A watchdog should release the relay after a software hang; simply choosing a normally open contact does not guarantee this.
- **Room-temperature hysteresis, fin-temperature cutoff and recovery thresholds, and compressor restart timing.** Frigidaire documents a three-minute restart delay, and CoolBot flags Frigidaire units as having a minimum compressor-on period. Therefore, changing the resistor is a thermostat request—not an instantaneous compressor shutdown. :chatgpt-content-reference{index="9"}
- **Electrical separation from the A/C board.** CN3’s isolation from mains has not been established. Keep its resistor/contact circuit isolated from ESP32 ground, USB and other equipment, and have the interface’s insulation and enclosure checked before permanent operation.

The unit also uses **R32 refrigerant**, and its manual warns against ignition sources and arcing switches near the appliance. Relay placement and enclosure therefore matter, even though you are switching a sensor circuit rather than compressor power. :chatgpt-content-reference{index="10"}

**Bottom line: yes—two measured resistance states, one isolated relay, the original thermistor on your ESP32, and a separate fin sensor form a workable prototype architecture.** Measure the complete resistor network in both relay states **before connecting it to CN3**, then verify that the A/C accepts both states and responds as expected.

Send the two measured temperatures/resistances and the model or a clear photo of your relay module; those are the missing details for selecting resistor values and checking the interface.