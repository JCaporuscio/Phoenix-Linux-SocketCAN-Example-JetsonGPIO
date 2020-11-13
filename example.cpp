#define Phoenix_No_WPI // remove WPI dependencies
#include "ctre/Phoenix.h"
#include "ctre/phoenix/platform/Platform.h"
#include "ctre/phoenix/unmanaged/Unmanaged.h"
#include "ctre/phoenix/cci/Unmanaged_CCI.h"
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <SDL2/SDL.h>
#include <unistd.h>
#include <JetsonGPIO.h>
#include <chrono>

using namespace ctre::phoenix;
using namespace ctre::phoenix::platform;
using namespace ctre::phoenix::motorcontrol;
using namespace ctre::phoenix::motorcontrol::can;
using namespace std::chrono;

/* make some talons for drive train */
TalonSRX talLeft(1);
TalonSRX talRght(2);
int jetsonToPir = 100;
int pirToJetson = 100;
int jetsonToLightRelay = 100;
bool parkMode = false;

void initDrive()
{
	/* both talons should blink green when driving forward */
	talRght.SetInverted(true);
	talLeft.ConfigContinuousCurrentLimit(20, 0);
	talLeft.ConfigPeakCurrentLimit(25, 0);
	talLeft.ConfigPeakCurrentDuration(100, 0);
	talLeft.EnableCurrentLimit(true);
	talRght.ConfigContinuousCurrentLimit(20, 0);
	talRght.ConfigPeakCurrentLimit(25, 0);
	talRght.ConfigPeakCurrentDuration(100, 0);
	talRght.EnableCurrentLimit(true);
	talLeft.ConfigOpenloopRamp(2, 0);
	talRght.ConfigOpenloopRamp(2, 0);
	talRght.ConfigSelectedFeedbackSensor(FeedbackDevice::CTRE_MagEncoder_Relative, 0, 30);
	talRght.SetSensorPhase(true);
	GPIO::setmode(GPIO::BOARD);
	/* Set up GPIO pins as input or outputs*/
	GPIO::setup(jetsonToPir, GPIO::OUT);
	GPIO::setup(pirToJetson, GPIO::IN);
	GPIO::setup(jetsonToLightRelay, GPIO::OUT, GPIO::LOW);
	
}

void drive(double fwd, double turn)
{
	double left = fwd - turn;
	double rght = fwd + turn; /* positive turn means turn robot LEFT */

	talLeft.Set(ControlMode::PercentOutput, left);
	talRght.Set(ControlMode::PercentOutput, rght);
}
/** simple wrapper for code cleanup */
void sleepApp(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void lightTimer(int ms){
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	duration<double, std::milli> time_span = t2 - t1;
	GPIO::output(jetsonToLightRelay, GPIO::HIGH);
	
	GPIO::output(jetsonToLightRelay, GPIO::LOW);
}

int main() {
	/* don't bother prompting, just use can0 */
	//std::cout << "Please input the name of your can interface: ";
	std::string interface;
	//std::cin >> interface;
	interface = "can0";
	ctre::phoenix::platform::can::SetCANInterface(interface.c_str());
	
	// Comment out the call if you would rather use the automatically running diag-server, note this requires uninstalling diagnostics from Tuner. 
	//c_SetPhoenixDiagnosticsStartTime(-1); // disable diag server, instead we will use the diag server stand alone application that Tuner installs

	/* setup drive */
	initDrive();

	while (true) {
		/* we are looking for gamepad (first time or after disconnect),
			neutral drive until gamepad (re)connected. */
		drive(0, 0);

		// wait for gamepad
		printf("Waiting for gamepad...\n");
		while (true) {
			/* SDL seems somewhat fragile, shut it down and bring it up */
			SDL_Quit();
            SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"); //so Ctrl-C still works
			SDL_Init(SDL_INIT_JOYSTICK);

			/* poll for gamepad */
			int res = SDL_NumJoysticks();
			if (res > 0) { break; }
			if (res < 0) { printf("Err = %i\n", res); }

			/* yield for a bit */
			sleepApp(20);
		}
		printf("Waiting for gamepad...Found one\n");

		// Open the joystick for reading and store its handle in the joy variable
		SDL_Joystick *joy = SDL_JoystickOpen(0);
		if (joy == NULL) {
			/* back to top of while loop */
			continue;
		}

		// Get information about the joystick
		const char *name = SDL_JoystickName(joy);
		const int num_axes = SDL_JoystickNumAxes(joy);
		const int num_buttons = SDL_JoystickNumButtons(joy);
		const int num_hats = SDL_JoystickNumHats(joy);
		printf("Now reading from joystick '%s' with:\n"
			"%d axes\n"
			"%d buttons\n"
			"%d hats\n\n",
			name,
			num_axes,
			num_buttons,
			num_hats);

		/* I'm using a logitech F350 wireless in D mode.
		If num axis is 6, then gamepad is in X mode, so neutral drive and wait for D mode.
		[SAFETY] This means 'X' becomes our robot-disable button.
		This can be removed if that's not the goal. */
		if (num_axes >= 7) {
			/* back to top of while loop */
			continue;
		}

		// Keep reading the state of the joystick in a loop
		while (true) {
			/* poll for disconnects or bad things */
			SDL_Event event;
			if (SDL_PollEvent(&event)) {
				if (event.type == SDL_QUIT) { break; }
				if (event.jdevice.type == SDL_JOYDEVICEREMOVED) { break; }
			}
			//If wanda is not parked
			if (!parkMode){
				/* grab some stick values */
			double y = .40 *((double)SDL_JoystickGetAxis(joy, 1))/32767;
			printf("Y value:%lf\n", y);
			double turn = .40 * ((double)SDL_JoystickGetAxis(joy, 0))/-32767;
			printf("turn value:%lf\n", turn);
			if(y <= .1 && y >= -.1){
			y=0;
			}
			if(turn <= .1 && turn >= -.1){
			turn =0;			
			}
			drive(y, turn);
			//printf("velocity: %d\n", talRght.GetSelectedSensorVelocity(0));

			/* [SAFETY] only enable drive if top left shoulder button is held down */
			if (SDL_JoystickGetButton(joy, 4)) {
				//y = .5;
				//turn = 0;
				ctre::phoenix::unmanaged::FeedEnable(100);
			}
			//dummy numbers for light controls (15 minutes)
			if (SDL_JoystickGetButton(joy, 0)) {
				parkMode = true;
				lightTimer(900000);
				parkMode = false;
			}
			//dummy numbers for light controls (30 minutes)
			if (SDL_JoystickGetButton(joy, 1)) {
				parkMode = true;
				lightTimer(1800000);
				parkMode = false;
			}
			//dummy numbers for light controls (45 minutes)
			if (SDL_JoystickGetButton(joy, 2)) {
				parkMode = true;
				lightTimer(2700000);
				parkMode = false;
			}
			//dummy numbers for light controls (OFF), still needs interrupt functionality to ignore timer loop.
			if (SDL_JoystickGetButton(joy, 3)) {
				parkMode = false;
				GPIO::output(jetsonToLightRelay, GPIO::LOW);
			}

			/* loop yield for a bit */
			sleepApp(20);
			}
			//If wanda is parked
			else{
				drive(0, 0);

			}
			
		}

		/* we've left the loop, likely due to gamepad disconnect */
		drive(0, 0);
		SDL_JoystickClose(joy);
		printf("gamepad disconnected\n");
	}
	GPIO::cleanup();
	SDL_Quit();
	return 0;
}

