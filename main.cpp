#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

extern "C"{
	#include "socket.h"
	#include "compass.h"
}

#include "Pwm.hpp"
#include "Adc.hpp"
#include "GpsHandler.h"
#include "Accelerometer.hpp"
#include "Gpio.hpp"

bool running = true;

#define PWM_PERIOD 20000000

Pwm* pwmMainSail;
#define MAIN_SAIL_DUTY_MIN 1000000.0
#define MAIN_SAIL_DUTY_MAX 2000000.0
Pwm* pwmSecondSail;
#define SECOND_SAIL_DUTY_MIN 1500000.0
#define SECOND_SAIL_DUTY_MAX 2000000.0

Pwm* pwmHelm;
#define HELM_DUTY_MIN 850000.0
#define HELM_DUTY_MAX 1950000.0


void SocketHandleReceivedEvent(struct Event ev){
	switch(ev.id){
		case DEVICE_ID_SAIL:
		{
			unsigned short val = ConvertToSailValue(ev.data);
			printf("\t\tReceived Sail=%d\n", val);

			if(val>255)			val=255;

			pwmMainSail->SetDuty(
								(MAIN_SAIL_DUTY_MAX-MAIN_SAIL_DUTY_MIN)*(val/255.0)+MAIN_SAIL_DUTY_MIN
								);
			pwmSecondSail->SetDuty(
								(SECOND_SAIL_DUTY_MAX-SECOND_SAIL_DUTY_MIN)*(val/255.0)+SECOND_SAIL_DUTY_MIN
								);
			break;
		}
		case DEVICE_ID_HELM:
		{
			float val = ConvertToHelmValue(ev.data);
			printf("\t\tReceived Helm=%f\n", val);

			if(val<-45.0)		val=-45.0;
			else if(val>45.0)	val=45.0;

			pwmHelm->SetDuty(
								(HELM_DUTY_MAX-HELM_DUTY_MIN)*(val+45.0)/90.0+HELM_DUTY_MIN
								);
			break;
		}
		default:
			printf("\t\tReceived unhandled device value");
			break;
	}
}

void term(int signum)
{
    printf("Received SIGINT, exiting...\n");
    running = false;
}

unsigned short GrayToBin(unsigned short gray)//not used anymore
{
    unsigned short result = gray & 64;
    result |= (gray ^ (result >> 1)) & 32;
    result |= (gray ^ (result >> 1)) & 16;
    result |= (gray ^ (result >> 1)) & 8;
    result |= (gray ^ (result >> 1)) & 4;
    result |= (gray ^ (result >> 1)) & 2;
    result |= (gray ^ (result >> 1)) & 1;
    return result;
}

int main(int argc, char const *argv[])
{
#ifndef UNITTEST
	//Setup interrupt signal handling
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGINT, &action, NULL);


    //Setup servo PWM
	pwmMainSail = new Pwm(PWM2A, PWM_PERIOD, (MAIN_SAIL_DUTY_MAX-MAIN_SAIL_DUTY_MIN)/2+MAIN_SAIL_DUTY_MIN);
	pwmSecondSail = new Pwm(PWM2B, PWM_PERIOD, (SECOND_SAIL_DUTY_MAX-SECOND_SAIL_DUTY_MIN)/2+SECOND_SAIL_DUTY_MIN);
	pwmHelm = new Pwm(PWM1A, PWM_PERIOD, (HELM_DUTY_MAX-HELM_DUTY_MIN)/2+HELM_DUTY_MIN);


	//Setup GPS
	GpsHandler *gps;
	gps = GpsHandler::get();
	double fLastGPS[2] = {0,0};

	//Setup compass
	InitCompass();
	float fLastCompass(0);

	//Setup accelerometer (roll)
	Accelerometer acc(2, 0, 1);
	float fLastRoll(0);

	//Setup wind direction
	Gpio* gpioWind[6];//P8 pin numbers: 11 12 14 15 16 17
	gpioWind[0] = new Gpio(45, Gpio::INPUT);
	gpioWind[1] = new Gpio(44, Gpio::INPUT);
	gpioWind[2] = new Gpio(26, Gpio::INPUT);
	gpioWind[3] = new Gpio(47, Gpio::INPUT);
	gpioWind[4] = new Gpio(46, Gpio::INPUT);
	gpioWind[5] = new Gpio(27, Gpio::INPUT);
	float fLastWindDir(0);

	//Setup Battery probe
	Adc adcBattery(3);
	float fLastBattery(0);

	//Init communication socket to the intelligence
	int error = SocketInit();
	if(error==0){

		//Start client handling thread
		SocketHandleClients();

		//Main loop
		while(running){

			// GPS HANDLING
			double fGps[2] = {gps->latitude(), gps->longitude()};
			if(!isnan(fGps[0]) && !isnan(fGps[1]) && (fGps[0]!=fLastGPS[0] || fGps[1]!=fLastGPS[1]) )
			{
//				printf("Sending GPS=(%.10f,%.10f)\n", fGps[0], fGps[1]);
				SocketSendGps(fGps[0], fGps[1]);
				fLastGPS[0] = fGps[0];
				fLastGPS[1] = fGps[1];
			}

			// COMPASS HANDLING
			float fCompass = GetCompass();
			if(fabs(fCompass-fLastCompass)>1)//re-send compass for each 1degree variation
			{
				printf("Sending Compass=%f\n", fCompass);
				SocketSendCompass(fCompass);
				fLastCompass = fCompass;
			}

			// ACCELEROMETER HANDLING
			float fRoll = acc.roll();
			if(fabs(fRoll-fLastRoll)>2)//re-send compass for each 1degree variation
			{
//				printf("Sending Roll=%f\n", fRoll);
				SocketSendRoll(fRoll);
				fLastRoll = fRoll;
			}

			// WIND DIRECTION HANDLING
//			short nWindDirData =
//					   ( 	(!gpioWind[0]->GetValue())*16	)
//                     + (	(!gpioWind[1]->GetValue())*8	)
//                     + (	(!gpioWind[2]->GetValue())*4	)
//                     + (	(!gpioWind[3]->GetValue())*2	)
//                     + (	(!gpioWind[4]->GetValue())*1	);
			short nWindDirData = //GrayToBin(
					   ( 	(!gpioWind[0]->GetValue())*32	)
                     + (	(!gpioWind[1]->GetValue())*16	)
                     + (	(!gpioWind[2]->GetValue())*8	)
                     + (	(!gpioWind[3]->GetValue())*4	)
                     + (	(!gpioWind[4]->GetValue())*2	)
                     + (	(!gpioWind[5]->GetValue())*1	);

//			char zone[32]={0,1,3,2,7,6,4,5,15,14,12,13,8,9,11,10,31,30,28,29,24,25,27,26,16,17,19,18,23,22,20,21};
			char zone[64]={0,1, 2,3, 6,7, 4,5, 14,15, 12,13, 8,9, 10,11, 30,31, 28,29, 24,25, 26,27, 16,17, 18,19, 22,23, 20,21, 62,63, 60,61, 56,57, 58,59, 48,49, 50,51, 54,55, 52,53, 32,33, 34,35, 38,39, 36,37, 46,47, 44,45, 40,41, 42, 43};
			float fWindDir = zone[nWindDirData]*5.625;
			if(fWindDir>180.0)	fWindDir-=360.0;

			if(fabs(fWindDir-fLastWindDir)>1)//re-send compass for each 1degree variation
			{
				printf("Sending WindDir=%f\n", fWindDir);
				SocketSendWindDir(fWindDir);
				fLastWindDir = fWindDir;
			}

			// BATTERY VOLTAGE HANDLING
			float fBattery = adcBattery.GetValue()*0.090818264;//voltage*R1/(R1+R2) = adcval*909/(909+9100)
			if(fabs(fBattery-fLastBattery)>0.05)
			{
				printf("Sending Battery=%f\n", fBattery);
				SocketSendBattery(fBattery);
				fLastBattery = fBattery;
			}

			usleep(100000);//sleep 1/10 sec
		}

		//Close sockets
		SocketClose();
	}
	else{
		printf("Unable to init socket ! Error code %d",error);
		return -1;
	}

	//Destructions
	for(auto pin : gpioWind)
		delete pin;

    gps->kill();
	delete pwmHelm;
	delete pwmMainSail;
	delete pwmSecondSail;

#else

    /* ==================================
    *
    *           UNIT TESTS
    *
    * ===================================*/



#endif // UNITTEST

	//That's the end
	return 0;
}
