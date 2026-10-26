// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "textscanner.hh"
#include "synth.hh"
#include "testutils.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

using namespace LiquidSFZInternal;
using std::string;
using std::string_view;

static void
test_scanner()
{
  TextScanner scanner ("abc12 \t\r\n\v\f\"a b\" rest");
  string_view digits, quoted, token;
  scanner.literal ("abc");
  scanner.digits (digits);
  scanner.spaces();
  scanner.quoted (quoted);
  scanner.skip_spaces();
  scanner.token (token);
  scanner.end();
  assert (scanner.ok() && digits == "12" && quoted == "a b" && token == "rest");

  // Failure is sticky and must not alter a capture or advance the cursor.
  scanner = TextScanner ("abc");
  token = "unchanged";
  scanner.digits (token);
  scanner.literal ("abc");
  scanner.skip_spaces();
  scanner.token (token);
  scanner.quoted (token);
  scanner.end();
  assert (!scanner.ok() && scanner.position() == 0 && token == "unchanged");

  for (const char *input : { "", "x", "\"unterminated" })
    {
      scanner = TextScanner (input);
      scanner.quoted (token);
      assert (!scanner.ok() && scanner.position() == 0 && token == "unchanged");
    }
  scanner = TextScanner ("\"\"tail");
  scanner.quoted (quoted);
  assert (scanner.ok() && quoted.empty() && scanner.position() == 2);
  scanner.end();
  assert (!scanner.ok());

  scanner = TextScanner ("");
  scanner.skip_spaces();
  scanner.end();
  assert (scanner.ok());
  scanner.spaces();
  assert (!scanner.ok());

  // Bytes outside ASCII are valid token content, not signed ctype arguments.
  const string bytes (1, char (0xff));
  scanner = TextScanner (bytes);
  scanner.token (token);
  scanner.end();
  assert (scanner.ok() && token == bytes);
}

static void
test_loader (const string& directory)
{
  const string filename = directory + "/input.sfz";
  std::ofstream (directory + "/part.sfz") << "<region> sample=*sine tune=3\n";
  Synth synth;
  synth.set_log_function ([] (LiquidSFZ::Log, const char *) {});
  SampleCache cache;
  auto load = [&] (const string& text, auto check) {
    std::ofstream (filename) << text;
    Loader loader (&synth);
    assert (loader.parse (filename, cache, {}));
    check (loader);
  };
  auto tune17 = [] (const Loader& loader) {
    assert (loader.regions.size() == 1 && loader.regions[0].tune == 17);
  };

  // Both include forms, whitespace around '=', and trailing opcodes.
  for (const string& separator : { " ", "\t", "=", " =", "= \t", " \t= \t" })
    load ("#include" + separator + "\"part.sfz\" tune=17\n", tune17);
  // A missing separator or closing quote must not load the referenced file.
  for (const string& directive : { "#include\"part.sfz\"", "#include \"part.sfz" })
    load (directive + "\n<region> sample=*sine tune=17\n", tune17);

  load ("#define $X 3\n#define $X 17 <region> sample=*sine tune=$X\n", tune17);
  load ("#define $FILE part.sfz\n#include \"$FILE\" tune=17\n", tune17);
  load ("#define $ 1\n<region> sample=*sine tune=17\n", [] (const Loader& loader) {
    assert (loader.control.defines.empty());
    assert (loader.regions.size() == 1 && loader.regions[0].tune == 17);
  });

  const auto check_lfos = [] (const Loader& loader) {
    assert (loader.regions.size() == 1);
    const auto& lfos = loader.regions[0].lfos;
    assert (lfos.size() == 2 && lfos[0].id == 1 && lfos[1].id == 6);
    assert (lfos[0].freq == 4 && lfos[1].freq == 5);
    assert (lfos[0].lfo_mods.size() == 1);
    const auto& mod = lfos[0].lfo_mods[0];
    assert (mod.to_index == 1 && mod.lfo_freq_cc.entries_.size() == 1);
    assert (mod.lfo_freq_cc.entries_[0].cc == 1 && mod.lfo_freq_cc.entries_[0].value == 3);
  };
  load ("<region> sample=*sine lfo1_freq=4 lfo1_freq_lfo6_oncc1=3 lfo6_freq=5\n", check_lfos);

  // Long captures and trailing text exercise the actual loader, not copies of
  // the removed regexes. Leading zeroes keep long IDs numerically in range.
  const string long_word (100000, 'a');
  const string zeroes (100000, '0');
  load ("#define $X " + long_word + "\n<region> sample=*sine tune=17\n", [&] (const Loader& loader) {
    tune17 (loader);
    assert (loader.control.defines[0].value == long_word);
  });
  load ("#include \"part.sfz\"" + string (100000, ' ') + "tune=17\n", tune17);
  load ("<region> sample=*sine lfo" + zeroes + "1_freq=4 lfo1_freq_lfo" + zeroes + "6_oncc1=3 lfo6_freq=5\n", check_lfos);
  load ("<region> sample=*sine lfo1_" + long_word + "=1 lfo1_freq_lfo6_" + long_word + "=1 tune=17\n", tune17);
}

int
main()
{
  test_scanner();
  const auto directory = LiquidSFZTest::create_temp_directory ("liquidsfz-textscanner-");
  test_loader (directory.string());
  std::filesystem::remove_all (directory);
}
