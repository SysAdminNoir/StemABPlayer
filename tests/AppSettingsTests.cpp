#include <catch2/catch_test_macros.hpp>

#include "app/AppSettings.h"

using stemab::AppSettings;
using Palette = AppSettings::Palette;

TEST_CASE ("colour palette setting round-trips through the settings file", "[app][settings]")
{
    const juce::TemporaryFile temp (".xml");
    const auto file = temp.getFile();

    {
        AppSettings settings (file);
        CHECK (settings.getColourPalette() == Palette::standard);      // the default
        settings.setColourPalette (Palette::colourBlindSafe);
        CHECK (settings.getColourPalette() == Palette::colourBlindSafe);
        settings.save();
    }

    REQUIRE (file.existsAsFile());
    CHECK (file.loadFileAsString().contains ("colourBlindSafe"));

    {
        AppSettings settings (file);
        CHECK (settings.getColourPalette() == Palette::colourBlindSafe);
        settings.setColourPalette (Palette::standard);
        settings.save();
    }

    AppSettings settings (file);
    CHECK (settings.getColourPalette() == Palette::standard);
}

TEST_CASE ("an unknown colour palette value reads as the standard palette", "[app][settings]")
{
    const juce::TemporaryFile temp (".xml");
    {
        juce::PropertiesFile::Options options;
        options.millisecondsBeforeSaving = -1;
        juce::PropertiesFile props (temp.getFile(), options);
        props.setValue (AppSettings::Keys::colourPalette, "sepia");
        props.saveIfNeeded();
    }

    AppSettings settings (temp.getFile());
    CHECK (settings.getColourPalette() == Palette::standard);
    CHECK (settings.getScaleIndex (1) == 1);                            // the other keys keep their defaults
}
