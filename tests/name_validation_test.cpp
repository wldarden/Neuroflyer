#include <neuroflyer/name_validation.h>
#include <gtest/gtest.h>
#include <string>

namespace nf = neuroflyer;

TEST(NameValidation, ValidNames) {
    EXPECT_TRUE(nf::is_valid_name("Lotus"));
    EXPECT_TRUE(nf::is_valid_name("Triple Threat"));
    EXPECT_TRUE(nf::is_valid_name("TT-night"));
    EXPECT_TRUE(nf::is_valid_name("net_v2"));
    EXPECT_TRUE(nf::is_valid_name("A"));
    EXPECT_TRUE(nf::is_valid_name("123"));
    EXPECT_TRUE(nf::is_valid_name(std::string(64, 'a')));  // max length OK
}

TEST(NameValidation, InvalidNames) {
    EXPECT_FALSE(nf::is_valid_name(""));
    EXPECT_FALSE(nf::is_valid_name("../../../etc/passwd"));
    EXPECT_FALSE(nf::is_valid_name("a/b"));
    EXPECT_FALSE(nf::is_valid_name("a\\b"));
    EXPECT_FALSE(nf::is_valid_name(std::string(65, 'a')));  // too long
    EXPECT_FALSE(nf::is_valid_name("hello!"));
    EXPECT_FALSE(nf::is_valid_name("test@net"));
    EXPECT_FALSE(nf::is_valid_name("name.bin"));
}

TEST(NameValidation, ReservedNames) {
    EXPECT_FALSE(nf::is_valid_name("CON"));
    EXPECT_FALSE(nf::is_valid_name("con"));
    EXPECT_FALSE(nf::is_valid_name("Con"));
    EXPECT_FALSE(nf::is_valid_name("NUL"));
    EXPECT_FALSE(nf::is_valid_name("nul"));
    EXPECT_FALSE(nf::is_valid_name("COM1"));
    EXPECT_FALSE(nf::is_valid_name("LPT9"));
    EXPECT_FALSE(nf::is_valid_name("PRN"));
    EXPECT_FALSE(nf::is_valid_name("AUX"));
}
