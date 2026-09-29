#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "format.h"
#include <cstdio>
#include <cstring>

static const char* T(int64_t n){ static char b[16]; return fmt_tokens(n,b,sizeof b); }
static const char* C(double v){ static char b[16]; return fmt_cost(v,b,sizeof b); }
static const char* A(int64_t s){ static char b[16]; return fmt_age(s,b,sizeof b); }

TEST_CASE("tokens") {
    CHECK(std::string(T(984)) == "984");
    CHECK(std::string(T(12500)) == "12.5K");
    CHECK(std::string(T(2400000)) == "2.4M");
    CHECK(std::string(T(190300000)) == "190M");
    CHECK(std::string(T(1200000000)) == "1.2B");
    CHECK(std::string(T(-1)) == "—");
}
TEST_CASE("cost") {
    CHECK(std::string(C(0.44)) == "$0.44");
    CHECK(std::string(C(14.32)) == "$14.32");
    CHECK(std::string(C(240.1)) == "$240");
    CHECK(std::string(C(-1)) == "—");
}
TEST_CASE("age") {
    CHECK(std::string(A(17)) == "17s");
    CHECK(std::string(A(240)) == "4m");
    CHECK(std::string(A(7200)) == "2h");
    CHECK(std::string(A(90000)) == "1d");
    CHECK(std::string(A(-1)) == "—");
}
TEST_CASE("tokens stay within 5 chars at rounding boundary") {
    CHECK(std::string(T(99960000)) == "100M");   // not "100.0M"
    CHECK(std::string(T(99940000)) == "99.9M");
    CHECK(std::string(T(11500000)) == "11.5M");   // §7 example
}
TEST_CASE("cost rounds cents into whole dollars near $100") {
    CHECK(std::string(C(99.995)) == "$100");
    CHECK(std::string(C(99.99)) == "$99.99");
}
