[![License][mpl2-badge]][mpl2-url]
[![Test Build][testing-badge]][testing-url]
[![Version][version-badge]][version-url]

# liquidsfz

## DESCRIPTION

liquidsfz is a free and open source sampler that can load and play .sfz files.
It can also load and play Hydrogen drumkits. We support JACK and LV2.

The main goal is to provide a library that is easy to integrate into other
projects.

 * API documentation is available here: https://space.twc.de/~stefan/liquidsfz/api-0.3.2
 * There is also a list of [currently supported SFZ opcodes](OPCODES.md)

The API should be fairly stable at this point, so we try to maintain source
compatibility.

## COMPILING

To compile liquidsfz, use the usual

    ./configure
    make
    make install

You need the packages (on Debian/Ubuntu):

* libjack-jackd2-dev (JACK client)
* libreadline-dev (JACK client)
* lv2-dev (LV2 plugin)
* libsndfile1-dev

Some components can be disabled during configure, --without-jack will turn off
building the JACK client, --without-lv2 will turn off building the LV2 plugin.

If you are building from git, you also need the package (for autogen.sh to work):

* autoconf-archive

## USING JACK

The liquidsfz command line program can be used like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

Or if you want to load a Hydrogen drumkit, like this:

    liquidsfz /usr/share/hydrogen/data/drumkits/GMRockKit/drumkit.xml

liquidsfz works as jack client with midi input and audio ouput. If you connect
the jack midi input / audio output using a patchbay, you can send midi events
to liquidsfz to test the sfz loader.

If you are interested in using a graphical front-end in conjunction with jack,
you can try [QLiquidSFZ](https://github.com/be1/qliquidsfz).

## LV2 PLUGIN

We provide a LV2 plugin for hosts like Ardour/Carla/Qtractor and others, which
is built and installed by default. It can also be downloaded as binary release
below.

## LICENSE

The code is licensed under
[MPL-2.0](https://github.com/swesterfeld/liquidsfz/blob/master/LICENSE).

## RELEASES

The current version of liquidsfz is liquidsfz-0.3.2, and can be downloaded
here:

* https://github.com/swesterfeld/liquidsfz/releases/download/0.3.2/liquidsfz-0.3.2.tar.bz2

## BINARY LV2 PLUGIN

The LV2 plugin (and only the LV2 plugin) is available as self-contained 64-bit
binary.

### LINUX BINARY

The Linux version is available here:

* https://github.com/swesterfeld/liquidsfz/releases/download/0.3.2/liquidsfz-0.3.2-x86_64.tar.gz

To install it, extract the archive and

    mkdir -p $HOME/.lv2
    cp -a liquidsfz.lv2  $HOME/.lv2/

### WINDOWS BINARY

The Windows pplugin is available here:

* https://github.com/swesterfeld/liquidsfz/releases/download/0.3.2/liquidsfz-0.3.2-win64.zip

To install it, extract the zip file to the location where your LV2 plugins are, usually

    C:\Program Files\Common Files\LV2

## WINDOWS DLL

If you are a windows developer and need only the .dll (not the plugin or jack
client), there is [experimental support for building on windows](README_WINDOWS.md)
but this experimental and may or may not work.

[mpl2-badge]: https://img.shields.io/github/license/swesterfeld/liquidsfz?style=for-the-badge
[mpl2-url]: https://github.com/swesterfeld/liquidsfz/blob/master/LICENSE
[testing-badge]: https://img.shields.io/github/actions/workflow/status/swesterfeld/liquidsfz/testing.yml?style=for-the-badge
[testing-url]: https://github.com/swesterfeld/liquidsfz/actions/workflows/testing.yml
[version-badge]: https://img.shields.io/github/v/release/swesterfeld/liquidsfz?label=version&style=for-the-badge
[version-url]: https://github.com/swesterfeld/liquidsfz/releases
