#include "plugin.hpp"


static const char *const PERSIST_KEY_RUNNING = "running";

static const char *const PERSIST_KEY_SYNC = "synchronize";

// The amount to scale voltages supplied to the threshold CV input by. Typical application is
// a bipolar control voltage [-5, 5]V, allowing the final threshold value to be 0.5 below or
// above the value set on the knob.
static const float THRESHOLD_CV_SCALING_FACTOR = 0.1f;

// TODO: Is there a documented, requested code style for VCV modules?
struct ClockSync : Module {
    enum ParamIds {
        RUNTOGGLE_PARAM,
        SYNCTOGGLE_PARAM,
        THRESHKNOB_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        RUNCV_INPUT,
        SYNCCV_INPUT,
        THRESHCV_INPUT,
        MAINCLKIN_INPUT,
        EXTCLKIN_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        EXTCLKOUT_OUTPUT,
        SYNCOUT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        ENUMS(SYNCLED_LIGHT, 2), // Need two for green+red
        RUNNING_LIGHT,
        SYNCTOGGLE_LIGHT,
        NUM_LIGHTS
    };

    // TODO: Make configurable -- context menu?
    const unsigned short NUM_PPQN = 24;
    const float OUTPUT_PULSE_DURATION = 5e-3f;

    dsp::SchmittTrigger mainClockTrigger, extClockTrigger;
    unsigned long sampleIndex = 0;
    float curSampleTime = 0.0f;
    bool currentlySynchronized = false;


    // Using args.sampleTime accumulates fp error, so we use
    // the number of samples and the sample rate to calculate
    // time estimates. But we only actually do the expensive
    // division upon request, which will be relatively infrequent.
    struct SampleTimer {
        int curSample = 0;

        void reset() {
          curSample = 0;
        }

        void tick() {
          curSample++;
        }

        float getElapsed(float sampleRate) const {
          return (float) curSample / sampleRate;
        }
    };

    struct ClockTiming {
        float currentSampleRate = 0.0f;

        SampleTimer periodTimer;
        float timePerPeriod = 0.0f;
        float halfPeriod = 0.0f;

        // TODO: Don't actually need these outside of dev debugging
        float beatsPerSecond = 0.0f, beatsPerMinute = 0.0f;

    };

    struct InternalClock {
        int numSamples = 0;
        float currentTime = 0.0f;
        float estimatedTime = 0.0f;
    };

    struct OutputClock {
        dsp::PulseGenerator pulseGenerator;

        bool active = false;
        int pulsesPerQN = 0;
        int pulsesThisNote = 0;
        float curNoteTime = 0.0f;
        float timePerPulse = 0.0f;
    };

    // Represents state that can be toggled by either a button or external CV (e.g. run, sync)
    struct CVButtonToggle {
        dsp::BooleanTrigger trigger;

        Param *button;
        Input *cv;
        bool state;

        CVButtonToggle(Param *button, Input *cv, bool initialState) {
          this->button = button;
          this->cv = cv;
          this->state = initialState;
        }

        // Processes the button and CV inputs and toggles the state if necessary. Returns
        // true if there was a change and false otherwise. Check the value of the state field
        // to get the most recent state.
        bool process() {
          // Check the CV input first if it's connected
          if (cv->isConnected()) {
            // TODO: Extract constant for the threshold -- is there a VCV standard I should follow?
            bool shouldBeEnabled = cv->getVoltage() > 1.0f;
            if (shouldBeEnabled != state) {
              state = shouldBeEnabled;
              return true;
            }
          } else {
            // TODO: Review voltage standards doc; not sure >0 is proper here
            // The CV input isn't connected, so use the button instead
            if (trigger.process(button->getValue() > 0.0f)) {
              state = !state;
              return true;
            }
          }

          return false;
        }
    };

    ClockTiming mainClock;
    ClockTiming externalClock;
    OutputClock outputClock;

    InternalClock internalClock;

    // TODO: Dynamic initialization seems uncommon--is this a poor practice for VCV plugins?
    std::unique_ptr<CVButtonToggle> runToggle;
    std::unique_ptr<CVButtonToggle> syncToggle;


    ClockSync() {
      // TODO: Add labels and set ranges/defaults
      config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
      configParam(RUNTOGGLE_PARAM, 0.f, 1.f, 0.f, "");
      configParam(SYNCTOGGLE_PARAM, 0.f, 1.f, 0.f, "");
      configParam(THRESHKNOB_PARAM, 1.0e-4f, 0.5f, 2.0e-4f, "");

      // TODO: Make configurable
      outputClock.pulsesPerQN = NUM_PPQN;

      // Initialize the run and sync toggles (using reset since we can't initialize until after the params and
      // inputs have been created, which means we can't use member initialization).
      runToggle.reset(new CVButtonToggle(&params[RUNTOGGLE_PARAM], &inputs[RUNCV_INPUT], false));
      syncToggle.reset(new CVButtonToggle(&params[SYNCTOGGLE_PARAM], &inputs[SYNCCV_INPUT], false));
    }

    void onAdd() override {
      Module::onAdd();

      setSampleRate();
    }

    void onSampleRateChange() override {
      Module::onSampleRateChange();

      setSampleRate();
    }

    void setSampleRate() {
      mainClock.currentSampleRate = APP->engine->getSampleRate();
      externalClock.currentSampleRate = APP->engine->getSampleRate();

      DEBUG("Sample Rate Changed");
    }

    static void updateInternalClock(const ProcessArgs &args, InternalClock *clock) {
      clock->currentTime += args.sampleTime;
      clock->numSamples++;
      clock->estimatedTime = (float) clock->numSamples / args.sampleRate;
    }

    // Returns the value of the threshold knob. If the threshold CV input is connected, the supplied voltage
    // is multiplied by the scaling factor and added to the threshold value.
    float getThresholdValue() {
      float baseThresholdValue = params[THRESHKNOB_PARAM].getValue();
      if (inputs[THRESHCV_INPUT].isConnected()) {
        baseThresholdValue += inputs[THRESHCV_INPUT].getVoltage() * THRESHOLD_CV_SCALING_FACTOR;
      }

      return baseThresholdValue;
    }

    // TODO: With sync off, error accumulates; figure out a way to keep in sync on a micro
    //       level without breaking the macro sync wrt non-quarter note external gates
    //       NB: Error accumulation with doubles rather than floats is significantly less (but not zero)...
    void process(const ProcessArgs &args) override {
      runToggle->process();
      syncToggle->process();

      curSampleTime += args.sampleTime;
      sampleIndex++;
      updateInternalClock(args, &internalClock);


      if (updateClockTiming(&mainClockTrigger, &inputs[MAINCLKIN_INPUT], &mainClock)) {
        outputClock.timePerPulse = mainClock.timePerPeriod / (float) NUM_PPQN;

        // We don't want to start sending output clock pulses until we've gotten timing from
        // the main clock; setting outputClock.active here signals that we're ready to send
        // those pulses. Also ensure that the time per pulse is at least as big as the pulse
        // period--the output gate would never be off otherwise.
        if (outputClock.timePerPulse > OUTPUT_PULSE_DURATION) {
          outputClock.active = true;
        }

#ifdef CLOCKSYNC_DEBUG
        DEBUG(
            "Got Clock Trigger: sppqn=%f / %f / %f / %f",
            mainClock.timePerPeriod, outputClock.timePerPulse,
            mainClock.beatsPerSecond, mainClock.beatsPerMinute
        );

        DEBUG(
            "Int Clock: Samples=%d, Est Time=%f, Cum Time=%f",
            internalClock.numSamples, internalClock.estimatedTime, internalClock.currentTime
        );
#endif
      }

      if (updateClockTiming(&extClockTrigger, &inputs[EXTCLKIN_INPUT], &externalClock)) {
        float offset = mainClock.periodTimer.getElapsed(args.sampleRate);
        float delay = mainClock.timePerPeriod - offset;
        float thresh = getThresholdValue();
        float error = (mainClock.halfPeriod - abs(offset - mainClock.halfPeriod)) / mainClock.halfPeriod;
        currentlySynchronized = error <= thresh;

        if (!currentlySynchronized && syncToggle->state) {
          if (offset > mainClock.halfPeriod) {
            // We're early: delay the next clock pulse by (period - offset)
            outputClock.curNoteTime -= delay;
          } else {
            // We're late: speed up the clock so that we finish a complete cycle just in time for the
            // next main clock beat. Once that beat arrives, the external clock period will be readjusted
            // to match the main clock.
            outputClock.timePerPulse = delay / (float) (NUM_PPQN);
          }
        }

#ifdef CLOCKSYNC_DEBUG
        DEBUG(
            "Got ext trigger:  sppqn=%f / %f / %f; err=%f, thresh=%f, off=%f, dly=%f, cNT=%f, tPP=%f, remPulses=%d, tSLP=%f, currentlySynchronized=%d",
            externalClock.timePerPeriod,
            externalClock.beatsPerSecond, externalClock.beatsPerMinute,
            error, thresh, offset, delay,
            outputClock.curNoteTime, outputClock.timePerPulse,
            (NUM_PPQN - outputClock.pulsesThisNote),
            currentlySynchronized
        );
#endif

        if (!currentlySynchronized) {
          lights[SYNCLED_LIGHT + 0].setBrightness(0);
          lights[SYNCLED_LIGHT + 1].setBrightness(error);
        } else {
          lights[SYNCLED_LIGHT + 0].setBrightness(1);
          lights[SYNCLED_LIGHT + 1].setBrightness(0);
        }
      }

      if (runToggle->state && processOutputClock(&outputClock, args.sampleTime)) {
        // TODO: extract constant; is there a VCV constant for the correct value for max V?
        outputs[EXTCLKOUT_OUTPUT].value = 10;
      } else {
        outputs[EXTCLKOUT_OUTPUT].value = 0;
      }

      lights[RUNNING_LIGHT].value = runToggle->state;
      lights[SYNCTOGGLE_LIGHT].value = syncToggle->state;
    }

    bool processOutputClock(OutputClock *outputClock, float dT) const {
      if (!outputClock->active) {
        return false;
      }

      outputClock->curNoteTime += dT;

      if (outputClock->curNoteTime >= outputClock->timePerPulse) {
        outputClock->pulseGenerator.trigger(OUTPUT_PULSE_DURATION);
        outputClock->curNoteTime -= outputClock->timePerPulse;
        outputClock->pulsesThisNote++;

        // The external sequencer and ClockSync may not be aligned on *which* pulse is actually the final
        // pulse of a quarter note, but we still want to keep track of when we've completed a quarter note's
        // worth of pulses in order to reset floating point timers.
        if (outputClock->pulsesThisNote % outputClock->pulsesPerQN == 0) {
#ifdef CLOCKSYNC_DEBUG
          DEBUG("Completed output quarter note (cNT=%f, tPP=%f)", outputClock->curNoteTime, outputClock->timePerPulse);
#endif
          outputClock->pulsesThisNote = 0;

          // Reset the current note time--otherwise we'll get accumulating floating point error; this may
          // result in a consistent offset below the threshold, but it shouldn't get *worse* over time,
          // at least to the extent that other timing factors remain the same.
          outputClock->curNoteTime = 0.0f;
        }
      }

      return outputClock->pulseGenerator.process(dT);
    }

    static bool
    updateClockTiming(dsp::SchmittTrigger *const trigger, Input *const inputPort, ClockTiming *timing) {
      timing->periodTimer.tick();
      float voltage = inputPort->getVoltage();

//      if (trigger->process(math::rescale(voltage, 0.1f, 2.f, 0.f, 1.f))) {
      if (trigger->process(voltage)) {
        // TODO: The sample rate can be module-wide
        timing->timePerPeriod = timing->periodTimer.getElapsed(timing->currentSampleRate);
        timing->halfPeriod = timing->timePerPeriod / 2.0f;

//        timing->lastClockSampleTime = internalClock.estimatedTime;
        timing->beatsPerSecond = 1.0f / timing->timePerPeriod;
        timing->beatsPerMinute = timing->beatsPerSecond * 60;

        timing->periodTimer.reset();

        return true;
      }

      return false;
    }

    json_t *dataToJson() override {
      json_t *rootJ = json_object();

      // Running & sync states
      json_object_set_new(rootJ, PERSIST_KEY_RUNNING, json_integer((int) this->runToggle->state));
      json_object_set_new(rootJ, PERSIST_KEY_SYNC, json_integer((int) this->syncToggle->state));

      return rootJ;
    }

    void dataFromJson(json_t *rootJ) override {
      // Running & sync states
      json_t *runningJ = json_object_get(rootJ, PERSIST_KEY_RUNNING);
      json_t *syncJ = json_object_get(rootJ, PERSIST_KEY_SYNC);

      if (runningJ) {
        this->runToggle->state = json_integer_value(runningJ);
      }

      if (syncJ) {
        this->syncToggle->state = json_integer_value(syncJ);
      }
    }
};


struct ClockSyncWidget : ModuleWidget {
    explicit ClockSyncWidget(ClockSync *module) {
      setModule(module);
      setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ClockSync.svg")));

      addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
      addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
      addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
      addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

      addParam(createParamCentered<LEDButton>(mm2px(Vec(29.97, 23.179)), module, ClockSync::RUNTOGGLE_PARAM));
      addChild(
          createLightCentered<LEDBezelLight<GreenLight>>(mm2px(Vec(29.97, 23.179)), module, ClockSync::RUNNING_LIGHT));

      addParam(createParamCentered<LEDButton>(mm2px(Vec(29.527, 40.452)), module, ClockSync::SYNCTOGGLE_PARAM));
      addChild(createLightCentered<LEDBezelLight<GreenLight>>(mm2px(Vec(29.527, 40.452)), module,
                                                              ClockSync::SYNCTOGGLE_LIGHT));
      addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(18.454, 54.587)), module, ClockSync::THRESHKNOB_PARAM));

      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.663, 22.883)), module, ClockSync::RUNCV_INPUT));
      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.515, 40.304)), module, ClockSync::SYNCCV_INPUT));
      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.791, 54.734)), module, ClockSync::THRESHCV_INPUT));
      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.63, 96.526)), module, ClockSync::MAINCLKIN_INPUT));
      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.777, 115.571)), module, ClockSync::EXTCLKIN_INPUT));

      addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.623, 76.074)), module, ClockSync::SYNCOUT_OUTPUT));
      addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.828, 114.981)), module, ClockSync::EXTCLKOUT_OUTPUT));

      addChild(
          createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(18.012, 75.951)), module,
                                                          ClockSync::SYNCLED_LIGHT));
    }
};


Model *modelClockSync = createModel<ClockSync, ClockSyncWidget>("ClockSync");
