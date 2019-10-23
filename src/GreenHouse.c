#include "GreenHouse.h"

int RTC;

int interval = 1;		//holds values related to output timing
int counter =0;
char outputHumidity[75];
char outputLDR[75];
char outputTemp[75];
char outputDAC[75];
char alarmOutput[75];
char outputAll[150];

long lastInterruptTime = 0;			//for btn debounce
int outputAlarm = 0;				//default false
int monitoring = 1;					//default true

float lowThreashold = 0.65;			//default threasholds for alarm
float highThreashold = 2.5;

int sysHour=0, sysMin=0, sysSec=0;	//Time holding variables
int rtcHour, rtcMin, rtcSec;

float humidity = 0;					//Values from ADC
int temp=0;
int light=0;
float dacOut =0;


int main()
{
	initGPIO();
	
	resetSysTime();
	
	setupThread();
    
	int fd;                             //Setup for named pipe
	char *myPipe = (char*)"/tmp/myfifo";
	mkfifo(myPipe, 0666);
	char updateCode[10];
	while (1)
    {
		fd = open(myPipe, O_RDONLY);
		read(fd, updateCode, sizeof(updateCode));
		//printf("updateCode: %s\n", updateCode);
		switch(updateCode[0])
		{
			case '1':
				outputAlarm = 0;
				break;
			case '2':
				sscanf((updateCode+1), "%f", &highThreashold);
		        break;
			case '3':
				sscanf((updateCode+1), "%f", &lowThreashold);
                break;
			default:
				printf("Error in update code");
		}
		close(fd);
    }

	return 0;
}

void setupThread()
{
	//black magic from Keegan to set up thread
	pthread_attr_t tattr;
    pthread_t thread_id;
    int newprio = 80;
    sched_param param;
    
    pthread_attr_init (&tattr);
    pthread_attr_getschedparam (&tattr, &param); /* safe to get existing scheduling param */
    param.sched_priority = newprio; /* set the priority; others are unchanged */
    pthread_attr_setschedparam (&tattr, &param); /* setting the new scheduling param */
    pthread_create(&thread_id, &tattr, adcThread, (void *)1);
}

void *adcThread(void *threadargs)
{
	unsigned char buffer[3];//Declare buffer array to RW to the ADC
	
	//Read values from ADC in loop
	while (1)
	{
		buffer[0] = 1;
		buffer[1] = 0b10110000;
		wiringPiSPIDataRW(SPI_CHAN, buffer, 3);
  		humidity = (((buffer[1]&3)<<8)+buffer[2]);
		humidity = (humidity/1023)*3.3;

		buffer[0] = 1;
		buffer[1] = 0b10010000;
		wiringPiSPIDataRW(SPI_CHAN, buffer, 3);
		temp = (((buffer[1]&3)<<8)+buffer[2]);

		buffer[0] = 1;
		buffer[1] = 0b10100000;
		wiringPiSPIDataRW(SPI_CHAN, buffer, 3);
		light = (((buffer[1]&3)<<8)+buffer[2]);

		dacOut = (light/1023.0)*humidity;
	}

}

void initGPIO()
{
	//set default pin scheme to wiringPi
	wiringPiSetup();
	
	//setup SPI
	wiringPiSPISetup(SPI_CHAN, SPI_SPEED);
	
	//Setup RTC
	RTC = wiringPiI2CSetup(RTCAddr);
	
	//Random default time
	wiringPiI2CWriteReg8(RTC, HOUR, 0x13);
    wiringPiI2CWriteReg8(RTC, MIN, 0x54);
    wiringPiI2CWriteReg8(RTC, SEC, 0x00);
    wiringPiI2CWriteReg8(RTC, SEC, 0b10000000);//start RTC
    
    //Setup RTC to produce a square wave with freq of 1HZ
	int setup = wiringPiI2CReadReg8(RTC, 0x07);
	setup = wiringPiI2CReadReg8(RTC, 0x07);
	setup |= 0b01000000;
	setup &= 0b11000000;
	wiringPiI2CWriteReg8(RTC, 0x07, setup);
	
	//setup buttons: b[0]-Interval change, b[1]-Reset Sys Time, b[2]-Dismiss alarm, b[3]-Toggle monitoring
	for(uint j=0; j < sizeof(BTNS)/sizeof(BTNS[0]); j++)
	{
		pinMode(BTNS[j], INPUT);
		pullUpDnControl(BTNS[j], PUD_UP);
	}
	
	//setup interupts
	wiringPiISR (BTNS[0], INT_EDGE_FALLING, intervalChange);
	wiringPiISR (BTNS[1], INT_EDGE_FALLING, resetSysTime);
	wiringPiISR (BTNS[2], INT_EDGE_FALLING, dismissAlarm);
	wiringPiISR (BTNS[3], INT_EDGE_FALLING, toggleMonitoring);
	wiringPiISR (RTCAlarm, INT_EDGE_FALLING, outputValues);
	
}

void intervalChange()//change update interval between 1s, 2s &5s
{
	printf("Change Interval:\n");

	long interruptTime = millis();

	if (interruptTime - lastInterruptTime>debounceTime)
	{
		switch(interval)
		{
			case 1: interval = 1;
				interval = 2;
				break;
			case 2: interval = 2;
				interval = 5;
				break;
			default: interval = 1;
		}
	}
	lastInterruptTime = interruptTime;
}


void resetSysTime()
{
	long interruptTime = millis();
	if (interruptTime - lastInterruptTime>debounceTime)
	{
		printf("Reset System Time:\n");
		sysHour = 0;
		sysMin = 0;
		sysSec = 0;
	}
	lastInterruptTime = interruptTime;
}

void dismissAlarm()
{
	long interruptTime = millis();
	if (interruptTime - lastInterruptTime>debounceTime)
	{
		outputAlarm = 0;
		printf("Dismiss Alarm:\n");
	}
	lastInterruptTime = interruptTime;
}

void toggleMonitoring()
{
	long interruptTime = millis();
	if (interruptTime - lastInterruptTime>debounceTime)
	{
		printf("Toggle Monitoring:\n");
		if (monitoring ==1){monitoring =0;}
		else {monitoring = 1;}
	}
	lastInterruptTime = interruptTime;
}

void triggerAlarm()
{
	outputAlarm = 1;
	//sprintf(alarmOutput, "mosquitto_pub -d -h 192.168.137.1 -p 1883 -t ADC/AlarmTrigger -m on");
	//system(alarmOutput);
}

void incrementSysTime()
{
	sysSec+=interval;
	if(sysSec>=60)
	{
		sysSec-=60;
		sysMin++;
		if(sysMin>=60)
		{
			sysMin-=60;
			sysHour++;
			if(sysHour>=24){sysHour-=24;}
		}
	}
}

void outputValues()
{
	incrementSysTime();
	if(monitoring)
	{
		if(counter<interval){counter++;}
		else
		{
			counter = 1;
			rtcHour = wiringPiI2CReadReg8(RTC, HOUR);
			rtcMin = wiringPiI2CReadReg8(RTC, MIN);
			rtcSec = wiringPiI2CReadReg8(RTC, SEC);
			rtcSec &= 0b01111111;
			if(dacOut<lowThreashold||dacOut>highThreashold){outputAlarm=1;}
			//printf("%02x:%02x:%02x\t%02d:%02d:%02d\t%.1f\t%d\t%d\t%.1f\t%d\n",rtcHour,rtcMin,rtcSec,sysHour,sysMin,sysSec,humidity, temp, light,dacOut,outputAlarm);
			/*sprintf(outputHumidity, "mosquitto_pub -d -h 192.168.137.15 -p 1883 -t ADC/Humidity -m %.1f",humidity);
			sprintf(outputTemp, "mosquitto_pub -d -h 192.168.137.15 -p 1883 -t ADC/Temp -m %d",temp);
			sprintf(outputLDR, "mosquitto_pub -d -h 192.168.137.15 -p 1883 -t ADC/LDR -m %d",light);
			sprintf(outputDAC, "mosquitto_pub -d -h 192.168.137.15 -p 1883 -t ADC/DAC -m %.1f",dacOut);
			system(outputHumidity);
			system(outputTemp);
			system(outputLDR);
			system(outputDAC);*/
			sprintf(outputAll, "mosquitto_pub -d -h 192.168.137.15 -p 1883 -t ADC -m \"%d,%.1f,%d,%.1f,%02d:%02d:%02d,%d\">/dev/null", light, humidity, temp, dacOut, sysHour, sysMin, sysSec, outputAlarm);
			system(outputAll);
		}
	}
}
