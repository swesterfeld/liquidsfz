#include "sfzreader.hh"
#include "log.hh"
#include <cassert>

using namespace LiquidSFZInternal;

using std::string;

int
main()
{
  SFZReader sfz_reader;
  string parsed;
  string expected;
  sfz_reader.on_opcode = [&] (const string& opcode, const string& value)
    {
      parsed += string_printf ("opcode: %s='%s'\n", opcode.c_str(), value.c_str());
    };
  sfz_reader.on_tag = [&] (const string& tag)
    {
      parsed += string_printf ("tag: %s\n", tag.c_str());
    };
  sfz_reader.on_warning = [&] (SFZReader::Warning w)
    {
      parsed += string_printf ("warning: %d\n", w);
    };

  auto expect_tag = [&] (const string& tag)
    {
      expected += string_printf ("tag: %s\n", tag.c_str());
    };
  auto expect_opcode = [&] (const string& opcode, const string& value)
    {
      expected += string_printf ("opcode: %s='%s'\n", opcode.c_str(), value.c_str());
    };
  auto expect_warning = [&] (SFZReader::Warning w)
    {
      expected += string_printf ("warning: %d\n", w);
    };

  auto begin_test = [&] (const string& sfz) { parsed = ""; expected = ""; printf ("## parse: %s\n", sfz.c_str()); sfz_reader.parse (sfz); };
  auto end_test = [&] () { printf ("%s\n", parsed.c_str()); assert (parsed == expected); };

  begin_test ("<region>sample=*sine");
  expect_tag ("region");
  expect_opcode ("sample", "*sine");
  end_test();

  begin_test ("<region>sample=synth string.wav loop_mode=loop_continuous");
  expect_tag ("region");
  expect_opcode ("sample", "synth string.wav");
  expect_opcode ("loop_mode", "loop_continuous");
  end_test();

  begin_test ("xxx<region>foo=bar=bazz");
  expect_warning (SFZReader::INCOMPLETE_OPCODE_ASSIGNMENT);
  expect_tag ("region");
  expect_warning (SFZReader::EQUAL_SIGN_IN_OPCODE_VALUE);
  expect_opcode ("foo", "bar=bazz");
  end_test();

  begin_test("<region>...<region>");
  expect_tag ("region");
  expect_warning (SFZReader::UNEXPECTED_CHARACTERS);
  expect_tag ("region");
  end_test();

  begin_test ("region_label= bar   group_label=#t  <group>");
  expect_opcode ("region_label", "bar");
  expect_opcode ("group_label", "#t");
  expect_tag ("group");
  end_test();

  begin_test ("region_label=   group_label=#t pan=<region>");
  expect_warning (SFZReader::MISSING_OPCODE_VALUE);
  expect_opcode ("group_label", "#t");
  expect_warning (SFZReader::MISSING_OPCODE_VALUE);
  expect_tag ("region");
  end_test();

  begin_test ("sample=*sine\tvolume=24");
  expect_opcode ("sample", "*sine");
  expect_opcode ("volume", "24");
  end_test();

}
