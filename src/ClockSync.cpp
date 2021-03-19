#include "plugin.hpp"


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
		NUM_OUTPUTS
	};
	enum LightIds {
		SYNCLED_LIGHT,
		NUM_LIGHTS
	};

	ClockSync() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RUNTOGGLE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(SYNCTOGGLE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(THRESHKNOB_PARAM, 0.f, 1.f, 0.f, "");
	}

	void process(const ProcessArgs& args) override {
	}
};


struct ClockSyncWidget : ModuleWidget {
	ClockSyncWidget(ClockSync* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ClockSync.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<LEDButton>(mm2px(Vec(29.97, 23.179)), module, ClockSync::RUNTOGGLE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(29.527, 40.452)), module, ClockSync::SYNCTOGGLE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(18.454, 59.349)), module, ClockSync::THRESHKNOB_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.663, 22.883)), module, ClockSync::RUNCV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.515, 40.304)), module, ClockSync::SYNCCV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.791, 59.497)), module, ClockSync::THRESHCV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.63, 96.526)), module, ClockSync::MAINCLKIN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.777, 115.571)), module, ClockSync::EXTCLKIN_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.828, 114.981)), module, ClockSync::EXTCLKOUT_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(24.36, 76.77)), module, ClockSync::SYNCLED_LIGHT));
	}
};


Model* modelClockSync = createModel<ClockSync, ClockSyncWidget>("ClockSync");
