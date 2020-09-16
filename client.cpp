/*
Name:       Project.ino
Created:	2020-01-10 오후 3:55:38
Author:     DESKTOP-5PCF805\MINDI
*/

#include <DHT.h>
#include <Wire.h>
#include <SoftwareSerial.h>


#define OPEN 1
#define CLOSE 0
#define LEDOFF 0
#define LEDON 1

#define MAGNETIC_ONE 4
#define MAGNETIC_TWO 5
#define LED_ONE 10 // LEDF
#define LED_TWO 11
#define DHTPIN 13      // DHT핀을 2번으로 정의한다(DATA핀)
#define DHTTYPE DHT11  // DHT타입을 DHT11로 정의한다
#define SLAVE_ADDRESS 0x04

DHT dht(DHTPIN, DHTTYPE);  // DHT설정 - dht (디지털2, dht11)
class pinController {
private:
	int pin;
	int state;
public:
	pinController(int pin, int state) {
		this->pin = pin;
		this->state = state;
	}
	int getPin() const {
		return pin;
	}
	void _on() const {
		digitalWrite(pin, HIGH);
	}
	void _off() const {
		digitalWrite(pin, LOW);
	}
	int _read() const {
		return digitalRead(pin);
	}
	int getState() const {
		return state;
	}
	void setPin(int pin) {
		this->pin = pin;
	}
	void setState(int _state) {
		state = _state;
	}
	virtual void _do() {}
};
virtual void pinController::_do() {

}
class Magnetic : public pinController {
private:
public:
	Magnetic() : pinController(0, CLOSE) {}

	void initialize(int pin) {
		setPin(pin);
		// LOW 면 닫힌거 HIGH 면 열린거
		if (this->_read() == HIGH) {
			pinController(pin, OPEN);
			setState(OPEN);
		}
		else {
			pinController(pin, CLOSE);
			setState(CLOSE);
		}
	}
	int readData() {
		// 열림
		// 상태가 닫힘에서 열림으로 바뀌었을때만 setState 호출
		// 이전과 같은 상태면 바꾸지않음
		if (this->_read() == HIGH && getState() == CLOSE) {
			setState(OPEN);
		}

		else if (this->_read() == LOW && getState() == OPEN) {
			setState(CLOSE);
		}
		return getState();
	}
};
class MagneticController {
private:
	Magnetic magnetic[2];
	int door_state;
public:
	MagneticController() {
		// initialize
		magnetic[0].initialize(MAGNETIC_ONE);
		magnetic[1].initialize(MAGNETIC_TWO);
		_do();
	}
	int _do() {
		// 상태 점검
		for (int i = 0; i < 2; i++) {
			magnetic[i].readData();
		}
		// 둘 다 열려 있다면
		if (magnetic[0].getState() && magnetic[1].getState()) {
			door_state = OPEN;
		}
		// 하나라도 닫혀있다면
		else {
			door_state = CLOSE;
		}
		return door_state;
	}
	int getDoorState() const {
		return door_state;
	}
};

class DHTController {
private:
	DHT dht;
	SoftwareSerial& DHTSerial; // 참조자 
public:
	// 생성자
	// 참조자로 raspberrySerial 접근
	DHTController(SoftwareSerial& ref) : dht(DHTPIN, DHTTYPE), DHTSerial(ref) {

	}
	void initialize() {
		pinMode(DHTPIN, OUTPUT);
		DHTSerial.begin(9600);
	}

	int readData() {
		delay(2000);
		int h = dht.readHumidity();  // 변수 h에 습도 값을 저장 
		int t = dht.readTemperature();  // 변수 t에 온도 값을 저장

		sendData(h);

		return h;
	}
	void sendData(int h) {
		DHTSerial.write(h);
	}
};

class LEDController : public pinController {
private:
public:
	// 생성자
	// 현재 문의 상태(OPEN || CLOSE) 에 따라서 LED 켜짐여부 결정
	
	LEDController(int pin, int DoorState) : pinController(pin, LEDOFF) {
		// 문열림 상태와 LED 상태를 동기화
		// 문이 열려있으면
		if (DoorState == OPEN) {
			// LED ON
			setState(LEDON);
			// 
			_on();
		}
		// 문이 닫혀있으면
		else {
			// LED OFF
			setState(LEDOFF);
			_off();
		}
	}

	void _do() {
		if (getState() == LEDOFF) {
			_on();
			setState(LEDON);
		}
		else {
			this->_off();
			setState(LEDOFF);
		}
	}
};

// 객체 생성
MagneticController magneticController; // 마그네틱도어센서 전체를 제어하는 magneticController
LEDController ledController[2] // LED 제어를 위한 LEDController 객체 생성
= { LEDController(LED_ONE, magneticController.getDoorState()), LEDController(LED_TWO, magneticController.getDoorState()) }; 

SoftwareSerial raspberrySerial(2, 3); // 라즈베리파이 시리얼통신 객체
DHTController dhtController(raspberrySerial); // 온습도센서 제어

char I2Cmsg[1];

void setup()
{
	Serial.begin(9600);
	Wire.begin(SLAVE_ADDRESS); // 슬레이브로써 I2C 초기화

	// I2C 통신 콜백함수 지정
	Wire.onRequest(sendData);    // 데이터 전송 

	pinMode(DHTPIN, OUTPUT);
	pinMode(MAGNETIC_ONE, INPUT_PULLUP);
	pinMode(MAGNETIC_TWO, INPUT_PULLUP);
	pinMode(LED_ONE, OUTPUT);
	pinMode(LED_TWO, OUTPUT);

	dhtController.initialize();
}

void loop()
{
	delay(500);
	char received = ' ';
	// 라즈베리파이로부터 전송한 데이터 
	if (raspberrySerial.available()) {
		// 읽어옴
		received = raspberrySerial.read();
		//Serial.println((char)received);
	}
	// 라즈베리파이 온습도 데이터 요청
	if (received == 'H') {	
		// readData() 함수를 호출에 humidity 값을 라즈베리파이에 전송
		dhtController.readData();
	}

	// 현재의 문상태 받아옴
	int door_state = magneticController.getDoorState();
	// 문열림 상태가 이전과 다를 때 (OPEN -> CLOSE || CLOSE -> OPEN)
	if (door_state != magneticController._do()) {
		// LED 작동
		ledController[0]._do();
		ledController[1]._do();

		// I2C 통신
		// send 'C'
		if (door_state == CLOSE) {
			I2Cmsg[0] = 'C';
		}
		// send 'O'
		else {
			I2Cmsg[0] = 'O';
		}
	}
}
// I2C 통신 데이터 전송 콜백함수
void sendData() {
	Wire.write(I2Cmsg, strlen(I2Cmsg));
}

