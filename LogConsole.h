#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

class LogConsole : public juce::Component,
                   private juce::Logger,
                   private juce::Timer
{
public:
    LogConsole()
    {
        console.setMultiLine (true);
        console.setReadOnly (true);
        console.setScrollBarThickness (12);
        console.setFont (juce::FontOptions (12.0f, juce::Font::plain));
        console.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1e1e1e));
        console.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd4d4d4));
        console.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3c3c3c));
        addAndMakeVisible (console);

        clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3c3c3c));
        clearButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd4d4d4));
        clearButton.onClick = [this] { clear(); };
        addAndMakeVisible (clearButton);

        juce::Logger::setCurrentLogger (this);

        startTimerHz (10);
    }

    ~LogConsole() override
    {
        stopTimer();

        if (juce::Logger::getCurrentLogger() == this)
            juce::Logger::setCurrentLogger (nullptr);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        clearButton.setBounds (r.removeFromTop (20).removeFromRight (60).reduced (2));

        if (collapsed)
        {
            console.setBounds (r.withHeight (0)); // Hide console when collapsed
        }
        else
        {
            console.setBounds (r);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e1e));
        g.setColour (juce::Colour (0xff3c3c3c));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    }

    void clear()
    {
        console.clear();
    }

    void setCollapsed (bool shouldCollapse)
    {
        collapsed = shouldCollapse;
        console.setVisible (!collapsed);
        resized();
        repaint();
    }

    bool isCollapsed() const noexcept
    {
        return collapsed;
    }

    int getPreferredHeight() const noexcept
    {
        return collapsed ? 22 : 170;
    }

private:
    void logMessage (const juce::String& message) override
    {
        const juce::ScopedLock lock (mutex);
        pending.push_back (message);
        if ((int) pending.size() > maxLines)
            pending.pop_front();
    }

    void timerCallback() override
    {
        std::deque<juce::String> messages;
        {
            const juce::ScopedLock lock (mutex);
            std::swap (messages, pending);
        }

        if (! messages.empty())
        {
            juce::String text;
            for (const auto& msg : messages)
                text += msg + "\n";

            console.moveCaretToEnd();
            console.insertTextAtCaret (text);
        }
    }

    juce::TextEditor console;
    juce::TextButton clearButton { "Clear" };
    juce::CriticalSection mutex;
    std::deque<juce::String> pending;
    int maxLines = 200;
    bool collapsed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogConsole)
};
