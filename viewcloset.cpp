#include <stdio.h>
// for GPIO use
#include <wiringPi.h>
#include <wiringSerial.h>
#include <iostream>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// for opencv
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>


#define OPEN 1
#define CLOSE 0
#define LEDOFF 0
#define LEDON 1
#define FANOFF 0
#define FANON 1
//#define LED_FPIN 23
//#define LED_SPIN 24
#define FAN_PIN 17

// tcp connection define
const char* IP_ADDRESS = "15.164.72.176";
#define CAMERA_PORT 5241

// i2c address
#define ADDRESS 0x04
// I2C-BUS
const char* deviceName = "/dev/i2c-1";

using namespace std;
using namespace cv;

// thread id
pthread_t camera_t, mic_t, speaker_t;

bool flag = false;
int fd; 
void humidity_alarm(int){
	flag = true;
// 	printf("send arduino to humidity>>> ");
	
	serialPutchar(fd, 'H');
	delay(1000);
	
	alarm(10);
	
}

typedef struct TCPFILE {
	FILE* fp;
	size_t fsize;

	TCPFILE(): fp(NULL),fsize(0) {}
	TCPFILE(const char* filename, const char* type) : fsize(0) {
		fp = fopen(filename, type);
		if (type == "rb") {
			fseek(fp, 0, SEEK_END);
			// calculate file size
			fsize = ftell(fp);
			// move file pointer to first
			fseek(fp, 0, SEEK_SET);
		}

	}
	// 후...
	bool operator=(TCPFILE& ref) {
		fp = ref.fp;
		fsize = ref.fsize;
		cout << "call operator=()- ref: " << ref.fp << "fp: " << fp << "fsize: " << fsize << endl;
	}

}TCPFILE;

class ALSARecord {
private:
	struct hwparams{
		snd_pcm_format_t format = SND_PCM_FORMAT_U8;
		unsigned int channels = 1;
		unsigned int rate = 8000;
	}hwparams;

	snd_pcm_t* handle;
	u_char* audiobuf = NULL;
	snd_pcm_uframes_t chunk_size;

	unsigned period_time;
	unsigned buffer_time;
	snd_pcm_uframes_t period_frames;
	snd_pcm_uframes_t buffer_frames;

	size_t bits_per_sample, bits_per_frame;
	size_t chunk_bytes;

	int timelimit;

public:
	ALSARecord(int _timelimit = 10): timelimit(_timelimit), period_time(0), buffer_time(0), period_frames(0), buffer_frames(0) {	
		chunk_size = 1024;
		audiobuf = (u_char *)malloc(1024);

		if (audiobuf == NULL) {
			fprintf(stderr, "error: not enough memory\n");
		}

		if ((snd_pcm_open(&handle, "plughw:1,0", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			fprintf(stderr, "error: can't open\n");
		}
		
		set_params();
	}
	void set_params();
	ssize_t pcm_read(u_char *data, size_t rcount);
	bool capture(char *name);

};
void ALSARecord::set_params() {
	snd_pcm_hw_params_t* params;
	snd_pcm_info_t* info;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	unsigned int rate;
	
	snd_pcm_info_alloca(&info);
	err = snd_pcm_info(handle, info);
	if (err < 0) {
		fprintf(stderr, "error: info\n");
		exit(EXIT_FAILURE);
	}

	snd_pcm_hw_params_alloca(&params);

	// 오디오 디바이스 매개변수 초기화
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		fprintf(stderr, "Broken configuration for this PCM: no configurations available\n");
		exit(EXIT_FAILURE);
	}

	// 오디오 데이터 접근 타입(인터브리드)
	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "Access type not available\n");
		exit(EXIT_FAILURE);
	}

	// 샘플링 레이트 설정: 부호 없는 8비트 리틀엔디안
	err = snd_pcm_hw_params_set_format(handle, params, hwparams.format);
	if (err < 0) {
		fprintf(stderr, "Sample format non available\n");
		exit(EXIT_FAILURE);
	}

	// 채널 설정
	err = snd_pcm_hw_params_set_channels(handle, params, hwparams.channels);
	if (err < 0) {
		fprintf(stderr, "Channels count non available\n");
		exit(EXIT_FAILURE);
	}

	// 샘플링 레이트 설정
	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &hwparams.rate, 0);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_rate_near fail\n");
		exit(EXIT_FAILURE);
	}

	rate = hwparams.rate;
	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0);
		if (err < 0) {
			fprintf(stderr, "snd_pcm_hw_params_get_buffer_time_max fail\n");
			exit(EXIT_FAILURE);
		}
		if (buffer_time > 500000)
			buffer_time = 500000;
	}

	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}

	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, params, &period_frames, 0);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_period_size_near fail\n");
		exit(EXIT_FAILURE);
	}

	// 버퍼의 크기 설정
	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, 0);
	}
	else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_frames);
	}
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_buffer_size_near fail\n");
		exit(EXIT_FAILURE);
	}

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		fprintf(stderr, "Unable to install hw params:\n");
		exit(EXIT_FAILURE);
	}

	bits_per_sample = snd_pcm_format_physical_width(hwparams.format);
	bits_per_frame = bits_per_sample * hwparams.channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;

	audiobuf = (u_char*)realloc(audiobuf, chunk_bytes);
	if (audiobuf == NULL) {
		fprintf(stderr, "not enough memory\n");
		exit(EXIT_FAILURE);
	}
}

/*  read from audio device (for recording) */
ssize_t ALSARecord::pcm_read(u_char *data, size_t rcount)
{
	ssize_t r;
	size_t count = rcount;

	if (count != chunk_size) {
		count = chunk_size;
	}

	while (count > 0) {
		r = snd_pcm_readi(handle, data, count);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
			fprintf(stderr, "again!\n");
			snd_pcm_wait(handle, 1000);
		}
		else if (r < 0) {
			fprintf(stderr, "audio read error: %s\n", snd_strerror(r));
			exit(EXIT_FAILURE);
		}
		if (r > 0) {
			count -= r;
			data += r * bits_per_frame / 8;
		}
	}
	return rcount;
}

/* recording raw data */
bool ALSARecord::capture(char *name)
{
	off64_t count, rest;        /* number of bytes to capture */
	int fd;

	/* get number of bytes to capture */
	count = snd_pcm_format_size(hwparams.format, hwparams.rate * hwparams.channels);
	count *= (off64_t)timelimit;
	count += count % 2;

	/* setup sound hardware */
	//	set_params();

	/* open a file to write */
	remove(name);
	if ((fd = open64(name, O_WRONLY | O_CREAT, 0644)) == -1) {
		perror(name);
		exit(EXIT_FAILURE);
	}

	rest = count;

	/* capture */
	while (rest > 0) {
		size_t c = (rest <= (off64_t)chunk_bytes) ? (size_t)rest : chunk_bytes;
		size_t f = c * 8 / bits_per_frame;
		if (pcm_read(audiobuf, f) != f)
			break;

		if (write(fd, audiobuf, c) != c) {
			perror(name);
			exit(EXIT_FAILURE);
		}

		printf("rest: %lld\n", rest);
		rest -= c;
	}
	close(fd);
	return true;
}
class ALSAPlay {
private:
	struct hwparams {
		snd_pcm_format_t format = SND_PCM_FORMAT_U8;
		unsigned int channels = 1;
		unsigned int rate = 8000;
	} hwparams;

	snd_pcm_t* handle;
	u_char* audiobuf = NULL;
	snd_pcm_uframes_t chunk_size;

	unsigned period_time;
	unsigned buffer_time;
	snd_pcm_uframes_t period_frames;
	snd_pcm_uframes_t buffer_frames;

	size_t bits_per_sample, bits_per_frame;
	size_t chunk_bytes;

	int timelimit;

public:
	ALSAPlay(int _timelimit = 10) : timelimit(_timelimit), period_time(0), buffer_time(0), period_frames(0), buffer_frames(0) {
		chunk_size = 1024;
		audiobuf = (u_char *)malloc(1024);

		if (audiobuf == NULL) {
			fprintf(stderr, "error: not enough memory\n");
		}

		if ((snd_pcm_open(&handle, "plughw:0,1", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
			fprintf(stderr, "error: can't open\n");
		}

		set_params();
	}
	void set_params();
	ssize_t pcm_write(u_char *data, size_t count);
	bool playback(char *name);
};
void ALSAPlay::set_params() {
	snd_pcm_hw_params_t* params;
	snd_pcm_info_t* info;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	unsigned int rate;

	snd_pcm_info_alloca(&info);
	err = snd_pcm_info(handle, info);
	if (err < 0) {
		fprintf(stderr, "error: info\n");
		exit(EXIT_FAILURE);
	}

	snd_pcm_hw_params_alloca(&params);

	// 오디오 디바이스 매개변수 초기화
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		fprintf(stderr, "Broken configuration for this PCM: no configurations available\n");
		exit(EXIT_FAILURE);
	}

	// 오디오 데이터 접근 타입(인터브리드)
	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "Access type not available\n");
		exit(EXIT_FAILURE);
	}

	// 샘플링 레이트 설정: 부호 없는 8비트 리틀엔디안
	err = snd_pcm_hw_params_set_format(handle, params, hwparams.format);
	if (err < 0) {
		fprintf(stderr, "Sample format non available\n");
		exit(EXIT_FAILURE);
	}

	// 채널 설정
	err = snd_pcm_hw_params_set_channels(handle, params, hwparams.channels);
	if (err < 0) {
		fprintf(stderr, "Channels count non available\n");
		exit(EXIT_FAILURE);
	}

	// 샘플링 레이트 설정
	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &hwparams.rate, 0);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_rate_near fail\n");
		exit(EXIT_FAILURE);
	}

	rate = hwparams.rate;
	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0);
		if (err < 0) {
			fprintf(stderr, "snd_pcm_hw_params_get_buffer_time_max fail\n");
			exit(EXIT_FAILURE);
		}
		if (buffer_time > 500000)
			buffer_time = 500000;
	}

	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}

	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, params, &period_frames, 0);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_period_size_near fail\n");
		exit(EXIT_FAILURE);
	}

	// 버퍼의 크기 설정
	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, 0);
	}
	else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_frames);
	}
	if (err < 0) {
		fprintf(stderr, "snd_pcm_hw_params_set_buffer_size_near fail\n");
		exit(EXIT_FAILURE);
	}

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		fprintf(stderr, "Unable to install hw params:\n");
		exit(EXIT_FAILURE);
	}

	bits_per_sample = snd_pcm_format_physical_width(hwparams.format);
	bits_per_frame = bits_per_sample * hwparams.channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;

	audiobuf = (u_char*)realloc(audiobuf, chunk_bytes);
	if (audiobuf == NULL) {
		fprintf(stderr, "not enough memory\n");
		exit(EXIT_FAILURE);
	}
}
ssize_t ALSAPlay::pcm_write(u_char *data, size_t count) {
	ssize_t r;
	ssize_t result = 0;

	if (count < chunk_size) {
		// fprintf(stderr,"set silence!\n");
		snd_pcm_format_set_silence(hwparams.format, data + count * bits_per_frame / 8, (chunk_size - count) * hwparams.channels);
		count = chunk_size;
	}
	while (count > 0) {
		r = snd_pcm_writei(handle, data, count);

		if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
			fprintf(stderr, "again!\n");
			snd_pcm_wait(handle, 1000);
		}

		else if (r < 0) {
			fprintf(stderr, "pcm_write: write error: %s\n", snd_strerror(r));
			exit(EXIT_FAILURE);
		}

		if (r > 0) {
			result += r;
			count -= r;
			data += r * bits_per_frame / 8;
		}
	}
	return result;
}

bool ALSAPlay::playback(char *name)
{
	int l, r;
	int fd;

	if ((fd = open64(name, O_RDONLY, 0)) == -1) {
		fprintf(stderr, "can't open: %s\n", name);
		exit(EXIT_FAILURE);
	}

	while (1) {
		r = read(fd, audiobuf, chunk_bytes);

		if (r < 0) {
			perror(name);
			exit(EXIT_FAILURE);
		}
		if (r == 0) {
			break;
		}
		/* if */
		l = r;

		l = l * 8 / bits_per_frame;
		r = pcm_write(audiobuf, l);
		if (r != l) {
			break;
		}
	}
	snd_pcm_drain(handle);
	close(fd);

	return true;
}
class tcpSocket{
private:
	const char* const ip;
	const int port;
	int serv_sock;
	struct sockaddr_in serv_addr;
	char buf[BUFSIZ];

public:
	tcpSocket(const char* const _ip, int _port) : ip(_ip), port(_port) {
		printf("Initialize: ip: %s port: %d\n", ip, port);

		memset(&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		// ip address assign
		serv_addr.sin_addr.s_addr = inet_addr(ip);
		// port assign
		serv_addr.sin_port = htons(port);
	}

	bool server_connect() {
		serv_sock = socket(PF_INET, SOCK_STREAM, 0);
		if (serv_sock == -1) {
			puts("socket error()");
			return false;
		}

		if (connect(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
			puts("connect error()");
			return false;
		}
		cout <<"server connect success!!"<<endl;
		return true;

	}

	// send image and record
	// like "camera.jpg", "record.wav"
	// always open "rb" mode
	void sendto(const char* filename) {
		TCPFILE file(filename, "rb");
		
		size_t nsize = 0, fpsize;
		
		while (nsize != file.fsize) {
			fpsize = fread(buf, 1, 256, file.fp);
			nsize += fpsize;
			// send to server sock
			send(serv_sock, buf, fpsize, 0);
		}
		puts("send complete!");
		fclose(file.fp);
	}

	// receive record file
	// like "result.wav"
	// always open "wb" mode
	bool recvfrom(const char* filename) {
		TCPFILE file(filename, "wb");	
		int nbyte = 256, bufsize = 256;

		while (nbyte != 0) {
			// recv from server sock
			nbyte = recv(serv_sock, buf, bufsize, 0);
			fwrite(buf, sizeof(char), nbyte, file.fp);
		}
		cout<<"recv complete!!"<<endl;
		fclose(file.fp);
		return true;
	}
	tcpSocket& return_this(){
		return *this;
	}
	void print_this(){
		cout <<"address: "<< this<< endl;
	}
	~tcpSocket() {
		close(serv_sock);
		cout << "call tcpSocket destructor!"<<endl;
	}
};
class Camera {
private:
	VideoCapture cap;
	Mat frame;
	int deviceID; // 0 = open default camera
	int apiID;      // 0 = autodetect default API
public:
	Camera(): deviceID(0), apiID(cv::CAP_ANY) {
		//cap.open(deviceID);
		// open selected camera using selected API
		cap.open(deviceID + apiID);

		// check if we succeeded
		if (!cap.isOpened()) {
			cerr << "ERROR! Unable to open camera\n";
			
		}
		else {
			cap.set(CV_CAP_PROP_FRAME_WIDTH, 1280);
			cap.set(CV_CAP_PROP_FRAME_HEIGHT, 720);

			cout << "Frame width: " << cap.get(CAP_PROP_FRAME_WIDTH) << endl;
			cout << "     height: " << cap.get(CAP_PROP_FRAME_HEIGHT) << endl;
			cout << "Capturing FPS: " << cap.get(CAP_PROP_FPS) << endl;

			cout << "Camera Intializing complete!" << endl;
		}		
	}
	bool clothes_recongnize(Mat& frame) {
		/* 동 작 과 정*/
		// 우선은 귀찮으니 sleep 해두겠다
		sleep(3);
		cout << "cloth recongnize!!" << endl;
		return true;
	}

	// play video
	bool video_streaming() {
		bool flag = false;
		while (!flag) {
			// wait for a new frame from camera and store it into 'frame'
			cap.read(frame);
			// check if we succeeded
			if (frame.empty()) {
				cerr << "ERROR! blank frame grabbed\n";
				break;
			}
			// show live and wait for a key with timeout long enough to show images
			imshow("Live", frame);
			
			if (clothes_recongnize(frame)) break;
		}	
	
		return true;

	}
	bool videocapture(){
		cout <<"start camera shooting..!"<<endl;
		cap >> frame;

		if (frame.empty()) {
			cerr << "ERROR! blank frame grabbed\n";
			return false;
		}
		else{
			imshow("camera.jpg", frame);
			imwrite("camera.jpg", frame);
			cout <<"videocaptrue() finish!"<<endl;
			return true;
		}
	}

};

class pinController {
private:
	int pin;
	int state;
public:
	pinController(int pin, int state) {
		this->pin = pin;
        pinMode(pin, OUTPUT);
		this->state = state;
	}
	int getPin() const{
		return pin;
	}
	void _on() const{
		digitalWrite(pin, HIGH);
	}
	void _off() const{
		digitalWrite(pin, LOW);
	}
	int _read() const{
		return digitalRead(pin);
	}
	int getState() const {
		return state;
	}
	void setState(int _state) {
		state = _state;
	}
	virtual void _do() {}
};
class FanController : public pinController {
private:
int humidity;
public:
	FanController(int pin) : pinController(pin, FANOFF) {
        
		if(getState()){
			printf("ON");
		}
		else{
			printf("OFF");
		}
    }
	/*
	void _do() {
		if (getState() == FANOFF) {
			this->_on();
			setState(FANON);
			
		}
		else {
			this->_off();
			setState(FANOFF);
		}
	}*/
	void _do() {
		if (getState() == FANOFF && humidity >= 60) {
			this->_on();
			setState(FANON);

		}

		else if (getState() == FANON && humidity < 60) {
			this->_off();
			setState(FANOFF);
		}
		if(getState() == FANON){
			printf("ON\n");
		}
		else{
			printf("OFF\n");
		}
		
	}
	void setHumidity(int humidity){
		this->humidity = humidity;
	}
};
class LEDController : public pinController {
private:
public:
	// 기본생성자
	LEDController():pinController(0, LEDOFF) {
	
	}
	LEDController(int pin): pinController(pin, LEDOFF){
		setState(LEDOFF);
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
void* serial_connection(void*){
	// 시리얼 통신 시작
	int i, received;
	FanController fanController(FAN_PIN);
	// 임시객체 만들어 초기화
//	LEDController ledController[2] = {LEDController(LED_FPIN), LEDController(LED_SPIN)};
  if((fd = serialOpen("/dev/ttyAMA0", 9600))< 0){
      fprintf(stderr, "Unable to open serial device: %s\n", strerror(errno));
  }

	// 데이터 전송
	alarm(10);

	signal(SIGALRM, humidity_alarm);

  while(1) {	
      // 데이터 수신  
    if(serialDataAvail(fd)){
        received = serialGetchar(fd);
		fanController.setHumidity(received);
		
		if(flag){
			printf("humidity: %3d\n", received);
			fanController._do();
			flag = false;
		}
		/*
		else {
			printf("door state changed:  %3d\n", received);
			ledController[0]._do();
			ledController[1]._do();
		}*/

		fflush(stdout);
    }
    else{
       // printf("there are no received data\n");
    }
	delay(100);
  }

}
void* camera_cheese(void*){
	// create tcp socket and connect to server
	tcpSocket socket(IP_ADDRESS, 5241);
	socket.server_connect();

	// create Camera object
	Camera camera;

	// wait 3 second
	sleep(3);

	while(true) {
		bool flag =false;

		// streming, recongnize
		if(camera.video_streaming()){
			// capture
			flag=camera.videocapture();
		}
		cout << "capture complete"<<endl;
	
		sleep(1);

		if(flag){		
			// send image to server 
			socket.sendto("camera.jpg");
		}
		break; // delete
	}
}

void* mic_record(void*) {
	tcpSocket socket(IP_ADDRESS, 5242);
	socket.connect();
	pthread_t speaker_t = NULL;
	ALSARecord recording;
	
	bool flag = true;
	while(1) {
			// 뭐 암튼 트리거가있겠지 하지만 그건 나중에 정하도록 하자 ^^
		if(/*trigger 판단 함수 하지만 우선 1로*/ flag){
		
			// 사용자의 음성을 녹음 시작
			// 트리거 땡 되면 녹음 중지			
			// 녹음이 완료되면 서버에 전송!
			if(recording.capture("record.wav")) {
				socket.sendto("record.wav");
				puts("mic_record - ");
				socket.print_this();
				// create speaker thread			
				if(speaker_t == NULL) pthread_create(&speaker_t, NULL, speaker_playing, (void*)socket.return_this());			
				// 임시
				flag = false;
				//break;
			}
		}
		else continue;		
	}
	
}

void* speaker_playing(void* ref){
	tcpSocket& socket = (tcpSocket&)ref;
	ALSAPlay playing;
	puts("speaker_playing - ");
	socket.print_this();

	while(1){
		if(socket.recvfrom("result.wav")){
			playing("result.wav");
		}
	}
}
void* i2c_connection(void*){
	int fd;
	int doorstate = false;
	if ((fd = open( deviceName, O_RDWR ) ) < 0)   
    {  
        fprintf(stderr, "I2C: Failed to access %s\n", deviceName);  
        exit(1);  
    }  
    printf("I2C: Connected\n");   
    printf("I2C: acquiring buss to 0x%x\n", ADDRESS);  

    if (ioctl(fd, I2C_SLAVE, ADDRESS) < 0)  {  
        fprintf(stderr, "I2C: Failed to acquire bus access/talk to slave 0x%x\n", ADDRESS);  
        exit(1);  
    }  
  
   while(1) {
 			//usleep(10000);  
			delay(1000);
           // char buf[1024], i2cmsg[1024]; 
		   char i2cmsg; 
           // read( file, buf, 1024 );        
  			read( fd, &i2cmsg, sizeof(i2cmsg));  
			/* 
			int i; 
			for(i=0; i<1024; i++)  {  
                i2cmsg[i] = buf[i];  
                if ( buf[i] == '\n' )   
                {  
                    i2cmsg[i]='\0';  
                    break;  
                }  
            }  */ 
		if(i2cmsg == 'O'){
 			printf("door open: %c\n", i2cmsg);
			 // 문이 닫혀있었다면
			 if(!doorstate){
				 doorstate= true;

				 // 외부서버 통신 스레드 생성
				 // 카메라 촬영시작
				 pthread_create(&camera_t, NULL, camera_cheese, NULL);
				 // 마이크 녹음기능 시작
				 pthread_create(&mic_t, NULL, mic_record, NULL);
			 }
		}
		else if(i2cmsg == 'C'){
			printf("door closed: %c\n", i2cmsg);
			// 문이 열려있었다면
			if(doorstate){
				doorstate =false;
				pthread_cancel(camera_t);
			//	pthread_cancel(&mic_t);
			}
		}
		else{
			printf("dump: %c\n", i2cmsg);
		}  		       
            //printf("[%s]\n", i2cmsg);  
   }

    close(fd);  
    return 0;  
}

int main ()
{
  	int i, received;
  	pthread_t serial_t, i2c_t;

	if(wiringPiSetupGpio() == -1){
		return -1;
	}
	
	/* thread create */
	// arduino uart communication thread
	pthread_create(&serial_t,NULL,serial_connection, NULL);
	// arduino I2C communication thread
	pthread_create(&i2c_t, NULL, i2c_connection, NULL);
	
	// detach
	pthread_join(serial_t, NULL);
	pthread_join(i2c_t, NULL);

  	return 0;
}

 