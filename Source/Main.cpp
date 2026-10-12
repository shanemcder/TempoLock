#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <complex>

class MainComponent final : public juce::AudioAppComponent,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent() : thumbnail(512, formatManager, thumbnailCache)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener(this);

        addAndMakeVisible(importButton);
        importButton.setButtonText("Import Audio");
        importButton.onClick = [this]
        { chooseFile(); };

        addAndMakeVisible(playButton);
        playButton.setButtonText("Play");
        playButton.onClick = [this]
        { transport.start(); };

        addAndMakeVisible(pauseButton);
        pauseButton.setButtonText("Pause");
        pauseButton.onClick = [this]
        { transport.stop(); };

        addAndMakeVisible(stopButton);
        stopButton.setButtonText("Stop");
        stopButton.onClick = [this]
        {
            transport.stop();
            transport.setPosition(0.0);
            repaint();
        };

        addAndMakeVisible(analyzeButton);
        analyzeButton.setButtonText("Analyze Audio");
        analyzeButton.onClick = [this]
        { analyzeAudio(); };

        addAndMakeVisible(exportButton);
        exportButton.setButtonText("Export WAV");
        exportButton.onClick = [this]
        { chooseExport(); };

        addAndMakeVisible(referenceButton);
        referenceButton.setButtonText("Compare Reference");
        referenceButton.setTooltip("Compare the loaded audio with a manually corrected WAV");
        referenceButton.onClick = [this]
        { chooseReference(); };

        addAndMakeVisible(showOnsetsToggle);
        showOnsetsToggle.setButtonText("Show raw onsets");
        showOnsetsToggle.setToggleState(false, juce::dontSendNotification);
        showOnsetsToggle.onClick = [this]
        { repaint(); };

        addAndMakeVisible(helpLabel);
        helpLabel.setText("Analyze, then Shift-click waveform to add beat anchors; Option-click to remove.", juce::dontSendNotification);
        helpLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        addAndMakeVisible(bpmLabel);
        bpmLabel.setText("Target BPM:", juce::dontSendNotification);
        addAndMakeVisible(bpmEditor);
        bpmEditor.setText("118");
        bpmEditor.setInputRestrictions(3, "0123456789");
        bpmEditor.setTooltip("Target tempo for exported audio");

        addAndMakeVisible(statusLabel);
        statusLabel.setText("No audio loaded", juce::dontSendNotification);

        setSize(1050, 615);
        setAudioChannels(0, 2);
        startTimerHz(30);
    }

    ~MainComponent() override
    {
        stopTimer();
        transport.stop();
        transport.setSource(nullptr);
        thumbnail.removeChangeListener(this);
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override
    {
        transport.getNextAudioBlock(bufferToFill);
    }

    void releaseResources() override
    {
        transport.releaseResources();
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colour(0xff17191d));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(28.0f)));
        g.drawText("TempoLock", 30, 16, getWidth() - 60, 54,
                   juce::Justification::centredLeft);

        auto area = waveformBounds();
        g.setColour(juce::Colour(0xff252830));
        g.fillRoundedRectangle(area.toFloat(), 10.0f);

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour(juce::Colour(0xff7eb6ff));
            thumbnail.drawChannels(g, area.reduced(10), 0.0,
                                   thumbnail.getTotalLength(), 1.0f);

            g.setColour(juce::Colour(0xff66e0a3));
            for (auto seconds : (showOnsetsToggle.getToggleState() ? onsetTimes : std::vector<double>{}))
            {
                const auto x = area.getX() + (float)(seconds / thumbnail.getTotalLength()) * area.getWidth();
                g.drawLine(x, (float)area.getY(), x, (float)area.getBottom(), 1.0f);
            }

            g.setColour(juce::Colour(0xffffcb70));
            for (auto seconds : beatAnchors)
            {
                const auto x = area.getX() + (float)(seconds / thumbnail.getTotalLength()) * area.getWidth();
                g.drawLine(x, (float)area.getY(), x, (float)area.getBottom(), 2.0f);
            }

            const auto fraction = juce::jlimit(0.0, 1.0,
                                               transport.getCurrentPosition() / thumbnail.getTotalLength());
            const auto cursorX = area.getX() + (int)(fraction * area.getWidth());
            g.setColour(juce::Colour(0xffffb454));
            g.drawLine((float)cursorX, (float)area.getY(),
                       (float)cursorX, (float)area.getBottom(), 2.0f);
        }
        else
        {
            g.setColour(juce::Colours::lightgrey);
            g.drawText("Import an audio file to display its waveform",
                       area, juce::Justification::centred);
        }
    }

    void resized() override
    {
        importButton.setBounds(30, 94, 135, 36);
        playButton.setBounds(177, 94, 76, 36);
        pauseButton.setBounds(265, 94, 76, 36);
        stopButton.setBounds(353, 94, 76, 36);
        analyzeButton.setBounds(444, 94, 130, 36);
        exportButton.setBounds(586, 94, 125, 36);
        bpmLabel.setBounds(730, 94, 100, 36);
        bpmEditor.setBounds(830, 94, 70, 36);
        showOnsetsToggle.setBounds(915, 94, 125, 36);
        referenceButton.setBounds(30, 141, 170, 32);
        helpLabel.setBounds(30, getHeight() - 92, getWidth() - 60, 24);
        statusLabel.setBounds(30, getHeight() - 58, getWidth() - 60, 30);
    }

    void mouseDown(const juce::MouseEvent &event) override
    {
        const auto area = waveformBounds();
        if (area.contains(event.getPosition()) && thumbnail.getTotalLength() > 0.0)
        {
            const double fraction = juce::jlimit(0.0, 1.0,
                                                 (double)(event.x - area.getX()) / (double)area.getWidth());
            const double seconds = fraction * thumbnail.getTotalLength();
            if (event.mods.isShiftDown())
            {
                beatAnchors.push_back(seconds);
                std::sort(beatAnchors.begin(), beatAnchors.end());
                beatAnchors.erase(std::unique(beatAnchors.begin(), beatAnchors.end()), beatAnchors.end());
            }
            else if (event.mods.isAltDown() && !beatAnchors.empty())
            {
                auto it = std::min_element(beatAnchors.begin(), beatAnchors.end(),
                                           [seconds](double a, double b)
                                           { return std::abs(a - seconds) < std::abs(b - seconds); });
                beatAnchors.erase(it);
            }
            else
                transport.setPosition(seconds);
            repaint();
        }
    }

private:
    juce::Rectangle<int> waveformBounds() const
    {
        return {30, 185, juce::jmax(100, getWidth() - 60),
                juce::jmax(100, getHeight() - 240)};
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Choose an audio file", juce::File{}, "*.wav;*.aiff;*.aif;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser &fc)
                             {
                                 auto file = fc.getResult();
                                 if (file.existsAsFile())
                                     loadFile(file);
                             });
    }

    void loadFile(const juce::File &file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
        {
            statusLabel.setText("Could not read this audio format", juce::dontSendNotification);
            return;
        }

        const double sourceRate = reader->sampleRate;
        transport.stop();
        transport.setSource(nullptr);
        readerSource.reset();
        readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
        transport.setSource(readerSource.get(), 0, nullptr, sourceRate);
        transport.setPosition(0.0);
        loadedFile = file;
        onsetTimes.clear();
        beatAnchors.clear();
        referenceFile = juce::File();
        thumbnail.setSource(new juce::FileInputSource(file));
        statusLabel.setText(file.getFileName(), juce::dontSendNotification);
        repaint();
    }

    // A spectral-flux onset envelope is less sensitive to sustained sound than RMS changes.
    void analyzeAudio()
    {
        if (!loadedFile.existsAsFile())
            return;
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(loadedFile));
        if (!reader || reader->sampleRate <= 0.0)
            return;

        constexpr int fftOrder = 11;
        constexpr int windowSize = 1 << fftOrder;
        constexpr int hop = 512;
        juce::dsp::FFT fft(fftOrder);
        juce::dsp::WindowingFunction<float> window(windowSize,
                                                   juce::dsp::WindowingFunction<float>::hann);
        juce::AudioBuffer<float> block(juce::jmax(1, (int)reader->numChannels), windowSize);
        std::vector<float> fftData((size_t)windowSize * 2, 0.0f);
        std::vector<float> previous((size_t)windowSize / 2, 0.0f);
        std::vector<float> novelty;
        const int frames = (int)juce::jmin((juce::int64)500000,
                                           (reader->lengthInSamples + hop - 1) / hop);
        novelty.reserve((size_t)frames);
        for (int i = 0; i < frames; ++i)
        {
            block.clear();
            if (!reader->read(&block, 0, windowSize, (juce::int64)i * hop, true, true))
                break;
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            for (int n = 0; n < windowSize; ++n)
            {
                float sum = 0.0f;
                for (int c = 0; c < block.getNumChannels(); ++c)
                    sum += block.getSample(c, n);
                fftData[(size_t)n] = sum / (float)block.getNumChannels();
            }
            window.multiplyWithWindowingTable(fftData.data(), windowSize);
            fft.performFrequencyOnlyForwardTransform(fftData.data());
            double flux = 0.0;
            for (int k = 2; k < windowSize / 2; ++k)
            {
                const float magnitude = std::log1p(fftData[(size_t)k]);
                flux += std::max(0.0f, magnitude - previous[(size_t)k]);
                previous[(size_t)k] = magnitude;
            }
            novelty.push_back((float)(flux / (windowSize / 2)));
        }

        const double hopSeconds = (double)hop / reader->sampleRate;
        std::vector<float> whitened(novelty.size(), 0.0f);
        for (int i = 0; i < (int)novelty.size(); ++i)
        {
            float local = 0.0f;
            const int lo = std::max(0, i - 25), hi = std::min((int)novelty.size() - 1, i + 25);
            for (int j = lo; j <= hi; ++j)
                local += novelty[(size_t)j];
            local /= (float)(hi - lo + 1);
            whitened[(size_t)i] = std::max(0.0f, novelty[(size_t)i] - local * 1.15f);
        }
        onsetTimes.clear();
        const int minSeparation = std::max(1, (int)std::round(0.09 / hopSeconds));
        int last = -minSeparation;
        for (int i = 2; i + 2 < (int)whitened.size(); ++i)
        {
            if (i - last < minSeparation)
                continue;
            float localMax = 0.0f;
            for (int j = std::max(0, i - 60); j <= std::min((int)whitened.size() - 1, i + 60); ++j)
                localMax = std::max(localMax, whitened[(size_t)j]);
            if (localMax > 0.0001f && whitened[(size_t)i] >= localMax * 0.28f &&
                whitened[(size_t)i] >= whitened[(size_t)i - 1] &&
                whitened[(size_t)i] > whitened[(size_t)i + 1])
            {
                onsetTimes.push_back(i * hopSeconds);
                last = i;
            }
        }
        buildBeatAnchors(whitened, hopSeconds);
        statusLabel.setText(loadedFile.getFileName() + " | " +
                                juce::String(estimatedBpm, 1) + " estimated BPM | " +
                                juce::String((int)beatAnchors.size()) + " confident beats | " +
                                juce::String((int)onsetTimes.size()) + " onsets",
                            juce::dontSendNotification);
        repaint();
    }

    void buildBeatAnchors(const std::vector<float> &strength, double dt)
    {
        beatAnchors.clear();
        estimatedBpm = 0.0;
        if (strength.size() < 200 || dt <= 0.0 || onsetTimes.size() < 8)
            return;

        // Determine a robust global pulse from the onset-strength autocorrelation.
        // Evaluate harmonic support so eighth-note strums are not always interpreted as beats.
        const int minLag = std::max(1, (int)std::round(60.0 / (180.0 * dt)));
        const int maxLag = (int)std::round(60.0 / (65.0 * dt));
        int bestLag = minLag;
        double bestScore = -1.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double score = 0.0;
            for (int i = lag; i < (int)strength.size(); i += 2)
            {
                score += strength[(size_t)i] * strength[(size_t)(i - lag)];
                if (i >= 2 * lag)
                    score += 0.35 * strength[(size_t)i] * strength[(size_t)(i - 2 * lag)];
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestLag = lag;
            }
        }
        if (bestScore <= 0.0)
            return;
        const double period = bestLag * dt;
        estimatedBpm = 60.0 / period;

        // Find a contiguous rhythmic region: several consecutive expected beats
        // must have a nearby onset. Isolated ambient transients cannot start a grid.
        double bestPhase = 0.0, bestPhaseScore = -1.0;
        const int tolerance = std::max(2, (int)std::round(0.085 / dt));
        for (int phase = 0; phase < bestLag; ++phase)
        {
            double score = 0.0;
            int run = 0;
            for (int center = phase; center < (int)strength.size(); center += bestLag)
            {
                float peak = 0.0f;
                for (int d = -tolerance; d <= tolerance; ++d)
                {
                    const int j = center + d;
                    if (j >= 0 && j < (int)strength.size())
                        peak = std::max(peak, strength[(size_t)j]);
                }
                if (peak > 0.0001f)
                    ++run;
                else
                    run = 0;
                score += peak * (run >= 3 ? 1.5 : 0.5);
            }
            if (score > bestPhaseScore)
            {
                bestPhaseScore = score;
                bestPhase = phase * dt;
            }
        }

        // Follow observed beat attacks, allowing modest tempo variation.
        // Only anchor a run after three supported beats. Stop during long rests.
        std::vector<double> candidate;
        std::vector<bool> supported;
        const double duration = thumbnail.getTotalLength();
        double prediction = bestPhase;
        double lastBeat = -1.0;
        for (; prediction < duration; prediction += period)
        {
            const double expected = lastBeat < 0.0 ? prediction : lastBeat + period;
            const double radius = std::min(0.105, period * 0.20);
            const int lo = std::max(0, (int)std::round((expected - radius) / dt));
            const int hi = std::min((int)strength.size() - 1, (int)std::round((expected + radius) / dt));
            int peakIndex = -1;
            float peak = 0.0f;
            for (int j = lo; j <= hi; ++j)
                if (strength[(size_t)j] > peak)
                {
                    peak = strength[(size_t)j];
                    peakIndex = j;
                }
            const bool hasBeat = peakIndex >= 0 && peak > 0.0001f;
            const double beat = hasBeat ? peakIndex * dt : expected;
            candidate.push_back(beat);
            supported.push_back(hasBeat);
            lastBeat = beat;
            if (candidate.size() > 200000)
                break;
        }
        // Retain beats inside stable runs only, including single missed beats.
        // No time-warp anchors are generated in long ambient sections.
        for (size_t i = 0; i < candidate.size(); ++i)
        {
            int count = 0;
            for (int j = std::max(0, (int)i - 2);
                 j <= std::min((int)candidate.size() - 1, (int)i + 2); ++j)
                count += supported[(size_t)j] ? 1 : 0;
            if (count >= 3 && (beatAnchors.empty() || candidate[i] > beatAnchors.back() + period * 0.5))
                beatAnchors.push_back(candidate[i]);
        }
    }

    void chooseReference()
    {
        if (!loadedFile.existsAsFile())
        {
            statusLabel.setText("Import the TempoLock output before comparing", juce::dontSendNotification);
            return;
        }
        referenceChooser = std::make_unique<juce::FileChooser>(
            "Choose manually corrected reference", juce::File{}, "*.wav;*.aiff;*.aif;*.mp3");
        referenceChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                          juce::FileBrowserComponent::canSelectFiles,
                                      [this](const juce::FileChooser &fc)
                                      {
                                          const auto file = fc.getResult();
                                          if (file.existsAsFile())
                                              compareReference(file);
                                      });
    }

    static std::vector<float> energyEnvelope(juce::AudioFormatReader &reader)
    {
        // One RMS value per 20 ms. This comparison is diagnostic, not a beat detector.
        const int blockSize = juce::jmax(1, (int)std::round(reader.sampleRate * 0.02));
        juce::AudioBuffer<float> block((int)juce::jmax((unsigned int)1, reader.numChannels), blockSize);
        const auto count = (int)juce::jmin((juce::int64)30000,
                                           (reader.lengthInSamples + blockSize - 1) / blockSize);
        std::vector<float> envelope;
        envelope.reserve((size_t)count);
        for (int i = 0; i < count; ++i)
        {
            block.clear();
            const auto start = (juce::int64)i * blockSize;
            const int available = (int)juce::jmin((juce::int64)blockSize,
                                                  reader.lengthInSamples - start);
            if (available <= 0 || !reader.read(&block, 0, available, start, true, true))
                break;
            double sum = 0.0;
            for (int c = 0; c < block.getNumChannels(); ++c)
                for (int n = 0; n < available; ++n)
                {
                    const double v = block.getSample(c, n);
                    sum += v * v;
                }
            envelope.push_back((float)std::sqrt(sum / (available * block.getNumChannels())));
        }
        return envelope;
    }

    void compareReference(const juce::File &file)
    {
        std::unique_ptr<juce::AudioFormatReader> original(formatManager.createReaderFor(loadedFile));
        std::unique_ptr<juce::AudioFormatReader> reference(formatManager.createReaderFor(file));
        if (!original || !reference)
        {
            statusLabel.setText("Could not read one of the comparison files", juce::dontSendNotification);
            return;
        }
        auto a = energyEnvelope(*original);
        auto b = energyEnvelope(*reference);
        const double durationA = (double)original->lengthInSamples / original->sampleRate;
        const double durationB = (double)reference->lengthInSamples / reference->sampleRate;
        // Search a +/- 2 second offset in multiple independent 6-second windows.
        // A positive offset means the loaded audio occurs later than the reference.
        std::vector<double> offsets;
        std::vector<double> scores;
        constexpr int window = 300, maxLag = 100;
        const int limit = (int)std::min(a.size(), b.size());
        for (int center = 400; center + window / 2 + maxLag < limit; center += 400)
        {
            double best = -2.0;
            int bestLag = 0;
            for (int lag = -maxLag; lag <= maxLag; ++lag)
            {
                double ab = 0, aa = 0, bb = 0;
                for (int k = -window / 2; k < window / 2; ++k)
                {
                    const int ai = center + k + lag;
                    const int bi = center + k;
                    if (ai < 0 || ai >= (int)a.size() || bi < 0 || bi >= (int)b.size())
                        continue;
                    const double x = a[(size_t)ai], y = b[(size_t)bi];
                    ab += x * y;
                    aa += x * x;
                    bb += y * y;
                }
                const double score = ab / std::sqrt(aa * bb + 1.0e-18);
                if (score > best)
                {
                    best = score;
                    bestLag = lag;
                }
            }
            if (best > 0.75)
            {
                offsets.push_back(bestLag * 0.02);
                scores.push_back(best);
            }
        }
        if (offsets.empty())
        {
            statusLabel.setText("Reference comparison: no reliable matching windows; durations differ by " + juce::String(durationA - durationB, 2) + " s", juce::dontSendNotification);
            return;
        }
        std::sort(offsets.begin(), offsets.end());
        const double median = offsets[offsets.size() / 2];
        const double range = offsets.back() - offsets.front();
        referenceFile = file;
        statusLabel.setText("Reference: " + juce::String((int)offsets.size()) + " matched windows | median offset " + juce::String(median, 3) + " s | offset range " + juce::String(range, 3) + " s | duration difference " + juce::String(durationA - durationB, 3) + " s",
                            juce::dontSendNotification);
    }

    void chooseExport()
    {
        if (!loadedFile.existsAsFile() || beatAnchors.size() < 2)
        {
            statusLabel.setText("Import and analyze audio before exporting", juce::dontSendNotification);
            return;
        }
        exportChooser = std::make_unique<juce::FileChooser>(
            "Save corrected WAV", loadedFile.getSiblingFile(loadedFile.getFileNameWithoutExtension() + "_locked.wav"), "*.wav");
        exportChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                       juce::FileBrowserComponent::canSelectFiles |
                                       juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this](const juce::FileChooser &fc)
                                   {
                                       auto output = fc.getResult();
                                       if (output != juce::File{})
                                           exportWav(output.withFileExtension(".wav"));
                                   });
    }

    void exportWav(const juce::File &output)
    {
        auto bpm = bpmEditor.getText().getDoubleValue();
        if (bpm < 30.0 || bpm > 300.0)
        {
            statusLabel.setText("Choose a BPM from 30 to 300", juce::dontSendNotification);
            return;
        }
        const juce::File rubberBand("/opt/homebrew/bin/rubberband");
        const juce::File intelRubberBand("/usr/local/bin/rubberband");
        auto executable = rubberBand.existsAsFile() ? rubberBand : intelRubberBand;
        if (!executable.existsAsFile())
        {
            statusLabel.setText("Install Rubber Band first: brew install rubberband", juce::dontSendNotification);
            return;
        }
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(loadedFile));
        if (!reader)
            return;
        auto tempInput = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("tempolock_input", ".wav");
        auto tempMap = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("tempolock_map", ".txt");
        {
            auto stream = tempInput.createOutputStream();
            if (!stream)
                return;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
                stream.release(), reader->sampleRate, reader->numChannels, 24, {}, 0));
            if (!writer)
                return;
            if (!writer->writeFromAudioReader(*reader, 0, -1))
                return;
        }
        // Rubber Band time map: source frame -> output frame.
        // Preserve the pre-roll; anchor the first detected beat at its original position.
        const double beatSeconds = 60.0 / bpm;
        // Reject invalid maps and extreme local stretching before writing files.
        for (size_t i = 1; i < beatAnchors.size(); ++i)
        {
            const double gap = beatAnchors[i] - beatAnchors[i - 1];
            if (gap <= 0.0 || (gap < 1.5 * 60.0 / estimatedBpm &&
                               (beatSeconds / gap < 0.5 || beatSeconds / gap > 2.0)))
            {
                statusLabel.setText("Unsafe beat spacing. Edit anchors or check source tempo.",
                                    juce::dontSendNotification);
                return;
            }
        }
        std::ofstream map(tempMap.getFullPathName().toStdString());
        auto sr = reader->sampleRate;
        map << "0 0\n";
        const double first = beatAnchors.front();
        double targetTime = first;
        double lastTargetTime = first;
        for (size_t i = 0; i < beatAnchors.size(); ++i)
        {
            if (i > 0)
            {
                const double gap = beatAnchors[i] - beatAnchors[i - 1];
                // Keep long untracked passages at their original duration.
                targetTime += gap > 1.5 * (60.0 / estimatedBpm) ? gap : beatSeconds;
            }
            auto sourceFrame = (juce::int64)std::llround(beatAnchors[i] * sr);
            auto targetFrame = (juce::int64)std::llround(targetTime * sr);
            map << sourceFrame << " " << targetFrame << "\n";
            lastTargetTime = targetTime;
        }
        auto lastSource = (juce::int64)std::llround(beatAnchors.back() * sr);
        auto lastTarget = (juce::int64)std::llround(lastTargetTime * sr);
        auto endSource = reader->lengthInSamples;
        if (endSource > lastSource)
            map << endSource << " " << lastTarget + (endSource - lastSource) << "\n";
        map.close();

        juce::ChildProcess process;
        const double outputDuration = lastTargetTime + (reader->lengthInSamples / reader->sampleRate - beatAnchors.back());
        if (!std::isfinite(outputDuration) || outputDuration <= 0.0)
        {
            statusLabel.setText("Invalid output duration", juce::dontSendNotification);
            tempInput.deleteFile();
            tempMap.deleteFile();
            return;
        }
        juce::StringArray args{executable.getFullPathName(), "--fine",
                               "--duration", juce::String(outputDuration, 6), "--timemap",
                               tempMap.getFullPathName(), tempInput.getFullPathName(), output.getFullPathName()};
        if (!process.start(args))
        {
            statusLabel.setText("Unable to start Rubber Band", juce::dontSendNotification);
        }
        else
        {
            const bool finished = process.waitForProcessToFinish(180000);
            const auto exitCode = finished ? process.getExitCode() : -1;
            if (!finished)
                process.kill();
            statusLabel.setText(exitCode == 0 && output.existsAsFile()
                                    ? "Exported: " + output.getFullPathName()
                                    : "Export failed (" + juce::String(exitCode) + "): " + process.readAllProcessOutput().substring(0, 500),
                                juce::dontSendNotification);
        }
        tempInput.deleteFile();
        tempMap.deleteFile();
    }

    void changeListenerCallback(juce::ChangeBroadcaster *) override { repaint(); }
    void timerCallback() override { repaint(waveformBounds()); }

    juce::TextButton importButton, playButton, pauseButton, stopButton, analyzeButton, exportButton, referenceButton;
    juce::ToggleButton showOnsetsToggle;
    double estimatedBpm = 0.0;
    juce::File loadedFile, referenceFile;
    std::vector<double> onsetTimes, beatAnchors;
    juce::Label bpmLabel, statusLabel, helpLabel;
    juce::TextEditor bpmEditor;
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache{5};
    juce::AudioThumbnail thumbnail;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> chooser, exportChooser, referenceChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class TempoLockApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "TempoLock"; }
    const juce::String getApplicationVersion() override { return "1.3.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String &) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String &name)
            : DocumentWindow(name, juce::Colours::darkgrey, allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(TempoLockApplication)
