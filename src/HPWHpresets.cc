/*
File Containing all of the presets available in HPWHsim
*/
#include <algorithm>

int HPWH::HPWHinit_resTank() {
	//a default resistance tank, nominal 50 gallons, 0.95 EF, standard double 4.5 kW elements
	return this->HPWHinit_resTank(GAL_TO_L(47.5), 0.95, 4500, 4500);
}
int HPWH::HPWHinit_resTank(double tankVol_L, double energyFactor, double upperPower_W, double lowerPower_W) {
	//low power element will cause divide by zero/negative UA in EF -> UA conversion
	if (lowerPower_W < 550) {
		if (hpwhVerbosity >= VRB_reluctant) {
			msg("Resistance tank wattage below 550 W.  DOES NOT COMPUTE\n");
		}
		return HPWH_ABORT;
	}
	if (energyFactor <= 0) {
		if (hpwhVerbosity >= VRB_reluctant) {
			msg("Energy Factor less than zero.  DOES NOT COMPUTE\n");
		}
		return HPWH_ABORT;
	}

	//use tank size setting function since it has bounds checking
	int failure = this->setTankSize(tankVol_L);
	if (failure == HPWH_ABORT) {
		return failure;
	}


	numNodes = 12;
	tankTemps_C = new double[numNodes];
	setpoint_C = F_TO_C(127.0);

	//start tank off at setpoint
	resetTankToSetpoint();

	nextTankTemps_C = new double[numNodes];


	doTempDepression = false;
	tankMixesOnDraw = true;

	numHeatSources = 2;
	setOfSources = new HeatSource[numHeatSources];

	HeatSource resistiveElementBottom(this);
	HeatSource resistiveElementTop(this);
	resistiveElementBottom.setupAsResistiveElement(0, lowerPower_W);
	resistiveElementTop.setupAsResistiveElement(8, upperPower_W);

	//standard logic conditions
	resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(40)));
	resistiveElementBottom.addTurnOnLogic(HPWH::standby(dF_TO_dC(10)));

	resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
	resistiveElementTop.isVIP = true;

	setOfSources[0] = resistiveElementTop;
	setOfSources[1] = resistiveElementBottom;

	setOfSources[0].followedByHeatSource = &setOfSources[1];

	// (1/EnFac + 1/RecovEff) / (67.5 * ((24/41094) - 1/(RecovEff * Power_btuperHr)))
	double recoveryEfficiency = 0.98;
	double numerator = (1.0 / energyFactor) - (1.0 / recoveryEfficiency);
	double temp = 1.0 / (recoveryEfficiency * lowerPower_W*3.41443);
	double denominator = 67.5 * ((24.0 / 41094.0) - temp);
	tankUA_kJperHrC = UAf_TO_UAc(numerator / denominator);

	if (tankUA_kJperHrC < 0.) {
		if (hpwhVerbosity >= VRB_reluctant && tankUA_kJperHrC < -0.1) {
			msg("Computed tankUA_kJperHrC is less than 0, and is reset to 0.");
		}
		tankUA_kJperHrC = 0.0;
	}

	hpwhModel = MODELS_CustomResTank;

	//calculate oft-used derived values
	calcDerivedValues();

	if (checkInputs() == HPWH_ABORT) return HPWH_ABORT;

	isHeating = false;
	for (int i = 0; i < numHeatSources; i++) {
		if (setOfSources[i].isOn) {
			isHeating = true;
		}
		setOfSources[i].sortPerformanceMap();
	}


	if (hpwhVerbosity >= VRB_emetic) {
		for (int i = 0; i < numHeatSources; i++) {
			msg("heat source %d: %p \n", i, &setOfSources[i]);
		}
		msg("\n\n");
	}

	simHasFailed = false;
	return 0;  //successful init returns 0
}

int HPWH::HPWHinit_genericHPWH(double tankVol_L, double energyFactor, double resUse_C) {
	//return 0 on success, HPWH_ABORT for failure
	simHasFailed = true;  //this gets cleared on successful completion of init

	//clear out old stuff if you're re-initializing
	delete[] tankTemps_C;
	delete[] setOfSources;




	//except where noted, these values are taken from MODELS_GE2014STDMode on 5/17/16
	numNodes = 12;
	tankTemps_C = new double[numNodes];
	setpoint_C = F_TO_C(127.0);

	nextTankTemps_C = new double[numNodes];

	//start tank off at setpoint
	resetTankToSetpoint();

	//custom settings - these are set later
	//tankVolume_L = GAL_TO_L(45);
	//tankUA_kJperHrC = 6.5;

	doTempDepression = false;
	tankMixesOnDraw = true;

	numHeatSources = 3;
	setOfSources = new HeatSource[numHeatSources];

	HeatSource compressor(this);
	HeatSource resistiveElementBottom(this);
	HeatSource resistiveElementTop(this);

	//compressor values
	compressor.isOn = false;
	compressor.isVIP = false;
	compressor.typeOfHeatSource = TYPE_compressor;

	double split = 1.0 / 4.0;
	compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

	compressor.perfMap.reserve(2);

	compressor.perfMap.push_back({
		50, // Temperature (T_F)
		{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
		{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
		});

	compressor.perfMap.push_back({
		70, // Temperature (T_F)
		{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
		{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
		});

	compressor.minT = F_TO_C(45.);
	compressor.maxT = F_TO_C(120.);
	compressor.hysteresis_dC = dF_TO_dC(2);
	compressor.configuration = HeatSource::CONFIG_WRAPPED;

	//top resistor values
	resistiveElementTop.setupAsResistiveElement(6, 4500);
	resistiveElementTop.isVIP = true;

	//bottom resistor values
	resistiveElementBottom.setupAsResistiveElement(0, 4000);
	resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

	//logic conditions
	//this is set customly, from input
	//resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(19.6605)));
	resistiveElementTop.addTurnOnLogic(HPWH::topThird(resUse_C));

	resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(86.1111)));

	compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
	compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(12.392)));

	//custom adjustment for poorer performance
	//compressor.addShutOffLogic(HPWH::lowT(F_TO_C(37)));


	//end section of parameters from GE model




	//set tank volume from input
	//use tank size setting function since it has bounds checking
	int failure = this->setTankSize(tankVol_L);
	if (failure == HPWH_ABORT) {
		if (hpwhVerbosity >= VRB_reluctant) {
			msg("Failure to set tank size in generic hpwh init.");
		}
		return failure;
	}


	// derive conservative (high) UA from tank volume
	//   curve fit by Jim Lutz, 5-25-2016
	double tankVol_gal = tankVol_L / GAL_TO_L(1.);
	double v1 = 7.5156316175 * pow(tankVol_gal, 0.33) + 5.9995357658;
	tankUA_kJperHrC = 0.0076183819 * v1 * v1;




	//do a linear interpolation to scale COP curve constant, using measured values
	// Chip's attempt 24-May-2014
	double uefSpan = 3.4 - 2.0;

	//force COP to be 70% of GE at UEF 2 and 95% at UEF 3.4
	//use a fudge factor to scale cop and input power in tandem to maintain constant capacity
	double fUEF = (energyFactor - 2.0) / uefSpan;
	double genericFudge = (1. - fUEF)*.7 + fUEF * .95;

	compressor.perfMap[0].COP_coeffs[0] *= genericFudge;
	compressor.perfMap[0].COP_coeffs[1] *= genericFudge;
	compressor.perfMap[0].COP_coeffs[2] *= genericFudge;

	compressor.perfMap[1].COP_coeffs[0] *= genericFudge;
	compressor.perfMap[1].COP_coeffs[1] *= genericFudge;
	compressor.perfMap[1].COP_coeffs[2] *= genericFudge;

	compressor.perfMap[0].inputPower_coeffs[0] /= genericFudge;
	compressor.perfMap[0].inputPower_coeffs[1] /= genericFudge;
	compressor.perfMap[0].inputPower_coeffs[2] /= genericFudge;

	compressor.perfMap[1].inputPower_coeffs[0] /= genericFudge;
	compressor.perfMap[1].inputPower_coeffs[1] /= genericFudge;
	compressor.perfMap[1].inputPower_coeffs[2] /= genericFudge;




	//set everything in its places
	setOfSources[0] = resistiveElementTop;
	setOfSources[1] = resistiveElementBottom;
	setOfSources[2] = compressor;

	//and you have to do this after putting them into setOfSources, otherwise
	//you don't get the right pointers
	setOfSources[2].backupHeatSource = &setOfSources[1];
	setOfSources[1].backupHeatSource = &setOfSources[2];

	setOfSources[0].followedByHeatSource = &setOfSources[1];
	setOfSources[1].followedByHeatSource = &setOfSources[2];



	//standard finishing up init, borrowed from init function

	hpwhModel = MODELS_genericCustomUEF;

	//calculate oft-used derived values
	calcDerivedValues();

	if (checkInputs() == HPWH_ABORT) {
		return HPWH_ABORT;
	}

	isHeating = false;
	for (int i = 0; i < numHeatSources; i++) {
		if (setOfSources[i].isOn) {
			isHeating = true;
		}
		setOfSources[i].sortPerformanceMap();
	}

	if (hpwhVerbosity >= VRB_emetic) {
		for (int i = 0; i < numHeatSources; i++) {
			msg("heat source %d: %p \n", i, &setOfSources[i]);
		}
		msg("\n\n");
	}

	simHasFailed = false;

	return 0;
}


int HPWH::HPWHinit_presets(MODELS presetNum) {
	//return 0 on success, HPWH_ABORT for failure
	simHasFailed = true;  //this gets cleared on successful completion of init

	//clear out old stuff if you're re-initializing
	delete[] tankTemps_C;
	delete[] setOfSources;


	//resistive with no UA losses for testing
	if (presetNum == MODELS_restankNoUA) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(50);
		tankUA_kJperHrC = 0; //0 to turn off


		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 2;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementTop.setupAsResistiveElement(8, 4500);

		//standard logic conditions
		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(40)));
		resistiveElementBottom.addTurnOnLogic(HPWH::standby(dF_TO_dC(10)));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
		resistiveElementTop.isVIP = true;

		//assign heat sources into array in order of priority
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;

		setOfSources[0].followedByHeatSource = &setOfSources[1];
	}

	//resistive tank with massive UA loss for testing
	else if (presetNum == MODELS_restankHugeUA) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = 50;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 120;
		tankUA_kJperHrC = 500; //0 to turn off

		doTempDepression = false;
		tankMixesOnDraw = false;


		numHeatSources = 2;
		setOfSources = new HeatSource[numHeatSources];

		//set up a resistive element at the bottom, 4500 kW
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementTop.setupAsResistiveElement(9, 4500);

		//standard logic conditions
		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(20));
		resistiveElementBottom.addTurnOnLogic(HPWH::standby(15));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(20));
		resistiveElementTop.isVIP = true;


		//assign heat sources into array in order of priority
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;

		setOfSources[0].followedByHeatSource = &setOfSources[1];

	}

	//realistic resistive tank
	else if (presetNum == MODELS_restankRealistic) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(50);
		tankUA_kJperHrC = 10; //0 to turn off

		doTempDepression = false;
		//should eventually put tankmixes to true when testing progresses
		tankMixesOnDraw = false;

		numHeatSources = 2;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementTop.setupAsResistiveElement(9, 4500);

		//standard logic conditions
		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(20));
		resistiveElementBottom.addTurnOnLogic(HPWH::standby(15));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(20));
		resistiveElementTop.isVIP = true;

		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;

		setOfSources[0].followedByHeatSource = &setOfSources[1];
	}

	else if (presetNum == MODELS_StorageTank) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = 52;

		//start tank off at setpoint
		resetTankToSetpoint();

		setpoint_C = 800;

		tankVolume_L = GAL_TO_L(80);
		tankUA_kJperHrC = 10; //0 to turn off

		doTempDepression = false;
		tankMixesOnDraw = false;

		////////////////////////////////////////////////////
		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];
		
		HeatSource extra(this);
		
		//compressor values
		extra.isOn = false;
		extra.isVIP = false;
		extra.typeOfHeatSource = TYPE_extra;
		extra.configuration = HeatSource::CONFIG_WRAPPED;
		
		extra.addTurnOnLogic(HPWH::topThird_absolute(1));

		//initial guess, will get reset based on the input heat vector
		extra.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		setOfSources[0] = extra;
	}

	//basic compressor tank for testing
	else if (presetNum == MODELS_basicIntegrated) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = 50;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 120;
		tankUA_kJperHrC = 10; //0 to turn off
		//tankUA_kJperHrC = 0; //0 to turn off

		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);
		HeatSource compressor(this);

		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementTop.setupAsResistiveElement(9, 4500);

		resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);

		//standard logic conditions
		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(20));
		resistiveElementBottom.addTurnOnLogic(HPWH::standby(15));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(20));
		resistiveElementTop.isVIP = true;


		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double oneSixth = 1.0 / 6.0;
		compressor.setCondensity(oneSixth, oneSixth, oneSixth, oneSixth, oneSixth, oneSixth, 0, 0, 0, 0, 0, 0);

		//GE tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{0.290 * 1000, 0.00159 * 1000, 0.00000107 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.49, -0.0187, -0.0000133} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{0.375 * 1000, 0.00121 * 1000, 0.00000216 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{5.60, -0.0252, 0.00000254} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = 0;
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(4);
		compressor.configuration = HeatSource::CONFIG_WRAPPED; //wrapped around tank

		compressor.addTurnOnLogic(HPWH::bottomThird(20));
		compressor.addTurnOnLogic(HPWH::standby(15));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = compressor;
		setOfSources[2] = resistiveElementBottom;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}

	//simple external style for testing
	else if (presetNum == MODELS_externalTest) {
		numNodes = 96;
		tankTemps_C = new double[numNodes];
		setpoint_C = 50;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 120;
		//tankUA_kJperHrC = 10; //0 to turn off
		tankUA_kJperHrC = 0; //0 to turn off

		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);

		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		//GE tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{0.290 * 1000, 0.00159 * 1000, 0.00000107 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.49, -0.0187, -0.0000133} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{0.375 * 1000, 0.00121 * 1000, 0.00000216 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{5.60, -0.0252, 0.00000254} // COP Coefficients (COP_coeffs)
			});

		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = 0;  //no hysteresis
		compressor.configuration = HeatSource::CONFIG_EXTERNAL;

		compressor.addTurnOnLogic(HPWH::bottomThird(20));
		compressor.addTurnOnLogic(HPWH::standby(15));

		//lowT cutoff
		compressor.addShutOffLogic(HPWH::bottomNodeMaxTemp(20));


		//set everything in its places
		setOfSources[0] = compressor;
	}
	//voltex 60 gallon
	else if (presetNum == MODELS_AOSmithPHPT60) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 215.8;
		tankUA_kJperHrC = 7.31;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 5.0;
		compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{0.467 * 1000, 0.00281 * 1000, 0.0000072 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.86, -0.0222, -0.00001} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{0.541 * 1000, 0.00147 * 1000, 0.0000176 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{6.58, -0.0392, 0.0000407} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(45.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(4);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4250);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 2000);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);

		//logic conditions
		double compStart = dF_TO_dC(43.6);
		double standbyT = dF_TO_dC(23.8);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(compStart));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(25.0)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = compressor;
		setOfSources[2] = resistiveElementBottom;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_AOSmithPHPT80) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 283.9;
		tankUA_kJperHrC = 8.8;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 5.0;
		compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{0.467 * 1000, 0.00281 * 1000, 0.0000072 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.86, -0.0222, -0.00001} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{0.541 * 1000, 0.00147 * 1000, 0.0000176 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{6.58, -0.0392, 0.0000407} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(45.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(4);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4250);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 2000);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);


		//logic conditions
		double compStart = dF_TO_dC(43.6);
		double standbyT = dF_TO_dC(23.8);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(compStart));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(25.0)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = compressor;
		setOfSources[2] = resistiveElementBottom;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2012) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 172;
		tankUA_kJperHrC = 6.8;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 5.0;
		compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{0.3 * 1000, 0.00159 * 1000, 0.00000107 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.7, -0.0210, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{0.378 * 1000, 0.00121 * 1000, 0.00000216 * 1000}, // Input Power Coefficients (inputPower_coeffs)
			{4.8, -0.0167, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(45.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(4);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4200);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4200);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);


		//logic conditions
		//    double compStart = dF_TO_dC(24.4);
		double compStart = dF_TO_dC(40.0);
		double standbyT = dF_TO_dC(5.2);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));
		// compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(66)));
		compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(65)));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(compStart));
		//resistiveElementBottom.addShutOffLogic(HPWH::lowTreheat(lowTcutoff));
		//GE element never turns off?

		// resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(31.0)));
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(28.0)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = compressor;
		setOfSources[2] = resistiveElementBottom;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	// If a Colmac single pass preset
	else if (MODELS_ColmacCxV_5_SP <= presetNum && presetNum <= MODELS_ColmacCxA_30_SP) {
		numNodes = 96;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(135.0);
		setpointFixed = false;

		doTempDepression = false;
		tankMixesOnDraw = false;
		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 315; // Stolen from Sanden, will adjust 
		tankUA_kJperHrC = 7; // Stolen from Sanden, will adjust to tank size

		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		compressor.isOn = false;
		compressor.isVIP = true;
		compressor.typeOfHeatSource = TYPE_compressor;
		compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		compressor.perfMap.reserve(1);

		//logic conditions
		compressor.minT = F_TO_C(40.0);
		compressor.hysteresis_dC = 0;
		compressor.configuration = HeatSource::CONFIG_EXTERNAL;

		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(4);
		compressor.addTurnOnLogic(HPWH::HeatingLogic("fourth node absolute", nodeWeights, F_TO_C(90), true));

		//lowT cutoff
		std::vector<NodeWeight> nodeWeights1;
		nodeWeights1.emplace_back(1);
		compressor.addShutOffLogic(HPWH::HeatingLogic("bottom node absolute", nodeWeights1, F_TO_C(100.), true, std::greater<double>()));
		compressor.depressesTemperature = false;  //no temp depression

		//Defrost Derate 
		compressor.setupDefrostMap();

		if (presetNum == MODELS_ColmacCxV_5_SP) {
			setTankSize_adjustUA(200., UNITS_GAL);
			compressor.minT = F_TO_C(-4.0);
			msg("MODELS_ColmacCxV_5_SP has not been set yet, aborting.  \n");
			return HPWH_ABORT;
		}
		else {
			//Perfmap for input power made from data for CxA-20 to be scalled for other models
			std::vector<double> inputPwr_coeffs = { 13.77317898,0.098118687,-0.127961444,0.015252227,-0.000398994,0.001158229,
					0.000570153,-7.3854E-05,-9.61881E-07,-0.000498209,1.52307E-07 };
			double pwrScalar = 0.0;

			if (presetNum == MODELS_ColmacCxA_10_SP) {
				setTankSize_adjustUA(500., UNITS_GAL);
				pwrScalar = 0.497;// Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_ColmacCxA_15_SP) {
				setTankSize_adjustUA(600., UNITS_GAL);
				pwrScalar = 0.736;// Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_ColmacCxA_20_SP) {
				setTankSize_adjustUA(800., UNITS_GAL);
				pwrScalar = 1.0;// Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_ColmacCxA_25_SP) {
				setTankSize_adjustUA(1000., UNITS_GAL);
				double pwrScalar = 1.167;// Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_ColmacCxA_30_SP) {
				setTankSize_adjustUA(1200., UNITS_GAL);
				pwrScalar = 1.516;// Scalar to adjust inputPwr_coeffs
			}

			std::transform(inputPwr_coeffs.begin(), inputPwr_coeffs.end(), inputPwr_coeffs.begin(),
				std::bind1st(std::multiplies<double>(), pwrScalar));
			//Set the scalled performance map in it's place
			compressor.perfMap.push_back({
				0, // Temperature (T_F)

				inputPwr_coeffs, // Input Power Coefficients (inputPower_coeffs)

				{0.466234434, 0.162032056, -0.019707518, 0.001442995, -0.000246901, 4.17009E-05,
				-0.0001036,-0.000573599, -0.000361645, 0.000105189,1.85347E-06 } // COP Coefficients (COP_coeffs)
				});
		} //End if MODELS_ColmacCxV_5_SP

		//set everything in its places
		setOfSources[0] = compressor;
	}
	
	// If Nyle single pass preset
	else if (MODELS_NyleC25A_SP <= presetNum && presetNum <= MODELS_NyleC250A_SP) {
		numNodes = 96;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(135.0);
		setpointFixed = false;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 315;
		tankUA_kJperHrC = 7; // Stolen from Sanden, will adjust to 800 gallon tank
		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		//HeatSource resistiveElement(this);

		compressor.isOn = false;
		compressor.isVIP = true;
		compressor.typeOfHeatSource = TYPE_compressor;
		compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		compressor.perfMap.reserve(1);

		//logic conditions
		compressor.minT = F_TO_C(40.0);
		compressor.hysteresis_dC = 0;
		compressor.configuration = HeatSource::CONFIG_EXTERNAL;

		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(4);
		compressor.addTurnOnLogic(HPWH::HeatingLogic("fourth node absolute", nodeWeights, F_TO_C(90), true));

		//lowT cutoff
		std::vector<NodeWeight> nodeWeights1;
		nodeWeights1.emplace_back(1);
		compressor.addShutOffLogic(HPWH::HeatingLogic("bottom node absolute", nodeWeights1, F_TO_C(100.), true, std::greater<double>()));
		compressor.depressesTemperature = false;  //no temp depression

		//Defrost Derate 
		compressor.setupDefrostMap();

		if (presetNum == MODELS_NyleC250A_SP) {
			setTankSize_adjustUA(800., UNITS_GAL);
			compressor.minT = F_TO_C(45.0);

			compressor.perfMap.push_back({
				0, // Temperature (T_F)

				{7.693092424, -0.072548382, 0.067038406, 0.03924693, -0.000769036, -0.000369488, -0.000302597,
				0.001689189, 0.001862817, -0.000298513, -6.19946E-06 }, // Input Power Coefficients (inputPower_coeffs)

				{4.136420036, 0.096548903, -0.008264666, -0.012210397, -4.09371E-05, 1.04316E-05, 9.01584E-05,
				-0.000460458, -0.000650959, -8.0251E-05, 3.71129E-06 } // COP Coefficients (COP_coeffs)
				});
		}
		else {
			//Perfmap for input power made from data for C185A_SP to be scalled for other models
			std::vector<double> inputPwr_coeffs = { -16.70800526, -0.059330655, 0.347312024, 0.064635197, 5.72238E-05, -0.0012,
					-5.96939E-05, 0.000590259, 0.000205245, 0., 0. }; // Input Power Coefficients (inputPower_coeffs);
			double pwrScalar = 0.0;

			if (presetNum == MODELS_NyleC25A_SP) {
				setTankSize_adjustUA(200., UNITS_GAL);
				pwrScalar = 0.112; // Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_NyleC60A_SP) {
				setTankSize_adjustUA(300., UNITS_GAL);
				pwrScalar = 0.281; // Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_NyleC90A_SP) {
				setTankSize_adjustUA(400., UNITS_GAL);
				pwrScalar = .493; // Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_NyleC125A_SP) {
				setTankSize_adjustUA(500., UNITS_GAL);
				pwrScalar = 0.642; // Scalar to adjust inputPwr_coeffs
			}
			else if (presetNum == MODELS_NyleC185A_SP) {
				setTankSize_adjustUA(800., UNITS_GAL);
				pwrScalar = 1.; // Scalar to adjust inputPwr_coeffs
			}
			// Adjust inputPwr_coeffs by the pwrScalar for the HPWH model chosen.
			std::transform(inputPwr_coeffs.begin(), inputPwr_coeffs.end(), inputPwr_coeffs.begin(),
					std::bind1st(std::multiplies<double>(), pwrScalar));

			//Set the scalled performance map in it's place
			compressor.perfMap.push_back({
				0, // Temperature (T_F)

				inputPwr_coeffs, // Input Power Coefficients (inputPower_coeffs)

				{0.466234434, 0.162032056, -0.019707518, 0.001442995, -0.000246901, 4.17009E-05,
				-0.0001036,-0.000573599, -0.000361645, 0.000105189,1.85347E-06 } // COP Coefficients (COP_coeffs)
				});
		} // End if MODELS_NyleC250A_SP

		//set everything in its places
		setOfSources[0] = compressor;
	}

	else if (presetNum == MODELS_Sanden80 || presetNum == MODELS_Sanden_GS3_45HPA_US_SP) {
		numNodes = 96;
		tankTemps_C = new double[numNodes];
		setpoint_C = 65;
		setpointFixed = true;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 315;
		//tankUA_kJperHrC = 10; //0 to turn off
		tankUA_kJperHrC = 7;

		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);

		compressor.isOn = false;
		compressor.isVIP = true;
		compressor.typeOfHeatSource = TYPE_compressor;

		compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(5);

		compressor.perfMap.push_back({
			17, // Temperature (T_F)
			{1650, 5.5, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{3.2, -0.015, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			35, // Temperature (T_F)
			{1100, 4.0, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{3.7, -0.015, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{880, 3.1, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.25, -0.025, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{740, 4.0, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.2, -0.03, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			95, // Temperature (T_F)
			{790, 2, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.15, -0.04, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.hysteresis_dC = 4;
		compressor.configuration = HeatSource::CONFIG_EXTERNAL;


		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(8);
		compressor.addTurnOnLogic(HPWH::HeatingLogic("eighth node absolute", nodeWeights, F_TO_C(113), true));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(8.2639)));

		//lowT cutoff
		std::vector<NodeWeight> nodeWeights1;
		nodeWeights1.emplace_back(1);
		compressor.addShutOffLogic(HPWH::HeatingLogic("bottom node absolute", nodeWeights1, F_TO_C(135), true, std::greater<double>()));
		compressor.depressesTemperature = false;  //no temp depression

		//set everything in its places
		setOfSources[0] = compressor;
	}
	else if (presetNum == MODELS_Sanden40) {
		numNodes = 96;
		tankTemps_C = new double[numNodes];
		setpoint_C = 65;
		setpointFixed = true;

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 160;
		//tankUA_kJperHrC = 10; //0 to turn off
		tankUA_kJperHrC = 5;

		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 1;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);

		compressor.isOn = false;
		compressor.isVIP = true;
		compressor.typeOfHeatSource = TYPE_compressor;

		compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(5);

		compressor.perfMap.push_back({
			17, // Temperature (T_F)
			{1650, 5.5, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{3.2, -0.015, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			35, // Temperature (T_F)
			{1100, 4.0, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{3.7, -0.015, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{880, 3.1, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.25, -0.025, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{740, 4.0, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.2, -0.03, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			95, // Temperature (T_F)
			{790, 2, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.15, -0.04, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.hysteresis_dC = 4;
		compressor.configuration = HeatSource::CONFIG_EXTERNAL;


		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(4);
		compressor.addTurnOnLogic(HPWH::HeatingLogic("fourth node absolute", nodeWeights, F_TO_C(113), true));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(8.2639)));

		//lowT cutoff
		std::vector<NodeWeight> nodeWeights1;
		nodeWeights1.emplace_back(1);
		compressor.addShutOffLogic(HPWH::HeatingLogic("bottom node absolute", nodeWeights1, F_TO_C(135), true, std::greater<double>()));
		compressor.depressesTemperature = false;  //no temp depression

		//set everything in its places
		setOfSources[0] = compressor;
	}
	else if (presetNum == MODELS_AOSmithHPTU50 || presetNum == MODELS_RheemHBDR2250 || presetNum == MODELS_RheemHBDR4550) {
		numNodes = 24;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 171;
		tankUA_kJperHrC = 6;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 5.0;
		compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

		// performance map
		compressor.perfMap.reserve(3);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{170, 2.02, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.93, -0.027, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{144.5, 2.42, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.67, -0.037, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			95, // Temperature (T_F)
			{94.1, 3.15, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{11.1, -0.056, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(42.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		if (presetNum == MODELS_RheemHBDR2250) {
			resistiveElementTop.setupAsResistiveElement(8, 2250);
		}
		else {
			resistiveElementTop.setupAsResistiveElement(8, 4500);
		}
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		if (presetNum == MODELS_RheemHBDR2250) {
			resistiveElementBottom.setupAsResistiveElement(0, 2250);
		}
		else {
			resistiveElementBottom.setupAsResistiveElement(0, 4500);
		}
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(35);
		double standbyT = dF_TO_dC(9);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(100)));


		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(11); nodeWeights.emplace_back(12);
		resistiveElementTop.addTurnOnLogic(HPWH::HeatingLogic("top sixth absolute", nodeWeights, F_TO_C(105), true));
		//		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(28)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

		setOfSources[0].companionHeatSource = &setOfSources[2];


	}
	else if (presetNum == MODELS_AOSmithHPTU66 || presetNum == MODELS_RheemHBDR2265 || presetNum == MODELS_RheemHBDR4565) {
		numNodes = 24;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		if (presetNum == MODELS_AOSmithHPTU66) {
			tankVolume_L = 244.6;
		}
		else {
			tankVolume_L = 221.4;
		}
		tankUA_kJperHrC = 8;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		// performance map
		compressor.perfMap.reserve(3);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{170, 2.02, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.93, -0.027, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{144.5, 2.42, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.67, -0.037, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			95, // Temperature (T_F)
			{94.1, 3.15, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{11.1, -0.056, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(42.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		if (presetNum == MODELS_RheemHBDR2265) {
			resistiveElementTop.setupAsResistiveElement(8, 2250);
		}
		else {
			resistiveElementTop.setupAsResistiveElement(8, 4500);
		}
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		if (presetNum == MODELS_RheemHBDR2265) {
			resistiveElementBottom.setupAsResistiveElement(0, 2250);
		}
		else {
			resistiveElementBottom.setupAsResistiveElement(0, 4500);
		}
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(35);
		double standbyT = dF_TO_dC(9);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(100)));

		std::vector<NodeWeight> nodeWeights;
		nodeWeights.emplace_back(11); nodeWeights.emplace_back(12);
		resistiveElementTop.addTurnOnLogic(HPWH::HeatingLogic("top sixth absolute", nodeWeights, F_TO_C(105), true));
		//		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(31)));


				//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

		setOfSources[0].companionHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_AOSmithHPTU80 || presetNum == MODELS_RheemHBDR2280 || presetNum == MODELS_RheemHBDR4580) {
		numNodes = 24;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 299.5;
		tankUA_kJperHrC = 9;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;
		compressor.configuration = HPWH::HeatSource::CONFIG_WRAPPED;

		double split = 1.0 / 3.0;
		compressor.setCondensity(split, split, split, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(3);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{170, 2.02, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.93, -0.027, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{144.5, 2.42, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.67, -0.037, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			95, // Temperature (T_F)
			{94.1, 3.15, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{11.1, -0.056, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(42.0);
		compressor.maxT = F_TO_C(120.0);
		compressor.hysteresis_dC = dF_TO_dC(1);

		//top resistor values
		if (presetNum == MODELS_RheemHBDR2280) {
			resistiveElementTop.setupAsResistiveElement(8, 2250);
		}
		else {
			resistiveElementTop.setupAsResistiveElement(8, 4500);
		}
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		if (presetNum == MODELS_RheemHBDR2280) {
			resistiveElementBottom.setupAsResistiveElement(0, 2250);
		}
		else {
			resistiveElementBottom.setupAsResistiveElement(0, 4500);
		}
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(35);
		double standbyT = dF_TO_dC(9);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(100)));

		std::vector<NodeWeight> nodeWeights;
		//		nodeWeights.emplace_back(9); nodeWeights.emplace_back(10);
		nodeWeights.emplace_back(11); nodeWeights.emplace_back(12);
		resistiveElementTop.addTurnOnLogic(HPWH::HeatingLogic("top sixth absolute", nodeWeights, F_TO_C(105), true));
		//		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(35)));


				//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

		setOfSources[0].companionHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_AOSmithHPTU80_DR) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = 283.9;
		tankUA_kJperHrC = 9;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{142.6, 2.152, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.989258, -0.038320, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{120.14, 2.513, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{8.188, -0.0432, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(42.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(34.1636);
		double standbyT = dF_TO_dC(7.1528);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(80.108)));

		// resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(39.9691)));
		resistiveElementTop.addTurnOnLogic(HPWH::topThird_absolute(F_TO_C(87)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2014STDMode) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(45);
		tankUA_kJperHrC = 6.5;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(19.6605)));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(86.1111)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(12.392)));
		//    compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(65)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2014STDMode_80) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(75.4);
		tankUA_kJperHrC = 10.;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(19.6605)));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(86.1111)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(12.392)));
		compressor.minT = F_TO_C(37);
		//    compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(65)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2014) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(45);
		tankUA_kJperHrC = 6.5;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
		resistiveElementTop.addShutOffLogic(HPWH::topNodeMaxTemp(F_TO_C(116.6358)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(11.0648)));
		// compressor.addShutOffLogic(HPWH::largerDraw(F_TO_C(62.4074)));

		resistiveElementBottom.addTurnOnLogic(HPWH::thirdSixth(dF_TO_dC(60)));
		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(80)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;


		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2014_80) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(75.4);
		tankUA_kJperHrC = 10.;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
		resistiveElementTop.addShutOffLogic(HPWH::topNodeMaxTemp(F_TO_C(116.6358)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(11.0648)));
		// compressor.addShutOffLogic(HPWH::largerDraw(F_TO_C(62.4074)));

		resistiveElementBottom.addTurnOnLogic(HPWH::thirdSixth(dF_TO_dC(60)));
		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(80)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;


		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_GE2014_80DR) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(75.4);
		tankUA_kJperHrC = 10.;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		// resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
		resistiveElementTop.addTurnOnLogic(HPWH::topThird_absolute(F_TO_C(87)));
		// resistiveElementTop.addShutOffLogic(HPWH::topNodeMaxTemp(F_TO_C(116.6358)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(11.0648)));
		// compressor.addShutOffLogic(HPWH::largerDraw(F_TO_C(62.4074)));

		resistiveElementBottom.addTurnOnLogic(HPWH::thirdSixth(dF_TO_dC(60)));
		// resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(80)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;


		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvTO BE UPDATEDvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	//PRELIMINARY PRESET USING GE2014 DATA 
	else if (presetNum == MODELS_BWC2020_65) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(65);
		tankUA_kJperHrC = 7.6;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.4977772, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{7.207307, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(20)));
		resistiveElementTop.addShutOffLogic(HPWH::topNodeMaxTemp(F_TO_C(116.6358)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(11.0648)));
		// compressor.addShutOffLogic(HPWH::largerDraw(F_TO_C(62.4074)));

		resistiveElementBottom.addTurnOnLogic(HPWH::thirdSixth(dF_TO_dC(60)));
		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(80)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;


		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];
	}
	// If Rheem Premium TO BE UPDATED 
	else if (MODELS_Rheem2020Prem40 <= presetNum && presetNum <= MODELS_Rheem2020Prem80) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		if (presetNum == MODELS_Rheem2020Prem40) {
			tankVolume_L = GAL_TO_L(36.1);
			tankUA_kJperHrC = 9.5;
		}
		else if (presetNum == MODELS_Rheem2020Prem50) {
			tankVolume_L = GAL_TO_L(45.1);
			tankUA_kJperHrC = 8.55;
		}
		else if (presetNum == MODELS_Rheem2020Prem65) {
			tankVolume_L = GAL_TO_L(58.5);		
			tankUA_kJperHrC = 10.64;
		}
		else if (presetNum == MODELS_Rheem2020Prem80) {
			tankVolume_L = GAL_TO_L(72);
			tankUA_kJperHrC = 10.83;
		}
		tankUA_kJperHrC = 7;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{280, 4.97342, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.634009, -0.029485, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{280, 5.35992, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.3, -0.03, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.hysteresis_dC = dF_TO_dC(1);
		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.0);

		compressor.configuration = HPWH::HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4200);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 2250);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(38);
		double standbyT = dF_TO_dC(13.2639);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(76.7747)));

		resistiveElementTop.addTurnOnLogic(HPWH::topSixth(dF_TO_dC(20.4167)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];
	}
	// If Rheem Premium TO BE UPDATED 
	else if (MODELS_Rheem2020Build40 <= presetNum && presetNum <= MODELS_Rheem2020Build80) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		if (presetNum == MODELS_Rheem2020Build40) {
			tankVolume_L = GAL_TO_L(36.1);
			tankUA_kJperHrC = 9.5;
		}
		else if (presetNum == MODELS_Rheem2020Build50) {
			tankVolume_L = GAL_TO_L(45.1);
			tankUA_kJperHrC = 8.55;
		}
		else if (presetNum == MODELS_Rheem2020Build65) {
			tankVolume_L = GAL_TO_L(58.5);
			tankUA_kJperHrC = 10.64;
		}
		else if (presetNum == MODELS_Rheem2020Build80) {
			tankVolume_L = GAL_TO_L(72.);
			tankUA_kJperHrC = 10.83;
		}

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{280, 4.97342, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.634009, -0.029485, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{280, 5.35992, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.3, -0.03, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.hysteresis_dC = dF_TO_dC(1);
		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.0);

		compressor.configuration = HPWH::HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4200);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 2250);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(38);
		double standbyT = dF_TO_dC(13.2639);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(76.7747)));

		resistiveElementTop.addTurnOnLogic(HPWH::topSixth(dF_TO_dC(20.4167)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];
	}
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^TO BE UPDATED^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	else if (presetNum == MODELS_RheemHB50) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(45);
		tankUA_kJperHrC = 7;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			47, // Temperature (T_F)
			{280, 4.97342, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.634009, -0.029485, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{280, 5.35992, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.3, -0.03, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.hysteresis_dC = dF_TO_dC(1);
		compressor.minT = F_TO_C(40.0);
		compressor.maxT = F_TO_C(120.0);

		compressor.configuration = HPWH::HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4200);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 2250);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		double compStart = dF_TO_dC(38);
		double standbyT = dF_TO_dC(13.2639);
		compressor.addTurnOnLogic(HPWH::bottomThird(compStart));
		compressor.addTurnOnLogic(HPWH::standby(standbyT));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(76.7747)));

		resistiveElementTop.addTurnOnLogic(HPWH::topSixth(dF_TO_dC(20.4167)));


		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_Stiebel220E) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(56);
		//tankUA_kJperHrC = 10; //0 to turn off
		tankUA_kJperHrC = 9;

		doTempDepression = false;
		tankMixesOnDraw = false;

		numHeatSources = 2;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElement(this);

		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		resistiveElement.setupAsResistiveElement(0, 1500);
		resistiveElement.hysteresis_dC = dF_TO_dC(0);

		compressor.setCondensity(0, 0.12, 0.22, 0.22, 0.22, 0.22, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{295.55337, 2.28518, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.744118, -0.025946, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{282.2126, 2.82001, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{8.012112, -0.039394, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(32.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = 0;  //no hysteresis
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		compressor.addTurnOnLogic(HPWH::thirdSixth(dF_TO_dC(6.5509)));
		compressor.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(100)));

		compressor.depressesTemperature = false;  //no temp depression

		//set everything in its places
		setOfSources[0] = compressor;
		setOfSources[1] = resistiveElement;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[0].backupHeatSource = &setOfSources[1];

	}
	else if (presetNum == MODELS_Generic1) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();
		tankVolume_L = GAL_TO_L(50);
		tankUA_kJperHrC = 9;
		doTempDepression = false;
		tankMixesOnDraw = true;
		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{472.58616, 2.09340, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{2.942642, -0.0125954, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{439.5615, 2.62997, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{3.95076, -0.01638033, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(45.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(8, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(40.0)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(10)));
		compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(65)));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(80)));
		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(110)));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(35)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_Generic2) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();
		tankVolume_L = GAL_TO_L(50);
		tankUA_kJperHrC = 7.5;
		doTempDepression = false;
		tankMixesOnDraw = true;
		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{272.58616, 2.09340, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{4.042642, -0.0205954, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{239.5615, 2.62997, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.25076, -0.02638033, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(40.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(40)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(10)));
		compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(60)));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(80)));
		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(100)));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(40)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_Generic3) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(50);
		tankUA_kJperHrC = 5;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		//voltex60 tier 1 values
		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{172.58616, 2.09340, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.242642, -0.0285954, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			67, // Temperature (T_F)
			{139.5615, 2.62997, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{6.75076, -0.03638033, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(35.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4500);
		resistiveElementBottom.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(40)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(10)));
		compressor.addShutOffLogic(HPWH::largeDraw(F_TO_C(55)));

		resistiveElementBottom.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(60)));

		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(40)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = compressor;
		setOfSources[2] = resistiveElementBottom;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}
	else if (presetNum == MODELS_UEF2generic) {
		numNodes = 12;
		tankTemps_C = new double[numNodes];
		setpoint_C = F_TO_C(127.0);

		//start tank off at setpoint
		resetTankToSetpoint();

		tankVolume_L = GAL_TO_L(45);
		tankUA_kJperHrC = 6.5;

		doTempDepression = false;
		tankMixesOnDraw = true;

		numHeatSources = 3;
		setOfSources = new HeatSource[numHeatSources];

		HeatSource compressor(this);
		HeatSource resistiveElementBottom(this);
		HeatSource resistiveElementTop(this);

		//compressor values
		compressor.isOn = false;
		compressor.isVIP = false;
		compressor.typeOfHeatSource = TYPE_compressor;

		double split = 1.0 / 4.0;
		compressor.setCondensity(split, split, split, split, 0, 0, 0, 0, 0, 0, 0, 0);

		compressor.perfMap.reserve(2);

		compressor.perfMap.push_back({
			50, // Temperature (T_F)
			{187.064124, 1.939747, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{4.29, -0.0243008, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.perfMap.push_back({
			70, // Temperature (T_F)
			{148.0418, 2.553291, 0.0}, // Input Power Coefficients (inputPower_coeffs)
			{5.61, -0.0335265, 0.0} // COP Coefficients (COP_coeffs)
			});

		compressor.minT = F_TO_C(37.0);
		compressor.maxT = F_TO_C(120.);
		compressor.hysteresis_dC = dF_TO_dC(2);
		compressor.configuration = HeatSource::CONFIG_WRAPPED;

		//top resistor values
		resistiveElementTop.setupAsResistiveElement(6, 4500);
		resistiveElementTop.isVIP = true;

		//bottom resistor values
		resistiveElementBottom.setupAsResistiveElement(0, 4000);
		resistiveElementBottom.setCondensity(0, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		resistiveElementBottom.hysteresis_dC = dF_TO_dC(2);

		//logic conditions
		resistiveElementTop.addTurnOnLogic(HPWH::topThird(dF_TO_dC(18.6605)));

		resistiveElementBottom.addShutOffLogic(HPWH::bottomTwelthMaxTemp(F_TO_C(86.1111)));

		compressor.addTurnOnLogic(HPWH::bottomThird(dF_TO_dC(33.6883)));
		compressor.addTurnOnLogic(HPWH::standby(dF_TO_dC(12.392)));

		//set everything in its places
		setOfSources[0] = resistiveElementTop;
		setOfSources[1] = resistiveElementBottom;
		setOfSources[2] = compressor;

		//and you have to do this after putting them into setOfSources, otherwise
		//you don't get the right pointers
		setOfSources[2].backupHeatSource = &setOfSources[1];
		setOfSources[1].backupHeatSource = &setOfSources[2];

		setOfSources[0].followedByHeatSource = &setOfSources[1];
		setOfSources[1].followedByHeatSource = &setOfSources[2];

	}

	else {
		if (hpwhVerbosity >= VRB_reluctant) {
			msg("You have tried to select a preset model which does not exist.  \n");
		}
		return HPWH_ABORT;
	}

	// initialize nextTankTemps_C 
	nextTankTemps_C = new double[numNodes];

	hpwhModel = presetNum;

	//calculate oft-used derived values
	calcDerivedValues();

	if (checkInputs() == HPWH_ABORT) {
		return HPWH_ABORT;
	}

	isHeating = false;
	for (int i = 0; i < numHeatSources; i++) {
		if (setOfSources[i].isOn) {
			isHeating = true;
		}
		setOfSources[i].sortPerformanceMap();
	}

	if (hpwhVerbosity >= VRB_emetic) {
		for (int i = 0; i < numHeatSources; i++) {
			msg("heat source %d: %p \n", i, &setOfSources[i]);
		}
		msg("\n\n");
	}

	simHasFailed = false;
	return 0;  //successful init returns 0
}  //end HPWHinit_presets

