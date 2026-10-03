#include <doctest/doctest.h>
#include "chart.h"
#include "history.h"

TEST_CASE("downsample buckets samples into columns") {
    History h;
    REQUIRE(h.init(100));
    // 10 samples, t = 0,10,...,90
    for (int i = 0; i < 10; i++)
        h.maybe_append(i * 10, (float)i, (float)(i * 2), 0);

    Column cols[5];
    chart_downsample(h, 0, 100, cols, 5);   // each column spans 20 s
    for (int i = 0; i < 5; i++) CHECK(cols[i].has);
    // column 0 covers t in [0,20) -> samples 0 and 1 -> temps 0.0 and 1.0
    CHECK(cols[0].tmin == doctest::Approx(0.0));
    CHECK(cols[0].tmax == doctest::Approx(1.0));
    // column 4 covers [80,100) -> samples 8 and 9 -> temps 8.0 and 9.0
    CHECK(cols[4].tmin == doctest::Approx(8.0));
    CHECK(cols[4].tmax == doctest::Approx(9.0));
    CHECK(cols[4].hmax == doctest::Approx(18.0));
}

TEST_CASE("empty columns are flagged not guessed") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(0, 5.0f, 50.0f, 0);
    h.maybe_append(90, 6.0f, 60.0f, 0);
    Column cols[5];
    chart_downsample(h, 0, 100, cols, 5);
    CHECK(cols[0].has);
    CHECK_FALSE(cols[1].has);   // gap -- must NOT interpolate
    CHECK_FALSE(cols[2].has);
    CHECK_FALSE(cols[3].has);
    CHECK(cols[4].has);
}

TEST_CASE("empty history yields all-empty columns") {
    History h;
    REQUIRE(h.init(10));
    Column cols[3];
    chart_downsample(h, 0, 100, cols, 3);
    for (int i = 0; i < 3; i++) CHECK_FALSE(cols[i].has);
}

TEST_CASE("samples outside the window are excluded") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(10,  1.0f, 10.0f, 0);
    h.maybe_append(500, 9.0f, 90.0f, 0);   // outside [0,100)
    Column cols[2];
    chart_downsample(h, 0, 100, cols, 2);
    CHECK(cols[0].has);
    CHECK(cols[0].tmax == doctest::Approx(1.0));
    CHECK_FALSE(cols[1].has);
}

TEST_CASE("each column reports the unit that ran for most of it") {
    History h;
    REQUIRE(h.init(100));
    // Four columns of 25 s, one sample every 5 s.
    // [0,25)  AC1 throughout      [25,50)  AC2 throughout
    // [50,75) idle throughout     [75,100) AC1 3 : AC2 2
    const uint8_t plan[20] = {1,1,1,1,1,  2,2,2,2,2,  0,0,0,0,0,  1,2,1,2,1};
    for (int i = 0; i < 20; i++)
        h.maybe_append(i * 5, 10.0f, 70.0f, 0, plan[i]);

    Column cols[4];
    chart_downsample(h, 0, 100, cols, 4);
    for (int i = 0; i < 4; i++) REQUIRE(cols[i].has);
    CHECK(cols[0].ac == 1);
    CHECK(cols[1].ac == 2);
    CHECK(cols[2].ac == 0);
    CHECK(cols[3].ac == 1);   // majority, not last-writer
}

TEST_CASE("a column split evenly between idle and a unit is drawn as the unit") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(0,  10.0f, 70.0f, 0, 0);
    h.maybe_append(10, 10.0f, 70.0f, 0, 2);
    Column cols[1];
    chart_downsample(h, 0, 20, cols, 1);
    REQUIRE(cols[0].has);
    CHECK(cols[0].ac == 2);
}

TEST_CASE("history without a unit reported stays uncoloured") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(0, 10.0f, 70.0f, 0);        // caller omits the unit
    h.maybe_append(10, 10.0f, 70.0f, 0, 9);    // and a bogus one is not a colour
    CHECK(h.at(0).ac == 0);
    CHECK(h.at(1).ac == 0);
    Column cols[1];
    chart_downsample(h, 0, 20, cols, 1);
    CHECK(cols[0].ac == 0);
}

TEST_CASE("the trend window ends at now, so a sensor outage shows as a live gap") {
    // History stops growing while temp is null. Anchoring the right edge to
    // the newest sample would freeze the pre-outage trace in place.
    CHECK(chart_window_end(1000, 5000) == 5001);   // outage: 4000 s of gap on the right
    CHECK(chart_window_end(5000, 5000) == 5001);
    CHECK(chart_window_end(6000, 5000) == 6001);   // clock behind the data: never clip samples
}

TEST_CASE("a stretch sampled once a minute draws without holes at the 1 h zoom") {
    // Node-RED's history is one sample a minute; at 1 h a column is ~35 s,
    // so without bridging every other column would be blank (a comb).
    History h;
    REQUIRE(h.init(100));
    for (int t = 0; t < 3600; t += 60) h.maybe_append(t, 4.0f, 80.0f, 30);
    Column cols[102];
    chart_downsample(h, 0, 3600, cols, 102);
    for (int i = 0; i <= 100; i++) CHECK_MESSAGE(cols[i].has, "column ", i);
}

TEST_CASE("an empty column is still a gap when columns are wider than a minute") {
    // At the 7 d zoom a column is ~100 min: one empty column is a real outage.
    History h;
    REQUIRE(h.init(100));
    h.maybe_append(0, 4.0f, 80.0f, 30);
    h.maybe_append(7000, 4.0f, 80.0f, 30);     // column 0 is [0,3000), 1 is [3000,6000)
    Column cols[3];
    chart_downsample(h, 0, 9000, cols, 3);
    CHECK(cols[0].has);
    CHECK_FALSE(cols[1].has);
    CHECK(cols[2].has);
}

static Column col(float tmin, float tmax, float hmin = 80, float hmax = 80) {
    return Column{tmin, tmax, hmin, hmax, true, 0};
}

TEST_CASE("a column reaches down to the previous one on a steep rise") {
    const Column c[] = {col(2.0f, 2.2f), col(3.0f, 3.1f)};
    const Span s = chart_joined_temp(c, 1);
    CHECK(s.lo == doctest::Approx(2.2f));   // meets the top of the previous column
    CHECK(s.hi == doctest::Approx(3.1f));
}

TEST_CASE("a column reaches up to the previous one on a steep fall") {
    const Column c[] = {col(5.0f, 5.5f), col(1.0f, 1.2f)};
    const Span s = chart_joined_temp(c, 1);
    CHECK(s.lo == doctest::Approx(1.0f));
    CHECK(s.hi == doctest::Approx(5.0f));   // meets the bottom of the previous column
}

TEST_CASE("overlapping columns keep their own range") {
    const Column c[] = {col(2.0f, 3.0f), col(2.5f, 3.5f)};
    const Span s = chart_joined_temp(c, 1);
    CHECK(s.lo == doctest::Approx(2.5f));
    CHECK(s.hi == doctest::Approx(3.5f));
}

TEST_CASE("after an empty column, or at the start, a column stands alone") {
    Column c[] = {col(2.0f, 2.2f), col(0, 0), col(6.0f, 6.1f)};
    c[1].has = false;
    const Span s = chart_joined_temp(c, 2);
    CHECK(s.lo == doctest::Approx(6.0f));
    CHECK(s.hi == doctest::Approx(6.1f));
    const Span f = chart_joined_temp(c, 0);
    CHECK(f.lo == doctest::Approx(2.0f));
    CHECK(f.hi == doctest::Approx(2.2f));
}

TEST_CASE("humidity joins midpoint to midpoint") {
    const Column c[] = {col(4, 4, 80, 82), col(4, 4, 86, 88)};
    const Span s = chart_joined_rh(c, 1);
    CHECK(s.lo == doctest::Approx(81.0f));
    CHECK(s.hi == doctest::Approx(87.0f));
    const Span f = chart_joined_rh(c, 0);
    CHECK(f.lo == doctest::Approx(81.0f));
    CHECK(f.hi == doctest::Approx(81.0f));
}

