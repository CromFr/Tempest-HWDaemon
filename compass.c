#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#define PI 3.14159265

#define CRA 0x00
#define CRB 0x01
#define MODE 0x02
#define XOUT_H 0x03
#define XOUT_L 0x04
#define ZOUT_H 0x05
#define ZOUT_L 0x06
#define YOUT_H 0x07
#define YOUT_L 0x08

#include "compass.h"
#include "i2c-dev.h"


short int initialize(int i2c_bus, int adresse);
short int writeMode(int file);
short int writeRegA(int file);
short int writeRegB(int file);
short int readX(int file);
short int readY(int file);
short int readZ(int file);
short int zero(int file);

short int XOUT_offset = 0;
short int YOUT_offset = 0;
short int ZOUT_offset = 0;


short int initialize(int i2c_bus, int adresse){
	char filename[20];
	int file;
	sprintf(filename, "/dev/i2c-%d", i2c_bus);
	file = open(filename, O_RDWR);

	if(file<0){
		return -errno;
	}

	if(ioctl(file, I2C_SLAVE, adresse) < 0){
		return -errno;
	}

	return file;
}


short int writeMode(int file){

	i2c_smbus_write_byte_data(file, MODE, 0x00);
	return 0;
}

short int writeRegA(int file){
	i2c_smbus_write_byte_data(file, CRA, 0x70);
	return 0;
}

short int writeRegB(int file){
	i2c_smbus_write_byte_data(file, CRB, 0xa0);
	return 0;
}

short int readX(int file){

	short int data=0;
	data = i2c_smbus_write_byte(file, XOUT_H)<<8;
	data = i2c_smbus_write_byte(file, XOUT_L);
  	data = i2c_smbus_read_byte_data(file, XOUT_H)<<8;
  	data |= i2c_smbus_read_byte_data(file, XOUT_L);

  	return data - XOUT_offset;
}

short int readY(int file){
	short int data=0;
	data = i2c_smbus_write_byte(file, YOUT_H)<<8;
	data = i2c_smbus_write_byte(file, YOUT_L);
	data = i2c_smbus_read_byte_data(file, YOUT_H)<<8;
	data |= i2c_smbus_read_byte_data(file, YOUT_L);

	return data - YOUT_offset;
}


short int readZ(int file){
	short int data=0;
	data = i2c_smbus_write_byte(file, ZOUT_H)<<8;
	data = i2c_smbus_write_byte(file, ZOUT_L);
	data = i2c_smbus_read_byte_data(file, ZOUT_H)<<8;
	data |= i2c_smbus_read_byte_data(file, ZOUT_L);

	return data - ZOUT_offset;
}

short int zero(int file){
	XOUT_offset = readX(file);
	YOUT_offset = readY(file);
	ZOUT_offset = readZ(file);
	return 0;//TODO ??
}











char *end;
int i2c_bus, adresse, dadresse, size, file;

double angle;



void InitCompass()
{
	system("echo BB-I2C1 > /sys/devices/bone_capemgr.8/slots");

	i2c_bus = 1;
	adresse = 0x1e;
	file = initialize(i2c_bus, adresse);
	zero(file);
}


float GetCompass()
{
	short x, y, z;

	writeRegA(file);
	writeRegB(file);
	writeMode(file);

	x = readX(file);
	y = readY(file);
	z = readZ(file);

	angle = atan2((double)y,(double)x); //calcul du cap

	float declinationAngle = 0.0457;
	angle += declinationAngle;

	if (angle <0){   //correction du signe
		angle += 2*PI;
	}
	else if(angle > 2*PI){
		angle -= 2*PI;
	}

	return angle*(180/PI);
}
