#pragma once

#include "../JuceHeader.h"

/**
 * Platform-specific path utilities.
 *
 * macOS:
 *   - Models: ~/Library/Application Support/Keinoaza/Kisyo Tune/models/, then
 *             /Library/Application Support/Keinoaza/Kisyo Tune/models/,
 *             then the same two under "Kinu Tune"
 *   - Logs: ~/Library/Logs/Kisyo Tune/
 *   - Config: ~/Library/Kisyo Tune/
 *
 * Windows:
 *   - Models: %APPDATA%/Keinoaza/Kisyo Tune/models/, then
 *             %PROGRAMDATA%/Keinoaza/Kisyo Tune/models/,
 *             then the same two under "Kinu Tune"
 *   - Logs: %APPDATA%/Kisyo Tune/Logs/
 *   - Config: %APPDATA%/Kisyo Tune/
 *
 * Linux:
 *   - Models: <exe_dir>/models/
 *   - Logs: ~/.config/Kisyo Tune/logs/
 *   - Config: ~/.config/Kisyo Tune/
 */
namespace PlatformPaths
{
    inline constexpr const char *kVendorFolderName = "Keinoaza";
    inline constexpr const char *kProductFolderName = "Kisyo Tune";

    // Kisyo Tune is a fork of Kinu Tune and uses the same model files. An
    // existing Kinu Tune install already has them - around 130 MB - so the
    // models folder is looked for under the old name too. That keeps a
    // Kisyo Tune download to the plug-in itself and means nobody has to
    // place the same files twice. It is read-only reuse: nothing is written
    // to the other product's folder.
    inline constexpr const char *kLegacyProductFolderName = "Kinu Tune";

    inline juce::File getProjectResourcesDirectory()
    {
#if JUCE_DEBUG
#ifdef PITCHNET_PROJECT_RESOURCES_DIR
        auto configuredResources = juce::File(juce::String(PITCHNET_PROJECT_RESOURCES_DIR));
        if (configuredResources.isDirectory())
            return configuredResources;
#endif

        auto sourceRelativeProbe = juce::File(__FILE__)
                                       .getParentDirectory()
                                       .getParentDirectory()
                                       .getParentDirectory()
                                       .getChildFile("Resources");
        if (sourceRelativeProbe.isDirectory())
            return sourceRelativeProbe;

        auto cwdProbe = juce::File::getCurrentWorkingDirectory()
                            .getChildFile("Resources");
        if (cwdProbe.isDirectory())
            return cwdProbe;

        return {};
#else
        // Kisyo Tune keeps its resources apart from PitchNet's. The per-user
        // folder comes first so a local install needs no administrator rights,
        // and this product's own folder comes before the one it inherited the
        // models from, so a Kisyo Tune install always wins.
        auto resourcesUnder = [](juce::File::SpecialLocationType location,
                                 const char *productFolder)
        {
            return juce::File::getSpecialLocation(location)
#if JUCE_MAC
                .getChildFile("Application Support")
#endif
                .getChildFile(kVendorFolderName)
                .getChildFile(productFolder);
        };

        const juce::File candidates[] = {
            resourcesUnder(juce::File::userApplicationDataDirectory, kProductFolderName),
            resourcesUnder(juce::File::commonApplicationDataDirectory, kProductFolderName),
            resourcesUnder(juce::File::userApplicationDataDirectory, kLegacyProductFolderName),
            resourcesUnder(juce::File::commonApplicationDataDirectory, kLegacyProductFolderName),
        };

        for (const auto &candidate : candidates)
            if (candidate.getChildFile("models").isDirectory())
                return candidate;

        // Nothing found: name this product's own per-user folder, so the
        // "models are missing" message points where they should be put.
        return candidates[0];
#endif
    }

    inline juce::File getApplicationFile()
    {
        return juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    }

    inline juce::File getDefaultLocalResourcesModelsDirectory()
    {
#if JUCE_MAC
        return getApplicationFile().getChildFile("Contents/Resources/models");
#else
        auto dir = getApplicationFile().getParentDirectory();
        const auto initialDir = dir;

        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            if (dir.getFileName().equalsIgnoreCase("Contents"))
                return dir.getChildFile("Resources/models");

            auto parent = dir.getParentDirectory();
            if (parent == dir)
                break;
            dir = parent;
        }

        return initialDir.getChildFile("Resources/models");
#endif
    }

    inline juce::File findLocalResourcesModelsDirectory()
    {
        auto isValid = [](const juce::File &candidate)
        {
            return candidate.isDirectory();
        };

        auto projectResources = getProjectResourcesDirectory();
        auto projectModels = projectResources.getChildFile("models");
        if (isValid(projectModels))
            return projectModels;

        auto dir = getApplicationFile().getParentDirectory();
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto resourcesCandidate = dir.getChildFile("Resources/models");
            if (isValid(resourcesCandidate))
                return resourcesCandidate;

            auto parent = dir.getParentDirectory();
            if (parent == dir)
                break;
            dir = parent;
        }

        return {};
    }

    inline juce::File getModelsDirectory()
    {
        auto localResourcesModels = findLocalResourcesModelsDirectory();
        if (localResourcesModels.isDirectory())
            return localResourcesModels;

        return getDefaultLocalResourcesModelsDirectory();
    }

    inline juce::File getModelFile(const juce::String &fileName)
    {
        auto probe = getModelsDirectory().getChildFile(fileName);
        if (probe.exists())
            return probe;

        // Development fallback: <repo>/Resources/models/
        auto cwdProbe = juce::File::getCurrentWorkingDirectory()
                            .getChildFile("Resources/models")
                            .getChildFile(fileName);
        if (cwdProbe.exists())
            return cwdProbe;

        // Walk up from executable directory and probe both:
        //   <dir>/models/<file>
        //   <dir>/Resources/models/<file>
        auto dir = getApplicationFile().getParentDirectory();
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto modelsCandidate = dir.getChildFile("models").getChildFile(fileName);
            if (modelsCandidate.exists())
                return modelsCandidate;

            auto resourcesCandidate = dir.getChildFile("Resources/models").getChildFile(fileName);
            if (resourcesCandidate.exists())
                return resourcesCandidate;

            auto parent = dir.getParentDirectory();
            if (parent == dir)
                break;
            dir = parent;
        }

        // Default path used in production packaging.
        return probe;
    }

    inline juce::File getVocoderModelFile()
    {
#if JUCE_MAC
        auto coreMLModel = getModelFile("pc_nsf_hifigan.mlmodelc");
        if (coreMLModel.isDirectory())
            return coreMLModel;
#endif
        return getModelFile("pc_nsf_hifigan.onnx");
    }

    inline juce::File getModelSubDir(const juce::String &dirName,
                                     const juce::String &verifyFile = "")
    {
        // Helper: check if candidate dir is valid
        auto isValid = [&](const juce::File &candidate) -> bool
        {
            if (!candidate.isDirectory())
                return false;
            if (verifyFile.isEmpty())
                return true;
            return candidate.getChildFile(verifyFile).existsAsFile();
        };

        auto probe = getModelsDirectory().getChildFile(dirName);
        if (isValid(probe))
            return probe;

        auto cwdProbe = juce::File::getCurrentWorkingDirectory()
                            .getChildFile("Resources/models")
                            .getChildFile(dirName);
        if (isValid(cwdProbe))
            return cwdProbe;

        auto dir = getApplicationFile().getParentDirectory();
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto modelsCandidate = dir.getChildFile("models").getChildFile(dirName);
            if (isValid(modelsCandidate))
                return modelsCandidate;

            auto resourcesCandidate = dir.getChildFile("Resources/models").getChildFile(dirName);
            if (isValid(resourcesCandidate))
                return resourcesCandidate;

            auto parent = dir.getParentDirectory();
            if (parent == dir)
                break;
            dir = parent;
        }

        return probe;
    }

    inline juce::File getLogsDirectory()
    {
#if JUCE_MAC
        // macOS: ~/Library/Logs/Kinu Tune/
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Logs")
            .getChildFile(kProductFolderName);
#elif JUCE_WINDOWS
        // Windows: %APPDATA%/Kinu Tune/Logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(kProductFolderName)
            .getChildFile("Logs");
#else
        // Linux: ~/.config/Kinu Tune/logs/
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(kProductFolderName)
            .getChildFile("logs");
#endif
    }

    inline juce::File getConfigDirectory()
    {
        // All platforms use userApplicationDataDirectory
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(kProductFolderName);
    }

    inline juce::File getLogFile(const juce::String &name)
    {
        auto logsDir = getLogsDirectory();
        logsDir.createDirectory();
        return logsDir.getChildFile(name);
    }

    inline juce::File getConfigFile(const juce::String &name)
    {
        auto configDir = getConfigDirectory();
        configDir.createDirectory();
        return configDir.getChildFile(name);
    }
}
