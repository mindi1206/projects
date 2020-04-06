import pyaudio
import wave
import array

class Recorder:
    def __init__(self):
        self.THRESHOLD = 500
        self.FORMAT = pyaudio.paInt16
        self.CHANNELS = 1
        self.RATE = 44100
        self.CHUNK = 8192  # int(self.RATE/10)
        self.f = None
        self.MAX_RECORD_SECONDS = 15

    def record_audio(self):
        def is_silent(data):
            # When it's silent, max(data) tend to be 100~200
            result = max(data) < self.THRESHOLD
            self.THRESHOLD = self.THRESHOLD + 0.3 * (max(data) - self.THRESHOLD)
            return result

        p = pyaudio.PyAudio()
        stream = p.open(format=self.FORMAT,
                        channels=self.CHANNELS,
                        rate=self.RATE,
                        input=True,
                        frames_per_buffer=self.CHUNK
                        )
        frames = []
        counter = 0
        MINIMUM_RECORD_SECONDS = 3

        for i in range(0, int(self.RATE / self.CHUNK * self.MAX_RECORD_SECONDS)):
            data = stream.read(self.CHUNK)
            frames.append(data)

            if MINIMUM_RECORD_SECONDS < i:
                # frames.append(data)
                print("SILENT COUNTER : " + str(counter))
                print("FRAME LENGTH : " + str(len(frames)))
                silent = is_silent(array.array('h', data))

                # If it is noisy
                if not silent:
                    # frames.append(data)
                    counter = 0

                else:
                    if counter < 5:
                        counter = counter + 1
                    else:
                        break

        print("Recording is finished.")

        stream.stop_stream()
        stream.close()
        p.terminate()

        wf = wave.open("output.wav", 'wb')
        wf.setnchannels(self.CHANNELS)
        wf.setsampwidth(p.get_sample_size(self.FORMAT))
        wf.setframerate(self.RATE)
        wf.writeframes(b''.join(frames))
        wf.close()


class Speaker:
    def __init__(self):
        self.THRESHOLD = 500
        self.FORMAT = pyaudio.paInt16
        self.CHANNELS = 1
        self.RATE = 44100
        self.CHUNK = 8192 #int(self.RATE/10)
        self.f = None

    def play_audio(self):
        p = pyaudio.PyAudio()
        wf = wave.open("output.wav", 'rb')

        stream = p.open(
            format=self.FORMAT,
            channels=self.CHANNELS,
            rate=self.RATE,
            output=True,
            input_device_index=2
        )

        data = wf.readframes(self.CHUNK)

        while data != '':
            stream.write(data)
            data = wf.readframes(self.CHUNK)

        stream.stop_stream()
        stream.close()


if __name__ == "__main__":
    '''
    
     p = pyaudio.PyAudio()

    CHUNK = 512
    FORMAT = pyaudio.paInt16
    CHANNELS = 1
    RATE = 48000
    RECORD_SECONDS = 5
    WAVE_OUTPUT_FILENAME = "output.wav"

    stream = p.open(format=FORMAT,
                channels=CHANNELS,
                rate=RATE,
                input=True,
                frames_per_buffer=CHUNK)

    print("Start to record the audio.")

    frames = []

    for i in range(0, int(RATE / CHUNK * RECORD_SECONDS)):
        data = stream.read(CHUNK)
        frames.append(data)

    print("Recording is finished.")

    stream.stop_stream()
    stream.close()
    p.terminate()
    
   


    wf = wave.open(WAVE_OUTPUT_FILENAME, 'wb')
    wf.setnchannels(CHANNELS)
    wf.setsampwidth(p.get_sample_size(FORMAT))
    wf.setframerate(RATE)
    wf.writeframes(b''.join(frames))
    wf.close()

'''
    r = Recorder()
    r.record_audio()
    s = Speaker()
    s.play_audio()
